// Parser do PK9 — Scarlet/Violet (spec 059, F09 da descoberta). E tambem o
// formato do Legends Z-A, o que a F15 vai aproveitar.
//
// Mesmo envelope do PK8 (328/344, cifra de bloco 0x50), offsets proprios a
// partir do byte 50. Referencia: PkHeX via pkm_rs/src/gen9_sv/pk9_buffer.rs.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "pkm_model.h"

namespace pk9 {

inline constexpr std::size_t kStoredSize = 328;  // 0x148
inline constexpr std::size_t kPartySize = 344;   // 0x158

std::optional<pkm::Pokemon> Parse(const std::uint8_t* data, std::size_t size);
std::optional<pkm::Pokemon> Parse(const std::vector<std::uint8_t>& data);

// Inverso do Parse (spec 063, G01) — binario DECIFRADO, checksum recalculado.
// Escrita conservadora: parte de `p.raw`, sobrescreve so o que o Parse le.
// `p.raw` vazio => buffer zerado de kStoredSize (TD-02, nao validado no jogo).
std::vector<std::uint8_t> Write(const pkm::Pokemon& p);

}  // namespace pk9
