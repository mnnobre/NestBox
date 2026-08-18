// Escrita em save moderno pela sessao de movimentacao (spec 086).
//
// O que este teste blinda, na ordem do risco:
//
//   1. O payload moderno viaja INTACTO pela MoveSession — pegar, soltar e
//      trocar carregam o pkm::Pokemon original, nao uma reconstrucao.
//   2. Nada toca o arquivo antes do commit: uma sessao inteira de movimentacao
//      deixa os bytes do save byte-identicos.
//   3. Roundtrip: aplicar as mudancas, Save(), Load() de novo — o Pokemon esta
//      no slot novo com os MESMOS bytes crus de origem.
//   4. ApplyBoxChanges recusa (tudo-ou-nada) um registro sem payload — o
//      caminho que gravaria um Pokemon mutilado.
//
// GUARDRAIL: o save do simulador e do dono e e SOMENTE LEITURA — todo o
// trabalho acontece em memoria; nenhum arquivo e gravado aqui.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "box_move.h"
#include "gen3_save.h"
#include "gen3_transfer.h"
#include "modern_box_view.h"
#include "moveset_memory.h"
#include "pkm_crypto.h"
#include "save_writer.h"
#include "swish_crypto.h"

namespace fs = std::filesystem;
namespace bx = pokehome::box;
namespace vw = pokehome::view;
namespace g3 = pokehome::gen3;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  } else {
    std::printf("  ok: %s\n", what.c_str());
  }
}

static std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(fs::path(path), std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

// O save do dono no simulador: Legends Z-A, 96 Pokemon (test_save_writer).
static const char* kZaRel = "0100F43008C44000/Amaral/main";

int main() {
  const std::string path = std::string(SIM_SAVES) + kZaRel;
  const std::vector<std::uint8_t> file = ReadFile(path);
  if (file.empty()) {
    std::printf("FALHOU: save do simulador ausente: %s\n", path.c_str());
    return 1;
  }

  auto sd = savew::Load(file);
  if (!sd || sd->game != savew::Game::kZA) {
    std::printf("FALHOU: Load nao reconheceu o Z-A\n");
    return 1;
  }

  // Um slot ocupado e um vazio para a dança.
  std::size_t from_box = 0, from_slot = 0;
  bool found = false;
  for (std::size_t b = 0; b < sd->box_count && !found; ++b) {
    for (std::size_t s = 0; s < sd->slots_per_box && !found; ++s) {
      if (sd->At(b, s).present) {
        from_box = b;
        from_slot = s;
        found = true;
      }
    }
  }
  Check(found, "ha um Pokemon nas caixas para mover");
  if (!found) return 1;

  std::size_t to_box = 0, to_slot = 0;
  found = false;
  for (std::size_t b = 0; b < sd->box_count && !found; ++b) {
    for (std::size_t s = 0; s < sd->slots_per_box && !found; ++s) {
      if (!sd->At(b, s).present) {
        to_box = b;
        to_slot = s;
        found = true;
      }
    }
  }
  Check(found, "ha um slot vazio de destino");
  if (!found) return 1;

  const pkm::Pokemon original = sd->At(from_box, from_slot).mon;

  // --- 1. payload viaja pela sessao -----------------------------------
  std::printf("payload na MoveSession:\n");
  const g3::BoxPokemon view = vw::ToBoxPokemon(original);
  Check(view.modern != nullptr, "ToBoxPokemon carrega o payload moderno");
  Check(view.modern && view.modern->raw == original.raw,
        "payload guarda os bytes crus originais");
  // Identidade da barra de status (spec 098): copiada do formato moderno.
  Check(view.ot_name == original.ot_name, "OT copiado para a view");
  Check(view.language == original.language, "idioma copiado para a view");
  Check(view.origin_game == original.origin_game, "origem copiada para a view");
  Check(view.display_gender == original.gender, "sexo copiado para a view");
  Check(view.display_ball == original.ball, "pokebola copiada (spec 099)");

  bx::MoveSession session;
  const bx::SlotRef a{1, from_box, from_slot};
  const bx::SlotRef b{1, to_box, to_slot};
  Check(session.Pick(a, view), "pega o Pokemon");
  Check(session.Drop(b, g3::BoxPokemon{}, true), "solta no vazio");
  const g3::BoxPokemon moved = session.Get(b, g3::BoxPokemon{});
  Check(moved.modern != nullptr, "payload sobreviveu ao pegar-e-soltar");
  Check(moved.modern && moved.modern->raw == original.raw,
        "payload no destino e byte-identico ao original");
  Check(session.Get(a, view).empty(), "origem ficou vazia no overlay");

  // --- 2. nada tocou o arquivo antes do commit -------------------------
  std::printf("sessao nao escreve em disco:\n");
  Check(ReadFile(path) == file,
        "arquivo do save byte-identico apos a sessao inteira");
  Check(savew::Save(*sd) == file,
        "SaveData intocado: Save() sem mudancas devolve o arquivo original");

  // --- 3. roundtrip: aplicar, salvar, reabrir --------------------------
  std::printf("roundtrip mover-salvar-reabrir (em memoria):\n");
  std::vector<vw::BoxChange> changes;
  for (const auto& [ref, mon] : session.changes()) {
    changes.push_back({ref.box, ref.slot, mon});
  }
  savew::SaveData applied = *sd;
  Check(vw::ApplyBoxChanges(applied, changes), "ApplyBoxChanges aceita");
  const std::vector<std::uint8_t> out = savew::Save(applied);
  Check(!out.empty(), "Save() produz arquivo");
  auto reopened = savew::Load(out);
  Check(reopened.has_value(), "arquivo regravado abre");
  if (reopened) {
    Check(!reopened->At(from_box, from_slot).present, "origem esta vazia");
    Check(reopened->At(to_box, to_slot).present, "destino esta ocupado");
    Check(reopened->At(to_box, to_slot).mon.raw == original.raw,
          "Pokemon no destino tem os bytes crus ORIGINAIS");
    Check(reopened->Count() == sd->Count(),
          "a contagem total nao mudou (mover nao cria nem some Pokemon)");
  }

  // --- 4. tudo-ou-nada sem payload -------------------------------------
  std::printf("recusa registro sem payload:\n");
  g3::BoxPokemon no_payload;
  no_payload.species = 25;  // parece um Pokemon, mas nao tem o original
  savew::SaveData guard = *sd;
  Check(!vw::ApplyBoxChanges(guard, {{to_box, to_slot, no_payload}}),
        "ApplyBoxChanges recusa mon sem payload moderno");
  Check(savew::Save(guard) == file,
        "a recusa nao deixou rastro: Save() continua byte-identico");

  // --- 5. slot vazio e blank CIFRADO, nunca zeros crus (spec 107) ------
  //
  // Zeros crus na regiao cifrada decifram como lixo de checksum invalido e o
  // jogo mostra BAD EGG — o bug real que o dono viu no Z-A. O vazio correto e
  // o blank cifrado: zeros em claro (EC 0, checksum 0) passados pelo Encrypt.
  // Oraculo: os vazios que o PROPRIO jogo gravou neste save tem essa forma.
  std::printf("slot vazio no repouso (spec 107):\n");
  constexpr std::uint32_t kZaBoxKey = 0x0D66012C;
  constexpr std::size_t kZaStride = 408, kZaRecord = 344, kZaSlots = 960;
  const auto record_at = [&](const std::vector<std::uint8_t>& f,
                             std::size_t idx) -> std::vector<std::uint8_t> {
    auto blocks = swc::Decrypt(f);
    if (!blocks) return {};
    for (const auto& b : *blocks) {
      if (b.key == kZaBoxKey && b.data.size() == kZaSlots * kZaStride) {
        const std::uint8_t* rec = b.data.data() + idx * kZaStride;
        return std::vector<std::uint8_t>(rec, rec + kZaRecord);
      }
    }
    return {};
  };

  std::vector<std::uint8_t> blank(kZaRecord, 0);
  pkc::Encrypt(blank.data(), blank.size(), pkc::kBlockPK8);

  const std::size_t from_idx = from_box * sd->slots_per_box + from_slot;
  const std::vector<std::uint8_t> emptied = record_at(out, from_idx);
  Check(emptied == blank,
        "slot esvaziado e o blank cifrado do jogo, nao zeros crus");

  // O reparo: zeros plantados (o dano da 0.9.1) somem no proximo Save.
  std::vector<std::uint8_t> damaged;
  {
    auto blocks = swc::Decrypt(out);
    Check(blocks.has_value(), "arquivo regravado decifra para o reparo");
    if (blocks) {
      for (auto& b : *blocks) {
        if (b.key == kZaBoxKey && b.data.size() == kZaSlots * kZaStride) {
          std::memset(b.data.data() + from_idx * kZaStride, 0, kZaRecord);
        }
      }
      damaged = swc::Encrypt(*blocks);
    }
  }
  auto hurt = savew::Load(damaged);
  Check(hurt.has_value(), "save com zeros crus ainda abre");
  if (hurt) {
    Check(!hurt->At(from_box, from_slot).present,
          "o registro zerado le como vazio (e nao como Pokemon)");
    // Qualquer commit repara: um Set noutro slot marca sujeira e o Save passa
    // o reparo por todos os registros.
    hurt->Set(to_box, to_slot, hurt->At(to_box, to_slot).mon);
    const std::vector<std::uint8_t> healed = savew::Save(*hurt);
    Check(record_at(healed, from_idx) == blank,
          "zeros crus plantados viram blank cifrado no proximo Save");
  }

  // --- 6. cauda de party no deposito convertido (spec 113) --------------
  //
  // Um Pokemon convertido (raw vazio) entra no slot com nivel, HP e stats
  // preenchidos — sem isso ele chega DESMAIADO (HP atual 0), o bug real do
  // Blastoise no Z-A.
  std::printf("cauda de party no deposito (spec 113):\n");
  {
    pokehome::gen3::FullRecord g3rec;
    g3rec.personality = 0x00010002;
    g3rec.ot_id = 0x00010001;
    pokehome::gen3::EncodeGen3String("PIKACHU", g3rec.nickname_raw,
                                     sizeof(g3rec.nickname_raw));
    g3rec.language = 2;
    g3rec.flags = 0x02;
    pokehome::gen3::EncodeGen3String("ASH", g3rec.ot_name_raw,
                                     sizeof(g3rec.ot_name_raw));
    g3rec.species = 25;
    g3rec.experience = 100000;  // nivel 46
    g3rec.moves[0] = 85;
    const std::uint32_t ivs46[6] = {31, 30, 29, 28, 27, 26};
    for (int i = 0; i < 6; ++i) g3rec.iv32 |= ivs46[i] << (i * 5);
    g3rec.origins = static_cast<std::uint16_t>(5 | (4u << 7) | (4u << 11));
    std::uint8_t raw80[80];
    pokehome::gen3::EncodeFullRecord(g3rec, raw80);

    pokehome::moveset::Memory mem;
    auto up = pokehome::g3x::ConvertUp(raw80, pkm::Format::kPK9,
                                       pokehome::moveset::Game::kZA, &mem);
    Check(up.has_value(), "gen3 sintetico converte para o Z-A");
    if (up) {
      savew::SaveData sd2 = *sd;
      Check(sd2.Set(to_box, to_slot, *up), "convertido entra no slot");
      const std::vector<std::uint8_t> out2 = savew::Save(sd2);
      Check(!out2.empty(), "Save() com o convertido produz arquivo");
      const std::vector<std::uint8_t> rec =
          record_at(out2, to_box * sd->slots_per_box + to_slot);
      Check(rec.size() == kZaRecord, "registro do slot lido de volta");
      if (rec.size() == kZaRecord) {
        std::vector<std::uint8_t> dec = rec;
        pkc::Decrypt(dec.data(), dec.size(), pkc::kBlockPK8);
        // Layout do Z-A (spec 116, medido nos nativos): nivel, 0, HP max,
        // Atk, Def, Spe, SpA, SpD, 00 00 — SEM campo de HP atual.
        const std::uint8_t level = dec[328];
        const std::uint16_t hp_max =
            static_cast<std::uint16_t>(dec[330] | (dec[331] << 8));
        const std::uint16_t atk =
            static_cast<std::uint16_t>(dec[332] | (dec[333] << 8));
        const std::uint16_t fim =
            static_cast<std::uint16_t>(dec[342] | (dec[343] << 8));
        Check(level == 46, "nivel na cauda de party");
        Check(hp_max > 50 && hp_max < 250, "HP maximo plausivel em 330");
        Check(atk != 0 && atk < hp_max, "Atk em 332 (layout Z-A, nao o do SV)");
        Check(fim == 0, "bytes finais zerados como nos nativos");
      }
    }
  }

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("modern_write: tudo verde\n");
  return 0;
}
