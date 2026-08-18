#include "pa8.h"

#include "pkm_crypto.h"
#include "pkm_write_util.h"

namespace pa8 {
namespace {

// Offsets do PA8. Marcador por origem:
//   [ok]  confirmado contra as fixtures do PkHeX — o valor bate.
//   [pkhex] confirmado contra o PRoPRIO PkHeX (spec 066): grava-se o campo
//         pelo setter do PKHeX.Core e observa-se qual byte do buffer muda.
//         Nao depende de a fixture trazer o campo preenchido.
//
// Nao ha mais offset `[~]`: a 066 sondou os ~20 que faltavam.
//
// A spec 066 sondou os ~20 offsets que a spec 060 deixou marcados `[~]` e
// achou 14 ERRADOS. A causa: eles foram derivados por analogia ao PK8, mas o
// PA8 tem bloco 0x58 (nao 0x50) e o layout depois do apelido esta deslocado.
// As 7 fixtures traziam esses campos zerados, entao o teste comparava 0 com
// 0 e passava lendo do lugar errado — exatamente o risco registrado na 060.
enum Off : std::size_t {
  kEC = 0,                 // [ok]
  kSanity = 4,             // [pkhex]
  kChecksum = 6,           // [ok]
  kSpecies = 8,            // [ok] National Dex direto (TD-01), nao indice interno
  kHeldItem = 10,          // [pkhex]
  kTid = 12,               // [ok]
  kSid = 14,               // [ok]
  kExp = 16,               // [ok]
  kAbility = 20,           // [ok]
  // [pkhex] bits 0-2 ability number, bit 3 favorito, bit 5 ALPHA, bit 6 NOBLE.
  // O alpha/noble moram AQUI, nao em 156/157 (ver kIsAlpha abaixo).
  kAbilityNum = 22,
  kMarkings = 24,          // [pkhex]
  kPid = 28,               // [ok]
  kNature = 32,            // [ok]
  kStatNature = 33,        // [ok]
  kFatefulGender = 34,     // [ok] gender em (b>>2)&3; fateful no bit 0
  kForm = 36,              // [pkhex]
  kEvs = 38,               // [ok] 6 bytes, ordem fisica HP/Atk/Def/Spe/SpA/SpD
  kContest = 44,           // [pkhex] 5 stats + sheen (sheen conferido em 49)
  kPokerus = 50,           // [pkhex]
  kRibbonsA = 52,          // [pkhex] 8 bytes (52..59), 62 ribbons classicas
  kContestMemoryCount = 60,  // [pkhex]
  kBattleMemoryCount = 61,   // [pkhex]
  kAlphaMove = 62,         // [ok] so PLA — u16 (523 no Tangrowth, 116 no Gallade)
  kRibbonsB = 64,          // [pkhex] 8 bytes (64..71), as marks do gen8+
  kHeight = 80,            // [ok]
  kWeight = 81,            // [ok]
  kHeightAbsolute = 172,   // 0xAC float (spec 121)
  kWeightAbsolute = 176,   // 0xB0 float (spec 121)
  kScale = 82,             // [ok] no PA8 o Scale do PkHeX repete o height scalar
  kMoves = 84,             // [ok] 4 x u16
  kPp = 92,                // [ok] 4 x u8
  kNickname = 96,          // [ok] 26 bytes UTF-16LE
  // CORRIGIDO na 066: era 122/126 por analogia ao PK8. O apelido do PA8 tem
  // 26 bytes a partir de 96 (termina em 121), mas ha 12 bytes de folga antes
  // dos PP-ups — o layout NAO e colado como no PK8.
  kPpUps = 134,            // [pkhex] 4 x u8
  kRelearn = 138,          // [pkhex] 4 x u16
  kIvsEggNick = 148,       // [ok] u32: 6x5 bits + egg + nicknamed
  kHpCurrent = 146,  // HP atual, no NUCLEO (spec 119)
  kStatus = 156,           // [ok] u32
  // CORRIGIDO na 066: alpha/noble NAO ficam em 156/157. Sao os bits 5 e 6 do
  // byte 22 (o mesmo do ability number). O byte 156 so parecia certo porque,
  // nas fixtures, o status_condition do alpha calhava de valer 1 — ler dali
  // acertava por coincidencia, que e o bug que a pendencia existia para pegar.
  kAlphaNobleBits = 22,    // [pkhex] bit 5 = alpha, bit 6 = noble
  kEffortLevels = 164,     // [ok] so PLA — 6 bytes (GV_* do PkHeX)
  // CORRIGIDO na 066: todo o bloco do HT estava 8 bytes a frente.
  kHtName = 184,           // [pkhex] 26 bytes
  kHtGender = 210,         // [pkhex]
  kHtLanguage = 211,       // [pkhex]
  kCurrentHandler = 212,   // [pkhex]
  kHtId = 214,             // [pkhex]
  kHtFriendship = 216,     // [pkhex]
  kHtMemoryIntensity = 217,  // [pkhex]
  kHtMemory = 218,         // [pkhex]
  kHtFeeling = 219,        // [pkhex]
  kHtTextVar = 220,        // [pkhex] u16
  kOriginGame = 238,       // [ok] constante 47 (PLA) nas 7 fixtures
  kLanguage = 242,         // [ok]
  kFormArgument = 244,     // [pkhex] u32
  kAffixedRibbon = 248,    // [ok] constante 255 = "nenhuma" (o -1 do PkHeX)
  kOtName = 272,           // [ok] 26 bytes
  kOtFriendship = 298,     // [ok]
  kOtMemoryIntensity = 299,  // [pkhex]
  kOtMemory = 300,         // [pkhex]
  kOtTextVar = 302,        // [pkhex] CORRIGIDO na 066: era 301 (301 e padding)
  kOtFeeling = 304,        // [pkhex]
  kEggDate = 305,          // [pkhex] ano-2000, mes, dia
  kMetDate = 308,          // [ok] ano-2000, mes, dia
  kBall = 311,             // [ok]
  kEggLocation = 312,      // [pkhex] u16
  kMetLocation = 314,      // [ok]
  kMetLevelOtGender = 317,  // [ok] nivel em & 0x7F, genero do OT no bit 7
  kHyperTrain = 318,       // [pkhex]
  // CORRIGIDO na 066: era 320. O tracker do PA8 comeca em 333 — offset impar,
  // que e justamente o tipo de coisa que a analogia ao PK8 nao adivinha.
  kHomeTracker = 333,      // [pkhex] u64
};

std::uint16_t U16(const std::uint8_t* d, std::size_t o) {
  return static_cast<std::uint16_t>(d[o] | d[o + 1] << 8);
}
std::uint32_t U32(const std::uint8_t* d, std::size_t o) {
  return static_cast<std::uint32_t>(d[o]) |
         static_cast<std::uint32_t>(d[o + 1]) << 8 |
         static_cast<std::uint32_t>(d[o + 2]) << 16 |
         static_cast<std::uint32_t>(d[o + 3]) << 24;
}
std::uint64_t U64(const std::uint8_t* d, std::size_t o) {
  return static_cast<std::uint64_t>(U32(d, o)) |
         static_cast<std::uint64_t>(U32(d, o + 4)) << 32;
}

std::string Utf16ToUtf8(const std::uint8_t* d, std::size_t o) {
  std::string out;
  for (int i = 0; i < 13; ++i) {
    const std::uint16_t c = U16(d, o + i * 2);
    if (c == 0) break;
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (c >> 12)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

}  // namespace

std::optional<pkm::Pokemon> Parse(const std::uint8_t* data, std::size_t size) {
  if (size != kStoredSize && size != kPartySize) return std::nullopt;

  std::vector<std::uint8_t> buf(data, data + size);
  if (!pkc::IsDecrypted(buf.data(), pkc::kBlockPA8)) {
    pkc::Decrypt(buf.data(), buf.size(), pkc::kBlockPA8);
    if (!pkc::IsDecrypted(buf.data(), pkc::kBlockPA8)) return std::nullopt;
  }
  const std::uint8_t* d = buf.data();

  pkm::Pokemon p;
  p.format = pkm::Format::kPA8;
  p.raw = buf;

  p.encryption_constant = U32(d, kEC);
  p.sanity = U16(d, kSanity);
  p.checksum = U16(d, kChecksum);

  p.species = U16(d, kSpecies);
  p.held_item = U16(d, kHeldItem);
  p.tid = U16(d, kTid);
  p.sid = U16(d, kSid);
  p.exp = U32(d, kExp);
  p.ability = U16(d, kAbility);
  p.ability_number = d[kAbilityNum] & 7;
  p.favorite = (d[kAbilityNum] >> 3) & 1;
  p.markings = U32(d, kMarkings);
  p.pid = U32(d, kPid);
  p.nature = d[kNature];
  p.stat_nature = d[kStatNature];
  p.fateful_encounter = d[kFatefulGender] & 1;
  p.gender = (d[kFatefulGender] >> 2) & 3;
  p.form = static_cast<std::uint8_t>(U16(d, kForm));

  for (int i = 0; i < 6; ++i) p.evs[i] = d[kEvs + i];
  for (int i = 0; i < 5; ++i) p.contest_stats[i] = d[kContest + i];
  p.contest_sheen = d[kContest + 5];
  p.pokerus = d[kPokerus];

  for (int i = 0; i < 8; ++i) p.ribbon_bytes[i] = d[kRibbonsA + i];
  for (int i = 0; i < 8; ++i) p.ribbon_bytes[8 + i] = d[kRibbonsB + i];
  p.ribbon_count_memory = d[kContestMemoryCount];
  p.affixed_ribbon = d[kAffixedRibbon];

  p.height_scalar = d[kHeight];
  p.weight_scalar = d[kWeight];
  p.height_absolute = pkw::RF32(d, kHeightAbsolute);
  p.weight_absolute = pkw::RF32(d, kWeightAbsolute);
  p.scale = d[kScale];

  p.nickname = Utf16ToUtf8(d, kNickname);
  for (int i = 0; i < 4; ++i) p.moves[i] = U16(d, kMoves + i * 2);
  for (int i = 0; i < 4; ++i) p.pp[i] = d[kPp + i];
  for (int i = 0; i < 4; ++i) p.pp_ups[i] = d[kPpUps + i];
  for (int i = 0; i < 4; ++i) p.relearn_moves[i] = U16(d, kRelearn + i * 2);

  const std::uint32_t iv32 = U32(d, kIvsEggNick);
  for (int i = 0; i < 6; ++i) {
    p.ivs[i] = static_cast<std::uint8_t>((iv32 >> (5 * i)) & 0x1F);
  }
  p.is_egg = (iv32 >> 30) & 1;
  p.is_nicknamed = (iv32 >> 31) & 1;

  p.hp_current = U16(d, kHpCurrent);
  p.status_condition = U32(d, kStatus);

  // Especificos de PLA. Alpha e noble sao bits do byte 22 (spec 066), nao
  // bytes proprios em 156/157 como a spec 060 supos.
  p.is_alpha = (d[kAlphaNobleBits] >> 5) & 1;
  p.is_noble = (d[kAlphaNobleBits] >> 6) & 1;
  for (int i = 0; i < 6; ++i) p.effort_levels[i] = d[kEffortLevels + i];
  p.alpha_move = U16(d, kAlphaMove);

  p.ht_name = Utf16ToUtf8(d, kHtName);
  p.ht_gender = d[kHtGender];
  p.ht_language = d[kHtLanguage];
  p.current_handler = d[kCurrentHandler];
  p.ht_id = U16(d, kHtId);
  p.ht_friendship = d[kHtFriendship];
  p.ht_memory.intensity = d[kHtMemoryIntensity];
  p.ht_memory.memory = d[kHtMemory];
  p.ht_memory.feeling = d[kHtFeeling];
  p.ht_memory.text_var = U16(d, kHtTextVar);

  p.origin_game = d[kOriginGame];
  p.form_argument = U32(d, kFormArgument);
  p.language = d[kLanguage];

  p.ot_name = Utf16ToUtf8(d, kOtName);
  p.ot_friendship = d[kOtFriendship];
  p.ot_memory.intensity = d[kOtMemoryIntensity];
  p.ot_memory.memory = d[kOtMemory];
  p.ot_memory.text_var = U16(d, kOtTextVar);
  p.ot_memory.feeling = d[kOtFeeling];

  for (int i = 0; i < 3; ++i) p.egg_date[i] = d[kEggDate + i];
  for (int i = 0; i < 3; ++i) p.met_date[i] = d[kMetDate + i];
  p.egg_location = U16(d, kEggLocation);
  p.met_location = U16(d, kMetLocation);
  p.ball = d[kBall];
  p.met_level = d[kMetLevelOtGender] & 0x7F;
  p.ot_gender = d[kMetLevelOtGender] >> 7;

  // Mesma conversao do PK8/PK9: flag em ordem de exibicao -> ordem fisica.
  const std::uint8_t ht = d[kHyperTrain];
  static constexpr int kHtBitForPhysical[6] = {0, 1, 2, 5, 3, 4};
  for (int i = 0; i < 6; ++i) {
    p.hyper_trained[i] = (ht >> kHtBitForPhysical[i]) & 1;
  }

  p.home_tracker = U64(d, kHomeTracker);

  // Zerados de proposito (TD-03): o PA8 nao tem tera type, obedience,
  // dynamax/gigantamax, sociability, palma, fullness/enjoyment nem TM flags.
  return p;
}

std::optional<pkm::Pokemon> Parse(const std::vector<std::uint8_t>& data) {
  return Parse(data.data(), data.size());
}

std::vector<std::uint8_t> Write(const pkm::Pokemon& p) {
  std::vector<std::uint8_t> buf = p.raw;  // TD-01
  if (buf.size() != kStoredSize && buf.size() != kPartySize) {
    buf.assign(kStoredSize, 0);  // TD-02
  }
  std::uint8_t* d = buf.data();

  pkw::W32(d, kEC, p.encryption_constant);
  pkw::W16(d, kSanity, p.sanity);

  pkw::W16(d, kSpecies, p.species);
  pkw::W16(d, kHeldItem, p.held_item);
  pkw::W16(d, kTid, p.tid);
  pkw::W16(d, kSid, p.sid);
  pkw::W32(d, kExp, p.exp);
  pkw::W16(d, kAbility, p.ability);
  // Byte 22: bits 0-2 ability number, bit 3 favorito, bit 5 alpha, bit 6
  // noble (spec 066 — o alpha/noble nao ficam em 156/157). Os bits 4 e 7
  // continuam preservados do raw.
  d[kAbilityNum] = static_cast<std::uint8_t>(
      (d[kAbilityNum] & 0x90) | (p.ability_number & 7) |
      (p.favorite ? 0x08 : 0) | (p.is_alpha ? 0x20 : 0) |
      (p.is_noble ? 0x40 : 0));
  pkw::W32(d, kMarkings, p.markings);
  pkw::W32(d, kPid, p.pid);
  d[kNature] = p.nature;
  d[kStatNature] = p.stat_nature;
  d[kFatefulGender] = static_cast<std::uint8_t>(
      (d[kFatefulGender] & 0xF2) | (p.fateful_encounter ? 1 : 0) |
      ((p.gender & 3) << 2));
  pkw::W16(d, kForm, p.form);

  for (int i = 0; i < 6; ++i) d[kEvs + i] = p.evs[i];
  for (int i = 0; i < 5; ++i) d[kContest + i] = p.contest_stats[i];
  d[kContest + 5] = p.contest_sheen;
  d[kPokerus] = p.pokerus;

  for (int i = 0; i < 8; ++i) d[kRibbonsA + i] = p.ribbon_bytes[i];
  for (int i = 0; i < 8; ++i) d[kRibbonsB + i] = p.ribbon_bytes[8 + i];
  d[kContestMemoryCount] = p.ribbon_count_memory;
  d[kAffixedRibbon] = p.affixed_ribbon;

  d[kHeight] = p.height_scalar;
  d[kWeight] = p.weight_scalar;
  pkw::WF32(d, kHeightAbsolute, p.height_absolute);
  pkw::WF32(d, kWeightAbsolute, p.weight_absolute);
  d[kScale] = p.scale;

  pkw::WriteUtf16(d, kNickname, p.nickname, 13);
  for (int i = 0; i < 4; ++i) pkw::W16(d, kMoves + i * 2, p.moves[i]);
  for (int i = 0; i < 4; ++i) d[kPp + i] = p.pp[i];
  for (int i = 0; i < 4; ++i) d[kPpUps + i] = p.pp_ups[i];
  for (int i = 0; i < 4; ++i) {
    pkw::W16(d, kRelearn + i * 2, p.relearn_moves[i]);
  }

  const std::uint8_t ivs[6] = {p.ivs[0], p.ivs[1], p.ivs[2],
                               p.ivs[3], p.ivs[4], p.ivs[5]};
  pkw::W32(d, kIvsEggNick, pkw::PackIvs(ivs, p.is_egg, p.is_nicknamed));

  // A briga entre status_condition e is_alpha que a spec 060 registrou nao
  // existe: os dois nunca dividiram byte. O alpha/noble vao no byte 22 (junto
  // do ability number, acima) e 156 e so o status. Spec 066.
  pkw::W16(d, kHpCurrent, p.hp_current);
  pkw::W32(d, kStatus, p.status_condition);
  for (int i = 0; i < 6; ++i) d[kEffortLevels + i] = p.effort_levels[i];
  pkw::W16(d, kAlphaMove, p.alpha_move);

  pkw::WriteUtf16(d, kHtName, p.ht_name, 13);
  d[kHtGender] = p.ht_gender;
  d[kHtLanguage] = p.ht_language;
  d[kCurrentHandler] = p.current_handler;
  pkw::W16(d, kHtId, p.ht_id);
  d[kHtFriendship] = p.ht_friendship;
  d[kHtMemoryIntensity] = p.ht_memory.intensity;
  d[kHtMemory] = p.ht_memory.memory;
  d[kHtFeeling] = p.ht_memory.feeling;
  pkw::W16(d, kHtTextVar, p.ht_memory.text_var);

  d[kOriginGame] = p.origin_game;
  pkw::W32(d, kFormArgument, p.form_argument);
  d[kLanguage] = p.language;

  pkw::WriteUtf16(d, kOtName, p.ot_name, 13);
  d[kOtFriendship] = p.ot_friendship;
  d[kOtMemoryIntensity] = p.ot_memory.intensity;
  d[kOtMemory] = p.ot_memory.memory;
  pkw::W16(d, kOtTextVar, p.ot_memory.text_var);
  d[kOtFeeling] = p.ot_memory.feeling;

  for (int i = 0; i < 3; ++i) d[kEggDate + i] = p.egg_date[i];
  for (int i = 0; i < 3; ++i) d[kMetDate + i] = p.met_date[i];
  pkw::W16(d, kEggLocation, p.egg_location);
  pkw::W16(d, kMetLocation, p.met_location);
  d[kBall] = p.ball;
  d[kMetLevelOtGender] = static_cast<std::uint8_t>((p.met_level & 0x7F) |
                                                   (p.ot_gender ? 0x80 : 0));

  const bool ht[6] = {p.hyper_trained[0], p.hyper_trained[1],
                      p.hyper_trained[2], p.hyper_trained[3],
                      p.hyper_trained[4], p.hyper_trained[5]};
  d[kHyperTrain] = pkw::PackHyperTrain(ht);

  pkw::W64(d, kHomeTracker, p.home_tracker);

  pkw::W16(d, kChecksum, pkc::Checksum(d, pkc::kBlockPA8));
  return buf;
}

}  // namespace pa8
