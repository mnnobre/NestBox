// Verificador de legalidade (spec 079) — a prova de que ele acerta.
//
// O DEFEITO PROIBIDO E O FALSO POSITIVO. Recusar um Pokemon legitimo do dono
// destroi a confianca no app; deixar passar um adulterado so mantem o estado
// atual. Por isso a parte 1 e a que pode reprovar este teste sozinha: os 40
// Pokemon de tests/saves-limpos sao 100% legais pelo PkHeX (spec 077), e
// nenhum pode ser marcado como suspeito.
//
// GUARDRAIL: os saves em build/switch-sim/saves/ e tests/saves-limpos/ sao do
// dono e SOMENTE LEITURA. Este teste nunca escreve — nem em sandbox, porque
// nao precisa: ele so le e verifica.
//
// O corpus dos saves reais entra como medida de DETECCAO, comparada com a
// medicao do oraculo (tools/pkhex-probe079, PkHeX): 90,98%.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "legality.h"
#include "gen3_save.h"
#include <cstring>
#include "pkm_convert.h"
#include "save_writer.h"

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

// std::filesystem::path e obrigatorio no ifstream (Windows: o construtor de
// string usa a codepage ANSI).
static std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(fs::path(path), std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

struct Jogo {
  const char* nome;
  const char* rel;
  savew::Game game;
};

// ---------------------------------------------------------------------------
// PARTE 1 — FALSO POSITIVO: os 40 Pokemon limpos da spec 077.
//
// Se algum destes for reprovado, a verificacao esta errada — nao o Pokemon.
// ---------------------------------------------------------------------------
static const Jogo kLimpos[] = {
    {"swsh", "swsh/main", savew::Game::kSwSh},
    {"sv", "sv/main", savew::Game::kSV},
    {"bdsp", "bdsp/SaveData.bin", savew::Game::kBDSP},
    {"pla", "pla/main", savew::Game::kPLA},
    {"lgpe", "lgpe/savedata.bin", savew::Game::kLGPE},
};

static void TestFalsoPositivo() {
  std::printf("\n=== PARTE 1: falso positivo nos saves LIMPOS (spec 077) ===\n");
  int total = 0, reprovados = 0;

  for (const auto& j : kLimpos) {
    const std::string path = std::string(CLEAN_SAVES) + j.rel;
    auto sd = savew::Load(ReadFile(path), j.game);
    if (!sd) {
      Check(false, std::string("save limpo abriu: ") + j.nome);
      continue;
    }
    int n = 0, bad = 0;
    for (std::size_t b = 0; b < sd->box_count; ++b) {
      for (std::size_t s = 0; s < sd->slots_per_box; ++s) {
        const auto& slot = sd->At(b, s);
        if (!slot.present) continue;
        ++n;
        const auto r = legality::CheckLegality(slot.mon);
        if (r.suspect) {
          ++bad;
          // Detalhar o motivo: um falso positivo tem de ser diagnosticavel
          // sem reabrir o save a mao.
          std::printf("    FP %s dex=%u form=%u:", j.nome,
                      pkm::NationalDex(slot.mon), slot.mon.form);
          for (const auto& i : r.issues) std::printf(" [%s]", i.code.c_str());
          std::printf("\n");
        }
      }
    }
    total += n;
    reprovados += bad;
    Check(bad == 0, std::string(j.nome) + ": " + std::to_string(n) +
                        " Pokemon legais, " + std::to_string(bad) +
                        " reprovados (esperado 0)");
  }

  std::printf("  TOTAL LIMPO: %d Pokemon, %d reprovados\n", total, reprovados);
  Check(total == 40, "os 40 Pokemon da spec 077 foram lidos (lidos " +
                         std::to_string(total) + ")");
  Check(reprovados == 0, "FALSO POSITIVO ZERO nos Pokemon legais");
}

// ---------------------------------------------------------------------------
// PARTE 2 — DETECCAO nos saves REAIS do dono (somente leitura).
//
// O oraculo (PkHeX, via tools/pkhex-probe079) mediu 90,98% com este mesmo
// conjunto de criterios. Aqui medimos quantos o NOSSO codigo pega. Os saves
// sao os mesmos; a contagem de ilegais por save vem da medicao registrada no
// evidence-log, porque o teste nao tem o PkHeX para consultar.
// ---------------------------------------------------------------------------
struct SaveReal {
  const char* nome;
  const char* rel;
  savew::Game game;
  int mons;        // total medido pelo oraculo
  int ilegais;     // ilegais medidos pelo oraculo
  int esperado;    // quantos o nosso criterio pega (medido na sondagem)
};

static const SaveReal kReais[] = {
    {"BDSP", "0100000011D90000/Amaral/SaveData.bin", savew::Game::kBDSP, 255, 255, 255},
    // LGPE: 90 e nao 92 DE PROPOSITO. Os dois que faltam (Eevee forma 1 e
    // Pikachu) divergem do calculado por ARREDONDAMENTO DE FLOAT — 31,55294
    // contra 31,552942 e 28,8 contra 28,800001. O PkHeX compara float por
    // igualdade exata e por isso os reprova; nos usamos tolerancia de 1%,
    // porque tratar ruido de float como adulteracao e a receita do falso
    // positivo. Diagnosticado na sondagem (spec 079, rodada 11).
    {"LGPE", "0100187003A36000/Amaral/savedata.bin", savew::Game::kLGPE, 92, 92, 90},
    {"Shield", "01008DB008C2C000/Amaral/main", savew::Game::kSwSh, 194, 194, 194},
    {"Scarlet", "0100A3D008C5C000/Amaral/main", savew::Game::kSV, 440, 440, 439},
    {"Sword", "0100ABF008968000/Amaral/main", savew::Game::kSwSh, 24, 0, 0},
    {"Z-A", "0100F43008C44000/Amaral/main", savew::Game::kZA, 96, 0, 0},
    // PLA: o oraculo nao contou Pokemon neste save (0 mons na caixa). Entra
    // como controle de que o formato PA8 nao gera falso positivo.
    {"PLA", "01001F5010DFA000/Amaral/main", savew::Game::kPLA, 0, 0, 0},
};

static void TestDeteccao() {
  std::printf("\n=== PARTE 2: deteccao nos saves REAIS (somente leitura) ===\n");
  int total = 0, suspeitos = 0, ilegais_oraculo = 0;

  for (const auto& s : kReais) {
    const std::string path = std::string(SIM_SAVES) + s.rel;
    auto sd = savew::Load(ReadFile(path), s.game);
    if (!sd) {
      std::printf("  N/A: %s nao abriu (%s)\n", s.nome, s.rel);
      continue;
    }
    int n = 0, sus = 0;
    for (std::size_t b = 0; b < sd->box_count; ++b) {
      for (std::size_t sl = 0; sl < sd->slots_per_box; ++sl) {
        const auto& slot = sd->At(b, sl);
        if (!slot.present) continue;
        ++n;
        if (legality::CheckLegality(slot.mon).suspect) ++sus;
      }
    }
    total += n;
    suspeitos += sus;
    ilegais_oraculo += s.ilegais;
    std::printf("  %-8s %3d mons, %3d ilegais (oraculo), %3d suspeitos "
                "(nosso), esperado %3d\n", s.nome, n, s.ilegais, sus, s.esperado);
    Check(sus == s.esperado,
          std::string(s.nome) + ": deteccao bate com a sondagem (" +
              std::to_string(sus) + " de " + std::to_string(s.esperado) + ")");
  }

  if (ilegais_oraculo > 0) {
    const double taxa = 100.0 * suspeitos / ilegais_oraculo;
    std::printf("  TAXA DE DETECCAO: %d/%d = %.2f%%\n", suspeitos,
                ilegais_oraculo, taxa);
    // A sondagem mediu 90,98% no corpus inteiro (com as duplicatas). Aqui a
    // lista e de saves UNICOS, entao a taxa e a deste subconjunto.
    Check(taxa > 85.0, "a deteccao fica acima de 85% dos ilegais");
  }
}

// ---------------------------------------------------------------------------
// PARTE 3 — SINTETICOS: para CADA regra, um que viola e um que nao.
//
// A fixture legal sai do save limpo, e nao de um Pokemon montado do zero: o
// contexto-tecnico registra (spec 076) que buffer zerado produz Pokemon
// ILEGAL, entao ele nao serve como controle negativo.
// ---------------------------------------------------------------------------
static bool HasCode(const legality::LegalityResult& r, const char* code) {
  for (const auto& i : r.issues) {
    if (i.code == code) return true;
  }
  return false;
}

// Um Pokemon legal de cada formato que precisamos.
static bool PegaLimpo(const Jogo& j, pkm::Pokemon* out) {
  auto sd = savew::Load(ReadFile(std::string(CLEAN_SAVES) + j.rel), j.game);
  if (!sd) return false;
  for (std::size_t b = 0; b < sd->box_count; ++b) {
    for (std::size_t s = 0; s < sd->slots_per_box; ++s) {
      const auto& slot = sd->At(b, s);
      // Ovo nao serve de fixture: varios campos ficam zerados nele.
      if (slot.present && !slot.mon.is_egg) {
        *out = slot.mon;
        return true;
      }
    }
  }
  return false;
}

static void TestSinteticos() {
  std::printf("\n=== PARTE 3: sinteticos, uma violacao por regra ===\n");

  pkm::Pokemon swsh, sv, bdsp, lgpe;
  if (!PegaLimpo(kLimpos[0], &swsh) || !PegaLimpo(kLimpos[1], &sv) ||
      !PegaLimpo(kLimpos[2], &bdsp) || !PegaLimpo(kLimpos[4], &lgpe)) {
    Check(false, "as fixtures limpas foram carregadas");
    return;
  }

  // O controle NEGATIVO comum: a fixture intacta passa.
  Check(!legality::CheckLegality(swsh).suspect, "controle: SwSh limpo passa");
  Check(!legality::CheckLegality(sv).suspect, "controle: SV limpo passa");
  Check(!legality::CheckLegality(bdsp).suspect, "controle: BDSP limpo passa");
  Check(!legality::CheckLegality(lgpe).suspect, "controle: LGPE limpo passa");

  {  // IV > 31
    auto p = swsh; p.ivs[0] = 32;
    Check(HasCode(legality::CheckLegality(p), "iv_range"), "IV 32 acusa");
  }
  {  // EV individual > 252
    auto p = swsh; p.evs[1] = 253;
    Check(HasCode(legality::CheckLegality(p), "ev_range"), "EV 253 acusa");
  }
  {  // soma de EVs > 510
    auto p = swsh;
    for (int i = 0; i < 6; ++i) p.evs[i] = 252;  // soma 1512
    Check(HasCode(legality::CheckLegality(p), "ev_sum"), "soma de EVs acusa");
  }
  {  // nature fora de faixa
    auto p = swsh; p.nature = 25;
    Check(HasCode(legality::CheckLegality(p), "nature"), "nature 25 acusa");
  }
  {  // especie inexistente
    auto p = swsh; p.species = 9999;
    Check(HasCode(legality::CheckLegality(p), "species"),
          "especie 9999 acusa");
  }
  {  // golpe inexistente
    auto p = swsh; p.moves[0] = 30000;
    Check(HasCode(legality::CheckLegality(p), "move"), "golpe 30000 acusa");
  }
  {  // contest stat num jogo sem concurso (SwSh)
    auto p = swsh; p.contest_stats[0] = 1;
    Check(HasCode(legality::CheckLegality(p), "contest_absent"),
          "contest stat em SwSh acusa");
  }
  {  // ... e o MESMO valor no BDSP, que TEM concurso, nao acusa
    auto p = bdsp; p.contest_stats[0] = 1;
    Check(!HasCode(legality::CheckLegality(p), "contest_absent"),
          "contest stat no BDSP NAO acusa (o jogo tem concurso)");
  }
  {  // sheen impossivel no BDSP
    auto p = bdsp; p.contest_sheen = 255;
    Check(HasCode(legality::CheckLegality(p), "contest_sheen"),
          "sheen 255 no BDSP acusa");
  }
  {  // Gigantamax em especie que nao Gmaxa
    auto p = swsh;
    p.species = 447;  // Riolu: nao esta na lista de 31
    p.can_gigantamax = true;
    Check(HasCode(legality::CheckLegality(p), "gmax"),
          "flag Gmax em Riolu acusa");
  }
  {  // ... e num que Gmaxa, nao acusa
    auto p = swsh;
    p.species = 25;  // Pikachu, forma 0
    p.form = 0;
    p.can_gigantamax = true;
    Check(!HasCode(legality::CheckLegality(p), "gmax"),
          "flag Gmax em Pikachu forma 0 NAO acusa");
  }
  {  // Pikachu de Alola (forma 1) NAO Gmaxa — a restricao de forma
    auto p = swsh;
    p.species = 25; p.form = 1; p.can_gigantamax = true;
    Check(HasCode(legality::CheckLegality(p), "gmax"),
          "flag Gmax em Pikachu forma 1 acusa (so a forma 0 Gmaxa)");
  }
  {  // Dynamax fora de faixa
    auto p = swsh; p.dynamax_level = 11;
    Check(HasCode(legality::CheckLegality(p), "dynamax"),
          "dynamax 11 acusa");
  }
  {  // EC == PID
    auto p = swsh; p.encryption_constant = p.pid;
    Check(HasCode(legality::CheckLegality(p), "ec_pid"), "EC == PID acusa");
  }
  {  // apelido vazio
    auto p = swsh; p.nickname.clear();
    Check(HasCode(legality::CheckLegality(p), "nickname_empty"),
          "apelido vazio acusa");
  }
  {  // checksum: mexer no raw sem refazer o checksum
    auto p = swsh;
    if (p.raw.size() > 0x20) {
      p.raw[0x20] = static_cast<std::uint8_t>(p.raw[0x20] ^ 0xFF);
      Check(HasCode(legality::CheckLegality(p), "checksum"),
            "raw alterado sem refazer o checksum acusa");
    }
  }
  {  // sanity
    auto p = swsh; p.sanity = 1;
    Check(HasCode(legality::CheckLegality(p), "sanity"), "sanity != 0 acusa");
  }
  {  // peso/altura: mexer no scalar sem refazer o absoluto gravado (LGPE)
    auto p = lgpe;
    p.height_scalar = static_cast<std::uint8_t>(p.height_scalar ^ 0xFF);
    Check(HasCode(legality::CheckLegality(p), "height"),
          "scalar de altura alterado sem refazer o absoluto acusa (LGPE)");
  }
  {  // ... e o peso
    auto p = lgpe;
    p.weight_scalar = static_cast<std::uint8_t>(p.weight_scalar ^ 0xFF);
    Check(HasCode(legality::CheckLegality(p), "weight"),
          "scalar de peso alterado sem refazer o absoluto acusa (LGPE)");
  }
  {  // slot vazio nunca e suspeito
    pkm::Pokemon vazio;
    Check(!legality::CheckLegality(vazio).suspect,
          "Pokemon vazio nao e suspeito");
  }
}

// --- gen3 (spec 112) --------------------------------------------------------
//
// TODAS as geracoes passam pelo verificador (decisao do dono, 2026-08-18).
// Um registro legitimo passa limpo; cada adulteracao que o formato permite e
// acusada com o codigo certo — vermelho plantado, como manda a regra de
// evidencia.
static pokehome::gen3::FullRecord RegistroLegitimo() {
  pokehome::gen3::FullRecord r;
  r.personality = 0x12345678;
  r.ot_id = 0x00420023;
  pokehome::gen3::EncodeGen3String("PIKACHU", r.nickname_raw,
                                   sizeof(r.nickname_raw));
  r.language = 2;
  r.flags = 0x02;
  pokehome::gen3::EncodeGen3String("ASH", r.ot_name_raw,
                                   sizeof(r.ot_name_raw));
  r.species = 25;
  r.experience = 100000;
  r.moves[0] = 85;
  r.moves[1] = 98;
  r.pp[0] = 15;
  const std::uint8_t evs[6] = {252, 100, 50, 100, 4, 4};
  std::memcpy(r.evs, evs, 6);
  r.origins = static_cast<std::uint16_t>(5 | (4u << 7) | (4u << 11));
  return r;
}

static bool TemCodigo(const legality::LegalityResult& r, const char* code) {
  for (const auto& i : r.issues) {
    if (i.code == code) return true;
  }
  return false;
}

static void TestGen3() {
  std::printf("\n=== gen3 (spec 112) ===\n");
  namespace g3 = pokehome::gen3;

  std::uint8_t raw[80];
  g3::EncodeFullRecord(RegistroLegitimo(), raw);
  Check(!legality::CheckLegalityGen3(raw).suspect,
        "registro gen3 legitimo passa limpo");

  std::uint8_t zeros[80] = {};
  Check(!legality::CheckLegalityGen3(zeros).suspect,
        "slot vazio nao e adulteracao");

  {
    auto r = RegistroLegitimo();
    r.flags |= 0x01;
    g3::EncodeFullRecord(r, raw);
    Check(TemCodigo(legality::CheckLegalityGen3(raw), "bad_egg"),
          "bad egg acusado");
  }
  {
    auto r = RegistroLegitimo();
    r.species = 500;  // acima da tabela gen3
    g3::EncodeFullRecord(r, raw);
    Check(TemCodigo(legality::CheckLegalityGen3(raw), "species"),
          "especie inexistente acusada");
  }
  {
    auto r = RegistroLegitimo();
    r.moves[2] = 999;
    g3::EncodeFullRecord(r, raw);
    Check(TemCodigo(legality::CheckLegalityGen3(raw), "move"),
          "golpe inexistente acusado");
  }
  {
    auto r = RegistroLegitimo();
    const std::uint8_t evs[6] = {255, 255, 255, 255, 255, 255};
    std::memcpy(r.evs, evs, 6);
    g3::EncodeFullRecord(r, raw);
    Check(TemCodigo(legality::CheckLegalityGen3(raw), "ev_sum"),
          "soma de EV acima de 510 acusada");
  }
  {
    auto r = RegistroLegitimo();
    r.experience = 3000000;  // Pikachu (MediumFast) capa em 1.000.000
    g3::EncodeFullRecord(r, raw);
    Check(TemCodigo(legality::CheckLegalityGen3(raw), "exp_max"),
          "exp acima da curva acusada");
  }
  {
    auto r = RegistroLegitimo();
    r.language = 0;
    g3::EncodeFullRecord(r, raw);
    Check(TemCodigo(legality::CheckLegalityGen3(raw), "language"),
          "idioma invalido acusado");
  }
}

// --- concurso por ORIGEM (spec 113) ----------------------------------------
//
// O falso positivo real: Blastoise subido do FireRed (origem 4) com bytes de
// concurso preservados era barrado ao voltar do Z-A. A regra virou "por
// historia": origem gen3/gen4 pode ter concurso; nativo moderno nao.
static void TestContestOrigem() {
  std::printf("\n=== concurso por origem (spec 113) ===\n");

  pkm::Pokemon p;
  p.format = pkm::Format::kPK9;
  p.species = pkm::SpeciesForFormat(9, pkm::Format::kPK9);  // Blastoise
  p.pid = 0x12345678;
  p.encryption_constant = p.pid;
  p.exp = 10000;
  p.contest_stats[0] = 47;  // cool vindo do gen3

  p.origin_game = 4;  // FireRed
  Check(!TemCodigo(legality::CheckLegality(p), "contest_absent"),
        "origem gen3 com pontos de concurso NAO e acusada");

  p.origin_game = 50;  // Scarlet
  Check(TemCodigo(legality::CheckLegality(p), "contest_absent"),
        "nativo moderno com pontos de concurso continua acusado");
}

int main() {
  TestFalsoPositivo();
  TestDeteccao();
  TestSinteticos();
  TestGen3();
  TestContestOrigem();

  std::printf("\n%s (%d falha%s)\n", g_failures == 0 ? "PASSOU" : "FALHOU",
              g_failures, g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
