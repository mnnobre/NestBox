#include "pk9.h"

#include "pkm_crypto.h"
#include "pkm_write_util.h"

namespace pk9 {
namespace {

// Offsets do PK9 (pk9_buffer.rs). Iguais ao PK8 ate 49; daí divergem — ver
// a tabela de diferencas na spec 059.
enum Off : std::size_t {
  kEC = 0,
  kSanity = 4,
  kChecksum = 6,
  kSpecies = 8,
  kHeldItem = 10,
  kTid = 12,
  kSid = 14,
  kExp = 16,
  kAbility = 20,
  kAbilityNum = 22,
  kMarkings = 24,
  kPid = 28,
  kNature = 32,
  kStatNature = 33,
  kFatefulGender = 34,
  kForm = 36,
  kEvs = 38,
  kContest = 44,
  kRibbonsA = 52,
  kContestMemoryCount = 60,
  kBattleMemoryCount = 61,
  kRibbonsB = 64,
  kHeight = 72,       // PK8: 80
  kWeight = 73,       // PK8: 81
  kScale = 74,        // so PK9
  kTmFlagsDlc = 75,   // 13 bytes
  kNickname = 88,
  kMoves = 114,
  kPp = 122,
  kPpUps = 126,
  kRelearn = 130,
  kIvsEggNick = 140,
  kHpCurrent = 138,  // HP atual, no NUCLEO (spec 119)
  kStatus = 144,          // PK8: 148
  kTeraOriginal = 148,    // so PK9
  kTeraOverride = 149,    // so PK9
  kHtName = 168,
  kHtGender = 194,
  kHtLanguage = 195,
  kCurrentHandler = 196,
  kHtId = 198,
  kHtFriendship = 200,
  kHtMemoryIntensity = 201,
  kHtMemory = 202,
  kHtFeeling = 203,
  kHtTextVar = 204,
  kOriginGame = 206,      // PK8: 222
  kFormArgument = 208,    // PK8: 228
  kAffixedRibbon = 212,   // PK8: 232
  kLanguage = 213,        // PK8: 226
  kOtName = 248,
  kOtFriendship = 274,
  kOtMemoryIntensity = 275,
  kOtMemory = 276,
  // CORRIGIDO na 067: era 277, que e PADDING. O text var e o u16 em 278-279.
  // Provado pela fixture sintetica (ver pk8.cpp, mesmo layout).
  kOtTextVar = 278,
  kOtFeeling = 280,
  kEggDate = 281,
  kMetDate = 284,
  kObedience = 287,       // so PK9
  kEggLocation = 288,
  kMetLocation = 290,
  kBall = 292,
  kMetLevelOtGender = 293,
  kHyperTrain = 294,
  kHomeTracker = 295,     // PK8: 309
  kTmFlagsBase = 303,     // 13 bytes
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
  if (!pkc::IsDecrypted(buf.data(), pkc::kBlockPK8)) {
    pkc::Decrypt(buf.data(), buf.size(), pkc::kBlockPK8);
    if (!pkc::IsDecrypted(buf.data(), pkc::kBlockPK8)) return std::nullopt;
  }
  const std::uint8_t* d = buf.data();

  pkm::Pokemon p;
  p.format = pkm::Format::kPK9;
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
  p.markings = U16(d, kMarkings);
  p.pid = U32(d, kPid);
  p.nature = d[kNature];
  p.stat_nature = d[kStatNature];
  p.fateful_encounter = d[kFatefulGender] & 1;
  // No PK9 o sexo mora nos bits 1-2 — DIFERENTE do PK8/PA8/PB8 (bits 2-3).
  // Conferido nas fixtures (spec 109): Gimmighoul/Roaring Moon/Miraidon, sem
  // sexo, gravam 0x04 = 2<<1; na convencao do PK8 isso leria "femea".
  p.gender = (d[kFatefulGender] >> 1) & 3;
  p.form = static_cast<std::uint8_t>(U16(d, kForm));

  for (int i = 0; i < 6; ++i) p.evs[i] = d[kEvs + i];
  for (int i = 0; i < 5; ++i) p.contest_stats[i] = d[kContest + i];
  p.contest_sheen = d[kContest + 5];

  for (int i = 0; i < 8; ++i) p.ribbon_bytes[i] = d[kRibbonsA + i];
  for (int i = 0; i < 8; ++i) p.ribbon_bytes[8 + i] = d[kRibbonsB + i];
  p.ribbon_count_memory = d[kContestMemoryCount];
  p.affixed_ribbon = d[kAffixedRibbon];

  p.height_scalar = d[kHeight];
  p.weight_scalar = d[kWeight];
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
  p.tera_type_original = d[kTeraOriginal];
  p.tera_type_override = d[kTeraOverride];

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
  p.obedience_level = d[kObedience];
  p.egg_location = U16(d, kEggLocation);
  p.met_location = U16(d, kMetLocation);
  p.ball = d[kBall];
  p.met_level = d[kMetLevelOtGender] & 0x7F;
  p.ot_gender = d[kMetLevelOtGender] >> 7;

  // Mesma conversao do PK8: flag em ordem de exibicao -> ordem fisica.
  const std::uint8_t ht = d[kHyperTrain];
  static constexpr int kHtBitForPhysical[6] = {0, 1, 2, 5, 3, 4};
  for (int i = 0; i < 6; ++i) {
    p.hyper_trained[i] = (ht >> kHtBitForPhysical[i]) & 1;
  }

  p.home_tracker = U64(d, kHomeTracker);

  // TM flags: base game (303) e DLC (75) sao blocos separados; o modelo
  // guarda concatenado, BASE PRIMEIRO (TD-02 da spec 059).
  p.move_record_flags.assign(d + kTmFlagsBase, d + kTmFlagsBase + 13);
  p.move_record_flags.insert(p.move_record_flags.end(), d + kTmFlagsDlc,
                             d + kTmFlagsDlc + 13);

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
  // Sem gigantamax no gen9: bits 4..7 preservados do raw (TD-04).
  d[kAbilityNum] = static_cast<std::uint8_t>((d[kAbilityNum] & 0xF0) |
                                             (p.ability_number & 7) |
                                             (p.favorite ? 8 : 0));
  pkw::W16(d, kMarkings, static_cast<std::uint16_t>(p.markings));
  pkw::W32(d, kPid, p.pid);
  d[kNature] = p.nature;
  d[kStatNature] = p.stat_nature;
  // Bits 1-2 para o sexo (ver o comentario do Parse): preserva os bits 3-7.
  d[kFatefulGender] = static_cast<std::uint8_t>(
      (d[kFatefulGender] & 0xF8) | (p.fateful_encounter ? 1 : 0) |
      ((p.gender & 3) << 1));
  pkw::W16(d, kForm, p.form);

  for (int i = 0; i < 6; ++i) d[kEvs + i] = p.evs[i];
  for (int i = 0; i < 5; ++i) d[kContest + i] = p.contest_stats[i];
  d[kContest + 5] = p.contest_sheen;

  for (int i = 0; i < 8; ++i) d[kRibbonsA + i] = p.ribbon_bytes[i];
  for (int i = 0; i < 8; ++i) d[kRibbonsB + i] = p.ribbon_bytes[8 + i];
  d[kContestMemoryCount] = p.ribbon_count_memory;
  d[kAffixedRibbon] = p.affixed_ribbon;

  d[kHeight] = p.height_scalar;
  d[kWeight] = p.weight_scalar;
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

  pkw::W16(d, kHpCurrent, p.hp_current);
  pkw::W32(d, kStatus, p.status_condition);

  // Plus flags do Z-A (spec 120): LIGA o bit de cada golpe pedido. Dois
  // blocos, medidos pela sonda tools/pkhex-za2 contra o PkHeX:
  //   id  < 256 -> 0xD6 + id/8
  //   id >= 256 -> 0x94 + (id-256)/8
  // Bits ja ligados no buffer original nao sao apagados.
  for (const std::uint16_t mv : p.za_plus_moves) {
    if (mv == 0 || mv > 359) continue;
    const std::size_t byte = mv < 256 ? 0xD6 + mv / 8 : 0x94 + (mv - 256) / 8;
    d[byte] = static_cast<std::uint8_t>(d[byte] | (1u << (mv % 8)));
  }
  d[kTeraOriginal] = p.tera_type_original;
  d[kTeraOverride] = p.tera_type_override;

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
  d[kObedience] = p.obedience_level;
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

  // Inverso da concatenacao do Parse: BASE primeiro (13), depois DLC (13).
  for (std::size_t i = 0; i < 13 && i < p.move_record_flags.size(); ++i) {
    d[kTmFlagsBase + i] = p.move_record_flags[i];
  }
  for (std::size_t i = 0; i < 13 && i + 13 < p.move_record_flags.size(); ++i) {
    d[kTmFlagsDlc + i] = p.move_record_flags[13 + i];
  }

  pkw::W16(d, kChecksum, pkc::Checksum(d, pkc::kBlockPK8));
  return buf;
}

}  // namespace pk9
