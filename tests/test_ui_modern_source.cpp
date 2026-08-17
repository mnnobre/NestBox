// Conversao pkm::Pokemon -> g3::BoxPokemon (spec 082): a ponte entre o motor
// de saves modernos e a TELA.
//
// O ORACULO E EXTERNO, e isso e o ponto do teste. A licao da spec 069 foi que
// um teste que chama a funcao testada nos DOIS lados da comparacao nao testa
// nada — ele prova que a funcao e consistente consigo mesma, o que toda funcao
// quebrada tambem e. Aqui o juiz e o `inventario.json` de cada save limpo:
// especie, nivel, shiny e item GERADOS PELO PkHeX na spec 077, escritos em
// disco muito antes de esta funcao existir.
//
// A amostra tambem foi conferida contra a pergunta que ela precisa responder
// (mesma licao da 069): o save de SV traz Lechonk (915), Pawmi (921),
// Iron Valiant (998), Tinkatink (931) e Koraidon (1007) — todos com INDICE
// INTERNO do gen9 diferente da National Dex. Se `ToBoxPokemon` copiasse
// `p.species` direto em vez de chamar `pkm::NationalDex`, este teste fica
// vermelho nomeando as especies. E o bug real da spec 076/069.
//
// GUARDRAIL: tests/saves-limpos/ e SOMENTE LEITURA. Este teste so le.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "gen3_save.h"
#include "modern_box_view.h"
#include "save_writer.h"

namespace fs = std::filesystem;
namespace g3 = pokehome::gen3;
namespace vw = pokehome::view;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  } else {
    std::printf("  ok: %s\n", what.c_str());
  }
}

// std::filesystem::path e obrigatorio no ifstream (Windows: o construtor de
// string usa a codepage ANSI).
static std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(fs::path(path), std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

// --- O oraculo: uma entrada do inventario.json do PkHeX --------------------

struct Esperado {
  int box = 0, slot = 0;
  int species = 0, level = 0, held_item = 0;
  bool shiny = false;
  std::string species_name;
};

// Leitor de JSON minimo, e de proposito: o arquivo e gerado pelo nosso proprio
// tooling com formato fixo, e trazer uma dependencia para ler cinco campos
// escalares seria custo sem retorno. Se o formato mudar, o teste fica vermelho
// dizendo que nao achou os campos — que e o comportamento certo.
static std::string CampoTexto(const std::string& obj, const std::string& key) {
  const std::string needle = "\"" + key + "\":";
  const auto p = obj.find(needle);
  if (p == std::string::npos) return "";
  const auto q = obj.find('"', p + needle.size());
  if (q == std::string::npos) return "";
  const auto r = obj.find('"', q + 1);
  return r == std::string::npos ? "" : obj.substr(q + 1, r - q - 1);
}

static long CampoNumero(const std::string& obj, const std::string& key,
                        long ausente = -1) {
  const std::string needle = "\"" + key + "\":";
  const auto p = obj.find(needle);
  if (p == std::string::npos) return ausente;
  return std::strtol(obj.c_str() + p + needle.size(), nullptr, 10);
}

static bool CampoBool(const std::string& obj, const std::string& key) {
  const std::string needle = "\"" + key + "\":";
  const auto p = obj.find(needle);
  if (p == std::string::npos) return false;
  return obj.compare(obj.find_first_not_of(" \t", p + needle.size()), 4,
                     "true") == 0;
}

static std::vector<Esperado> LerInventario(const std::string& path) {
  // Chaves extras: sem elas `std::ifstream f(fs::path(path))` e declaracao de
  // FUNCAO, nao de variavel (most vexing parse).
  std::ifstream f{fs::path(path)};
  if (!f) return {};
  const std::string all((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
  std::vector<Esperado> out;
  // Cada Pokemon e um objeto que comeca em "box":
  for (std::size_t p = all.find("\"box\":"); p != std::string::npos;
       p = all.find("\"box\":", p + 1)) {
    const std::size_t fim = all.find('}', p);
    if (fim == std::string::npos) break;
    const std::string obj = all.substr(p, fim - p);

    Esperado e;
    e.box = static_cast<int>(CampoNumero(obj, "box"));
    e.slot = static_cast<int>(CampoNumero(obj, "slot"));
    e.species = static_cast<int>(CampoNumero(obj, "species"));
    e.level = static_cast<int>(CampoNumero(obj, "level"));
    e.held_item = static_cast<int>(CampoNumero(obj, "heldItem", 0));
    e.shiny = CampoBool(obj, "shiny");
    e.species_name = CampoTexto(obj, "speciesName");
    out.push_back(std::move(e));
  }
  return out;
}

struct Jogo {
  const char* nome;
  const char* save;
  const char* inv;
  savew::Game game;
};

static const Jogo kJogos[] = {
    {"swsh", "swsh/main", "swsh/inventario.json", savew::Game::kSwSh},
    {"sv", "sv/main", "sv/inventario.json", savew::Game::kSV},
    {"bdsp", "bdsp/SaveData.bin", "bdsp/inventario.json", savew::Game::kBDSP},
    {"pla", "pla/main", "pla/inventario.json", savew::Game::kPLA},
    {"lgpe", "lgpe/savedata.bin", "lgpe/inventario.json", savew::Game::kLGPE},
};

// ---------------------------------------------------------------------------
// PARTE 1 — a conversao bate com o PkHeX, campo a campo.
// ---------------------------------------------------------------------------
static void TestConversao() {
  std::printf("\n=== PARTE 1: a tela recebe o que o PkHeX diz que esta la ===\n");

  for (const auto& j : kJogos) {
    const auto esperados = LerInventario(std::string(CLEAN_SAVES) + j.inv);
    Check(esperados.size() == 8,
          std::string(j.nome) + ": inventario do PkHeX tem 8 Pokemon (" +
              std::to_string(esperados.size()) + ")");
    if (esperados.empty()) continue;

    auto sd = savew::Load(ReadFile(std::string(CLEAN_SAVES) + j.save), j.game);
    if (!sd) {
      Check(false, std::string(j.nome) + ": save abriu");
      continue;
    }

    for (const auto& e : esperados) {
      // O inventario conta caixa e slot a partir de 1.
      const auto& slot = sd->At(static_cast<std::size_t>(e.box - 1),
                                static_cast<std::size_t>(e.slot - 1));
      const std::string onde = std::string(j.nome) + " caixa " +
                               std::to_string(e.box) + " slot " +
                               std::to_string(e.slot) + " (" +
                               e.species_name + ")";
      if (!slot.present) {
        Check(false, onde + ": slot ocupado");
        continue;
      }

      const g3::BoxPokemon mon = vw::ToBoxPokemon(slot.mon);

      // A ESPECIE e o assert que este teste existe para fazer. O PkHeX diz a
      // National Dex; ler `p.species` direto no PK9 devolveria o indice
      // interno do gen9 e este assert cairia nomeando a especie.
      Check(mon.national_dex == e.species,
            onde + ": dex " + std::to_string(mon.national_dex) + " == " +
                std::to_string(e.species));
      // `species` tambem, porque e ele que o `empty()` e o sprite da tela leem.
      Check(mon.species == e.species,
            onde + ": species da tela " + std::to_string(mon.species) + " == " +
                std::to_string(e.species));
      Check(!mon.empty(), onde + ": a tela NAO ve o slot como vazio");
      Check(mon.species_name == e.species_name,
            onde + ": nome \"" + mon.species_name + "\" == \"" +
                e.species_name + "\"");
      Check(mon.is_shiny() == e.shiny,
            onde + std::string(": shiny ") + (mon.is_shiny() ? "sim" : "nao"));
      // Nivel via ComputeStats: e por essa funcao que os cinco pontos da UI
      // leem o nivel, entao e ela que precisa devolver o numero certo — nao
      // so o campo cru.
      Check(g3::ComputeStats(mon).level == e.level,
            onde + ": nivel " +
                std::to_string(g3::ComputeStats(mon).level) + " == " +
                std::to_string(e.level));
      Check(mon.held_item == e.held_item,
            onde + ": item " + std::to_string(mon.held_item) + " == " +
                std::to_string(e.held_item));
    }
  }
}

// ---------------------------------------------------------------------------
// PARTE 2 — a amostra EXERCITA o bug que ela promete pegar.
//
// Trava contra a cegueira da spec 069: se uma regeneracao futura das fixtures
// deixar so especies em que o indice interno coincide com a dex nacional, o
// assert de especie da parte 1 passa mesmo com a conversao quebrada. Aqui o
// teste EXIGE que existam casos discriminantes, e fica vermelho dizendo que
// ficou cego se sumirem.
// ---------------------------------------------------------------------------
static void TestAmostraDiscriminante() {
  std::printf("\n=== PARTE 2: a amostra distingue interno de nacional ===\n");

  auto sd = savew::Load(ReadFile(std::string(CLEAN_SAVES) + "sv/main"),
                        savew::Game::kSV);
  if (!sd) {
    Check(false, "sv: save abriu");
    return;
  }

  int discriminantes = 0;
  for (std::size_t b = 0; b < sd->box_count; ++b) {
    for (std::size_t s = 0; s < sd->slots_per_box; ++s) {
      const auto& slot = sd->At(b, s);
      if (!slot.present) continue;
      const g3::BoxPokemon mon = vw::ToBoxPokemon(slot.mon);
      // O campo cru do binario contra a dex que a tela recebeu. Diferentes =
      // este Pokemon reprova uma conversao que copie o campo direto.
      if (mon.national_dex != slot.mon.species) {
        ++discriminantes;
        std::printf("    discriminante: interno=%u nacional=%u (%s)\n",
                    slot.mon.species, mon.national_dex,
                    mon.species_name.c_str());
      }
    }
  }
  Check(discriminantes >= 3,
        "SV tem ao menos 3 especies com interno != nacional (" +
            std::to_string(discriminantes) +
            ") — abaixo disso o teste fica CEGO para a troca de especie");
}

// ---------------------------------------------------------------------------
// PARTE 3 — o slot vazio continua vazio.
//
// A tela usa `empty()` (species == 0) para decidir se desenha sprite. Uma
// conversao que devolvesse dex 0 para Pokemon real, ou dex != 0 para slot
// vazio, produziria celula errada nas duas direcoes.
// ---------------------------------------------------------------------------
static void TestVazio() {
  std::printf("\n=== PARTE 3: vazio continua vazio ===\n");

  Check(vw::ToBoxPokemon(pkm::Pokemon{}).empty(),
        "Pokemon zerado converte para slot VAZIO");

  for (const auto& j : kJogos) {
    auto sd = savew::Load(ReadFile(std::string(CLEAN_SAVES) + j.save), j.game);
    if (!sd) continue;

    std::size_t vistos = 0;
    bool vazio_virou_cheio = false, cheio_virou_vazio = false;
    for (std::size_t b = 0; b < sd->box_count; ++b) {
      for (std::size_t s = 0; s < sd->slots_per_box; ++s) {
        const auto& slot = sd->At(b, s);
        const g3::BoxPokemon mon = vw::ToBoxPokemon(slot.mon);
        if (slot.present && slot.mon.species != 0) {
          ++vistos;
          if (mon.empty()) cheio_virou_vazio = true;
        } else if (!mon.empty()) {
          vazio_virou_cheio = true;
        }
      }
    }
    // A contagem que a tela mostra e a que o savew ja provou contra o PkHeX
    // (portao G03 + contagem da spec 080).
    Check(vistos == sd->Count(),
          std::string(j.nome) + ": a tela ve os " +
              std::to_string(sd->Count()) + " Pokemon do save");
    Check(!cheio_virou_vazio,
          std::string(j.nome) + ": nenhum Pokemon SUMIU na conversao");
    Check(!vazio_virou_cheio,
          std::string(j.nome) + ": nenhum slot vazio virou Pokemon");
  }
}

int main() {
  std::printf("=== spec 082: pkm::Pokemon -> g3::BoxPokemon (a tela) ===\n");
  TestConversao();
  TestAmostraDiscriminante();
  TestVazio();

  if (g_failures > 0) {
    std::printf("\n%d FALHA(S)\n", g_failures);
    return 1;
  }
  std::printf("\nTUDO OK\n");
  return 0;
}
