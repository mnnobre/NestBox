#include "swish_crypto.h"

#include <cstring>

#include "sha256.h"

namespace swc {
namespace {

constexpr std::size_t kHashSize = 32;

// Os 64+64 bytes fixos que o jogo concatena em volta do payload antes do
// SHA256. Valores do PkHeX (SwishCrypto.cs).
constexpr std::uint8_t kIntro[64] = {
    0x9e, 0xc9, 0x9c, 0xd7, 0x0e, 0xd3, 0x3c, 0x44, 0xfb, 0x93, 0x03,
    0xdc, 0xeb, 0x39, 0xb4, 0x2a, 0x19, 0x47, 0xe9, 0x63, 0x4b, 0xa2,
    0x33, 0x44, 0x16, 0xbf, 0x82, 0xa2, 0xba, 0x63, 0x55, 0xb6, 0x3d,
    0x9d, 0xf2, 0x4b, 0x5f, 0x7b, 0x6a, 0xb2, 0x62, 0x1d, 0xc2, 0x1b,
    0x68, 0xe5, 0xc8, 0xb5, 0x3a, 0x05, 0x90, 0x00, 0xe8, 0xa8, 0x10,
    0x3d, 0xe2, 0xec, 0xf0, 0x0c, 0xb2, 0xed, 0x4f, 0x6d,
};
constexpr std::uint8_t kOutro[64] = {
    0xd6, 0xc0, 0x1c, 0x59, 0x8b, 0xc8, 0xb8, 0xcb, 0x46, 0xe1, 0x53,
    0xfc, 0x82, 0x8c, 0x75, 0x75, 0x13, 0xe0, 0x45, 0xdf, 0x32, 0x69,
    0x3c, 0x75, 0xf0, 0x59, 0xf8, 0xd9, 0xa2, 0x5f, 0xb2, 0x17, 0xe0,
    0x80, 0x52, 0xdb, 0xea, 0x89, 0x73, 0x99, 0x75, 0x79, 0xaf, 0xcb,
    0x2e, 0x80, 0x07, 0xe6, 0xf1, 0x26, 0xe0, 0x03, 0x0a, 0xe6, 0x6f,
    0xf6, 0x41, 0xbf, 0x7e, 0x59, 0xc2, 0xae, 0x55, 0xfd,
};

constexpr std::size_t kPadLength = 127;
constexpr std::uint8_t kXorPad[kPadLength] = {
    0xa0, 0x92, 0xd1, 0x06, 0x07, 0xdb, 0x32, 0xa1, 0xae, 0x01, 0xf5,
    0xc5, 0x1e, 0x84, 0x4f, 0xe3, 0x53, 0xca, 0x37, 0xf4, 0xa7, 0xb0,
    0x4d, 0xa0, 0x18, 0xb7, 0xc2, 0x97, 0xda, 0x5f, 0x53, 0x2b, 0x75,
    0xfa, 0x48, 0x16, 0xf8, 0xd4, 0x8a, 0x6f, 0x61, 0x05, 0xf4, 0xe2,
    0xfd, 0x04, 0xb5, 0xa3, 0x0f, 0xfc, 0x44, 0x92, 0xcb, 0x32, 0xe6,
    0x1b, 0xb9, 0xb1, 0x2e, 0x01, 0xb0, 0x56, 0x53, 0x36, 0xd2, 0xd1,
    0x50, 0x3d, 0xde, 0x5b, 0x2e, 0x0e, 0x52, 0xfd, 0xdf, 0x2f, 0x7b,
    0xca, 0x63, 0x50, 0xa4, 0x67, 0x5d, 0x23, 0x17, 0xc0, 0x52, 0xe1,
    0xa6, 0x30, 0x7c, 0x2b, 0xb6, 0x70, 0x36, 0x5b, 0x2a, 0x27, 0x69,
    0x33, 0xf5, 0x63, 0x7b, 0x36, 0x3f, 0x26, 0x9b, 0xa3, 0xed, 0x7a,
    0x53, 0x00, 0xa4, 0x48, 0xb3, 0x50, 0x9e, 0x14, 0xa0, 0x52, 0xde,
    0x7e, 0x10, 0x2b, 0x1b, 0x77, 0x6e,
};

sha256::Digest HashPayload(const std::uint8_t* payload, std::size_t size) {
  sha256::Context ctx;
  ctx.Update(kIntro, sizeof(kIntro));
  ctx.Update(payload, size);
  ctx.Update(kOutro, sizeof(kOutro));
  return ctx.Finish();
}

void XorPadInPlace(std::vector<std::uint8_t>& data) {
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] ^= kXorPad[i % kPadLength];
  }
}

// O gerador por bloco: xorshift semeado pela key, avancado popcount(key)
// vezes antes do primeiro byte. Bytes saem da palavra corrente de 4 em 4.
struct BlockRng {
  explicit BlockRng(std::uint32_t key) {
    std::uint32_t ones = 0;
    for (std::uint32_t k = key; k; k >>= 1) ones += k & 1;
    state_ = key;
    for (std::uint32_t i = 0; i < ones; ++i) state_ = Advance(state_);
  }

  std::uint8_t NextU8() {
    const std::uint8_t out =
        static_cast<std::uint8_t>(state_ >> (counter_ << 3));
    if (counter_ == 3) {
      state_ = Advance(state_);
      counter_ = 0;
    } else {
      ++counter_;
    }
    return out;
  }

  std::uint32_t NextU32() {
    std::uint32_t v = NextU8();
    v |= static_cast<std::uint32_t>(NextU8()) << 8;
    v |= static_cast<std::uint32_t>(NextU8()) << 16;
    v |= static_cast<std::uint32_t>(NextU8()) << 24;
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

std::size_t ScalarSize(std::uint8_t type) {
  switch (type) {
    case 1: case 2: case 3:  // bool: o type e o valor
      return 0;
    case 8: case 12:  return 1;   // u8 / i8
    case 9: case 13:  return 2;   // u16 / i16
    case 10: case 14: case 16: return 4;  // u32 / i32 / f32
    case 11: case 15: case 17: return 8;  // u64 / i64 / f64
    default:
      return static_cast<std::size_t>(-1);  // tipo invalido
  }
}

std::uint32_t ReadU32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         static_cast<std::uint32_t>(p[1]) << 8 |
         static_cast<std::uint32_t>(p[2]) << 16 |
         static_cast<std::uint32_t>(p[3]) << 24;
}

}  // namespace

bool HashIsValid(const std::vector<std::uint8_t>& file) {
  if (file.size() <= kHashSize) return false;
  const std::size_t payload = file.size() - kHashSize;
  const sha256::Digest d = HashPayload(file.data(), payload);
  return std::memcmp(d.data(), file.data() + payload, kHashSize) == 0;
}

std::optional<std::vector<ScBlock>> Decrypt(
    const std::vector<std::uint8_t>& file) {
  if (!HashIsValid(file)) return std::nullopt;

  std::vector<std::uint8_t> body(file.begin(), file.end() - kHashSize);
  XorPadInPlace(body);

  std::vector<ScBlock> blocks;
  std::size_t off = 0;
  const std::size_t n = body.size();
  while (off < n) {
    if (off + 5 > n) return std::nullopt;  // key + type nao cabem
    ScBlock b;
    b.key = ReadU32(&body[off]);
    off += 4;
    BlockRng rng(b.key);
    b.type = body[off++] ^ rng.NextU8();

    if (b.type == 4) {  // object
      if (off + 4 > n) return std::nullopt;
      const std::uint32_t size = ReadU32(&body[off]) ^ rng.NextU32();
      off += 4;
      if (off + size > n) return std::nullopt;
      b.data.resize(size);
      for (std::uint32_t i = 0; i < size; ++i) {
        b.data[i] = body[off + i] ^ rng.NextU8();
      }
      off += size;
    } else if (b.type == 5) {  // array
      if (off + 5 > n) return std::nullopt;
      const std::uint32_t count = ReadU32(&body[off]) ^ rng.NextU32();
      off += 4;
      b.subtype = body[off++] ^ rng.NextU8();
      const std::size_t esize =
          b.subtype >= 8 ? ScalarSize(b.subtype) : 1;  // bool arrays: 1 byte
      if (esize == static_cast<std::size_t>(-1)) return std::nullopt;
      const std::size_t size = count * esize;
      if (off + size > n) return std::nullopt;
      b.data.resize(size);
      for (std::size_t i = 0; i < size; ++i) {
        b.data[i] = body[off + i] ^ rng.NextU8();
      }
      off += size;
    } else {  // escalar/bool
      const std::size_t size = ScalarSize(b.type);
      if (size == static_cast<std::size_t>(-1)) return std::nullopt;
      if (off + size > n) return std::nullopt;
      b.data.resize(size);
      for (std::size_t i = 0; i < size; ++i) {
        b.data[i] = body[off + i] ^ rng.NextU8();
      }
      off += size;
    }
    blocks.push_back(std::move(b));
  }
  return blocks;
}

std::vector<std::uint8_t> Encrypt(const std::vector<ScBlock>& blocks) {
  std::vector<std::uint8_t> body;
  for (const ScBlock& b : blocks) {
    body.push_back(static_cast<std::uint8_t>(b.key));
    body.push_back(static_cast<std::uint8_t>(b.key >> 8));
    body.push_back(static_cast<std::uint8_t>(b.key >> 16));
    body.push_back(static_cast<std::uint8_t>(b.key >> 24));
    BlockRng rng(b.key);
    body.push_back(b.type ^ rng.NextU8());
    if (b.type == 4) {
      const std::uint32_t size =
          static_cast<std::uint32_t>(b.data.size()) ^ rng.NextU32();
      body.push_back(static_cast<std::uint8_t>(size));
      body.push_back(static_cast<std::uint8_t>(size >> 8));
      body.push_back(static_cast<std::uint8_t>(size >> 16));
      body.push_back(static_cast<std::uint8_t>(size >> 24));
    } else if (b.type == 5) {
      const std::size_t esize =
          b.subtype >= 8 ? ScalarSize(b.subtype) : 1;
      const std::uint32_t count =
          static_cast<std::uint32_t>(b.data.size() / esize) ^ rng.NextU32();
      body.push_back(static_cast<std::uint8_t>(count));
      body.push_back(static_cast<std::uint8_t>(count >> 8));
      body.push_back(static_cast<std::uint8_t>(count >> 16));
      body.push_back(static_cast<std::uint8_t>(count >> 24));
      body.push_back(b.subtype ^ rng.NextU8());
    }
    for (const std::uint8_t byte : b.data) {
      body.push_back(byte ^ rng.NextU8());
    }
  }

  XorPadInPlace(body);
  const sha256::Digest d = HashPayload(body.data(), body.size());
  body.insert(body.end(), d.begin(), d.end());
  return body;
}

const ScBlock* Find(const std::vector<ScBlock>& blocks, std::uint32_t key) {
  for (const ScBlock& b : blocks) {
    if (b.key == key) return &b;
  }
  return nullptr;
}

}  // namespace swc
