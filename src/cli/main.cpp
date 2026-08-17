// CLI de teste do parser gen3. Roda no PC — nao depende de libnx.
//
// Uso: pokehome-cli <caminho-do-save> [box]

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gen3_save.h"
#include "za_save.h"

namespace {

std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "uso: %s <save> [box]\n", argv[0]);
    return 2;
  }

  const std::string path = argv[1];
  const std::size_t box = (argc >= 3) ? std::stoul(argv[2]) - 1 : 0;

  const auto file = ReadFile(path);
  if (file.empty()) {
    std::fprintf(stderr, "erro: nao consegui ler '%s'\n", path.c_str());
    return 1;
  }
  std::printf("Arquivo: %s (%zu bytes)\n", path.c_str(), file.size());

  // Save do Legends Z-A? O hash do SwishCrypto decide — nenhum outro formato
  // que lemos passa nessa verificacao.
  if (const auto za_save = za::ParseZaSave(file)) {
    std::printf("Formato: Legends Z-A (SwishCrypto valido)\n");
    std::printf("Treinador: %s  ID: %u  Tempo: %u:%02u  Pokemon: %zu\n",
                za_save->trainer.name.c_str(), za_save->trainer.tid,
                za_save->trainer.play_seconds / 3600,
                (za_save->trainer.play_seconds % 3600) / 60, za_save->count);
    std::printf("Party:\n");
    for (std::size_t sl = 0; sl < za::kPartySlots; ++sl) {
      const auto mon = za::ReadZaPartyPokemon(*za_save, sl);
      if (!mon) continue;
      std::printf("  slot %zu: #%04u %-12s Lv.%-3u %s\n", sl + 1,
                  mon->species, za::ZaSpeciesName(mon->species).c_str(),
                  mon->level, mon->nickname.c_str());
    }
    int total = 0;
    for (std::size_t b = 0; b < za::kBoxCount; ++b) {
      bool header = false;
      for (std::size_t sl = 0; sl < za::kSlotsPerBox; ++sl) {
        const auto mon = za::ReadZaBoxPokemon(*za_save, b, sl);
        if (!mon) continue;
        if (!header) {
          std::printf("Caixa %zu:\n", b + 1);
          header = true;
        }
        std::printf("  slot %2zu: #%04u %-12s Lv.%-3u %s\n", sl + 1,
                    mon->species, za::ZaSpeciesName(mon->species).c_str(),
                    mon->level, mon->nickname.c_str());
        ++total;
      }
    }
    std::printf("Total: %d Pokemon\n", total);
    return 0;
  }

  const auto save = pokehome::gen3::ParseSave(file);
  if (!save) {
    std::fprintf(stderr, "erro: nao e um save gen3 reconhecivel\n");
    return 1;
  }

  const auto& active = pokehome::gen3::ActiveSlot(*save);
  std::printf("Offset do save: %zu (%s)\n", save->base_offset,
              save->base_offset == 0 ? ".sav cru" : "container/SharkPortSave");
  std::printf("Slot ativo: %c (save index %u)\n",
              save->active_slot == 1 ? 'B' : 'A', active.save_index);

  int ok = 0;
  for (const auto& s : active.sections) {
    if (s.checksum_ok()) ++ok;
  }
  std::printf("Secoes: %d/%zu com checksum valido\n", ok,
              active.sections.size());

  if (ok != static_cast<int>(pokehome::gen3::kSectionCount)) {
    std::printf(
        "\nAVISO: %zu secoes com checksum invalido.\n"
        "Os dados lidos abaixo provavelmente estao incorretos.\n",
        pokehome::gen3::kSectionCount - ok);
    if (save->base_offset != 0) {
      std::printf(
          "Este arquivo e um container (SharkPortSave), nao um save cru.\n"
          "Containers podem nao preservar o save inteiro. Prefira um .sav/.srm\n"
          "gravado direto pelo emulador (Retroarch ou mGBA).\n");
    }
  }

  std::printf("\nBox %zu:\n", box + 1);
  int found = 0;
  for (std::size_t slot = 0; slot < pokehome::gen3::kSlotsPerBox; ++slot) {
    const auto mon = pokehome::gen3::ReadBoxPokemon(file, *save, box, slot);
    if (!mon) {
      std::fprintf(stderr, "erro: falha lendo slot %zu\n", slot + 1);
      return 1;
    }
    if (mon->empty()) continue;

    ++found;
    std::printf("  %2zu. %-12s %-12s (especie %u, PID %08X)\n", slot + 1,
                pokehome::gen3::SpeciesName(mon->species).c_str(),
                mon->nickname.c_str(), mon->species, mon->personality);
  }

  if (found == 0) {
    std::printf("  (vazia)\n");
  } else {
    std::printf("\n%d Pokemon encontrados.\n", found);
  }
  return 0;
}
