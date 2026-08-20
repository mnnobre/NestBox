// MATRIZ DA NESTBOX: o round-trip save -> NestBox -> save (spec 145).
//
// As 30 rotas que o `matriz_rotas` mede vao de save a save, direto. Mas o
// gesto que o USUARIO faz e outro: ele deposita no NestBox e depois retira.
// A coluna `NB` da matriz de compatibilidade cobre exatamente isso, e nada a
// exercitava — o matriz_rotas nao tem uma linha sequer sobre o banco.
//
// Este programa faz o caminho inteiro pelo `cmt::BuildPlan`, que e a MESMA
// decisao que a tela usa (spec 128: a UI so executa I/O, a decisao mora no
// core). Nao precisa de UI nem de emulador — e por isso este teste e mais
// forte que a conferencia visual: ele compara CAMPO A CAMPO o que saiu com o
// que voltou.
//
// O round-trip e o teste mais rigoroso da matriz: sair e voltar exercita
// subida E descida no mesmo Pokemon, e o diff pega qualquer campo perdido no
// caminho.
//
// Uso:
//   matriz_nestbox <save A> [save B] [--limite N]
//
// Com um save: deposita e retira do MESMO jogo (ida e volta pura).
// Com dois: deposita a partir de A e retira para B (a rota via banco).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <optional>
#include <vector>

#include "commit_plan.h"
#include "gen3_save.h"
#include "modern_box_view.h"
#include "moveset_memory.h"
#include "pkm_convert.h"
#include "pkm_model.h"
#include "save_writer.h"
#include "transfer_rules.h"

namespace {

namespace cmt = pokehome::commit;
namespace view = pokehome::view;

std::vector<std::uint8_t> Ler(const char* p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
}

// O jogo no vocabulario da regra de compatibilidade.
pokehome::compat::Game CompatDe(savew::Game g) {
  using CG = pokehome::compat::Game;
  switch (g) {
    case savew::Game::kPLA:  return CG::kLegendsArceus;
    case savew::Game::kBDSP: return CG::kBdsp;
    case savew::Game::kZA:   return CG::kLegendsZA;
    case savew::Game::kSV:   return CG::kScarletViolet;
    case savew::Game::kSwSh: return CG::kSwordShield;
    case savew::Game::kLGPE: return CG::kLetsGo;
  }
  return CG::kScarletViolet;
}

const char* NomeJogo(savew::Game g) {
  switch (g) {
    case savew::Game::kPLA: return "Legends: Arceus";
    case savew::Game::kBDSP: return "Brilliant Diamond/Shining Pearl";
    case savew::Game::kZA: return "Legends: Z-A";
    case savew::Game::kSV: return "Scarlet/Violet";
    case savew::Game::kSwSh: return "Sword/Shield";
    case savew::Game::kLGPE: return "Let's Go";
  }
  return "?";
}

// Uma fonte de Pokemon, seja save moderno ou gen3. Existe para o laco
// principal nao precisar de um `if` por geracao em cada passo.
struct Fonte {
  std::optional<savew::SaveData> moderno;
  std::optional<pokehome::gen3::SaveFile> g3;
  std::vector<std::uint8_t> pc;   // PC buffer, so no gen3
  std::size_t caixas = 0, por_caixa = 0;
  bool eh_gen3() const { return g3.has_value(); }

  // Le um slot como BoxPokemon — o registro de exibicao que o `cmt::Change`
  // carrega. E o mesmo tipo nas duas geracoes, o que e justamente o que
  // permite ao laco principal nao se importar com a origem.
  bool Le(std::size_t b, std::size_t s, pokehome::gen3::BoxPokemon* out) const {
    if (eh_gen3()) {
      auto m = pokehome::gen3::ReadBoxPokemonFrom(pc, b, s);
      if (!m || m->species == 0) return false;
      *out = *m;
      return true;
    }
    const auto& sl = moderno->At(b, s);
    if (!sl.present || sl.mon.empty()) return false;
    *out = view::ToBoxPokemon(sl.mon);
    return true;
  }

  const char* nome() const {
    return eh_gen3() ? "FireRed (gen3)" : NomeJogo(moderno->game);
  }
};

// Abre um save de qualquer geracao. O gen3 e testado PRIMEIRO porque o
// `savew::Load` nao o conhece e o rejeitaria como "nao reconhecido".
bool Abrir(const std::vector<std::uint8_t>& buf, Fonte* f) {
  if (auto g3 = pokehome::gen3::ParseSave(buf)) {
    f->pc = pokehome::gen3::BuildPcBuffer(buf, *g3);
    if (f->pc.empty()) return false;
    f->g3 = std::move(g3);
    f->caixas = pokehome::gen3::kBoxCount;
    f->por_caixa = pokehome::gen3::kSlotsPerBox;
    return true;
  }
  auto sd = savew::Load(buf);
  if (!sd) return false;
  f->caixas = sd->box_count;
  f->por_caixa = sd->slots_per_box;
  f->moderno = std::move(sd);
  return true;
}

cmt::SaveInfo InfoDe(const savew::SaveData& sd) {
  using MG = pokehome::moveset::Game;
  cmt::SaveInfo i;
  i.kind = cmt::SaveKind::kModerno;
  i.trainer_name = sd.trainer_name;
  switch (sd.game) {
    case savew::Game::kPLA:
      i.formato = pkm::Format::kPA8; i.jogo_ms = MG::kLegendsArceus; break;
    case savew::Game::kBDSP:
      i.formato = pkm::Format::kPB8; i.jogo_ms = MG::kBdsp; break;
    case savew::Game::kZA:
      i.formato = pkm::Format::kPK9; i.jogo_ms = MG::kZA; break;
    case savew::Game::kSV:
      i.formato = pkm::Format::kPK9; i.jogo_ms = MG::kSV; break;
    case savew::Game::kSwSh:
      i.formato = pkm::Format::kPK8; i.jogo_ms = MG::kSwSh; break;
    case savew::Game::kLGPE:
      i.formato = pkm::Format::kPB7; i.jogo_ms = MG::kLgpe; break;
  }
  return i;
}

// O SaveInfo de um save gen3. O `origem_gen3` 4 = FireRed; o learnset idem.
cmt::SaveInfo InfoGen3() {
  cmt::SaveInfo i;
  i.kind = cmt::SaveKind::kGen3;
  i.learnset_gen3 = pokehome::learnset::Game::kFireRed;
  i.origem_gen3 = 4;
  return i;
}


// O que precisa sobreviver ao round-trip. Nao e a lista inteira de campos de
// proposito: `handler`, `ht_name` e o moveset MUDAM por regra ao trocar de
// jogo, e cobrar igualdade neles acusaria o comportamento correto como bug.
struct Falha {
  std::string campo;
  std::string antes, depois;
  std::uint16_t dex;
};

void Compara(const pkm::Pokemon& a, const pkm::Pokemon& b,
             std::vector<Falha>* out) {
  const std::uint16_t dex = pkm::NationalDex(a);
  auto cmp = [&](const char* campo, auto x, auto y) {
    if (x == y) return;
    out->push_back({campo, std::to_string(x), std::to_string(y), dex});
  };
  cmp("dex", dex, pkm::NationalDex(b));
  cmp("form", a.form, b.form);
  cmp("pid", a.pid, b.pid);
  cmp("exp", a.exp, b.exp);
  cmp("ability", a.ability, b.ability);
  cmp("gender", a.gender, b.gender);
  cmp("met_level", a.met_level, b.met_level);
  cmp("shiny_pid_lo", a.pid & 0xFFFF, b.pid & 0xFFFF);
  for (int i = 0; i < 6; ++i) cmp("iv", a.ivs[i], b.ivs[i]);
  if (a.nickname != b.nickname)
    out->push_back({"nickname", a.nickname, b.nickname, dex});
  if (a.ot_name != b.ot_name)
    out->push_back({"ot_name", a.ot_name, b.ot_name, dex});
  // O tracker e IMUTAVEL por definicao do HOME: uma vez atribuido, nunca
  // muda. Se ele mudar no round-trip, o banco perdeu a identidade do
  // Pokemon — o defeito mais grave que este teste pode achar.
  if (a.home_tracker != 0 && a.home_tracker != b.home_tracker)
    out->push_back({"home_tracker", std::to_string(a.home_tracker),
                    std::to_string(b.home_tracker), dex});
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "uso: matriz_nestbox <save A> [save B] [--limite N]\n");
    return 2;
  }
  int limite = 960;
  for (int i = 2; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--limite") == 0) limite = std::atoi(argv[i + 1]);

  const char* path_b = (argc >= 3 && argv[2][0] != '-') ? argv[2] : argv[1];

  auto bufA = Ler(argv[1]);
  auto bufB = Ler(path_b);
  if (bufA.empty() || bufB.empty()) {
    std::fprintf(stderr, "nao consegui ler os saves\n");
    return 1;
  }

  Fonte A, B;
  if (!Abrir(bufA, &A) || !Abrir(bufB, &B)) {
    std::fprintf(stderr, "save nao reconhecido\n");
    return 1;
  }

  // O banco vive so na memoria: o que importa e a DECISAO, e ela e a mesma
  // que a tela usa. Persistir em disco so acrescentaria I/O ao teste.
  pokehome::moveset::Memory memoria;
  const pokehome::rules::SaveContext ctx;
  const cmt::SaveInfo infoA = A.eh_gen3() ? InfoGen3() : InfoDe(*A.moderno);
  const cmt::SaveInfo infoB = B.eh_gen3() ? InfoGen3() : InfoDe(*B.moderno);
  const pokehome::compat::Game compatB =
      B.eh_gen3() ? pokehome::compat::Game::kFireRed
                  : CompatDe(B.moderno->game);

  std::size_t lidos = 0, depositados = 0, retirados = 0;
  std::vector<Falha> falhas;
  std::map<std::string, int> recusas;

  for (std::size_t b = 0; b < A.caixas && lidos < (std::size_t)limite; ++b) {
    for (std::size_t s = 0;
         s < A.por_caixa && lidos < (std::size_t)limite; ++s) {
      pokehome::gen3::BoxPokemon reg;
      if (!A.Le(b, s, &reg)) continue;
      ++lidos;
      // O original para o diff: no gen3 ele so existe DEPOIS de subir, entao
      // a comparacao usa o que o banco guardou (o `modern` do deposito).
      const bool origem_moderna = !A.eh_gen3();
      const pkm::Pokemon original =
          origem_moderna && reg.modern ? *reg.modern : pkm::Pokemon{};

      // --- IDA: o save deposita no NestBox --------------------------------
      std::vector<cmt::Change> ida;
      ida.push_back({true, 0, 0, reg});
      const cmt::Plan p1 = cmt::BuildPlan(ida, infoA, &memoria);
      if (!p1.ok()) {
        recusas["deposito: " + p1.error]++;
        continue;
      }
      if (p1.nest_writes.empty()) {
        recusas["deposito nao gerou escrita no banco"]++;
        continue;
      }
      ++depositados;

      // O que o banco guardou. Num deposito MODERNO, `modern` e o payload que
      // sobrevive ao banco — sem ele o Pokemon voltaria mutilado (spec 086).
      // Num deposito GEN3 nao ha `modern` e isso esta certo: o banco guarda os
      // 80 bytes crus, e a subida so acontece na RETIRADA, dentro do
      // BuildPlan (commit_plan.cpp:209, o ramo `!ch.mon.modern`).
      const auto& guardado = p1.nest_writes.front().mon;
      if (origem_moderna && !guardado.modern) {
        recusas["o banco guardou sem payload moderno"]++;
        continue;
      }

      // --- VOLTA: o NestBox devolve ao save --------------------------------
      //
      // A regra de compatibilidade e aplicada AQUI porque o `BuildPlan` nao a
      // aplica — e isso esta certo: quem barra e a UI, no gesto (o icone
      // vermelho impede arrastar o incompativel; main.cpp:1699). Sem esta
      // checagem o teste contornava a barreira e "retirava" 493 num destino
      // que so aceita 207.
      //
      // So da para consultar a regra com o Pokemon MODERNO em maos: ela fala
      // em National Dex, e o gen3 guarda indice interno. Quando a origem e
      // gen3, a checagem cabe ao proprio BuildPlan na subida.
      if (guardado.modern) {
        const auto v = pokehome::rules::CanTransfer(*guardado.modern, compatB,
                                                    ctx);
        if (v.verdict == pokehome::rules::Verdict::kBlocked) {
          recusas["regra: " + v.reason]++;
          continue;
        }
      }
      std::vector<cmt::Change> volta;
      volta.push_back({false, 0, 0, guardado});
      const cmt::Plan p2 = cmt::BuildPlan(volta, infoB, &memoria);
      if (!p2.ok()) {
        recusas["retirada: " + p2.error]++;
        continue;
      }
      // O destino gen3 escreve pelo mapa por indice; o moderno, pela lista.
      const bool escreveu = B.eh_gen3() ? !p2.save_changes.empty()
                                        : !p2.modern_changes.empty();
      if (!escreveu) {
        recusas["retirada nao gerou escrita no save"]++;
        continue;
      }
      ++retirados;

      // O diff campo a campo so vale entre dois modernos: o gen3 nao guarda
      // metade dos campos (tracker, ability slot, forma), e cobrar igualdade
      // acusaria a perda ESPERADA da descida como defeito.
      if (origem_moderna && !B.eh_gen3()) {
        const auto& volta_mon = p2.modern_changes.front().mon;
        if (!volta_mon.modern) {
          recusas["a retirada voltou sem payload moderno"]++;
          continue;
        }
        Compara(original, *volta_mon.modern, &falhas);
      }
    }
  }

  std::printf("round-trip: %s -> NestBox -> %s\n", A.nome(), B.nome());
  std::printf("lidos=%zu depositados=%zu retirados=%zu\n", lidos, depositados,
              retirados);
  if (!recusas.empty()) {
    std::printf("recusas:\n");
    for (const auto& [motivo, n] : recusas)
      std::printf("  %4d x %s\n", n, motivo.c_str());
  }
  if (falhas.empty()) {
    std::printf("DIFF: nenhum campo mudou no round-trip\n");
    return 0;
  }
  std::printf("DIFF: %zu campos mudaram\n", falhas.size());
  std::map<std::string, int> por_campo;
  for (const auto& f : falhas) por_campo[f.campo]++;
  for (const auto& [campo, n] : por_campo)
    std::printf("  %4d x %s\n", n, campo.c_str());
  for (std::size_t i = 0; i < falhas.size() && i < 8; ++i)
    std::printf("     dex %u: %s  %s -> %s\n", falhas[i].dex,
                falhas[i].campo.c_str(), falhas[i].antes.c_str(),
                falhas[i].depois.c_str());
  return 1;
}
