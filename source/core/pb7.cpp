#include "pb7.h"

#include "pkm_crypto.h"
#include "pkm_write_util.h"

namespace pb7 {
namespace {

// Offsets do PB7 (pb7.rs). Os comentarios marcam o que NAO tem equivalente
// nos formatos gen8+, para quem comparar os parsers lado a lado.
enum Off : std::size_t {
  kEC = 0,
  kSanity = 4,
  kChecksum = 6,
  kSpecies = 8,
  kHeldItem = 10,
  kTid = 12,
  kSid = 14,
  kExp = 16,
  kAbility = 20,      // u8 aqui (u16 no PK8/PK9)
  kAbilityNum = 21,
  kMarkings = 22,
  kPid = 24,          // PK8/PK9: 28
  kNature = 28,
  kFlags29 = 29,      // bit0 fateful, bits1-2 genero, bits3-7 FORMA
  kEvs = 30,
  kAvs = 36,          // so LGPE: awakening values
  kResortEventStatus = 42,
  kPokerus = 43,
  kHeightAbsolute = 44,   // 0x2C float (spec 121)
  kHeightScalar = 58,
  kWeightScalar = 59,
  kFormArgument = 60,
  kNickname = 64,     // 26 bytes = 12 chars + terminador
  kMoves = 90,
  kPp = 98,
  kPpUps = 102,
  kRelearn = 106,
  kIvsEggNick = 116,
  kHtName = 120,      // 24 bytes = 11 chars + terminador
  kHtGender = 146,
  kCurrentHandler = 147,
  kHtFriendship = 162,
  kFullness = 174,
  kEnjoyment = 175,
  kOtName = 176,      // 24 bytes
  kOtFriendship = 202,
  kReceivedYear = 203,  // 203..209: data/hora de recebimento (so LGPE)
  kEggDate = 209,
  kMetDate = 212,
  kEggLocation = 216,
  kMetLocation = 218,
  kBall = 220,
  kMetLevelOtGender = 221,
  kHyperTrain = 222,
  kOriginGame = 223,
  kLanguage = 227,
  kWeightAbsolute = 228,  // 0xE4 float (spec 121)
  kHpCurrent = 240,  // HP atual, no NUCLEO (spec 119)
  kStatus = 232,
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

// No PB7 os campos de texto tem tamanhos diferentes (26 bytes o apelido, 24
// os nomes de treinador), entao o limite e parametro — ao contrario do
// PK8/PK9, onde todos sao 13 chars.
std::string Utf16ToUtf8(const std::uint8_t* d, std::size_t o, int max_chars) {
  std::string out;
  for (int i = 0; i < max_chars; ++i) {
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
  if (size != kStoredSize) return std::nullopt;

  std::vector<std::uint8_t> buf(data, data + size);
  if (!pkc::IsDecrypted(buf.data(), pkc::kBlockPB7)) {
    pkc::Decrypt(buf.data(), buf.size(), pkc::kBlockPB7);
    if (!pkc::IsDecrypted(buf.data(), pkc::kBlockPB7)) return std::nullopt;
  }
  const std::uint8_t* d = buf.data();

  pkm::Pokemon p;
  p.format = pkm::Format::kPB7;
  p.raw = buf;

  p.encryption_constant = U32(d, kEC);
  p.sanity = U16(d, kSanity);
  p.checksum = U16(d, kChecksum);

  p.species = U16(d, kSpecies);  // ja e National Dex no LGPE (TD-03)
  p.held_item = U16(d, kHeldItem);
  p.tid = U16(d, kTid);
  p.sid = U16(d, kSid);
  p.exp = U32(d, kExp);
  p.ability = d[kAbility];
  p.ability_number = d[kAbilityNum] & 7;
  p.favorite = (d[kAbilityNum] >> 3) & 1;
  p.markings = U16(d, kMarkings);
  p.pid = U32(d, kPid);
  p.nature = d[kNature];
  // O PB7 nao tem mint: a natureza dos stats e a mesma.
  p.stat_nature = p.nature;

  const std::uint8_t f29 = d[kFlags29];
  p.fateful_encounter = f29 & 1;
  p.gender = (f29 >> 1) & 3;
  p.form = static_cast<std::uint8_t>((f29 >> 3) & 0x1F);

  for (int i = 0; i < 6; ++i) p.evs[i] = d[kEvs + i];
  for (int i = 0; i < 6; ++i) p.awakening_values[i] = d[kAvs + i];
  p.pokerus = d[kPokerus];
  p.height_scalar = d[kHeightScalar];
  p.weight_scalar = d[kWeightScalar];
  p.height_absolute = pkw::RF32(d, kHeightAbsolute);
  p.weight_absolute = pkw::RF32(d, kWeightAbsolute);
  p.form_argument = U32(d, kFormArgument);

  p.nickname = Utf16ToUtf8(d, kNickname, 12);
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

  p.ht_name = Utf16ToUtf8(d, kHtName, 11);
  p.ht_gender = d[kHtGender] & 1;
  p.current_handler = d[kCurrentHandler] & 1;
  p.ht_friendship = d[kHtFriendship];
  p.fullness = d[kFullness];
  p.enjoyment = d[kEnjoyment];

  p.ot_name = Utf16ToUtf8(d, kOtName, 11);
  p.ot_friendship = d[kOtFriendship];

  for (int i = 0; i < 3; ++i) p.egg_date[i] = d[kEggDate + i];
  for (int i = 0; i < 3; ++i) p.met_date[i] = d[kMetDate + i];
  p.egg_location = U16(d, kEggLocation);
  p.met_location = U16(d, kMetLocation);
  p.ball = d[kBall];
  p.met_level = d[kMetLevelOtGender] & 0x7F;
  p.ot_gender = d[kMetLevelOtGender] >> 7;

  // Mesma conversao ordem-de-exibicao -> ordem fisica do PK8/PK9.
  const std::uint8_t ht = d[kHyperTrain];
  static constexpr int kHtBitForPhysical[6] = {0, 1, 2, 5, 3, 4};
  for (int i = 0; i < 6; ++i) {
    p.hyper_trained[i] = (ht >> kHtBitForPhysical[i]) & 1;
  }

  p.origin_game = d[kOriginGame];
  p.language = d[kLanguage];
  p.hp_current = U16(d, kHpCurrent);
  p.status_condition = U32(d, kStatus);

  // Campos do modelo sem correspondente no PB7 (TD-02 da spec 057, ficam
  // zerados): memorias OT/HT, ribbons, home_tracker, contest stats,
  // move_record_flags, dynamax/gigantamax, scale, tera, obedience, alpha.
  // `received_flags` idem — o PB7 tem data/hora de recebimento (203..209),
  // que e outra grandeza; ver TD-04 da spec 062.

  return p;
}

std::optional<pkm::Pokemon> Parse(const std::vector<std::uint8_t>& data) {
  return Parse(data.data(), data.size());
}

std::vector<std::uint8_t> Write(const pkm::Pokemon& p) {
  std::vector<std::uint8_t> buf = p.raw;  // TD-01
  if (buf.size() != kStoredSize) buf.assign(kStoredSize, 0);  // TD-02
  std::uint8_t* d = buf.data();

  pkw::W32(d, kEC, p.encryption_constant);
  pkw::W16(d, kSanity, p.sanity);

  pkw::W16(d, kSpecies, p.species);
  pkw::W16(d, kHeldItem, p.held_item);
  pkw::W16(d, kTid, p.tid);
  pkw::W16(d, kSid, p.sid);
  pkw::W32(d, kExp, p.exp);
  d[kAbility] = static_cast<std::uint8_t>(p.ability);  // u8 aqui, nao u16
  d[kAbilityNum] = static_cast<std::uint8_t>((d[kAbilityNum] & 0xF0) |
                                             (p.ability_number & 7) |
                                             (p.favorite ? 8 : 0));
  pkw::W16(d, kMarkings, static_cast<std::uint16_t>(p.markings));
  pkw::W32(d, kPid, p.pid);
  d[kNature] = p.nature;
  // Sem mint no PB7: `stat_nature` e copia de `nature` na leitura, entao nao
  // ha byte proprio para escrever.

  // Byte 29 empacota fateful (bit 0), genero (1-2) e FORMA (3-7) — todos os
  // 8 bits sao parseados, nada a preservar.
  d[kFlags29] = static_cast<std::uint8_t>((p.fateful_encounter ? 1 : 0) |
                                          ((p.gender & 3) << 1) |
                                          ((p.form & 0x1F) << 3));

  for (int i = 0; i < 6; ++i) d[kEvs + i] = p.evs[i];
  for (int i = 0; i < 6; ++i) d[kAvs + i] = p.awakening_values[i];
  d[kPokerus] = p.pokerus;
  d[kHeightScalar] = p.height_scalar;
  d[kWeightScalar] = p.weight_scalar;
  pkw::WF32(d, kHeightAbsolute, p.height_absolute);
  pkw::WF32(d, kWeightAbsolute, p.weight_absolute);
  pkw::W32(d, kFormArgument, p.form_argument);

  // Tamanhos proprios do PB7: 12 code units no apelido, 11 nos nomes de
  // treinador (TD-05).
  pkw::WriteUtf16(d, kNickname, p.nickname, 12);
  for (int i = 0; i < 4; ++i) pkw::W16(d, kMoves + i * 2, p.moves[i]);
  for (int i = 0; i < 4; ++i) d[kPp + i] = p.pp[i];
  for (int i = 0; i < 4; ++i) d[kPpUps + i] = p.pp_ups[i];
  for (int i = 0; i < 4; ++i) {
    pkw::W16(d, kRelearn + i * 2, p.relearn_moves[i]);
  }

  const std::uint8_t ivs[6] = {p.ivs[0], p.ivs[1], p.ivs[2],
                               p.ivs[3], p.ivs[4], p.ivs[5]};
  pkw::W32(d, kIvsEggNick, pkw::PackIvs(ivs, p.is_egg, p.is_nicknamed));

  pkw::WriteUtf16(d, kHtName, p.ht_name, 11);
  // O parser le so o bit 0 destes dois: o resto vem do raw (TD-04).
  d[kHtGender] = static_cast<std::uint8_t>((d[kHtGender] & 0xFE) |
                                           (p.ht_gender & 1));
  d[kCurrentHandler] = static_cast<std::uint8_t>((d[kCurrentHandler] & 0xFE) |
                                                 (p.current_handler & 1));
  d[kHtFriendship] = p.ht_friendship;
  d[kFullness] = p.fullness;
  d[kEnjoyment] = p.enjoyment;

  pkw::WriteUtf16(d, kOtName, p.ot_name, 11);
  d[kOtFriendship] = p.ot_friendship;

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

  d[kOriginGame] = p.origin_game;
  d[kLanguage] = p.language;
  pkw::W16(d, kHpCurrent, p.hp_current);
  pkw::W32(d, kStatus, p.status_condition);

  // Nao escritos de proposito (o parser nao le): altura/peso absolutos
  // (floats em 44 e 228), resort event status, data de recebimento
  // (203..209). Ficam do raw.
  pkw::W16(d, kChecksum, pkc::Checksum(d, pkc::kBlockPB7));
  return buf;
}

}  // namespace pb7
