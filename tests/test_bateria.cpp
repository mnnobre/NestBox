// Bateria de cenarios (spec 078, G14 — o fecho do epico da escrita).
//
// A pergunta que este teste responde, e que nenhum anterior responde: as
// regras da Pokemon HOME valem em TODOS os pares jogo↔jogo, em TODOS os
// casos? A spec 075 provou que a transferencia funciona em 6 pares escolhidos;
// aqui a matriz e inteira.
//
// DESENHO: uma tabela de PARES x uma tabela de CENARIOS, com UM executor. Nao
// uma sequencia de testes copiados — 20 pares x 9 cenarios copiados a mao seria
// codigo que ninguem mantem, e a spec 069 ja registrou o que amostragem por
// conveniencia esconde.
//
// FIXTURE: tests/saves-limpos/ (spec 077) — 8 Pokemon 100% LEGAIS por save,
// OT=NESTBOX. Por isso o criterio aqui e LEGALIDADE ABSOLUTA (decisao do dono
// na descoberta), e nao o criterio DELTA da 075.
//
// GUARDRAIL: nada e escrito no lugar. Todo save abre atraves de
// sandbox::SaveSandbox, e o SHA256 dos originais e conferido no fim.
//
// CELULA SEM SENTIDO NAO SOME: quando um par nao tem fixture para um cenario,
// a matriz imprime `n/a` E o motivo. Omitir em silencio e o falso-verde que a
// spec 066 documenta.
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "bag_writer.h"
#include "game_moves.h"
#include "moveset_memory.h"
#include "species_facts.h"
#include "pkm_convert.h"
#include "save_sandbox.h"
#include "save_writer.h"
#include "sha256.h"
#include "transfer.h"

namespace fs = std::filesystem;
namespace cp = pokehome::compat;

static int g_failures = 0;

// std::filesystem::path e obrigatorio no ifstream (Windows).
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

// ---------------------------------------------------------------------------
// Os 5 saves limpos da spec 077.
// ---------------------------------------------------------------------------
struct Jogo {
  const char* nome;    // curto, cabe na coluna da matriz
  const char* rel;     // caminho dentro de tests/saves-limpos/
  savew::Game game;
};

static const Jogo kJogos[] = {
    {"swsh", "swsh/main", savew::Game::kSwSh},
    {"sv", "sv/main", savew::Game::kSV},
    {"bdsp", "bdsp/SaveData.bin", savew::Game::kBDSP},
    {"pla", "pla/main", savew::Game::kPLA},
    {"lgpe", "lgpe/savedata.bin", savew::Game::kLGPE},
};
static constexpr std::size_t kNJogos = sizeof(kJogos) / sizeof(kJogos[0]);

static std::string CaminhoLimpo(const Jogo& j) {
  return std::string(CLEAN_SAVES) + j.rel;
}

// Um save aberto na COPIA do sandbox.
struct Aberto {
  sandbox::SaveSandbox sb;
  savew::SaveData sd;
};

static std::optional<Aberto> Abrir(const Jogo& j) {
  auto sb = sandbox::SaveSandbox::Create(CaminhoLimpo(j));
  if (!sb) return std::nullopt;
  auto sd = savew::Load(ReadFile(sb->path()), j.game);
  if (!sd) return std::nullopt;
  return Aberto{std::move(*sb), std::move(*sd)};
}

// ---------------------------------------------------------------------------
// O veredito de UMA celula da matriz.
//
// `kNA` NAO e sucesso nem falha: e "este par nao tem como exercitar este
// cenario", e vem sempre com o motivo. Essa distincao existe porque um `n/a`
// silencioso e indistinguivel de um teste que nunca rodou.
// ---------------------------------------------------------------------------
enum class Veredito { kPass, kFail, kNA };

struct Celula {
  Veredito v = Veredito::kNA;
  std::string nota;  // motivo do n/a, ou detalhe do pass/fail
};

static Celula Pass(std::string n = "") { return {Veredito::kPass, std::move(n)}; }
static Celula Fail(std::string n) { return {Veredito::kFail, std::move(n)}; }
static Celula NA(std::string n) { return {Veredito::kNA, std::move(n)}; }

// ---------------------------------------------------------------------------
// Auxiliares compartilhados pelos cenarios.
// ---------------------------------------------------------------------------

// Onde o Pokemon caiu no destino: o primeiro slot que antes estava livre.
static bool AchaNovo(const savew::SaveData& antes, const savew::SaveData& depois,
                     std::size_t* b, std::size_t* s) {
  for (std::size_t bi = 0; bi < depois.box_count; ++bi)
    for (std::size_t si = 0; si < depois.slots_per_box; ++si)
      if (depois.At(bi, si).present && !antes.At(bi, si).present) {
        *b = bi;
        *s = si;
        return true;
      }
  return false;
}

// O primeiro Pokemon da origem que o destino ACEITA. Varrer e obrigatorio:
// pegar "o primeiro do save" e a amostragem por conveniencia que a spec 069
// condenou — nos saves limpos o slot 1 pode ser justamente o bloqueado.
static bool AchaAceito(const Aberto& a, const Aberto& b, cp::Game dest,
                       const pokehome::moveset::Memory& mem,
                       transfer::Request* req) {
  for (std::size_t bi = 0; bi < a.sd.box_count; ++bi)
    for (std::size_t si = 0; si < a.sd.slots_per_box; ++si) {
      if (!a.sd.At(bi, si).present) continue;
      transfer::Request r = *req;
      r.src_box = bi;
      r.src_slot = si;
      auto p = transfer::Prepare(a.sd, b.sd, a.sb.path(), b.sb.path(), dest, r,
                                 mem);
      if (p.result.ok()) {
        *req = r;
        return true;
      }
    }
  return false;
}

// ---------------------------------------------------------------------------
// CENARIO 1 — COMPATIVEL. Passa, e o resultado sobrevive a releitura do disco
// com os campos [HOME] intactos.
//
// A LEGALIDADE (criterio absoluto) e julgada FORA, pelo tools/pkhex-verify: o
// teste deixa os arquivos gravados em CLEAN_OUT para o juiz externo abrir.
// "passa no nosso teste" nao fecha criterio — regra do dono.
// ---------------------------------------------------------------------------
static Celula CenCompativel(const Jogo& A, const Jogo& B) {
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) return Fail("sandbox/Load falhou");
  if (a->sd.Count() == 0) return NA("origem sem Pokemon nas caixas");

  pokehome::moveset::Memory mem;
  transfer::Request req;
  const cp::Game dest = transfer::ToCompatGame(B.game);
  if (!AchaAceito(*a, *b, dest, mem, &req))
    return NA("nenhum Pokemon da origem e aceito pelo destino");

  const std::size_t n_a = a->sd.Count(), n_b = b->sd.Count();
  const pkm::Pokemon origem = a->sd.At(req.src_box, req.src_slot).mon;

  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), dest,
                                req, mem);
  if (!plan.result.ok())
    return Fail(std::string("Prepare: ") + transfer::StatusName(plan.result.status));
  const auto r = transfer::Commit(plan);
  if (!r.ok()) return Fail(std::string("Commit: ") + transfer::StatusName(r.status));

  auto sa = savew::Load(ReadFile(a->sb.path()), A.game);
  auto sb2 = savew::Load(ReadFile(b->sb.path()), B.game);
  if (!sa || !sb2) return Fail("os saves gravados nao reabrem");
  if (sa->Count() != n_a - 1) return Fail("origem nao caiu 1");
  if (sb2->Count() != n_b + 1) return Fail("destino nao subiu 1");

  std::size_t db, ds;
  if (!AchaNovo(b->sd, *sb2, &db, &ds)) return Fail("nao achou o novo no destino");
  const pkm::Pokemon& d = sb2->At(db, ds).mon;

  // Especie por National Dex nos DOIS lados: no PK9 o campo cru e o indice
  // interno do gen9 (contexto-tecnico.md).
  if (pkm::NationalDex(d) != pkm::NationalDex(origem)) return Fail("especie mudou");
  if (d.pid != origem.pid) return Fail("PID mudou");
  if (d.ivs != origem.ivs) return Fail("IVs mudaram");
  if (d.evs != origem.evs) return Fail("EVs mudaram");
  if (d.ot_name != origem.ot_name) return Fail("OT mudou");
  if (d.tid != origem.tid || d.sid != origem.sid) return Fail("TID/SID mudaram");
  if (d.ball != origem.ball) return Fail("ball mudou");
  // O tracker so e exigivel onde o FORMATO tem onde guarda-lo. O PB7 (Let's
  // Go, gen7b) NAO tem campo de HOME tracker — o formato e anterior ao HOME e
  // o proprio PKHeX.Core nao expoe `Tracker` nele (pb7.h ja declara a
  // ausencia). Exigir tracker ali seria o teste cobrando do produto uma coisa
  // que o binario de destino nao representa. Ver P-01 do evidence-log.
  if (B.game != savew::Game::kLGPE && d.home_tracker == 0)
    return Fail("sem tracker no destino");
  if (d.held_item != 0) return Fail("o item viajou (§7)");

  // Os arquivos ficam para o juiz externo. O nome carrega o par.
  std::error_code ec;
  fs::create_directories(fs::path(CLEAN_OUT), ec);
  const std::string base = std::string(CLEAN_OUT) + A.nome + "_" + B.nome;
  fs::copy_file(fs::path(a->sb.path()), fs::path(base + ".src.bin"),
                fs::copy_options::overwrite_existing, ec);
  fs::copy_file(fs::path(b->sb.path()), fs::path(base + ".dst.bin"),
                fs::copy_options::overwrite_existing, ec);

  return Pass("dex " + std::to_string(pkm::NationalDex(origem)));
}

// ---------------------------------------------------------------------------
// CENARIO 2 — ESPECIE BLOQUEADA. Nada e escrito em NENHUM dos dois arquivos.
//
// A prova e o SHA256 dos DOIS, nao "o teste nao reclamou".
// ---------------------------------------------------------------------------
static Celula CenBloqueado(const Jogo& A, const Jogo& B) {
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) return Fail("sandbox/Load falhou");
  if (a->sd.Count() == 0) return NA("origem sem Pokemon nas caixas");

  const cp::Game dest = transfer::ToCompatGame(B.game);
  pokehome::moveset::Memory mem;

  // Um Pokemon cuja ESPECIE nao existe no destino. Se a origem nao tem
  // nenhum, plantamos um na COPIA: o que se testa e a REGRA, e a fixture de 8
  // Pokemon pode nao conter o caso natural em todo par.
  transfer::Request req;
  bool achou = false, plantado = false;
  for (std::size_t bi = 0; bi < a->sd.box_count && !achou; ++bi)
    for (std::size_t si = 0; si < a->sd.slots_per_box && !achou; ++si) {
      if (!a->sd.At(bi, si).present) continue;
      const auto& m = a->sd.At(bi, si).mon;
      if (m.is_egg) continue;  // o ovo tem cenario proprio (6)
      if (!cp::HasSpecies(dest, pkm::NationalDex(m))) {
        req.src_box = bi;
        req.src_slot = si;
        achou = true;
      }
    }
  if (!achou) {
    // Planta a especie inexistente sobre o primeiro slot ocupado da COPIA.
    // Dex 1000 (Gholdengo) nao existe fora de SV; dex 700 (Sylveon) nao
    // existe no LGPE. Escolhe pelo destino.
    for (std::size_t bi = 0; bi < a->sd.box_count && !plantado; ++bi)
      for (std::size_t si = 0; si < a->sd.slots_per_box && !plantado; ++si) {
        if (!a->sd.At(bi, si).present) continue;
        pkm::Pokemon m = a->sd.At(bi, si).mon;
        for (std::uint16_t dex : {1000, 905, 700, 493}) {
          if (cp::HasSpecies(dest, dex)) continue;
          const auto sp = pkm::SpeciesForFormat(dex, m.format);
          if (sp == 0) continue;
          m.species = sp;
          m.form = 0;
          m.is_egg = false;
          a->sd.Set(bi, si, m);
          req.src_box = bi;
          req.src_slot = si;
          plantado = true;
          break;
        }
      }
    if (!plantado)
      return NA("o destino aceita todas as especies testadas (nenhuma fixture)");
  }

  const std::string ha = HashOf(a->sb.path()), hb = HashOf(b->sb.path());
  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), dest,
                                req, mem);
  if (plan.result.status != transfer::Status::kBlocked)
    return Fail(std::string("esperava kBlocked, veio ") +
                transfer::StatusName(plan.result.status));
  if (plan.result.message.empty()) return Fail("kBlocked sem motivo");
  if (transfer::Commit(plan).status != transfer::Status::kBlocked)
    return Fail("Commit nao recusou o plano bloqueado");
  if (HashOf(a->sb.path()) != ha) return Fail("o arquivo de ORIGEM mudou");
  if (HashOf(b->sb.path()) != hb) return Fail("o arquivo de DESTINO mudou");

  return Pass(std::string(plantado ? "plantado; " : "") + "hash dos 2 intacto");
}

// ---------------------------------------------------------------------------
// CENARIO 3 — COM ITEM. held_item zera E a bag da ORIGEM sobe 1 (§7).
//
// A escrita do item e no save de ORIGEM: o item nunca viaja. Onde a bag da
// origem nao e suportada pelo bag_writer, a limitacao vira `item_lost`
// DECLARADO — nao um pulo em silencio.
// ---------------------------------------------------------------------------
static Celula CenItem(const Jogo& A, const Jogo& B) {
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) return Fail("sandbox/Load falhou");
  if (a->sd.Count() == 0) return NA("origem sem Pokemon nas caixas");

  const cp::Game dest = transfer::ToCompatGame(B.game);
  pokehome::moveset::Memory mem;
  transfer::Request req;
  req.level = 100;  // o BDSP recusa Hyper Training abaixo de 100
  if (!AchaAceito(*a, *b, dest, mem, &req))
    return NA("nenhum Pokemon da origem e aceito pelo destino");

  // Master Ball (id 1) existe em todos os jogos. Plantada porque a regra e o
  // que se testa, e nem todo save limpo tem item segurado em slot aceito.
  const std::uint16_t item = 1;
  pkm::Pokemon m = a->sd.At(req.src_box, req.src_slot).mon;
  m.held_item = item;
  a->sd.Set(req.src_box, req.src_slot, m);
  const std::uint16_t antes = bagw::CountOf(a->sd, item);

  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), dest,
                                req, mem);
  if (!plan.result.ok())
    return Fail(std::string("Prepare: ") + transfer::StatusName(plan.result.status));
  if (plan.result.item_returned != item) return Fail("item_returned nao declarado");

  if (!bagw::Supported(A.game)) {
    // Nao e sucesso: e limitacao conhecida, e o produto TEM de declarar.
    if (!plan.result.item_lost)
      return Fail("bag nao suportada e item_lost NAO declarado (silencio)");
    return NA("bag de " + std::string(A.nome) +
              " nao suportada; item_lost DECLARADO (TD-03 da 075)");
  }

  if (plan.result.item_lost) return Fail("item_lost com bag suportada");
  if (bagw::CountOf(plan.src, item) != antes + 1)
    return Fail("a bag da origem nao subiu 1");

  const auto r = transfer::Commit(plan);
  if (!r.ok()) return Fail(std::string("Commit: ") + transfer::StatusName(r.status));

  auto sa = savew::Load(ReadFile(a->sb.path()), A.game);
  auto sb2 = savew::Load(ReadFile(b->sb.path()), B.game);
  if (!sa || !sb2) return Fail("os saves gravados nao reabrem");
  if (bagw::CountOf(*sa, item) != antes + 1)
    return Fail("a bag RELIDA do disco nao tem +1");

  std::size_t db, ds;
  if (!AchaNovo(b->sd, *sb2, &db, &ds)) return Fail("nao achou o novo no destino");
  if (sb2->At(db, ds).mon.held_item != 0) return Fail("o item VIAJOU (§7)");

  return Pass("bag " + std::to_string(antes) + "->" +
              std::to_string(bagw::CountOf(*sa, item)));
}

// ---------------------------------------------------------------------------
// CENARIO 4 — GOLPE AUSENTE NO DESTINO. Passa com kWarning, nao bloqueia.
//
// TD-02 da spec 070 / spec 038: o destino reseta o moveset por conta propria,
// entao a regra AVISA. Plantamos um golpe fora do alcance do destino.
// ---------------------------------------------------------------------------
static Celula CenGolpeAusente(const Jogo& A, const Jogo& B) {
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) return Fail("sandbox/Load falhou");
  if (a->sd.Count() == 0) return NA("origem sem Pokemon nas caixas");

  const cp::Game dest = transfer::ToCompatGame(B.game);
  pokehome::moveset::Memory mem;
  transfer::Request req;
  if (!AchaAceito(*a, *b, dest, mem, &req))
    return NA("nenhum Pokemon da origem e aceito pelo destino");

  // Um golpe que o destino nao conhece: o primeiro acima do teto dele.
  const int teto = cp::MaxMoveId(dest);
  const int ausente = teto + 1;
  if (cp::HasMove(dest, ausente))
    return NA("o destino conhece todo golpe acima do teto (tabela suspeita)");

  pkm::Pokemon m = a->sd.At(req.src_box, req.src_slot).mon;
  m.moves[3] = static_cast<std::uint16_t>(ausente);
  a->sd.Set(req.src_box, req.src_slot, m);

  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), dest,
                                req, mem);
  if (plan.result.status == transfer::Status::kBlocked)
    return Fail("golpe ausente BLOQUEOU (deveria avisar): " + plan.result.message);
  if (!plan.result.ok())
    return Fail(std::string("Prepare: ") + transfer::StatusName(plan.result.status));
  if (!plan.result.warning)
    return Fail("passou SEM kWarning — o aviso se perdeu no caminho");

  return Pass("warning: " + plan.result.warning_reason);
}

// ---------------------------------------------------------------------------
// CENARIO 5 — LENDARIO PARA BDSP: o primeiro passa, o segundo e bloqueado.
//
// So faz sentido com o BDSP como DESTINO — a regra "1 exemplar por save" e
// dele (§7). Nos outros destinos a celula e n/a com o motivo.
// ---------------------------------------------------------------------------
static Celula CenLendarioBdsp(const Jogo& A, const Jogo& B) {
  if (B.game != savew::Game::kBDSP)
    return NA("a regra de 1 lendario por save e do BDSP");
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) return Fail("sandbox/Load falhou");

  const cp::Game dest = transfer::ToCompatGame(B.game);
  pokehome::moveset::Memory mem;
  transfer::Request req;
  req.level = 100;

  // Um lendario que o BDSP tenha e que o destino ainda nao possua. O
  // inventario da 077 poe um lendario em cada save limpo.
  bool achou = false;
  for (std::size_t bi = 0; bi < a->sd.box_count && !achou; ++bi)
    for (std::size_t si = 0; si < a->sd.slots_per_box && !achou; ++si) {
      if (!a->sd.At(bi, si).present) continue;
      transfer::Request r = req;
      r.src_box = bi;
      r.src_slot = si;
      const auto& m = a->sd.At(bi, si).mon;
      if (!pokehome::species::IsLegendary(pkm::NationalDex(m))) continue;
      auto p = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), dest,
                                 r, mem);
      if (p.result.ok()) {
        req = r;
        achou = true;
      }
    }
  if (!achou) return NA("nenhum lendario da origem e aceito pelo BDSP");

  const std::uint16_t dex = pkm::NationalDex(a->sd.At(req.src_box, req.src_slot).mon);

  // PRIMEIRO: passa.
  auto p1 = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), dest,
                              req, mem);
  if (!p1.result.ok()) return Fail("o PRIMEIRO lendario foi recusado");
  if (!transfer::Commit(p1).ok()) return Fail("Commit do primeiro falhou");

  // SEGUNDO: o mesmo lendario, agora com o destino ja tendo um. Releitura do
  // disco — e o destino REAL que a regra tem de enxergar.
  auto sa = savew::Load(ReadFile(a->sb.path()), A.game);
  auto sb2 = savew::Load(ReadFile(b->sb.path()), B.game);
  if (!sa || !sb2) return Fail("os saves gravados nao reabrem");

  // Planta um clone do mesmo lendario num slot livre da ORIGEM relida.
  pkm::Pokemon clone = b->sd.Count() ? pkm::Pokemon{} : pkm::Pokemon{};
  {
    std::size_t db, ds;
    if (!AchaNovo(b->sd, *sb2, &db, &ds)) return Fail("nao achou o lendario no BDSP");
    auto conv = pkm::Convert(sb2->At(db, ds).mon, sa->At(0, 0).present
                                                      ? sa->At(0, 0).mon.format
                                                      : pkm::Format::kPK8);
    if (!conv) return Fail("o clone nao converte de volta para a origem");
    clone = *conv;
  }
  bool posto = false;
  transfer::Request req2;
  req2.level = 100;
  for (std::size_t bi = 0; bi < sa->box_count && !posto; ++bi)
    for (std::size_t si = 0; si < sa->slots_per_box && !posto; ++si)
      if (!sa->At(bi, si).present && sa->Set(bi, si, clone)) {
        req2.src_box = bi;
        req2.src_slot = si;
        posto = true;
      }
  if (!posto) return Fail("nao deu para plantar o clone na origem");

  auto p2 = transfer::Prepare(*sa, *sb2, a->sb.path(), b->sb.path(), dest, req2,
                              mem);
  if (p2.result.status != transfer::Status::kBlocked)
    return Fail(std::string("o SEGUNDO lendario NAO foi bloqueado: ") +
                transfer::StatusName(p2.result.status));

  return Pass("dex " + std::to_string(dex) + "; 2o: " + p2.result.message);
}

// ---------------------------------------------------------------------------
// CENARIO 6 — OVO. Bloqueado em todo destino (§4/§7: o HOME so ve Pokemon).
// ---------------------------------------------------------------------------
static Celula CenOvo(const Jogo& A, const Jogo& B) {
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) return Fail("sandbox/Load falhou");
  if (a->sd.Count() == 0) return NA("origem sem Pokemon nas caixas");

  const cp::Game dest = transfer::ToCompatGame(B.game);
  pokehome::moveset::Memory mem;
  transfer::Request req;

  // Um ovo natural, se a fixture tem (swsh e sv tem). Senao, planta: a REGRA
  // vale para todo jogo, mesmo os que o inventario da 077 nao deu ovo.
  bool natural = false;
  for (std::size_t bi = 0; bi < a->sd.box_count && !natural; ++bi)
    for (std::size_t si = 0; si < a->sd.slots_per_box && !natural; ++si)
      if (a->sd.At(bi, si).present && a->sd.At(bi, si).mon.is_egg) {
        req.src_box = bi;
        req.src_slot = si;
        natural = true;
      }
  if (!natural) {
    bool posto = false;
    for (std::size_t bi = 0; bi < a->sd.box_count && !posto; ++bi)
      for (std::size_t si = 0; si < a->sd.slots_per_box && !posto; ++si) {
        if (!a->sd.At(bi, si).present) continue;
        pkm::Pokemon m = a->sd.At(bi, si).mon;
        m.is_egg = true;
        a->sd.Set(bi, si, m);
        req.src_box = bi;
        req.src_slot = si;
        posto = true;
      }
    if (!posto) return NA("origem sem Pokemon para marcar como ovo");
  }

  const std::string ha = HashOf(a->sb.path()), hb = HashOf(b->sb.path());
  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), dest,
                                req, mem);
  if (plan.result.status != transfer::Status::kBlocked)
    return Fail(std::string("ovo NAO bloqueado: ") +
                transfer::StatusName(plan.result.status));
  if (HashOf(a->sb.path()) != ha || HashOf(b->sb.path()) != hb)
    return Fail("o bloqueio escreveu em disco");

  return Pass(std::string(natural ? "ovo da fixture" : "ovo plantado") + ": " +
              plan.result.message);
}

// ---------------------------------------------------------------------------
// CENARIO 7 — SHINY sobrevive a transferencia.
//
// pkm::IsShiny antes E depois, sobre o Pokemon RELIDO do disco. O shiny nao e
// um flag: deriva de PID/TID/SID, entao ele so sobrevive se os tres
// sobreviverem a conversao de formato.
// ---------------------------------------------------------------------------
static Celula CenShiny(const Jogo& A, const Jogo& B) {
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) return Fail("sandbox/Load falhou");
  if (a->sd.Count() == 0) return NA("origem sem Pokemon nas caixas");

  const cp::Game dest = transfer::ToCompatGame(B.game);
  pokehome::moveset::Memory mem;
  transfer::Request req;
  req.level = 100;

  bool achou = false;
  for (std::size_t bi = 0; bi < a->sd.box_count && !achou; ++bi)
    for (std::size_t si = 0; si < a->sd.slots_per_box && !achou; ++si) {
      if (!a->sd.At(bi, si).present) continue;
      if (!pkm::IsShiny(a->sd.At(bi, si).mon)) continue;
      transfer::Request r = req;
      r.src_box = bi;
      r.src_slot = si;
      auto p = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), dest,
                                 r, mem);
      if (p.result.ok()) {
        req = r;
        achou = true;
      }
    }
  if (!achou) return NA("nenhum shiny da origem e aceito pelo destino");

  const pkm::Pokemon origem = a->sd.At(req.src_box, req.src_slot).mon;
  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), dest,
                                req, mem);
  if (!transfer::Commit(plan).ok()) return Fail("Commit falhou");

  auto sb2 = savew::Load(ReadFile(b->sb.path()), B.game);
  if (!sb2) return Fail("o destino gravado nao reabre");
  std::size_t db, ds;
  if (!AchaNovo(b->sd, *sb2, &db, &ds)) return Fail("nao achou o novo no destino");
  const pkm::Pokemon& d = sb2->At(db, ds).mon;

  if (!pkm::IsShiny(origem)) return Fail("a fixture escolhida nao era shiny");
  if (!pkm::IsShiny(d)) return Fail("o shiny SUMIU na transferencia");

  return Pass("dex " + std::to_string(pkm::NationalDex(d)));
}

// ---------------------------------------------------------------------------
// CENARIO 8 — GIGANTAMAX para BDSP/PLA: bloqueado (§7).
//
// So faz sentido nesses dois destinos. O Gmax e plantado: os saves limpos da
// 077 nao tem nenhum marcado (por design — sao Pokemon legais).
// ---------------------------------------------------------------------------
static Celula CenGigantamax(const Jogo& A, const Jogo& B) {
  if (B.game != savew::Game::kBDSP && B.game != savew::Game::kPLA)
    return NA("so BDSP e PLA recusam Gigantamax");
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) return Fail("sandbox/Load falhou");
  if (a->sd.Count() == 0) return NA("origem sem Pokemon nas caixas");

  const cp::Game dest = transfer::ToCompatGame(B.game);
  pokehome::moveset::Memory mem;
  transfer::Request req;
  req.level = 100;
  if (!AchaAceito(*a, *b, dest, mem, &req))
    return NA("nenhum Pokemon da origem e aceito pelo destino (sem base para o caso)");

  pkm::Pokemon m = a->sd.At(req.src_box, req.src_slot).mon;
  m.can_gigantamax = true;
  a->sd.Set(req.src_box, req.src_slot, m);

  const std::string ha = HashOf(a->sb.path()), hb = HashOf(b->sb.path());
  auto plan = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), dest,
                                req, mem);
  if (plan.result.status != transfer::Status::kBlocked)
    return Fail(std::string("Gmax NAO bloqueado: ") +
                transfer::StatusName(plan.result.status));
  if (HashOf(a->sb.path()) != ha || HashOf(b->sb.path()) != hb)
    return Fail("o bloqueio escreveu em disco");

  return Pass(plan.result.message);
}

// ---------------------------------------------------------------------------
// CENARIO 9 — IDA E VOLTA A->B->A. Campos [HOME] preservados e, quando A
// memoriza moveset (PLA/BDSP — engine propria, §7), o moveset de A restaurado
// pelo tracker (spec 071).
// ---------------------------------------------------------------------------
static Celula CenIdaEVolta(const Jogo& A, const Jogo& B) {
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) return Fail("sandbox/Load falhou");
  if (a->sd.Count() == 0) return NA("origem sem Pokemon nas caixas");

  const cp::Game destB = transfer::ToCompatGame(B.game);
  const cp::Game destA = transfer::ToCompatGame(A.game);
  pokehome::moveset::Memory mem;
  transfer::Request req;
  req.level = 100;
  if (!AchaAceito(*a, *b, destB, mem, &req))
    return NA("nenhum Pokemon da origem e aceito pelo destino");

  const pkm::Pokemon origem = a->sd.At(req.src_box, req.src_slot).mon;
  const auto moves_a = origem.moves;

  auto ida = transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), destB,
                               req, mem);
  if (!ida.result.ok()) return Fail("ida recusada");
  if (!transfer::Commit(ida).ok()) return Fail("Commit da ida falhou");
  mem = ida.memory;

  auto s_b = savew::Load(ReadFile(b->sb.path()), B.game);
  auto s_a = savew::Load(ReadFile(a->sb.path()), A.game);
  if (!s_b || !s_a) return Fail("os saves nao reabrem apos a ida");
  std::size_t vb, vs;
  if (!AchaNovo(b->sd, *s_b, &vb, &vs)) return Fail("nao achou o Pokemon em B");

  transfer::Request volta;
  volta.src_box = vb;
  volta.src_slot = vs;
  volta.level = 100;
  auto pv = transfer::Prepare(*s_b, *s_a, b->sb.path(), a->sb.path(), destA,
                              volta, mem);
  if (!pv.result.ok())
    return Fail(std::string("volta recusada: ") + pv.result.message);
  if (!transfer::Commit(pv).ok()) return Fail("Commit da volta falhou");

  auto s_final = savew::Load(ReadFile(a->sb.path()), A.game);
  if (!s_final) return Fail("A final nao reabre");
  std::size_t fb, fs2;
  if (!AchaNovo(*s_a, *s_final, &fb, &fs2)) return Fail("o Pokemon nao voltou a A");
  const pkm::Pokemon& f = s_final->At(fb, fs2).mon;

  // Os campos [HOME] atravessaram A->B->A.
  if (pkm::NationalDex(f) != pkm::NationalDex(origem)) return Fail("especie mudou na volta");
  if (f.pid != origem.pid) return Fail("PID mudou na volta");
  if (f.ivs != origem.ivs) return Fail("IVs mudaram na volta");
  if (f.ot_name != origem.ot_name) return Fail("OT mudou na volta");
  // O tracker so atravessa se os DOIS formatos tiverem onde guarda-lo. O PB7
  // (Let's Go) nao tem o campo — ver o comentario no cenario 1. Passando por
  // ele, o tracker e ZERO na volta, e isso e propriedade do formato, nao bug
  // nosso. Ver P-01.
  const bool ha_tracker =
      A.game != savew::Game::kLGPE && B.game != savew::Game::kLGPE;
  if (ha_tracker && f.home_tracker == 0)
    return Fail("tracker perdido na volta");

  // O moveset so e memorizado por quem tem engine propria (PLA/BDSP).
  pokehome::moveset::Game dummy;
  if (!transfer::MemorizesMoveset(A.game, &dummy))
    return Pass("[HOME] ok; " + std::string(A.nome) + " nao memoriza moveset");
  // A memoria da spec 071 e indexada pelo TRACKER. Sem tracker no caminho, ela
  // nao tem chave — a consequencia e o reset por nivel, nao a restauracao.
  if (!ha_tracker)
    return Pass("[HOME] ok; sem tracker no caminho (LGPE), a memoria nao tem chave");
  if (!pv.result.moveset_restored)
    return Fail("A memoriza moveset mas a volta nao restaurou da memoria");
  if (f.moves != moves_a) return Fail("o moveset de A NAO foi restaurado");

  return Pass("moveset restaurado " + std::to_string(moves_a[0]) + "/" +
              std::to_string(moves_a[1]) + "/" + std::to_string(moves_a[2]) +
              "/" + std::to_string(moves_a[3]));
}

// ---------------------------------------------------------------------------
// A MATRIZ.
// ---------------------------------------------------------------------------
struct Cenario {
  const char* nome;
  Celula (*fn)(const Jogo&, const Jogo&);
};

static const Cenario kCenarios[] = {
    {"1-compat", CenCompativel},
    {"2-bloq", CenBloqueado},
    {"3-item", CenItem},
    {"4-golpe", CenGolpeAusente},
    {"5-lend", CenLendarioBdsp},
    {"6-ovo", CenOvo},
    {"7-shiny", CenShiny},
    {"8-gmax", CenGigantamax},
    {"9-volta", CenIdaEVolta},
};
static constexpr std::size_t kNCen = sizeof(kCenarios) / sizeof(kCenarios[0]);

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  // A linha de base do guardrail, ANTES de qualquer coisa. Os saves limpos sao
  // fixture versionada: nenhum byte deles pode mudar.
  std::vector<std::pair<std::string, std::string>> baseline;
  for (const Jogo& j : kJogos)
    baseline.emplace_back(j.nome, HashOf(CaminhoLimpo(j)));

  std::printf("=== MATRIZ: %zu pares x %zu cenarios ===\n\n",
              kNJogos * (kNJogos - 1), kNCen);

  // Cabecalho.
  std::printf("%-14s", "par");
  for (const auto& c : kCenarios) std::printf("%-10s", c.nome);
  std::printf("\n");

  int pass = 0, fail = 0, na = 0;
  std::vector<std::string> detalhes;

  for (std::size_t i = 0; i < kNJogos; ++i) {
    for (std::size_t k = 0; k < kNJogos; ++k) {
      if (i == k) continue;
      const Jogo& A = kJogos[i];
      const Jogo& B = kJogos[k];
      const std::string par = std::string(A.nome) + "->" + B.nome;
      std::printf("%-14s", par.c_str());
      for (const auto& c : kCenarios) {
        // Breadcrumb em stderr: 180 celulas fazem I/O de save, e sem ele um
        // travamento nao diz QUAL celula travou. Nao polui stdout (a matriz).
        std::fprintf(stderr, "[celula] %s %s\n", par.c_str(), c.nome);
        std::fflush(stderr);
        const Celula cel = c.fn(A, B);
        switch (cel.v) {
          case Veredito::kPass: ++pass; std::printf("%-10s", "PASS"); break;
          case Veredito::kFail: ++fail; std::printf("%-10s", "FAIL"); break;
          case Veredito::kNA: ++na; std::printf("%-10s", "n/a"); break;
        }
        if (!cel.nota.empty())
          detalhes.push_back(par + "  " + c.nome + "  " +
                             (cel.v == Veredito::kFail ? "FAIL: "
                              : cel.v == Veredito::kNA ? "n/a: "
                                                       : "pass: ") +
                             cel.nota);
      }
      std::printf("\n");
    }
  }

  std::printf("\n=== DETALHE DE CADA CELULA ===\n");
  for (const auto& d : detalhes) std::printf("%s\n", d.c_str());

  std::printf("\nPASS=%d  FAIL=%d  n/a=%d  (total %d celulas)\n", pass, fail, na,
              pass + fail + na);

  // O guardrail: os saves limpos continuam byte-identicos.
  std::printf("\n=== GUARDRAIL: as fixtures de tests/saves-limpos/ ===\n");
  for (const auto& [nome, h] : baseline) {
    const Jogo* j = nullptr;
    for (const Jogo& x : kJogos)
      if (nome == x.nome) j = &x;
    const std::string agora = HashOf(CaminhoLimpo(*j));
    if (agora != h) {
      std::printf("FALHOU: %s: SHA256 MUDOU\n", nome.c_str());
      ++g_failures;
    } else {
      std::printf("  ok: %s: sha256 inalterado (%s...)\n", nome.c_str(),
                  h.substr(0, 16).c_str());
    }
  }

  if (fail || g_failures) {
    std::printf("\n%d FALHA(S)\n", fail + g_failures);
    return 1;
  }
  std::printf("\ntodos os cenarios exercitados passaram\n");
  return 0;
}
