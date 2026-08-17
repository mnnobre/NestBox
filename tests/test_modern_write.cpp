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
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "box_move.h"
#include "modern_box_view.h"
#include "save_writer.h"

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

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("modern_write: tudo verde\n");
  return 0;
}
