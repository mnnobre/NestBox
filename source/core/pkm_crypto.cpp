#include "pkm_crypto.h"

#include <cstring>
#include <vector>

namespace pkc {
namespace {

// As 24 permutacoes de ABCD indexadas por ((EC & 0x3E000) >> 13) % 24.
// SHUFFLE aplica (decifrado -> cifrado); UNSHUFFLE desfaz. Tabelas do PkHeX.
constexpr int kShuffle[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2},
    {0, 3, 2, 1}, {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0},
    {1, 3, 0, 2}, {1, 3, 2, 0}, {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3},
    {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0}, {3, 0, 1, 2}, {3, 0, 2, 1},
    {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0},
};
constexpr int kUnshuffle[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1},
    {0, 3, 2, 1}, {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2},
    {2, 0, 3, 1}, {3, 0, 2, 1}, {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3},
    {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1}, {1, 2, 3, 0}, {1, 3, 2, 0},
    {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
};

constexpr std::size_t kHeader = 8;

std::uint32_t ReadEC(const std::uint8_t* d) {
  return static_cast<std::uint32_t>(d[0]) |
         static_cast<std::uint32_t>(d[1]) << 8 |
         static_cast<std::uint32_t>(d[2]) << 16 |
         static_cast<std::uint32_t>(d[3]) << 24;
}

// XOR de uma faixa [from, to) com o LCRNG semeado pelo EC. A mesma funcao
// cifra e decifra — XOR e involutivo.
void CryptRange(std::uint8_t* d, std::size_t from, std::size_t to) {
  std::uint32_t seed = ReadEC(d);
  for (std::size_t i = from; i + 1 < to; i += 2) {
    seed = seed * 0x41C64E6Du + 0x6073u;
    const std::uint16_t x = static_cast<std::uint16_t>(seed >> 16);
    d[i] ^= static_cast<std::uint8_t>(x);
    d[i + 1] ^= static_cast<std::uint8_t>(x >> 8);
  }
}

// Blocos e party sao DUAS passadas independentes, cada uma RESEMEADA com o
// EC — nao um stream continuo (spec 062).
//
// Descoberto pelo PB7, o primeiro formato com fixture de tamanho party: ate
// entao todas as fixtures (PK8/PK9/PA8) eram de caixa, onde o corpo termina
// exatamente em 8+4*bloco e a segunda passada nem chega a rodar. O bug
// existia para os cinco formatos e so nao aparecia por falta de fixture.
void CryptBody(std::uint8_t* d, std::size_t size, std::size_t block_size) {
  const std::size_t body_end = kHeader + 4 * block_size;
  CryptRange(d, kHeader, body_end);
  if (size > body_end) CryptRange(d, body_end, size);
}

void Rearrange(std::uint8_t* d, std::size_t block_size, const int order[4]) {
  if (block_size > 0x58) return;  // maior formato conhecido (PA8)
  std::uint8_t tmp[4 * 0x58];
  std::memcpy(tmp, d + kHeader, 4 * block_size);
  for (int i = 0; i < 4; ++i) {
    std::memcpy(d + kHeader + i * block_size, tmp + order[i] * block_size,
                block_size);
  }
}

int ShiftValue(const std::uint8_t* d) {
  return static_cast<int>(((ReadEC(d) & 0x3E000u) >> 13) % 24u);
}

}  // namespace

void Decrypt(std::uint8_t* data, std::size_t size, std::size_t block_size) {
  if (size < kHeader + 4 * block_size) return;
  CryptBody(data, size, block_size);
  Rearrange(data, block_size, kUnshuffle[ShiftValue(data)]);
}

void Encrypt(std::uint8_t* data, std::size_t size, std::size_t block_size) {
  if (size < kHeader + 4 * block_size) return;
  Rearrange(data, block_size, kShuffle[ShiftValue(data)]);
  CryptBody(data, size, block_size);
}

std::uint16_t Checksum(const std::uint8_t* data, std::size_t block_size) {
  std::uint16_t sum = 0;
  for (std::size_t i = kHeader; i < kHeader + 4 * block_size; i += 2) {
    sum = static_cast<std::uint16_t>(
        sum + (static_cast<std::uint16_t>(data[i]) |
               static_cast<std::uint16_t>(data[i + 1]) << 8));
  }
  return sum;
}

bool IsDecrypted(const std::uint8_t* data, std::size_t block_size) {
  const std::uint16_t stored = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(data[6]) |
      static_cast<std::uint16_t>(data[7]) << 8);
  return Checksum(data, block_size) == stored;
}

}  // namespace pkc
