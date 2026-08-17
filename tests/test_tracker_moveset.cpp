// HOME Tracker e memoria de moveset (spec 071 — G10, G11, G12).
//
// A regra da §7 de docs/pesquisa-pokemon-home.md e a mais sofisticada da HOME,
// e este teste cobre as tres pernas dela:
//
//   G10  tracker de 64 bits, unico e IMUTAVEL
//   G11  o banco memoriza o ultimo moveset POR JOGO, indexado pelo tracker
//   G12  entrando em PLA/BDSP sem memoria, o moveset e recalculado por nivel
//
// O criterio de integracao da descoberta e o teste de ida-e-volta: A -> B -> A
// tem que devolver em A exatamente o moveset que o Pokemon tinha em A.
//
// O G12 nao usa numero escrito a mao: compara com moveset-reset.txt, gerado
// por tools/pkhex-moveset a partir de Learnset.SetEncounterMoves do PKHeX.Core.
// E o que fecha a pendencia 4 do evidence-log da spec 065 — o desempate.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "moveset_memory.h"
#include "nestbox_file.h"
#include "pkm_model.h"

namespace mm = pokehome::moveset;
namespace nb = pokehome::nest;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
  if (cond) {
    std::printf("  ok   %s\n", what);
  } else {
    std::printf("  FAIL %s\n", what);
    ++g_failures;
  }
}

// Um Pokemon plausivel, com identidade preenchida. Os campos entram com
// valores DIFERENTES entre si de proposito (regra da spec 067): se dois
// vizinhos valessem o mesmo, trocar os offsets passaria no teste.
pkm::Pokemon MakeMon() {
  pkm::Pokemon p;
  p.format = pkm::Format::kPK8;
  p.species = 149;  // Dragonite: o caso do desempate
  p.form = 0;
  p.encryption_constant = 0xDEADBEEF;
  p.pid = 0x12345678;
  p.tid = 54321;
  p.sid = 11111;
  p.ot_name = "Amaral";
  p.ot_gender = 1;
  p.language = 2;
  p.ball = 4;
  p.origin_game = 44;
  p.met_location = 6789;
  p.egg_location = 30001;
  p.met_date = {24, 7, 13};
  p.egg_date = {23, 11, 5};
  p.ivs = {31, 30, 29, 28, 27, 26};
  p.moves = {85, 86, 87, 88};
  p.pp_ups = {3, 2, 1, 0};
  return p;
}

// --- G10 -------------------------------------------------------------------

void TestTracker() {
  std::printf("G10 — HOME Tracker (§7: unico e imutavel):\n");

  pkm::Pokemon p = MakeMon();
  Check(p.home_tracker == 0, "Pokemon novo nasce sem tracker");

  Check(mm::AssignTracker(p), "AssignTracker atribui quando o campo e zero");
  const std::uint64_t primeiro = p.home_tracker;
  Check(primeiro != 0, "o tracker atribuido nao e zero");

  // ESTA e a imutabilidade — a violacao plantada obrigatoria bate aqui.
  Check(!mm::AssignTracker(p), "chamar de novo NAO reatribui (devolve false)");
  Check(p.home_tracker == primeiro, "o tracker permanece IDENTICO");

  // Tracker de verdade (vindo da HOME da Nintendo) nunca pode ser destruido.
  pkm::Pokemon real = MakeMon();
  real.home_tracker = 0x0123456789ABCDEFULL;
  Check(!mm::AssignTracker(real), "Pokemon que ja veio da HOME nao e tocado");
  Check(real.home_tracker == 0x0123456789ABCDEFULL,
        "tracker do servidor sobrevive intacto");

  // Estabilidade: o mesmo Pokemon, derivado duas vezes, da o mesmo valor.
  pkm::Pokemon copia = MakeMon();
  Check(mm::DeriveTracker(copia) == primeiro,
        "o mesmo Pokemon deriva sempre o mesmo tracker");

  // Unicidade: mudar QUALQUER campo de identidade muda o tracker.
  struct Caso {
    const char* nome;
    void (*muda)(pkm::Pokemon&);
  };
  const Caso casos[] = {
      {"pid", [](pkm::Pokemon& x) { x.pid ^= 1u; }},
      {"encryption constant", [](pkm::Pokemon& x) { x.encryption_constant ^= 1u; }},
      {"especie", [](pkm::Pokemon& x) { x.species = 150; }},
      {"forma", [](pkm::Pokemon& x) { x.form = 1; }},
      {"TID", [](pkm::Pokemon& x) { x.tid = 1; }},
      {"SID", [](pkm::Pokemon& x) { x.sid = 2; }},
      {"nome do OT", [](pkm::Pokemon& x) { x.ot_name = "Pedro"; }},
      {"ball", [](pkm::Pokemon& x) { x.ball = 5; }},
      {"local de origem", [](pkm::Pokemon& x) { x.met_location = 1; }},
      {"data de origem", [](pkm::Pokemon& x) { x.met_date = {1, 1, 1}; }},
      {"IVs", [](pkm::Pokemon& x) { x.ivs[3] = 0; }},
  };
  for (const auto& c : casos) {
    pkm::Pokemon v = MakeMon();
    c.muda(v);
    char msg[128];
    std::snprintf(msg, sizeof(msg), "mudar %s muda o tracker", c.nome);
    Check(mm::DeriveTracker(v) != primeiro, msg);
  }

  // O que NAO pode mexer no tracker: tudo que muda com o tempo. Se o moveset
  // mudasse o tracker, a memoria de moveset perderia o indice na hora.
  const struct {
    const char* nome;
    void (*muda)(pkm::Pokemon&);
  } estaveis[] = {
      {"golpes", [](pkm::Pokemon& x) { x.moves = {1, 2, 3, 4}; }},
      {"exp/nivel", [](pkm::Pokemon& x) { x.exp = 999999; }},
      {"item", [](pkm::Pokemon& x) { x.held_item = 55; }},
      {"apelido", [](pkm::Pokemon& x) { x.nickname = "Bicho"; }},
      {"handling trainer", [](pkm::Pokemon& x) { x.ht_name = "Nicolas"; }},
      {"EVs", [](pkm::Pokemon& x) { x.evs = {252, 252, 4, 0, 0, 0}; }},
      {"amizade", [](pkm::Pokemon& x) { x.ot_friendship = 255; }},
  };
  for (const auto& c : estaveis) {
    pkm::Pokemon v = MakeMon();
    c.muda(v);
    char msg[128];
    std::snprintf(msg, sizeof(msg), "mudar %s NAO muda o tracker", c.nome);
    Check(mm::DeriveTracker(v) == primeiro, msg);
  }

  // Slot vazio nao ganha tracker: nao ha Pokemon a identificar.
  pkm::Pokemon vazio;
  Check(!mm::AssignTracker(vazio), "slot vazio nao recebe tracker");
  Check(vazio.home_tracker == 0, "e continua zerado");
}

// O tracker vive nos bytes do PKM, entao tem de sobreviver ao roundtrip.
// pkm_convert e de outro agente; aqui a prova e pelo formato do NestBox, que e
// o que esta spec toca: o Pokemon guardado e recuperado mantem o tracker.
void TestTrackerSobreviveAoArquivo() {
  std::printf("G10 — o tracker sobrevive ao arquivo:\n");

  pkm::Pokemon p = MakeMon();
  mm::AssignTracker(p);

  nb::NestData d = nb::MakeEmpty(2, 30);
  d.movesets.Remember(p, mm::Game::kBdsp);

  const nb::NestData back = nb::Decode(nb::Encode(d));
  Check(back.movesets.size() == 1, "a entrada sobreviveu ao arquivo");
  Check(back.movesets.Recall(p.home_tracker, mm::Game::kBdsp) != nullptr,
        "e continua indexada pelo MESMO tracker");
}

// --- G12 -------------------------------------------------------------------

// Compara nossa MovesAtLevel com o que o PKHeX.Core calculou, linha a linha.
// Esta e a resposta a pendencia 4 da spec 065.
void TestResetPorNivelContraPkHeX() {
  std::printf("G12 — reset por nivel, conferido contra o PkHeX:\n");

  const std::filesystem::path path =
      std::filesystem::path(PKHEX_JSON) / "moveset-reset.txt";
  std::ifstream f(path);
  if (!f) {
    std::printf("  FAIL nao achei %s (rode tools/pkhex-moveset)\n",
                path.string().c_str());
    ++g_failures;
    return;
  }

  int total = 0, divergencias = 0;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line);
    std::string jogo;
    int sp = 0, fm = 0, lv = 0, esperado[4] = {0, 0, 0, 0};
    is >> jogo >> sp >> fm >> lv >> esperado[0] >> esperado[1] >> esperado[2] >>
        esperado[3];

    pkm::Pokemon p;
    p.species = static_cast<std::uint16_t>(sp);
    p.form = static_cast<std::uint8_t>(fm);
    // pp_ups sujos de proposito: o reset tem de zera-los.
    p.pp_ups = {3, 3, 3, 3};
    const mm::Game g = jogo == "kBdsp" ? mm::Game::kBdsp : mm::Game::kLegendsArceus;
    mm::ResetMovesByLevel(p, g, static_cast<std::uint8_t>(lv));

    ++total;
    for (int i = 0; i < 4; ++i) {
      if (p.moves[i] != esperado[i]) {
        if (divergencias < 5) {
          std::printf("  FAIL %s sp=%d fm=%d lv=%d: pkhex=%d,%d,%d,%d "
                      "nosso=%d,%d,%d,%d\n",
                      jogo.c_str(), sp, fm, lv, esperado[0], esperado[1],
                      esperado[2], esperado[3], p.moves[0], p.moves[1],
                      p.moves[2], p.moves[3]);
        }
        ++divergencias;
        break;
      }
    }
  }

  Check(total > 10000, "a fixture do PkHeX tem as 14 mil combinacoes");
  char msg[160];
  std::snprintf(msg, sizeof(msg),
                "%d combinacoes especie x forma x nivel, ZERO divergencias "
                "contra o PkHeX (%d)",
                total, divergencias);
  Check(divergencias == 0, msg);

  // O caso que a conferencia achou, fixado como regressao explicita: o
  // Dragonite do BDSP tem `{41,200}` DEPOIS de `{62,349}` na tabela, e o PkHeX
  // percorre em ordem de tabela parando no primeiro acima do nivel. No nivel
  // 43 o golpe 200 NAO entra, apesar de ser de nivel 41.
  pkm::Pokemon dragonite;
  dragonite.species = 149;
  mm::ResetMovesByLevel(dragonite, mm::Game::kBdsp, 43);
  Check(dragonite.moves[0] == 97 && dragonite.moves[1] == 21 &&
            dragonite.moves[2] == 401 && dragonite.moves[3] == 407,
        "Dragonite BDSP lv43: a faixa fora de ordem nao vaza o golpe 200");

  // E o nivel 0 (golpe de evolucao/relearn) fica de fora: o Cloyster de PLA
  // tem `{0,93}` na tabela, e o PkHeX nao o inclui no reset.
  pkm::Pokemon cloyster;
  cloyster.species = 91;
  mm::ResetMovesByLevel(cloyster, mm::Game::kLegendsArceus, 15);
  bool tem_93 = false;
  for (auto m : cloyster.moves) {
    if (m == 93) tem_93 = true;
  }
  Check(!tem_93, "golpe de nivel 0 (evolucao) fica FORA do reset por nivel");

  // Reset zera PP-ups e nao inventa golpe para especie que o jogo nao tem.
  pkm::Pokemon fora;
  fora.species = 1000;  // nao existe em PLA nem BDSP
  fora.moves = {11, 22, 33, 44};
  Check(mm::ResetMovesByLevel(fora, mm::Game::kBdsp, 50) == 0,
        "especie ausente do jogo nao e resetada");
  Check(fora.moves[0] == 11, "e os golpes dela ficam intactos");
}

// --- G11 -------------------------------------------------------------------

// O CRITERIO DE INTEGRACAO da descoberta: ida A -> B -> A restaura o moveset
// de A. Sem isso a "versao completa" que o dono pediu nao existe.
void TestIdaEVoltaRestauraMoveset() {
  std::printf("G11 — ida e volta A -> B -> A (criterio de integracao):\n");

  mm::Memory mem;
  pkm::Pokemon p = MakeMon();
  p.species = 25;  // Pikachu: existe nos dois jogos
  p.form = 0;
  mm::AssignTracker(p);

  // 1. O Pokemon esta em PLA, com o moveset que PLA lhe deu no nivel 30.
  mem.ApplyOnEntry(p, mm::Game::kLegendsArceus, 30);
  const auto moveset_pla = p.moves;
  Check(moveset_pla[0] != 0, "PLA deu um moveset por nivel");
  mem.Remember(p, mm::Game::kLegendsArceus);

  // 2. Vai para BDSP: engine diferente, moveset RESETADO por nivel.
  const bool restaurou_bdsp = mem.ApplyOnEntry(p, mm::Game::kBdsp, 30);
  Check(!restaurou_bdsp, "primeira ida ao BDSP nao tem o que restaurar");
  const auto moveset_bdsp = p.moves;
  Check(moveset_bdsp != moveset_pla,
        "o moveset do BDSP e DIFERENTE do de PLA (engines distintas)");
  mem.Remember(p, mm::Game::kBdsp);

  // 3. Volta para PLA: o moveset de PLA e RESTAURADO, nao recalculado.
  //    O nivel passado e outro de proposito — se a memoria falhasse e o codigo
  //    caisse no reset por nivel, o resultado seria diferente e o teste pegaria.
  const bool restaurou_pla = mem.ApplyOnEntry(p, mm::Game::kLegendsArceus, 55);
  Check(restaurou_pla, "voltar a PLA RESTAUROU da memoria (nao recalculou)");
  Check(p.moves == moveset_pla, "e o moveset de PLA voltou IDENTICO");

  // 4. E voltar ao BDSP tambem restaura o dele.
  Check(mem.ApplyOnEntry(p, mm::Game::kBdsp, 55), "voltar ao BDSP restaura");
  Check(p.moves == moveset_bdsp, "com o moveset que o BDSP tinha");

  // Memoriza o ULTIMO moveset por jogo, nao o primeiro: o jogador que troca um
  // golpe em PLA e sai encontra o golpe trocado ao voltar.
  p.moves = {1, 2, 3, 4};
  p.pp_ups = {3, 3, 3, 3};
  mem.Remember(p, mm::Game::kLegendsArceus);
  p.moves = {0, 0, 0, 0};
  mem.ApplyOnEntry(p, mm::Game::kLegendsArceus, 30);
  Check(p.moves[0] == 1 && p.moves[3] == 4, "o ULTIMO moveset e o memorizado");
  Check(p.pp_ups[0] == 3, "e os PP-ups vem junto");

  // Cada tracker tem a propria memoria: dois Pokemon no mesmo jogo nao se
  // misturam (e o que o anticlonagem exige).
  pkm::Pokemon outro = MakeMon();
  outro.pid ^= 0xFFFFu;
  mm::AssignTracker(outro);
  Check(outro.home_tracker != p.home_tracker, "sao trackers diferentes");
  Check(mem.Recall(outro.home_tracker, mm::Game::kLegendsArceus) == nullptr,
        "o outro Pokemon nao herda a memoria do primeiro");

  // Pokemon sem tracker nao entra na memoria: sem indice nao ha o que guardar.
  pkm::Pokemon sem = MakeMon();
  const std::size_t antes = mem.size();
  mem.Remember(sem, mm::Game::kBdsp);
  Check(mem.size() == antes, "Pokemon sem tracker nao vira entrada");
}

// --- Persistencia e compatibilidade retroativa -----------------------------

void TestPersistencia() {
  std::printf("G11 — a memoria persiste no arquivo do NestBox (v4):\n");

  nb::NestData d = nb::MakeEmpty(4, 30);
  d.SetBoxName(0, "Favoritos");
  d.MarkSeen(150);

  pkm::Pokemon a = MakeMon();
  mm::AssignTracker(a);
  a.moves = {100, 200, 300, 400};
  a.pp_ups = {3, 2, 1, 0};
  d.movesets.Remember(a, mm::Game::kLegendsArceus);

  pkm::Pokemon b = MakeMon();
  b.pid = 0xABCDEF01;
  mm::AssignTracker(b);
  b.moves = {11, 22, 33, 44};
  b.pp_ups = {0, 1, 2, 3};
  d.movesets.Remember(b, mm::Game::kBdsp);

  const auto bytes = nb::Encode(d);
  const nb::NestData back = nb::Decode(bytes);

  Check(back.boxes == 4 && back.slots == 30, "o cabecalho volta certo");
  Check(back.BoxName(0) == "Favoritos", "os nomes das caixas continuam la");
  Check(back.Seen(150), "a dex global continua la");
  Check(back.movesets.size() == 2, "as duas entradas de moveset voltaram");

  const mm::Snapshot* sa =
      back.movesets.Recall(a.home_tracker, mm::Game::kLegendsArceus);
  Check(sa != nullptr, "a entrada de PLA e encontrada pelo tracker");
  if (sa) {
    Check(sa->moves[0] == 100 && sa->moves[1] == 200 && sa->moves[2] == 300 &&
              sa->moves[3] == 400,
          "com os 4 golpes intactos");
    Check(sa->pp_ups[0] == 3 && sa->pp_ups[3] == 0, "e os PP-ups intactos");
  }

  const mm::Snapshot* sb = back.movesets.Recall(b.home_tracker, mm::Game::kBdsp);
  Check(sb != nullptr && sb->moves[3] == 44, "a entrada de BDSP tambem");
  Check(back.movesets.Recall(a.home_tracker, mm::Game::kBdsp) == nullptr,
        "e nao vaza entre jogos: A nunca esteve no BDSP");

  // Banco sem memoria nenhuma continua valido (so o contador zerado).
  const nb::NestData vazio = nb::Decode(nb::Encode(nb::MakeEmpty(2, 30)));
  Check(vazio.boxes == 2, "banco sem memoria de moveset abre normal");
  Check(vazio.movesets.size() == 0, "com a memoria vazia");
}

// Monta um arquivo da versao ANTERIOR (v3) a mao e exige que ele abra. E o
// teste de compatibilidade retroativa: quem atualiza o app nao pode perder o
// banco. A regra ja existia (specs 029/030); a v4 tem de mante-la.
std::vector<std::uint8_t> MakeArquivoV3(std::uint16_t boxes,
                                        std::uint16_t slots) {
  std::vector<std::uint8_t> out;
  out.insert(out.end(), nb::kMagic, nb::kMagic + 4);
  const auto u16 = [&out](std::uint16_t x) {
    out.push_back(static_cast<std::uint8_t>(x & 0xFF));
    out.push_back(static_cast<std::uint8_t>(x >> 8));
  };
  u16(3);  // versao 3 — a anterior a esta spec
  u16(boxes);
  u16(slots);
  u16(0);  // reserva
  // Slot de 80 bytes e dex de 49: os tamanhos da v3, nao os da v5.
  out.resize(out.size() +
                 static_cast<std::size_t>(boxes) * slots * nb::kLegacySlotBytes,
             0);
  // Um Pokemon reconhecivel no primeiro slot.
  for (std::size_t i = 0; i < nb::kLegacySlotBytes; ++i) {
    out[nb::kHeaderBytes + i] = static_cast<std::uint8_t>(0x40 + i);
  }
  // Dex: marca a especie 25.
  const std::size_t dex_off = out.size();
  out.resize(out.size() + nb::kLegacyDexBytes, 0);
  out[dex_off + 25 / 8] |= static_cast<std::uint8_t>(1u << (25 % 8));
  // Nomes: a caixa 0 chama "Antigo".
  const std::size_t names_off = out.size();
  out.resize(out.size() + static_cast<std::size_t>(boxes) * nb::kBoxNameBytes, 0);
  const char nome[] = "Antigo";
  for (std::size_t i = 0; i < sizeof(nome) - 1; ++i) {
    out[names_off + i] = static_cast<std::uint8_t>(nome[i]);
  }
  return out;
}

void TestCompatibilidadeRetroativa() {
  std::printf("compatibilidade retroativa (arquivo da versao anterior):\n");

  const auto v3 = MakeArquivoV3(6, 30);
  const nb::NestData d = nb::Decode(v3);

  Check(d.boxes == 6 && d.slots == 30, "arquivo v3 ABRE (nao vira banco vazio)");
  Check(d.valid(), "e e valido");
  // Migrado para slot v5: o payload carrega os mesmos 80 bytes, com a tag de
  // formato gen3 no cabecalho do slot (spec 090).
  Check(d.At(0, 0) != nullptr &&
            nb::SlotFormatOf(d.At(0, 0)) == nb::kGen3 &&
            nb::SlotPayload(d.At(0, 0))[0] == 0x40,
        "os Pokemon do banco antigo estao la");
  Check(d.Seen(25), "a dex global do arquivo antigo foi lida");
  Check(d.BoxName(0) == "Antigo", "os nomes das caixas tambem");
  Check(d.movesets.size() == 0,
        "e a memoria de moveset entra VAZIA (o banco antigo nunca memorizou)");

  // Reescrever o banco antigo o promove a v4 sem perder nada.
  const nb::NestData promovido = nb::Decode(nb::Encode(d));
  Check(promovido.boxes == 6 &&
            nb::SlotPayload(promovido.At(0, 0))[0] == 0x40,
        "regravar o banco antigo preserva os Pokemon");
  Check(promovido.Seen(25) && promovido.BoxName(0) == "Antigo",
        "preserva dex e nomes");

  // v1 e v2 tambem continuam abrindo — a regra vale para toda versao anterior.
  std::vector<std::uint8_t> v1(nb::kHeaderBytes + 30u * nb::kLegacySlotBytes, 0);
  for (int i = 0; i < 4; ++i) v1[i] = nb::kMagic[i];
  v1[4] = 1;  // versao 1
  v1[6] = 1;  // 1 caixa
  v1[8] = 30;  // 30 slots
  const nb::NestData d1 = nb::Decode(v1);
  Check(d1.boxes == 1 && d1.slots == 30, "arquivo v1 continua abrindo");
  Check(d1.movesets.size() == 0, "com memoria vazia");

  // Versao ACIMA da conhecida continua sendo recusada (TD-02 da spec 030): um
  // app mais novo pode ter layout que nao sabemos adivinhar.
  auto futuro = nb::Encode(nb::MakeEmpty(2, 30));
  futuro[4] = static_cast<std::uint8_t>(nb::kVersion + 1);
  Check(nb::Decode(futuro).boxes == 0, "versao acima da conhecida e recusada");

  // Secao de moveset truncada: recusa em vez de restaurar moveset pela metade.
  nb::NestData comMem = nb::MakeEmpty(2, 30);
  pkm::Pokemon p = MakeMon();
  mm::AssignTracker(p);
  comMem.movesets.Remember(p, mm::Game::kBdsp);
  auto trunc = nb::Encode(comMem);
  trunc.resize(trunc.size() - 4);
  Check(nb::Decode(trunc).boxes == 0, "memoria truncada recusa o arquivo");
}

}  // namespace

int main() {
  std::printf("== tracker e memoria de moveset (spec 071) ==\n\n");
  TestTracker();
  std::printf("\n");
  TestTrackerSobreviveAoArquivo();
  std::printf("\n");
  TestResetPorNivelContraPkHeX();
  std::printf("\n");
  TestIdaEVoltaRestauraMoveset();
  std::printf("\n");
  TestPersistencia();
  std::printf("\n");
  TestCompatibilidadeRetroativa();

  std::printf("\n%s\n", g_failures == 0 ? "TUDO OK" : "FALHOU");
  return g_failures == 0 ? 0 : 1;
}
