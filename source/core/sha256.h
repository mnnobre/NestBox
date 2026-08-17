// SHA-256 (spec 055). Implementacao propria porque o core nao tem
// dependencias externas; o algoritmo e o FIPS 180-4, sem variacao.
// Usado pelo SwishCrypto (hash de integridade dos saves SwSh/PLA/SV).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sha256 {

using Digest = std::array<std::uint8_t, 32>;

// Hash de um buffer continuo.
Digest Hash(const std::uint8_t* data, std::size_t size);

// Streaming, para hashear intro + payload + outro sem concatenar.
struct Context {
  Context();
  void Update(const std::uint8_t* data, std::size_t size);
  Digest Finish();

 private:
  void ProcessBlock(const std::uint8_t* block);

  std::uint32_t state_[8];
  std::uint8_t buffer_[64];
  std::size_t buffered_ = 0;
  std::uint64_t total_ = 0;
};

}  // namespace sha256
