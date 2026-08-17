#include "za_save.h"

#include <cstring>

#include "gen9_species.h"

namespace za {
namespace {

// --- SHA256 ----------------------------------------------------------------
// Implementação compacta e sem dependências: o core precisa conferir o hash
// do SwishCrypto e não depende de nenhuma biblioteca de plataforma.

struct Sha256 {
  std::uint32_t h[8] = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
                        0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};
  std::uint8_t buf[64];
  std::uint64_t total = 0;
  std::size_t fill = 0;

  static std::uint32_t Rot(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
  }

  void Block(const std::uint8_t* p) {
    static constexpr std::uint32_t K[64] = {
        0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B,
        0x59F111F1, 0x923F82A4, 0xAB1C5ED5, 0xD807AA98, 0x12835B01,
        0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7,
        0xC19BF174, 0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
        0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA, 0x983E5152,
        0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147,
        0x06CA6351, 0x14292967, 0x27B70A85, 0x2E1B2138, 0x4D2C6DFC,
        0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
        0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819,
        0xD6990624, 0xF40E3585, 0x106AA070, 0x19A4C116, 0x1E376C08,
        0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F,
        0x682E6FF3, 0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
        0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2};
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
             (std::uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          Rot(w[i - 15], 7) ^ Rot(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 =
          Rot(w[i - 2], 17) ^ Rot(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5],
                  g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t S1 = Rot(e, 6) ^ Rot(e, 11) ^ Rot(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
      const std::uint32_t S0 = Rot(a, 2) ^ Rot(a, 13) ^ Rot(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = S0 + maj;
      hh = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }

  void Update(const std::uint8_t* p, std::size_t n) {
    total += n;
    while (n > 0) {
      const std::size_t take = std::min(n, sizeof(buf) - fill);
      std::memcpy(buf + fill, p, take);
      fill += take; p += take; n -= take;
      if (fill == sizeof(buf)) { Block(buf); fill = 0; }
    }
  }

  void Final(std::uint8_t out[32]) {
    const std::uint64_t bits = total * 8;
    const std::uint8_t pad = 0x80;
    Update(&pad, 1);
    const std::uint8_t zero = 0;
    while (fill != 56) Update(&zero, 1);
    std::uint8_t len[8];
    for (int i = 0; i < 8; ++i) len[i] = std::uint8_t(bits >> (56 - i * 8));
    Update(len, 8);
    for (int i = 0; i < 8; ++i) {
      out[i * 4] = std::uint8_t(h[i] >> 24);
      out[i * 4 + 1] = std::uint8_t(h[i] >> 16);
      out[i * 4 + 2] = std::uint8_t(h[i] >> 8);
      out[i * 4 + 3] = std::uint8_t(h[i]);
    }
  }
};

// --- Constantes do SwishCrypto ---------------------------------------------
// Sais e xorpad são constantes do formato (as mesmas para todo save SwSh/ZA).

constexpr std::uint8_t kIntro[64] = {
    0x9E, 0xC9, 0x9C, 0xD7, 0x0E, 0xD3, 0x3C, 0x44, 0xFB, 0x93, 0x03, 0xDC,
    0xEB, 0x39, 0xB4, 0x2A, 0x19, 0x47, 0xE9, 0x63, 0x4B, 0xA2, 0x33, 0x44,
    0x16, 0xBF, 0x82, 0xA2, 0xBA, 0x63, 0x55, 0xB6, 0x3D, 0x9D, 0xF2, 0x4B,
    0x5F, 0x7B, 0x6A, 0xB2, 0x62, 0x1D, 0xC2, 0x1B, 0x68, 0xE5, 0xC8, 0xB5,
    0x3A, 0x05, 0x90, 0x00, 0xE8, 0xA8, 0x10, 0x3D, 0xE2, 0xEC, 0xF0, 0x0C,
    0xB2, 0xED, 0x4F, 0x6D};

constexpr std::uint8_t kOutro[64] = {
    0xD6, 0xC0, 0x1C, 0x59, 0x8B, 0xC8, 0xB8, 0xCB, 0x46, 0xE1, 0x53, 0xFC,
    0x82, 0x8C, 0x75, 0x75, 0x13, 0xE0, 0x45, 0xDF, 0x32, 0x69, 0x3C, 0x75,
    0xF0, 0x59, 0xF8, 0xD9, 0xA2, 0x5F, 0xB2, 0x17, 0xE0, 0x80, 0x52, 0xDB,
    0xEA, 0x89, 0x73, 0x99, 0x75, 0x79, 0xAF, 0xCB, 0x2E, 0x80, 0x07, 0xE6,
    0xF1, 0x26, 0xE0, 0x03, 0x0A, 0xE6, 0x6F, 0xF6, 0x41, 0xBF, 0x7E, 0x59,
    0xC2, 0xAE, 0x55, 0xFD};

// 0x80 bytes, mas aplicado avancando 0x7F por rodada (o ultimo byte e 0).
constexpr std::uint8_t kXorpad[128] = {
    0xA0, 0x92, 0xD1, 0x06, 0x07, 0xDB, 0x32, 0xA1, 0xAE, 0x01, 0xF5, 0xC5,
    0x1E, 0x84, 0x4F, 0xE3, 0x53, 0xCA, 0x37, 0xF4, 0xA7, 0xB0, 0x4D, 0xA0,
    0x18, 0xB7, 0xC2, 0x97, 0xDA, 0x5F, 0x53, 0x2B, 0x75, 0xFA, 0x48, 0x16,
    0xF8, 0xD4, 0x8A, 0x6F, 0x61, 0x05, 0xF4, 0xE2, 0xFD, 0x04, 0xB5, 0xA3,
    0x0F, 0xFC, 0x44, 0x92, 0xCB, 0x32, 0xE6, 0x1B, 0xB9, 0xB1, 0x2E, 0x01,
    0xB0, 0x56, 0x53, 0x36, 0xD2, 0xD1, 0x50, 0x3D, 0xDE, 0x5B, 0x2E, 0x0E,
    0x52, 0xFD, 0xDF, 0x2F, 0x7B, 0xCA, 0x63, 0x50, 0xA4, 0x67, 0x5D, 0x23,
    0x17, 0xC0, 0x52, 0xE1, 0xA6, 0x30, 0x7C, 0x2B, 0xB6, 0x70, 0x36, 0x5B,
    0x2A, 0x27, 0x69, 0x33, 0xF5, 0x63, 0x7B, 0x36, 0x3F, 0x26, 0x9B, 0xA3,
    0xED, 0x7A, 0x53, 0x00, 0xA4, 0x48, 0xB3, 0x50, 0x9E, 0x14, 0xA0, 0x52,
    0xDE, 0x7E, 0x10, 0x2B, 0x1B, 0x77, 0x6E, 0x00};

constexpr std::uint32_t kKeyBox = 0x0D66012C;
constexpr std::uint32_t kKeyParty = 0x3AA1A9AD;
constexpr std::uint32_t kKeyStatus = 0xE3E89BD1;   // treinador
constexpr std::uint32_t kKeySeconds = 0xCE3AF8F2;  // tempo jogado (double)

// --- XorShift32 por bloco ---------------------------------------------------

class XorShift32 {
 public:
  explicit XorShift32(std::uint32_t seed) {
    // O estado inicial avanca uma vez por bit ligado da semente.
    int pop = 0;
    for (std::uint32_t v = seed; v; v >>= 1) pop += v & 1;
    state_ = seed;
    for (int i = 0; i < pop; ++i) state_ = Advance(state_);
  }

  std::uint8_t Next() {
    const std::uint8_t r = std::uint8_t(state_ >> (counter_ * 8));
    if (counter_ == 3) {
      state_ = Advance(state_);
      counter_ = 0;
    } else {
      ++counter_;
    }
    return r;
  }

  std::uint32_t Next32() {
    std::uint32_t v = Next();
    v |= std::uint32_t(Next()) << 8;
    v |= std::uint32_t(Next()) << 16;
    v |= std::uint32_t(Next()) << 24;
    return v;
  }

 private:
  static std::uint32_t Advance(std::uint32_t s) {
    s ^= s << 2;
    s ^= s >> 15;
    s ^= s << 13;
    return s;
  }

  std::uint32_t state_ = 0;
  int counter_ = 0;
};

std::uint32_t ReadU32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint16_t ReadU16(const std::uint8_t* p) {
  return std::uint16_t(p[0] | (p[1] << 8));
}

// Tamanho dos tipos primitivos do SCBlock (indices 8..17).
int TypeSize(std::uint8_t t) {
  switch (t) {
    case 8: case 12: return 1;
    case 9: case 13: return 2;
    case 10: case 14: case 16: return 4;
    case 11: case 15: case 17: return 8;
    default: return -1;
  }
}

// --- PokeCrypto gen8 --------------------------------------------------------

constexpr std::uint8_t kBlockPosition[32 * 4] = {
    0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1,
    1, 0, 2, 3, 1, 0, 3, 2, 2, 0, 1, 3, 3, 0, 1, 2, 2, 0, 3, 1, 3, 0, 2, 1,
    1, 2, 0, 3, 1, 3, 0, 2, 2, 1, 0, 3, 3, 1, 0, 2, 2, 3, 0, 1, 3, 2, 0, 1,
    1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 0,
    0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1,
    1, 0, 2, 3, 1, 0, 3, 2};

// Decifra um registro party gen8 (344 bytes): corpo e party stats pelo LCRNG
// semeado pelo EncryptionConstant, e desfaz o embaralhamento dos 4 blocos.
void DecryptRecord(std::uint8_t* rec) {
  const std::uint32_t ec = ReadU32(rec);

  auto crypt = [&](std::size_t from, std::size_t to) {
    std::uint32_t seed = ec;
    for (std::size_t i = from; i + 1 < to + 1; i += 2) {
      seed = 0x41C64E6Du * seed + 0x6073u;
      const std::uint16_t x = std::uint16_t(seed >> 16);
      const std::uint16_t v = ReadU16(rec + i) ^ x;
      rec[i] = std::uint8_t(v);
      rec[i + 1] = std::uint8_t(v >> 8);
    }
  };
  crypt(8, 0x148);        // corpo
  crypt(0x148, kRecordSize);  // party stats — a semente reinicia

  // Desembaralhar: resultado[i] recebe o bloco fisico order[i]. A direcao foi
  // decidida empiricamente no save real: e a que produz 96/96 especies
  // validas (a inversa produzia lixo em 19 registros).
  const std::uint32_t sv = (ec >> 13) & 31;
  const std::uint8_t* order = kBlockPosition + sv * 4;
  std::uint8_t body[320];
  std::memcpy(body, rec + 8, sizeof(body));
  for (int i = 0; i < 4; ++i) {
    std::memcpy(rec + 8 + i * 80, body + order[i] * 80, 80);
  }
}

std::string Utf16ToUtf8(const std::uint8_t* p, std::size_t max_units) {
  std::string out;
  for (std::size_t i = 0; i < max_units; ++i) {
    const std::uint16_t c = ReadU16(p + i * 2);
    if (c == 0) break;
    if (c < 0x80) {
      out.push_back(char(c));
    } else if (c < 0x800) {
      out.push_back(char(0xC0 | (c >> 6)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else {
      out.push_back(char(0xE0 | (c >> 12)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

}  // namespace

std::optional<ZaSave> ParseZaSave(const std::vector<std::uint8_t>& file) {
  // Grande o bastante para hash + alguns blocos; o main do Z-A tem ~3 MB.
  if (file.size() < 0x100000) return std::nullopt;

  // 1. Hash: SHA256(intro || payload || outro) == ultimos 32 bytes.
  const std::size_t payload_size = file.size() - 32;
  Sha256 sha;
  sha.Update(kIntro, sizeof(kIntro));
  sha.Update(file.data(), payload_size);
  sha.Update(kOutro, sizeof(kOutro));
  std::uint8_t digest[32];
  sha.Final(digest);
  if (std::memcmp(digest, file.data() + payload_size, 32) != 0) {
    return std::nullopt;
  }

  // 2. Xorpad estatico, avancando 0x7F por rodada.
  std::vector<std::uint8_t> payload(file.begin(), file.begin() + payload_size);
  for (std::size_t base = 0; base < payload.size(); base += 0x7F) {
    const std::size_t n = std::min<std::size_t>(0x80, payload.size() - base);
    for (std::size_t i = 0; i < n; ++i) payload[base + i] ^= kXorpad[i];
  }

  // 3. Caminhar pelos SCBlocks coletando caixas e party.
  ZaSave save;
  std::size_t off = 0;
  while (off + 5 <= payload.size()) {
    const std::uint32_t key = ReadU32(payload.data() + off);
    off += 4;
    XorShift32 xk(key);
    const std::uint8_t type = payload[off++] ^ xk.Next();

    if (type >= 1 && type <= 3) continue;  // booleano: sem dados

    if (type == 4) {  // Object
      if (off + 4 > payload.size()) return std::nullopt;
      const std::uint32_t n = ReadU32(payload.data() + off) ^ xk.Next32();
      off += 4;
      if (off + n > payload.size()) return std::nullopt;
      if (key == kKeyBox || key == kKeyParty || key == kKeyStatus) {
        std::vector<std::uint8_t> blk(payload.begin() + off,
                                      payload.begin() + off + n);
        for (std::size_t i = 0; i < blk.size(); ++i) blk[i] ^= xk.Next();

        if (key == kKeyBox) {
          save.box_data = std::move(blk);
        } else if (key == kKeyParty) {
          save.party_data = std::move(blk);
        } else if (blk.size() >= 0x2A) {
          // Layout do MyStatus9a: TID em 0x00, SID em 0x02, nome em 0x10
          // (UTF-16, 0x1A bytes).
          save.trainer.tid = ReadU16(blk.data());
          save.trainer.sid = ReadU16(blk.data() + 2);
          save.trainer.name = Utf16ToUtf8(blk.data() + 0x10, 13);
        }
      }
      off += n;
    } else if (type == 5) {  // Array
      if (off + 5 > payload.size()) return std::nullopt;
      const std::uint32_t count = ReadU32(payload.data() + off) ^ xk.Next32();
      off += 4;
      const std::uint8_t sub = payload[off++] ^ xk.Next();
      const int esize = sub == 3 ? 1 : TypeSize(sub);
      if (esize < 0) return std::nullopt;
      off += std::size_t(count) * esize;
    } else {
      const int n = TypeSize(type);
      if (n < 0) return std::nullopt;
      if (key == kKeySeconds && n == 8 && off + 8 <= payload.size()) {
        std::uint8_t raw[8];
        for (int i = 0; i < 8; ++i) raw[i] = payload[off + i] ^ xk.Next();
        double secs = 0.0;
        std::memcpy(&secs, raw, sizeof(secs));
        if (secs > 0.0 && secs < 1e9) {
          save.trainer.play_seconds = static_cast<std::uint32_t>(secs);
        }
      }
      off += n;
    }
  }
  if (save.box_data.size() < kBoxCount * kSlotsPerBox * kSlotSize) {
    return std::nullopt;  // bloco das caixas ausente ou curto demais
  }

  for (std::size_t b = 0; b < kBoxCount; ++b) {
    for (std::size_t sl = 0; sl < kSlotsPerBox; ++sl) {
      if (ReadZaBoxPokemon(save, b, sl)) ++save.count;
    }
  }
  return save;
}

namespace {

std::optional<ZaPokemon> ReadRecordAt(const std::vector<std::uint8_t>& data,
                                      std::size_t off) {
  if (off + kRecordSize > data.size()) return std::nullopt;

  std::uint8_t rec[kRecordSize];
  std::memcpy(rec, data.data() + off, kRecordSize);
  if (ReadU32(rec) == 0) return std::nullopt;  // slot vazio

  DecryptRecord(rec);

  ZaPokemon mon;
  mon.species = ReadU16(rec + 0x08);
  if (mon.species == 0) return std::nullopt;
  mon.nickname = Utf16ToUtf8(rec + 0x58, 13);
  mon.level = rec[0x148];
  return mon;
}

}  // namespace

std::optional<ZaPokemon> ReadZaBoxPokemon(const ZaSave& save, std::size_t box,
                                          std::size_t slot) {
  if (box >= kBoxCount || slot >= kSlotsPerBox) return std::nullopt;
  return ReadRecordAt(save.box_data, (box * kSlotsPerBox + slot) * kSlotSize);
}

std::optional<ZaPokemon> ReadZaPartyPokemon(const ZaSave& save,
                                            std::size_t slot) {
  if (slot >= kPartySlots) return std::nullopt;
  return ReadRecordAt(save.party_data, slot * kPartySlotSize);
}

std::string ZaSpeciesName(std::uint16_t species) {
  if (species == 0 || species >= kGen9SpeciesCount) return "?";
  return kGen9Species[species];
}

}  // namespace za
