#include "pk4.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "gen4_charset.h"
#include "pkm_write_util.h"

namespace pk4 {
namespace {

// Offsets do PK4 (PKHeX PK4.cs). Ordem canonica A/B/C/D.
enum Off : std::size_t {
  kPid = 0,
  kSanity = 4,
  kChecksum = 6,
  // Bloco A (0x08)
  kSpecies = 0x08,
  kHeldItem = 0x0A,
  kTid = 0x0C,
  kSid = 0x0E,
  kExp = 0x10,
  kFriendship = 0x14,
  kAbility = 0x15,
  kMarkings = 0x16,
  kLanguage = 0x17,
  kEvs = 0x18,
  kContest = 0x1E,
  // Bloco B (0x28)
  kMoves = 0x28,
  kPp = 0x30,
  kPpUps = 0x34,
  kIvsEggNick = 0x38,
  kFatefulGenderForm = 0x40,
  kEggLocPlat = 0x44,
  kMetLocPlat = 0x46,
  // Bloco C (0x48)
  kNickname = 0x48,     // 11 u16 no charset gen4
  kOriginGame = 0x5F,
  // Bloco D (0x68)
  kOtName = 0x68,       // 8 u16
  kEggDate = 0x78,
  kMetDate = 0x7B,
  kEggLocDp = 0x7E,
  kMetLocDp = 0x80,
  kPokerus = 0x82,
  kBallDppt = 0x83,
  kMetLevelOtGender = 0x84,
  kBallHgss = 0x86,
};

std::uint16_t U16(const std::uint8_t* d, std::size_t o) {
  return static_cast<std::uint16_t>(d[o] | (d[o + 1] << 8));
}
std::uint32_t U32(const std::uint8_t* d, std::size_t o) {
  return static_cast<std::uint32_t>(d[o]) | (d[o + 1] << 8) |
         (static_cast<std::uint32_t>(d[o + 2]) << 16) |
         (static_cast<std::uint32_t>(d[o + 3]) << 24);
}

// Checksum: soma u16 dos blocos (0x08..0x87) em claro.
std::uint16_t Checksum(const std::uint8_t* d) {
  std::uint32_t sum = 0;
  for (std::size_t i = 0x08; i < kStoredSize; i += 2) sum += U16(d, i);
  return static_cast<std::uint16_t>(sum);
}

// Cifra da gen4: cada u16 XOR com os 16 bits altos de um LCRNG semeado pelo
// checksum (blocos) ou pelo PID (party). Simetrica: cifrar = decifrar.
void Crypt(std::uint8_t* d, std::size_t begin, std::size_t end,
           std::uint32_t seed) {
  for (std::size_t i = begin; i + 1 < end + 1 && i < end; i += 2) {
    seed = 0x41C64E6Du * seed + 0x6073u;
    const std::uint16_t x =
        static_cast<std::uint16_t>(U16(d, i) ^ (seed >> 16));
    d[i] = static_cast<std::uint8_t>(x & 0xFF);
    d[i + 1] = static_cast<std::uint8_t>(x >> 8);
  }
}

// As mesmas 24 permutacoes dos formatos modernos, indexadas por
// ((PID >> 13) & 0x1F) % 24.
const std::array<std::array<int, 4>, 24>& Orders() {
  static const auto kOrders = [] {
    std::array<std::array<int, 4>, 24> o{};
    std::array<int, 4> perm = {0, 1, 2, 3};
    for (auto& e : o) {
      e = perm;
      std::next_permutation(perm.begin(), perm.end());
    }
    return o;
  }();
  return kOrders;
}

void Unshuffle(std::uint8_t* d, std::uint32_t pid) {
  const auto& order = Orders()[((pid >> 13) & 0x1F) % 24];
  std::uint8_t tmp[128];
  std::memcpy(tmp, d + 8, 128);
  for (std::size_t phys = 0; phys < 4; ++phys) {
    std::memcpy(d + 8 + static_cast<std::size_t>(order[phys]) * 32,
                tmp + phys * 32, 32);
  }
}

void Shuffle(std::uint8_t* d, std::uint32_t pid) {
  const auto& order = Orders()[((pid >> 13) & 0x1F) % 24];
  std::uint8_t tmp[128];
  std::memcpy(tmp, d + 8, 128);
  for (std::size_t phys = 0; phys < 4; ++phys) {
    std::memcpy(d + 8 + phys * 32,
                tmp + static_cast<std::size_t>(order[phys]) * 32, 32);
  }
}

std::string DecodeName(const std::uint8_t* d, std::size_t o, int max_chars) {
  std::string out;
  for (int i = 0; i < max_chars; ++i) {
    const std::uint16_t code = U16(d, o + static_cast<std::size_t>(i) * 2);
    if (code == pokehome::gen4::kTerminator || code == 0) break;
    out.push_back(pokehome::gen4::DecodeChar(code));
  }
  return out;
}

void EncodeName(std::uint8_t* d, std::size_t o, const std::string& s,
                int max_chars) {
  int n = 0;
  for (std::size_t i = 0; i < s.size() && n < max_chars; ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c >= 0x80) continue;  // multibyte: sem mapa no charset gen4
    pkw::W16(d, o + static_cast<std::size_t>(n) * 2,
             pokehome::gen4::EncodeChar(static_cast<char>(c)));
    ++n;
  }
  if (n < max_chars) {
    pkw::W16(d, o + static_cast<std::size_t>(n) * 2,
             pokehome::gen4::kTerminator);
  }
}

}  // namespace

std::optional<pkm::Pokemon> Parse(const std::uint8_t* data, std::size_t size) {
  if (size != kStoredSize && size != kPartySize) return std::nullopt;

  std::vector<std::uint8_t> buf(data, data + size);
  std::uint8_t* d = buf.data();
  const std::uint32_t pid = U32(d, kPid);
  const std::uint16_t stored_sum = U16(d, kChecksum);

  // Em claro? O checksum confere direto. Cifrado? Decifra e re-confere.
  if (Checksum(d) != stored_sum) {
    Crypt(d, 0x08, kStoredSize, stored_sum);
    Unshuffle(d, pid);
    if (Checksum(d) != stored_sum) return std::nullopt;
    if (size == kPartySize) Crypt(d, kStoredSize, kPartySize, pid);
  }

  pkm::Pokemon p;
  p.format = pkm::Format::kPK4;
  p.raw = buf;
  p.pid = pid;
  p.encryption_constant = pid;  // a gen4 nao tem EC; PID cumpre o papel
  p.sanity = U16(d, kSanity);
  p.checksum = stored_sum;

  p.species = U16(d, kSpecies);  // na gen4 ja e a dex nacional
  p.held_item = U16(d, kHeldItem);
  p.tid = U16(d, kTid);
  p.sid = U16(d, kSid);
  p.exp = U32(d, kExp);
  p.ot_friendship = d[kFriendship];
  p.ability = d[kAbility];
  p.markings = d[kMarkings];
  p.language = d[kLanguage];
  for (int i = 0; i < 6; ++i) p.evs[i] = d[kEvs + i];
  for (int i = 0; i < 5; ++i) p.contest_stats[i] = d[kContest + i];
  p.contest_sheen = d[kContest + 5];

  for (int i = 0; i < 4; ++i) {
    p.moves[i] = U16(d, kMoves + static_cast<std::size_t>(i) * 2);
    p.pp[i] = d[kPp + i];
    p.pp_ups[i] = d[kPpUps + i];
  }
  const std::uint32_t iv32 = U32(d, kIvsEggNick);
  for (int i = 0; i < 6; ++i) {
    p.ivs[i] = static_cast<std::uint8_t>((iv32 >> (5 * i)) & 0x1F);
  }
  p.is_egg = (iv32 >> 30) & 1;
  p.is_nicknamed = (iv32 >> 31) & 1;

  const std::uint8_t fgf = d[kFatefulGenderForm];
  p.fateful_encounter = fgf & 1;
  // bit1 = femea, bit2 = sem sexo (numeracao propria da gen4).
  p.gender = (fgf & 0x04) ? 2 : (fgf & 0x02) ? 1 : 0;
  p.form = static_cast<std::uint8_t>(fgf >> 3);

  p.nickname = DecodeName(d, kNickname, 10);
  p.origin_game = d[kOriginGame];
  p.ot_name = DecodeName(d, kOtName, 7);

  for (int i = 0; i < 3; ++i) p.egg_date[i] = d[kEggDate + i];
  for (int i = 0; i < 3; ++i) p.met_date[i] = d[kMetDate + i];
  // Platinum/HGSS estendem os locais; o campo de DP fica como fallback.
  const std::uint16_t egg_ext = U16(d, kEggLocPlat);
  const std::uint16_t met_ext = U16(d, kMetLocPlat);
  p.egg_location_dp = U16(d, kEggLocDp);
  p.met_location_dp = U16(d, kMetLocDp);
  p.egg_location = egg_ext ? egg_ext : p.egg_location_dp;
  p.met_location = met_ext ? met_ext : p.met_location_dp;
  p.pokerus = d[kPokerus];
  p.ball = d[kBallDppt] ? d[kBallDppt] : d[kBallHgss];
  p.met_level = d[kMetLevelOtGender] & 0x7F;
  p.ot_gender = d[kMetLevelOtGender] >> 7;

  return p;
}

std::optional<pkm::Pokemon> Parse(const std::vector<std::uint8_t>& data) {
  return Parse(data.data(), data.size());
}

std::vector<std::uint8_t> Write(const pkm::Pokemon& p) {
  std::vector<std::uint8_t> buf = p.raw;  // conservador, como os modernos
  if (buf.size() != kStoredSize && buf.size() != kPartySize) {
    buf.assign(kStoredSize, 0);
  }
  std::uint8_t* d = buf.data();

  pkw::W32(d, kPid, p.pid);
  pkw::W16(d, kSanity, p.sanity);
  pkw::W16(d, kSpecies, p.species);
  pkw::W16(d, kHeldItem, p.held_item);
  pkw::W16(d, kTid, p.tid);
  pkw::W16(d, kSid, p.sid);
  pkw::W32(d, kExp, p.exp);
  d[kFriendship] = p.ot_friendship;
  d[kAbility] = static_cast<std::uint8_t>(p.ability);
  d[kMarkings] = static_cast<std::uint8_t>(p.markings);
  d[kLanguage] = p.language;
  for (int i = 0; i < 6; ++i) d[kEvs + i] = p.evs[i];
  for (int i = 0; i < 5; ++i) d[kContest + i] = p.contest_stats[i];
  d[kContest + 5] = p.contest_sheen;

  for (int i = 0; i < 4; ++i) {
    pkw::W16(d, kMoves + static_cast<std::size_t>(i) * 2, p.moves[i]);
    d[kPp + i] = p.pp[i];
    d[kPpUps + i] = p.pp_ups[i];
  }
  std::uint32_t iv32 = 0;
  for (int i = 0; i < 6; ++i) {
    iv32 |= static_cast<std::uint32_t>(p.ivs[i] & 0x1F) << (5 * i);
  }
  if (p.is_egg) iv32 |= 1u << 30;
  if (p.is_nicknamed) iv32 |= 1u << 31;
  pkw::W32(d, kIvsEggNick, iv32);

  d[kFatefulGenderForm] = static_cast<std::uint8_t>(
      (p.fateful_encounter ? 1 : 0) |
      (p.gender == 1 ? 0x02 : p.gender == 2 ? 0x04 : 0) |
      (p.form << 3));

  EncodeName(d, kNickname, p.nickname, 10);
  d[kOriginGame] = p.origin_game;
  EncodeName(d, kOtName, p.ot_name, 7);

  for (int i = 0; i < 3; ++i) d[kEggDate + i] = p.egg_date[i];
  for (int i = 0; i < 3; ++i) d[kMetDate + i] = p.met_date[i];
  // Locais: os pares sao regravados como foram LIDOS (roundtrip fiel). O
  // estendido so recebe o efetivo quando ele difere do par DP — e o padrao
  // Platinum/HGSS; um registro de D/P mantem o estendido zerado.
  pkw::W16(d, kEggLocDp, p.egg_location_dp);
  pkw::W16(d, kMetLocDp, p.met_location_dp);
  pkw::W16(d, kEggLocPlat,
           p.egg_location == p.egg_location_dp ? 0 : p.egg_location);
  pkw::W16(d, kMetLocPlat,
           p.met_location == p.met_location_dp ? 0 : p.met_location);
  d[kPokerus] = p.pokerus;
  d[kBallDppt] = p.ball;
  d[kMetLevelOtGender] = static_cast<std::uint8_t>((p.met_level & 0x7F) |
                                                   (p.ot_gender ? 0x80 : 0));

  pkw::W16(d, kChecksum, Checksum(d));
  return buf;
}

}  // namespace pk4
