// Testes da transferencia end-to-end (spec 075, G13).
//
// GUARDRAIL: os saves em build/switch-sim/saves/ sao do dono e SOMENTE
// LEITURA. Toda escrita acontece dentro de um SaveSandbox (spec 064) — e a
// prova disso e o SHA256 dos originais, conferido no fim.
//
// O que este teste tem de provar, e que nenhum modulo consumido prova
// sozinho:
//   1. o Pokemon SAI da origem e APARECE no destino, com os campos [HOME]
//      preservados (especie pela conversao de indice CERTA, PID, IVs, EVs,
//      OT, ball, tracker);
//   2. um kBlocked nao escreve em NENHUM dos dois arquivos (hash antes/depois);
//   3. o item zera no Pokemon E sobe 1 na bag da ORIGEM;
//   4. A->B->A restaura o moveset de A;
//   5. ATOMICIDADE: falha na segunda escrita deixa o primeiro arquivo
//      byte-identico ao que era.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "bag_writer.h"
#include "moveset_memory.h"
#include "pkm_convert.h"
#include "save_sandbox.h"
#include "save_writer.h"
#include "sha256.h"
#include "transfer.h"

namespace fs = std::filesystem;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  } else {
    std::printf("  ok: %s\n", what.c_str());
  }
}

// std::filesystem::path e obrigatorio no Windows (nome com caractere especial).
static std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(fs::path(path), std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

static std::string HashOf(const std::string& path) {
  const auto b = ReadFile(path);
  if (b.empty()) return "<vazio>";
  const auto d = sha256::Hash(b.data(), b.size());
  std::string out;
  char buf[3];
  for (std::uint8_t x : d) {
    std::snprintf(buf, sizeof(buf), "%02x", x);
    out += buf;
  }
  return out;
}

struct Jogo {
  const char* nome;
  const char* rel;
  savew::Game game;
};

// Os saves reais do dono. Contagens da linha de base da descoberta
// (§"O portao (G03) passou"), oraculo tools/pkhex-verify.
static const Jogo kShield{"Shield", "01008DB008C2C000/Amaral/main", savew::Game::kSwSh};
static const Jogo kScarlet{"Scarlet", "0100A3D008C5C000/Amaral/main", savew::Game::kSV};
static const Jogo kBdsp{"BDSP", "0100000011D90000/Amaral/SaveData.bin", savew::Game::kBDSP};
static const Jogo kLgpe{"LGPE", "0100187003A36000/Amaral/savedata.bin", savew::Game::kLGPE};
static const Jogo kPla{"Legends Arceus", "01001F5010DFA000/Amaral/main", savew::Game::kPLA};

// Um save aberto na COPIA do sandbox.
struct Aberto {
  sandbox::SaveSandbox sb;
  savew::SaveData sd;
  std::string hash_inicial;
};

static std::optional<Aberto> Abrir(const Jogo& j) {
  auto sb = sandbox::SaveSandbox::Create(std::string(SIM_SAVES) + j.rel);
  if (!sb) {
    std::printf("FALHOU: %s: sandbox nao criado (save ausente?)\n", j.nome);
    ++g_failures;
    return std::nullopt;
  }
  const std::string h = HashOf(sb->path());
  auto sd = savew::Load(ReadFile(sb->path()), j.game);
  if (!sd) {
    std::printf("FALHOU: %s: Load falhou\n", j.nome);
    ++g_failures;
    return std::nullopt;
  }
  return Aberto{std::move(*sb), std::move(*sd), h};
}

// ---------------------------------------------------------------------------
// 1. TRANSFERENCIA FELIZ. Sai de A, aparece em B, campos [HOME] preservados.
//
// A comparacao de especie usa pkm::NationalDex nos dois lados — NAO o campo
// `species` cru, que no PK9 e o indice interno do gen9 (contexto-tecnico.md).
// ---------------------------------------------------------------------------
static void TestFeliz(const Jogo& A, const Jogo& B) {
  std::printf("[feliz] %s -> %s\n", A.nome, B.nome);
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) return;

  const std::size_t n_a = a->sd.Count(), n_b = b->sd.Count();

  // Um Pokemon que as regras do destino ACEITEM. Varre ate achar — pegar
  // "o primeiro" e a amostragem por conveniencia que a spec 069 condenou.
  transfer::Request req;
  req.level = 50;
  pkm::Pokemon origem_mon;
  bool achou = false;
  pokehome::moveset::Memory mem;
  for (std::size_t bi = 0; bi < a->sd.box_count && !achou; ++bi) {
    for (std::size_t si = 0; si < a->sd.slots_per_box && !achou; ++si) {
      if (!a->sd.At(bi, si).present) continue;
      req.src_box = bi;
      req.src_slot = si;
      auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(),
                                    transfer::ToCompatGame(B.game), req, mem);
      if (plan.result.ok()) {
        origem_mon = a->sd.At(bi, si).mon;
        achou = true;
      }
    }
  }
  if (!achou) {
    std::printf("  N/A: %s: nenhum Pokemon aceito por %s\n", A.nome, B.nome);
    return;
  }

  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(),
                                transfer::ToCompatGame(B.game), req, mem);
  Check(plan.result.ok(), std::string("Prepare ok: ") + transfer::StatusName(plan.result.status));
  const transfer::Result r = transfer::Commit(plan);
  Check(r.ok(), std::string("Commit ok: ") + transfer::StatusName(r.status) +
                    (r.message.empty() ? "" : " (" + r.message + ")"));
  if (!r.ok()) return;

  // Os arquivos foram REGRAVADOS: relemos do disco, nao da memoria.
  auto sa = savew::Load(ReadFile(a->sb.path()), A.game);
  auto sb2 = savew::Load(ReadFile(b->sb.path()), B.game);
  if (!sa || !sb2) {
    Check(false, "os saves gravados reabrem");
    return;
  }

  Check(sa->Count() == n_a - 1,
        std::string(A.nome) + ": origem foi de " + std::to_string(n_a) +
            " para " + std::to_string(sa->Count()) + " (esperado -1)");
  Check(sb2->Count() == n_b + 1,
        std::string(B.nome) + ": destino foi de " + std::to_string(n_b) +
            " para " + std::to_string(sb2->Count()) + " (esperado +1)");
  Check(!sa->At(req.src_box, req.src_slot).present,
        "o slot de origem ficou vazio");

  // Onde ele caiu: o primeiro slot que antes estava livre e agora tem alguem.
  std::size_t db = 0, ds = 0;
  bool novo = false;
  for (std::size_t bi = 0; bi < sb2->box_count && !novo; ++bi)
    for (std::size_t si = 0; si < sb2->slots_per_box && !novo; ++si)
      if (sb2->At(bi, si).present && !b->sd.At(bi, si).present) {
        db = bi; ds = si; novo = true;
      }
  if (!novo) {
    Check(false, "o Pokemon apareceu num slot novo do destino");
    return;
  }
  const pkm::Pokemon& d = sb2->At(db, ds).mon;

  // --- Os campos [HOME] ---------------------------------------------------
  Check(pkm::NationalDex(d) == pkm::NationalDex(origem_mon),
        "especie preservada (National Dex " +
            std::to_string(pkm::NationalDex(origem_mon)) + " -> " +
            std::to_string(pkm::NationalDex(d)) + ")");
  Check(d.pid == origem_mon.pid, "PID preservado");
  Check(d.ivs == origem_mon.ivs, "IVs preservados");
  Check(d.evs == origem_mon.evs, "EVs preservados");
  Check(d.ot_name == origem_mon.ot_name,
        "OT preservado (\"" + origem_mon.ot_name + "\")");
  Check(d.tid == origem_mon.tid && d.sid == origem_mon.sid, "TID/SID preservados");
  Check(d.ball == origem_mon.ball, "ball preservada");
  Check(d.home_tracker != 0, "tracker presente no destino");
  if (origem_mon.home_tracker != 0)
    Check(d.home_tracker == origem_mon.home_tracker,
          "tracker PRESERVADO (nunca sobrescrito)");
  else
    Check(r.tracker_assigned, "tracker ATRIBUIDO (era 0)");
  // O item nunca viaja (§7).
  Check(d.held_item == 0, "o Pokemon chegou ao destino SEM item");
}

// ---------------------------------------------------------------------------
// 2. BLOQUEIO. Nada e escrito em NENHUM dos dois arquivos.
//
// A prova e o HASH dos dois arquivos antes e depois — nao "o teste nao
// reclamou".
// ---------------------------------------------------------------------------
static void TestBloqueado() {
  std::printf("[bloqueado] especie que nao existe no destino\n");
  auto a = Abrir(kScarlet), b = Abrir(kLgpe);
  if (!a || !b) return;

  // Let's Go so tem os 151 de Kanto + Meltan/Melmetal. Procura em Scarlet um
  // Pokemon que ele NAO tenha.
  transfer::Request req;
  req.level = 50;
  bool achou = false;
  for (std::size_t bi = 0; bi < a->sd.box_count && !achou; ++bi)
    for (std::size_t si = 0; si < a->sd.slots_per_box && !achou; ++si) {
      if (!a->sd.At(bi, si).present) continue;
      const std::uint16_t dex = pkm::NationalDex(a->sd.At(bi, si).mon);
      if (dex > 151 && dex != 808 && dex != 809) {
        req.src_box = bi; req.src_slot = si; achou = true;
      }
    }
  if (!achou) {
    std::printf("  N/A: nenhuma especie fora de Kanto em Scarlet\n");
    return;
  }

  const std::string ha = HashOf(a->sb.path()), hb = HashOf(b->sb.path());

  pokehome::moveset::Memory mem;
  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(),
                                transfer::ToCompatGame(kLgpe.game), req, mem);
  Check(plan.result.status == transfer::Status::kBlocked,
        std::string("veredito kBlocked: ") + transfer::StatusName(plan.result.status));
  Check(!plan.result.message.empty(),
        "o motivo veio junto: \"" + plan.result.message + "\"");

  // O Commit de um plano recusado nao grava.
  const transfer::Result r = transfer::Commit(plan);
  Check(r.status == transfer::Status::kBlocked, "Commit recusa o plano bloqueado");

  Check(HashOf(a->sb.path()) == ha,
        "o arquivo de ORIGEM nao mudou (sha256 " + ha.substr(0, 16) + "...)");
  Check(HashOf(b->sb.path()) == hb,
        "o arquivo de DESTINO nao mudou (sha256 " + hb.substr(0, 16) + "...)");
}

// ---------------------------------------------------------------------------
// 3. COM ITEM. held_item zera E a bag da ORIGEM sobe 1.
//
// A origem tem de ter bag suportada pelo bag_writer (spec 072).
// ---------------------------------------------------------------------------
static void TestComItem() {
  std::printf("[item] LGPE -> BDSP (a escrita do item e na ORIGEM)\n");
  auto a = Abrir(kLgpe), b = Abrir(kBdsp);
  if (!a || !b) return;
  Check(bagw::Supported(kLgpe.game), "LGPE tem bag suportada");

  // Um Pokemon aceito pelo BDSP. Plantamos o item — o save do dono pode nao
  // ter o caso, e o que se testa e a REGRA.
  //
  // Por que LGPE e nao Shield: TODOS os 194 Pokemon do save de Shield estao
  // marcados como Gigantamax, e o BDSP bloqueia Gmax (§7). O par Shield->BDSP
  // e integralmente bloqueado nos saves reais do dono — medido, nao suposto.
  transfer::Request req;
  req.level = 100;  // BDSP recusa Hyper Training abaixo de 100
  pokehome::moveset::Memory mem;
  bool achou = false;
  for (std::size_t bi = 0; bi < a->sd.box_count && !achou; ++bi)
    for (std::size_t si = 0; si < a->sd.slots_per_box && !achou; ++si) {
      if (!a->sd.At(bi, si).present) continue;
      req.src_box = bi; req.src_slot = si;
      auto p = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(),
                                 transfer::ToCompatGame(kBdsp.game), req, mem);
      if (p.result.ok()) achou = true;
    }
  if (!achou) {
    std::printf("  N/A: nenhum Pokemon de LGPE aceito pelo BDSP\n");
    return;
  }

  const std::uint16_t item = 1;  // Master Ball: id 1, existe nos dois jogos
  pkm::Pokemon m = a->sd.At(req.src_box, req.src_slot).mon;
  m.held_item = item;
  a->sd.Set(req.src_box, req.src_slot, m);
  const std::uint16_t antes = bagw::CountOf(a->sd, item);

  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(),
                                transfer::ToCompatGame(kBdsp.game), req, mem);
  Check(plan.result.ok(), "Prepare ok com item segurado");
  Check(plan.result.item_returned == item,
        "o resultado declara o item devolvido (" +
            std::to_string(plan.result.item_returned) + ")");
  Check(!plan.result.item_lost, "LGPE NAO caiu na pendencia de bag");
  Check(bagw::CountOf(plan.src, item) == antes + 1,
        "a bag da ORIGEM foi de " + std::to_string(antes) + " para " +
            std::to_string(bagw::CountOf(plan.src, item)));

  const transfer::Result r = transfer::Commit(plan);
  Check(r.ok(), std::string("Commit ok: ") + transfer::StatusName(r.status));
  if (!r.ok()) return;

  auto sa = savew::Load(ReadFile(a->sb.path()), kLgpe.game);
  auto sb2 = savew::Load(ReadFile(b->sb.path()), kBdsp.game);
  if (!sa || !sb2) { Check(false, "os saves reabrem"); return; }
  Check(bagw::CountOf(*sa, item) == antes + 1,
        "a bag RELIDA do disco tem +1 do item");

  // O item nao viajou: nenhum slot novo do destino segura nada.
  bool viajou = false;
  for (std::size_t bi = 0; bi < sb2->box_count; ++bi)
    for (std::size_t si = 0; si < sb2->slots_per_box; ++si)
      if (sb2->At(bi, si).present && !b->sd.At(bi, si).present &&
          sb2->At(bi, si).mon.held_item != 0)
        viajou = true;
  Check(!viajou, "o item NAO viajou para o destino (§7)");
}

// ---------------------------------------------------------------------------
// 3b. Origem SEM bag suportada: pendencia DECLARADA, nao silencio (TD-03).
// ---------------------------------------------------------------------------
static void TestItemBagNaoSuportada() {
  std::printf("[item] PLA -> Shield (bag da origem NAO suportada)\n");

  // Quem esta suportado HOJE. O bag_writer nasceu na spec 072 com SwSh e LGPE
  // e foi estendido para SV e BDSP; o PLA e o unico que sobrou, e por razao
  // estrutural (o jogo nao tem held item). Este teste le o estado em vez de
  // afirma-lo: quando o PLA entrar, ele avisa que a pendencia fechou.
  Check(bagw::Supported(kShield.game) && bagw::Supported(kLgpe.game) &&
            bagw::Supported(kScarlet.game) && bagw::Supported(kBdsp.game),
        "SwSh, LGPE, SV e BDSP tem bag suportada");
  if (bagw::Supported(kPla.game)) {
    std::printf(
        "  N/A: o PLA passou a ter bag suportada — a pendencia TD-03 fechou "
        "e este teste ficou sem caso\n");
    return;
  }
  Check(!bagw::Supported(kPla.game),
        "PLA sem bag suportada (o jogo nao tem held item)");

  auto a = Abrir(kPla), b = Abrir(kShield);
  if (!a || !b) return;

  // O save de PLA do dono tem 0 Pokemon nas CAIXAS (o dele esta na party, que
  // esta fora do epico por decisao do dono). Para exercitar a regra, plantamos
  // na copia do sandbox um Pokemon vindo do Shield, ja convertido para PA8, e
  // segurando um item.
  std::size_t sb_ = 0, ss_ = 0;
  bool tem = false;
  for (std::size_t bi = 0; bi < b->sd.box_count && !tem; ++bi)
    for (std::size_t si = 0; si < b->sd.slots_per_box && !tem; ++si)
      if (b->sd.At(bi, si).present) { sb_ = bi; ss_ = si; tem = true; }
  if (!tem) { std::printf("  N/A: Shield vazio\n"); return; }

  auto conv = pkm::Convert(b->sd.At(sb_, ss_).mon, pkm::Format::kPA8);
  if (!conv) {
    std::printf("  N/A: a especie escolhida nao converte para PA8\n");
    return;
  }
  pkm::Pokemon m = *conv;
  m.held_item = 1;
  m.can_gigantamax = false;  // o destino Shield aceita; o Gmax e do save de la
  if (!a->sd.Set(0, 0, m)) { Check(false, "plantou o Pokemon no PLA"); return; }

  transfer::Request req;
  req.level = 50;
  pokehome::moveset::Memory mem;
  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(),
                                transfer::ToCompatGame(kShield.game), req, mem);
  Check(plan.result.ok(),
        std::string("a transferencia PROSSEGUE mesmo sem bag: ") +
            transfer::StatusName(plan.result.status));
  if (!plan.result.ok()) return;
  Check(plan.result.item_lost,
        "item_lost DECLARADO — a limitacao nao vira silencio (TD-03)");
  Check(plan.result.item_returned == 1, "o id do item perdido veio junto");
}

// ---------------------------------------------------------------------------
// 4. IDA E VOLTA A -> B -> A: o moveset de A e restaurado.
//
// Criterio de integracao da descoberta ("G13 consome G11"). Usa BDSP como A,
// porque so PLA e BDSP memorizam (engine de golpes propria).
// ---------------------------------------------------------------------------
static void TestIdaEVolta() {
  std::printf("[ida e volta] BDSP -> Shield -> BDSP\n");
  auto a = Abrir(kBdsp), b = Abrir(kShield);
  if (!a || !b) return;

  transfer::Request req;
  req.level = 50;
  pokehome::moveset::Memory mem;
  bool achou = false;
  for (std::size_t bi = 0; bi < a->sd.box_count && !achou; ++bi)
    for (std::size_t si = 0; si < a->sd.slots_per_box && !achou; ++si) {
      if (!a->sd.At(bi, si).present) continue;
      const auto& m = a->sd.At(bi, si).mon;
      if (m.moves[0] == 0) continue;  // sem golpe nao ha o que restaurar
      req.src_box = bi; req.src_slot = si;
      auto p = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(),
                                 transfer::ToCompatGame(kShield.game), req, mem);
      if (p.result.ok()) achou = true;
    }
  if (!achou) { std::printf("  N/A: nenhum aceito por Shield\n"); return; }

  const auto moves_a = a->sd.At(req.src_box, req.src_slot).mon.moves;

  // IDA: BDSP -> Shield. O snapshot do moveset do BDSP fica na memoria.
  auto ida = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(),
                               transfer::ToCompatGame(kShield.game), req, mem);
  Check(ida.result.ok(), "ida ok");
  if (!ida.result.ok()) return;
  Check(transfer::Commit(ida).ok(), "ida commitada");
  mem = ida.memory;
  Check(mem.size() >= 1, "o moveset do BDSP foi memorizado (" +
                             std::to_string(mem.size()) + " entrada(s))");

  // Onde ele caiu no Shield.
  auto s_shield = savew::Load(ReadFile(b->sb.path()), kShield.game);
  auto s_bdsp = savew::Load(ReadFile(a->sb.path()), kBdsp.game);
  if (!s_shield || !s_bdsp) { Check(false, "reabrem apos a ida"); return; }
  std::size_t vb = 0, vs = 0;
  bool novo = false;
  for (std::size_t bi = 0; bi < s_shield->box_count && !novo; ++bi)
    for (std::size_t si = 0; si < s_shield->slots_per_box && !novo; ++si)
      if (s_shield->At(bi, si).present && !b->sd.At(bi, si).present) {
        vb = bi; vs = si; novo = true;
      }
  if (!novo) { Check(false, "achou o Pokemon no Shield"); return; }

  // VOLTA: Shield -> BDSP.
  transfer::Request volta;
  volta.src_box = vb;
  volta.src_slot = vs;
  volta.level = 50;
  auto pv = transfer::Prepare(*s_shield, *s_bdsp, b->sb.path(), a->sb.path(),
                              transfer::ToCompatGame(kBdsp.game), volta, mem);
  Check(pv.result.ok(), std::string("volta ok: ") + transfer::StatusName(pv.result.status) +
                            (pv.result.message.empty() ? "" : " (" + pv.result.message + ")"));
  if (!pv.result.ok()) return;
  Check(pv.result.moveset_restored,
        "o moveset veio da MEMORIA (nao foi reset por nivel)");
  Check(transfer::Commit(pv).ok(), "volta commitada");

  auto s_final = savew::Load(ReadFile(a->sb.path()), kBdsp.game);
  if (!s_final) { Check(false, "o BDSP final reabre"); return; }
  std::size_t fb = 0, fs2 = 0;
  bool ok = false;
  for (std::size_t bi = 0; bi < s_final->box_count && !ok; ++bi)
    for (std::size_t si = 0; si < s_final->slots_per_box && !ok; ++si)
      if (s_final->At(bi, si).present && !s_bdsp->At(bi, si).present) {
        fb = bi; fs2 = si; ok = true;
      }
  if (!ok) { Check(false, "o Pokemon voltou ao BDSP"); return; }

  Check(s_final->At(fb, fs2).mon.moves == moves_a,
        "o moveset de A foi RESTAURADO ao voltar (" +
            std::to_string(moves_a[0]) + "/" + std::to_string(moves_a[1]) +
            "/" + std::to_string(moves_a[2]) + "/" +
            std::to_string(moves_a[3]) + ")");
}

// ---------------------------------------------------------------------------
// 5. ATOMICIDADE. A falha na escrita do SEGUNDO save nao pode deixar o
//    primeiro alterado.
//
// §4 da pesquisa: "nada e gravado durante a movimentacao; o unico ponto de
// escrita e o Save changes and exit".
//
// Como se simula a falha sem monkeypatch: o caminho de destino aponta para um
// DIRETORIO. O ofstream nao abre um diretorio para escrita — falha real do
// sistema de arquivos, no lugar exato onde a falha de verdade aconteceria.
// ---------------------------------------------------------------------------
static void TestAtomicidade() {
  std::printf("[atomicidade] LGPE -> BDSP, falha na escrita do SEGUNDO save\n");
  auto a = Abrir(kLgpe), b = Abrir(kBdsp);
  if (!a || !b) return;

  transfer::Request req;
  req.level = 100;
  pokehome::moveset::Memory mem;
  bool achou = false;
  for (std::size_t bi = 0; bi < a->sd.box_count && !achou; ++bi)
    for (std::size_t si = 0; si < a->sd.slots_per_box && !achou; ++si) {
      if (!a->sd.At(bi, si).present) continue;
      req.src_box = bi; req.src_slot = si;
      auto p = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(),
                                 transfer::ToCompatGame(kBdsp.game), req, mem);
      if (p.result.ok()) achou = true;
    }
  if (!achou) { std::printf("  N/A: nenhum aceito\n"); return; }

  const std::string hash_antes = HashOf(a->sb.path());

  // O destino impossivel: um diretorio dentro do proprio sandbox.
  const std::string destino_ruim = a->sb.dir() + "/nao-da-para-gravar";
  std::error_code ec;
  fs::create_directory(fs::path(destino_ruim), ec);
  Check(fs::is_directory(fs::path(destino_ruim)),
        "destino de escrita impossivel preparado");

  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), destino_ruim,
                                transfer::ToCompatGame(kBdsp.game), req, mem);
  Check(plan.result.ok(), "o plano foi preparado normalmente");

  const transfer::Result r = transfer::Commit(plan);
  Check(r.status == transfer::Status::kWriteFailed,
        std::string("Commit devolveu kWriteFailed: ") +
            transfer::StatusName(r.status));
  Check(r.status != transfer::Status::kRollbackFailed,
        "o rollback do primeiro save funcionou");

  // A PROVA: o primeiro arquivo esta byte-identico ao que era.
  const std::string hash_depois = HashOf(a->sb.path());
  Check(hash_depois == hash_antes,
        "o save de ORIGEM ficou INTACTO (sha256 " + hash_antes.substr(0, 16) +
            "... == " + hash_depois.substr(0, 16) + "...)");

  // E o conteudo ainda e um save valido com a contagem original.
  auto relido = savew::Load(ReadFile(a->sb.path()), kLgpe.game);
  Check(relido.has_value() && relido->Count() == a->sd.Count(),
        "a origem reabre com a contagem original (" +
            std::to_string(relido ? relido->Count() : 0) + ")");
}

// ---------------------------------------------------------------------------
// 6. Os saves ORIGINAIS do dono nunca foram tocados. O guardrail nao e o
//    guard do sandbox — e este hash.
// ---------------------------------------------------------------------------
static void TestOriginaisIntactos(const std::vector<std::pair<Jogo, std::string>>& antes) {
  std::printf("[guardrail] os saves do dono continuam intactos\n");
  for (const auto& [j, h] : antes) {
    const std::string agora = HashOf(std::string(SIM_SAVES) + j.rel);
    Check(agora == h, std::string(j.nome) + ": sha256 inalterado (" +
                          h.substr(0, 16) + "...)");
  }
}

int main() {
  // A linha de base do guardrail, ANTES de qualquer coisa.
  std::vector<std::pair<Jogo, std::string>> baseline;
  for (const Jogo& j : {kShield, kScarlet, kBdsp, kLgpe, kPla})
    baseline.emplace_back(j, HashOf(std::string(SIM_SAVES) + j.rel));

  TestFeliz(kShield, kScarlet);
  TestFeliz(kScarlet, kShield);
  TestFeliz(kBdsp, kShield);
  TestFeliz(kLgpe, kShield);
  TestBloqueado();
  TestComItem();
  TestItemBagNaoSuportada();
  TestIdaEVolta();
  TestAtomicidade();
  TestOriginaisIntactos(baseline);

  if (g_failures) {
    std::printf("\n%d FALHA(S)\n", g_failures);
    return 1;
  }
  std::printf("\ntodos os testes passaram\n");
  return 0;
}
