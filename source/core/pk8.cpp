#include "pk8.h"

#include <cstring>

#include "pkm_crypto.h"
#include "pkm_write_util.h"

namespace pk8 {
namespace {

// Offsets do formato (PkHeX/pk8_buffer.rs, decimais como na referencia).
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
  kAbilityNumFavGmax = 22,
  kMarkings = 24,
  kPid = 28,
  kNature = 32,
  kStatNature = 33,
  kFatefulGender = 34,
  kForm = 36,
  kEvs = 38,
  kContest = 44,
  kPokerus = 50,
  kRibbonsA = 52,       // 8 bytes
  kContestMemoryCount = 60,
  kBattleMemoryCount = 61,
  kRibbonsB = 64,       // 8 bytes (marks)
  kSociability = 72,
  kHeight = 80,
  kWeight = 81,
  kNickname = 88,       // 26 bytes UTF-16LE
  kMoves = 114,         // 4 x u16
  kPp = 122,            // 4 x u8
  kPpUps = 126,         // 4 x u8
  kRelearn = 130,       // 4 x u16
  kIvsEggNick = 140,    // u32: 6x5 bits + egg + nicknamed
  kDynamax = 144,
  kStatus = 148,
  kPalma = 152,
  kHtName = 168,        // 26 bytes
  kHtGender = 194,
  kHtLanguage = 195,
  kCurrentHandler = 196,
  kHtId = 198,
  kHtFriendship = 200,
  kHtMemoryIntensity = 201,
  kHtMemory = 202,
  kHtFeeling = 203,
  kHtTextVar = 204,
  kFullness = 220,
  kEnjoyment = 221,
  kOriginGame = 222,
  kLanguage = 226,
  kFormArgument = 228,
  kAffixedRibbon = 232,
  kOtName = 248,        // 26 bytes
  kOtFriendship = 274,
  kOtMemoryIntensity = 275,
  kOtMemory = 276,
  // CORRIGIDO na 067: era 277, que e PADDING. O text var e o u16 em 278-279.
  // Provado pela fixture sintetica (OriginalTrainerMemoryVariable = 1123):
  // [277]=00 [278]=63 [279]=04 -> 0x0463 = 1123. Lendo de 277 saia 25344.
  // Mesmo erro que a spec 066 achou no PA8 (kOtTextVar era 301, e 302).
  // Nenhuma fixture real exercitava o campo: todas tinham memoria zerada.
  kOtTextVar = 278,
  kOtFeeling = 280,
  kEggDate = 281,
  kMetDate = 284,
  kEggLocation = 288,
  kMetLocation = 290,
  kBall = 292,
  kMetLevelOtGender = 293,
  kHyperTrain = 294,
  kTrFlags = 295,       // 14 bytes
  kHomeTracker = 309,   // u64
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

// UTF-16LE (BMP) -> UTF-8, parando no terminador. 13 code units.
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

  // Copia local: se vier cifrado, decifra aqui sem tocar o buffer de quem
  // chamou. O save guarda cifrado; fixtures do PkHeX vem decifradas.
  std::vector<std::uint8_t> buf(data, data + size);
  if (!pkc::IsDecrypted(buf.data(), pkc::kBlockPK8)) {
    pkc::Decrypt(buf.data(), buf.size(), pkc::kBlockPK8);
    if (!pkc::IsDecrypted(buf.data(), pkc::kBlockPK8)) return std::nullopt;
  }
  const std::uint8_t* d = buf.data();

  pkm::Pokemon p;
  p.format = pkm::Format::kPK8;
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
  p.ability_number = d[kAbilityNumFavGmax] & 7;
  p.favorite = (d[kAbilityNumFavGmax] >> 3) & 1;
  p.can_gigantamax = (d[kAbilityNumFavGmax] >> 4) & 1;
  p.markings = U16(d, kMarkings);
  p.pid = U32(d, kPid);
  p.nature = d[kNature];
  p.stat_nature = d[kStatNature];
  p.fateful_encounter = d[kFatefulGender] & 1;
  p.gender = (d[kFatefulGender] >> 2) & 3;
  p.form = static_cast<std::uint8_t>(U16(d, kForm));

  for (int i = 0; i < 6; ++i) p.evs[i] = d[kEvs + i];
  // 5 stats de contest + sheen no 6o byte.
  for (int i = 0; i < 5; ++i) p.contest_stats[i] = d[kContest + i];
  p.contest_sheen = d[kContest + 5];
  p.pokerus = d[kPokerus];

  for (int i = 0; i < 8; ++i) p.ribbon_bytes[i] = d[kRibbonsA + i];
  for (int i = 0; i < 8; ++i) p.ribbon_bytes[8 + i] = d[kRibbonsB + i];
  p.ribbon_count_memory = d[kContestMemoryCount];
  p.affixed_ribbon = d[kAffixedRibbon];

  p.sociability = U32(d, kSociability);
  p.height_scalar = d[kHeight];
  p.weight_scalar = d[kWeight];

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

  p.dynamax_level = d[kDynamax];
  p.status_condition = U32(d, kStatus);
  p.palma = U32(d, kPalma);

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

  p.fullness = d[kFullness];
  p.enjoyment = d[kEnjoyment];
  p.origin_game = d[kOriginGame];
  p.language = d[kLanguage];
  p.form_argument = U32(d, kFormArgument);

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

  // HyperTrainFlags: bit0 HP, 1 Atk, 2 Def, 3 SpA, 4 SpD, 5 Spe — ordem de
  // EXIBICAO, nao a fisica (TD-01). O modelo usa a fisica
  // (HP,Atk,Def,Spe,SpA,SpD): mapa explicito.
  const std::uint8_t ht = d[kHyperTrain];
  static constexpr int kHtBitForPhysical[6] = {0, 1, 2, 5, 3, 4};
  for (int i = 0; i < 6; ++i) {
    p.hyper_trained[i] = (ht >> kHtBitForPhysical[i]) & 1;
  }

  p.move_record_flags.assign(d + kTrFlags, d + kTrFlags + 14);
  p.home_tracker = U64(d, kHomeTracker);

  return p;
}

std::optional<pkm::Pokemon> Parse(const std::vector<std::uint8_t>& data) {
  return Parse(data.data(), data.size());
}

std::vector<std::uint8_t> Write(const pkm::Pokemon& p) {
  // TD-01: parte dos bytes crus. Todo offset que o Parse acima NAO le sai
  // identico ao que entrou — padding, party e campos nao mapeados.
  std::vector<std::uint8_t> buf = p.raw;
  if (buf.size() != kStoredSize && buf.size() != kPartySize) {
    buf.assign(kStoredSize, 0);  // TD-02: Pokemon sem raw
  }
  std::uint8_t* d = buf.data();

  pkw::W32(d, kEC, p.encryption_constant);
  pkw::W16(d, kSanity, p.sanity);
  // kChecksum e recalculado no fim (TD-03).

  pkw::W16(d, kSpecies, p.species);
  pkw::W16(d, kHeldItem, p.held_item);
  pkw::W16(d, kTid, p.tid);
  pkw::W16(d, kSid, p.sid);
  pkw::W32(d, kExp, p.exp);
  pkw::W16(d, kAbility, p.ability);
  // TD-04: bits 5..7 deste byte nao sao parseados — preservados do raw.
  d[kAbilityNumFavGmax] = static_cast<std::uint8_t>(
      (d[kAbilityNumFavGmax] & 0xE0) | (p.ability_number & 7) |
      (p.favorite ? 8 : 0) | (p.can_gigantamax ? 16 : 0));
  pkw::W16(d, kMarkings, static_cast<std::uint16_t>(p.markings));
  pkw::W32(d, kPid, p.pid);
  d[kNature] = p.nature;
  d[kStatNature] = p.stat_nature;
  // TD-04: bit 1 e bits 4..7 nao parseados.
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

  pkw::W32(d, kSociability, p.sociability);
  d[kHeight] = p.height_scalar;
  d[kWeight] = p.weight_scalar;

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

  d[kDynamax] = p.dynamax_level;
  pkw::W32(d, kStatus, p.status_condition);
  pkw::W32(d, kPalma, p.palma);

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

  d[kFullness] = p.fullness;
  d[kEnjoyment] = p.enjoyment;
  d[kOriginGame] = p.origin_game;
  d[kLanguage] = p.language;
  pkw::W32(d, kFormArgument, p.form_argument);

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

  // O parser le 14 bytes; um modelo com vetor mais curto so escreve o que tem
  // (o resto fica como estava no raw).
  for (std::size_t i = 0; i < 14 && i < p.move_record_flags.size(); ++i) {
    d[kTrFlags + i] = p.move_record_flags[i];
  }
  pkw::W64(d, kHomeTracker, p.home_tracker);

  pkw::W16(d, kChecksum, pkc::Checksum(d, pkc::kBlockPK8));
  return buf;
}

}  // namespace pk8
