#include "legality.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "body_size.h"
#include "game_moves.h"
#include "gen3_moves.h"  // kMoveCount do gen3 (spec 112)
#include "gen3_save.h"   // DecodeFullRecord/NationalDex/MaxExp (spec 112)
#include "gmax_species.h"
#include "pkm_convert.h"
#include "pkm_crypto.h"
#include "species_facts.h"

namespace legality {
namespace {

void Add(LegalityResult& r, const char* code, std::string reason) {
  r.suspect = true;
  r.issues.push_back({code, std::move(reason)});
}

// --- nivel 1: checksum e sanity -------------------------------------------
//
// A pesquisa mediu que isto pega ZERO nos saves reais: quem edita usa
// ferramenta que recalcula o checksum. Entra assim mesmo porque custa duas
// linhas e um dia pode aparecer um save corrompido de verdade.
void CheckChecksum(const pkm::Pokemon& p, LegalityResult& r) {
  std::size_t block = 0;
  switch (p.format) {
    case pkm::Format::kPB7: block = pkc::kBlockPB7; break;
    case pkm::Format::kPK8:
    case pkm::Format::kPB8:
    case pkm::Format::kPK9: block = pkc::kBlockPK8; break;
    case pkm::Format::kPA8: block = pkc::kBlockPA8; break;
    default: return;
  }
  // `raw` vazio e Pokemon montado em memoria, nao lido de save: nao ha
  // checksum gravado a conferir.
  if (p.raw.size() < 8 + 4 * block) return;

  const std::uint16_t calc = pkc::Checksum(p.raw.data(), block);
  if (calc != p.checksum) {
    Add(r, "checksum",
        "O checksum gravado nao bate com o conteudo do Pokemon.");
  }
  // Sanity != 0 e flag interna que o jogo nao usa em Pokemon de caixa.
  if (p.sanity != 0) {
    Add(r, "sanity", "O campo de sanidade do formato nao esta zerado.");
  }
}

// --- faixas puras ----------------------------------------------------------
const char* kStatNames[6] = {"HP", "Ataque", "Defesa", "Velocidade",
                             "Ataque Especial", "Defesa Especial"};

void CheckRanges(const pkm::Pokemon& p, LegalityResult& r) {
  for (int i = 0; i < 6; ++i) {
    if (p.ivs[i] > 31) {
      Add(r, "iv_range", std::string("O IV de ") + kStatNames[i] + " e " +
                             std::to_string(p.ivs[i]) + "; o maximo e 31.");
    }
  }
  int ev_sum = 0;
  for (int i = 0; i < 6; ++i) {
    ev_sum += p.evs[i];
    if (p.evs[i] > 252) {
      Add(r, "ev_range", std::string("O EV de ") + kStatNames[i] + " e " +
                             std::to_string(p.evs[i]) + "; o maximo e 252.");
    }
  }
  if (ev_sum > 510) {
    Add(r, "ev_sum", "A soma dos EVs e " + std::to_string(ev_sum) +
                         "; o maximo e 510.");
  }
  if (p.nature > 24) {
    Add(r, "nature", "A natureza e " + std::to_string(p.nature) +
                         "; so existem 25 (0 a 24).");
  }
}

// --- especie e forma -------------------------------------------------------
void CheckSpecies(const pkm::Pokemon& p, LegalityResult& r) {
  const std::uint16_t dex = pkm::NationalDex(p);
  if (dex == 0 || dex > pokehome::species::kMaxDex) {
    Add(r, "species", "A especie " + std::to_string(dex) + " nao existe.");
  }
}

// --- golpes ----------------------------------------------------------------
//
// So a EXISTENCIA do id. "A especie aprende este golpe" ficou de fora de
// proposito: learnset.h so cobre PLA e BDSP, e aplicar isso aos outros jogos
// reprovaria Pokemon legal em massa.
void CheckMoves(const pkm::Pokemon& p, LegalityResult& r) {
  for (int i = 0; i < 4; ++i) {
    if (p.moves[i] > pokehome::compat::kMaxMoveId) {
      Add(r, "move", "O golpe " + std::to_string(p.moves[i]) +
                         " nao existe em jogo nenhum.");
    }
  }
}

// --- nivel vs experiencia --------------------------------------------------
void CheckLevel(const pkm::Pokemon& p, LegalityResult& r) {
  const std::uint16_t dex = pkm::NationalDex(p);
  if (dex == 0 || dex > pokehome::species::kMaxDex) return;  // ja acusado
  const std::uint8_t level = pokehome::species::LevelFromExp(dex, p.exp);
  if (level == 0) return;
  // O nivel 100 e o teto: exp acima do exigido pelo 100 continua sendo 100.
  if (level < 1 || level > 100) {
    Add(r, "level", "A experiencia " + std::to_string(p.exp) +
                        " nao corresponde a nivel nenhum.");
  }
}

// --- contest stats ---------------------------------------------------------
//
// O criterio NAO e "fora de faixa" (sao bytes, tudo cabe). E por HISTORIA,
// como no PkHeX (spec 113): concurso existe no gen3 (codigos de origem 1-5 e
// 15), no gen4 (7-12) e no BDSP. Um Pokemon com ORIGEM nesses jogos pode ter
// pontos legitimos mesmo guardado em pk8/pk9 — foi o falso positivo real que
// barrou o Blastoise do dono voltando do Z-A (a subida preserva os bytes de
// concurso do gen3, §7). Ate a spec 113 o criterio era por FORMATO, e isso
// condenava qualquer origem antiga.
//
// Medido na spec 079 (regra por formato): pegava 633 Pokemon com zero falso
// positivo nos 40 limpos; re-medido na 113 com a regra por origem.
void CheckContest(const pkm::Pokemon& p, LegalityResult& r) {
  const bool has_contest =
      p.format == pkm::Format::kPB8 ||
      (p.origin_game >= 1 && p.origin_game <= 15);

  int sum = 0;
  for (int i = 0; i < 6; ++i) sum += p.contest_stats[i];

  if (!has_contest) {
    if (sum != 0 || p.contest_sheen != 0) {
      Add(r, "contest_absent",
          "O Pokemon tem pontos de concurso, mas o jogo de origem nao tem "
          "concurso nenhum.");
    }
    return;
  }
  // BDSP: o teto de sheen medido no PkHeX (BestSheenStat8b) e 120. 255 e o
  // valor que um editor grava ao "encher tudo", e nao e alcancavel jogando.
  if (p.contest_sheen == 255) {
    Add(r, "contest_sheen",
        "O brilho de concurso e 255, acima do maximo que o jogo concede.");
  }
}

// --- Gigantamax ------------------------------------------------------------
void CheckGigantamax(const pkm::Pokemon& p, LegalityResult& r) {
  if (!p.can_gigantamax) return;
  const std::uint16_t dex = pkm::NationalDex(p);
  if (!pokehome::gmax::CanGigantamax(dex, p.form)) {
    Add(r, "gmax",
        "O Pokemon esta marcado como Gigantamax, mas esta especie nao "
        "Gigantamaxa.");
  }
}

// --- Dynamax ---------------------------------------------------------------
void CheckDynamax(const pkm::Pokemon& p, LegalityResult& r) {
  if (p.dynamax_level > 10) {
    Add(r, "dynamax", "O nivel de Dynamax e " +
                          std::to_string(p.dynamax_level) +
                          "; o maximo e 10.");
  }
}

// --- encryption constant ---------------------------------------------------
//
// EC igual ao PID e assinatura de Pokemon montado do zero por um editor: o
// jogo sorteia os dois de forma independente e a coincidencia teria chance
// 1 em 2^32.
//
// EXCECAO por historia (spec 114, mesmo criterio do PkHeX): Pokemon com
// ORIGEM ate a gen5 (codigos 1..23) ganharam EC na transferencia para cima,
// e a regra la e EC = PID — o Bank/HOME fazem assim, e a nossa subida gen3
// tambem. Para essas origens a igualdade e o esperado, nao a suspeita.
void CheckEcPid(const pkm::Pokemon& p, LegalityResult& r) {
  if (p.origin_game >= 1 && p.origin_game <= 23) return;
  if (p.encryption_constant == p.pid && p.pid != 0) {
    Add(r, "ec_pid",
        "A semente de cifra e o PID sao identicos, o que o jogo nao produz.");
  }
}

// --- apelido vazio ---------------------------------------------------------
void CheckNickname(const pkm::Pokemon& p, LegalityResult& r) {
  if (p.nickname.empty()) {
    Add(r, "nickname_empty", "O Pokemon esta sem nome.");
  }
}

// --- peso e altura ---------------------------------------------------------
//
// So o LGPE (PB7) e o Legends: Arceus (PA8) gravam o peso/altura ABSOLUTO
// dentro do binario; nos outros formatos so os scalars existem e nao ha o que
// divergir. As formulas e os offsets foram MEDIDOS contra o PkHeX (spec 079):
//
//   PB7 0x2C/0xE4  altura = H * (hs/255*0.8 + 0.6)
//                  peso   = W * (hs/255*0.8 + 0.6) * (ws/255*0.4 + 0.8)
//   PA8 0xAC/0xB0  altura = H * (hs/255*0.4 + 0.8)
//                  peso   = W * (hs/255*0.4 + 0.8) * (ws/255*0.4 + 0.8)
//
// ATENCAO: o fator da ALTURA difere entre os dois jogos. Trocar um pelo outro
// reprova Pokemon legal — foi por isso que cada um foi ajustado separadamente.
float ReadF32(const std::vector<std::uint8_t>& d, std::size_t off) {
  std::uint32_t bits = static_cast<std::uint32_t>(d[off]) |
                       (static_cast<std::uint32_t>(d[off + 1]) << 8) |
                       (static_cast<std::uint32_t>(d[off + 2]) << 16) |
                       (static_cast<std::uint32_t>(d[off + 3]) << 24);
  float out;
  static_assert(sizeof(out) == sizeof(bits), "float de 32 bits");
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

void CheckBodySize(const pkm::Pokemon& p, LegalityResult& r) {
  std::size_t h_off = 0, w_off = 0;
  const pokehome::body::Entry* table = nullptr;
  std::size_t n = 0;
  double h_a = 0, h_b = 0;

  if (p.format == pkm::Format::kPB7) {
    h_off = 0x2C; w_off = 0xE4;
    table = pokehome::body::kGg;
    n = sizeof(pokehome::body::kGg) / sizeof(pokehome::body::kGg[0]);
    h_a = 0.8; h_b = 0.6;
  } else if (p.format == pkm::Format::kPA8) {
    h_off = 0xAC; w_off = 0xB0;
    table = pokehome::body::kLa;
    n = sizeof(pokehome::body::kLa) / sizeof(pokehome::body::kLa[0]);
    h_a = 0.4; h_b = 0.8;
  } else {
    return;
  }

  if (p.raw.size() < w_off + 4 || p.raw.size() < h_off + 4) return;

  std::uint16_t base_h = 0, base_w = 0;
  const std::uint16_t dex = pkm::NationalDex(p);
  // Especie/forma ausente da tabela: NAO ha altura base, e chutar uma
  // reprovaria Pokemon legal. Silencio e a resposta certa aqui.
  if (!pokehome::body::Lookup(table, n, dex, p.form, &base_h, &base_w)) return;
  if (base_h == 0 || base_w == 0) return;

  const double fh = p.height_scalar / 255.0 * h_a + h_b;
  const double fw = p.weight_scalar / 255.0 * 0.4 + 0.8;
  const float calc_h = static_cast<float>(base_h * fh);
  const float calc_w = static_cast<float>(base_w * fh * fw);

  const float got_h = ReadF32(p.raw, h_off);
  const float got_w = ReadF32(p.raw, w_off);

  // Tolerancia relativa: a conta do jogo e em float e a nossa em double, e a
  // diferenca de arredondamento medida foi < 1e-3 em valores ate 4600. Exigir
  // igualdade binaria transformaria ruido de float em falso positivo.
  const auto off_by = [](float got, float calc) {
    const float scale = calc > 1.0f ? calc : 1.0f;
    return std::fabs(got - calc) / scale > 0.01f;
  };

  if (off_by(got_h, calc_h)) {
    Add(r, "height", "A altura gravada nao bate com a calculada a partir do "
                     "tamanho do Pokemon.");
  }
  if (off_by(got_w, calc_w)) {
    Add(r, "weight", "O peso gravado nao bate com o calculado a partir do "
                     "tamanho do Pokemon.");
  }
}

}  // namespace

LegalityResult CheckLegality(const pkm::Pokemon& p) {
  LegalityResult r;
  // Slot vazio nao e adulteracao.
  if (p.empty() || p.format == pkm::Format::kNone) return r;

  CheckChecksum(p, r);
  CheckRanges(p, r);
  CheckSpecies(p, r);
  CheckMoves(p, r);
  CheckLevel(p, r);
  CheckContest(p, r);
  CheckGigantamax(p, r);
  CheckDynamax(p, r);
  CheckEcPid(p, r);
  CheckNickname(p, r);
  CheckBodySize(p, r);
  return r;
}

LegalityResult CheckLegalityGen3(const std::uint8_t raw[80]) {
  LegalityResult r;
  const auto full = pokehome::gen3::DecodeFullRecord(raw);
  if (!full) return r;  // vazio nao e adulteracao

  const auto Add = [&r](const char* code, std::string reason) {
    r.suspect = true;
    r.issues.push_back({code, std::move(reason)});
  };

  // Bad egg: o proprio jogo ja condenou este registro (checksum quebrado em
  // algum momento). E o unico veredito que vem pronto do save.
  if (full->flags & 0x01) Add("bad_egg", "O jogo marcou este Pokemon como Bad Egg.");

  // Checksum do registro (0x1C): o jogo confere e vira Bad Egg na hora. O
  // Encode e o inverso exato do Decode (roundtrip da spec 108), entao os
  // unicos bytes que podem divergir na reconstrucao sao os do checksum — se
  // divergirem, o valor gravado estava errado (spec 114, Q3).
  {
    std::uint8_t rebuilt[80];
    pokehome::gen3::EncodeFullRecord(*full, rebuilt);
    if (raw[0x1C] != rebuilt[0x1C] || raw[0x1D] != rebuilt[0x1D]) {
      Add("checksum", "O checksum do registro nao confere — o jogo mostraria "
                      "um Bad Egg.");
    }
  }

  const int dex = pokehome::gen3::NationalDex(full->species);
  if (dex == 0) {
    Add("species", "A especie " + std::to_string(full->species) +
                       " nao existe no gen3.");
  }

  for (int i = 0; i < 4; ++i) {
    if (full->moves[i] >= pokehome::gen3::kMoveCount) {
      Add("move", "O golpe " + std::to_string(full->moves[i]) +
                      " nao existe no gen3.");
    }
  }

  int ev_sum = 0;
  for (int i = 0; i < 6; ++i) ev_sum += full->evs[i];
  if (ev_sum > 510) {
    Add("ev_sum",
        "A soma dos EVs e " + std::to_string(ev_sum) + "; o maximo e 510.");
  }

  // Exp acima do teto da curva no nivel 100: nenhum caminho legitimo produz.
  if (dex != 0 && full->experience > pokehome::gen3::MaxExp(full->species)) {
    Add("exp_max", "A experiencia passa do maximo do nivel 100.");
  }

  // Idiomas do gen3: 1=JPN 2=ENG 3=FRE 4=ITA 5=GER 6=KOR(nunca usado) 7=SPA.
  // Zero e acima de 7 nao existem; o 6 fica tolerado (na duvida, nao acusa).
  if (full->language == 0 || full->language > 7) {
    Add("language",
        "O idioma " + std::to_string(full->language) + " nao existe.");
  }

  return r;
}

}  // namespace legality
