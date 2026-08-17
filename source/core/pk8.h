// Parser do PK8 — Sword/Shield (spec 058, F06 da descoberta
// parsers-switch-completos).
//
// Aceita o buffer cifrado ou decifrado (328 stored / 344 party); devolve o
// modelo unico preenchido. Offsets do PkHeX (fonte de verdade), via enum
// Offset de pkm_rs/src/gen8_swsh/pk8_buffer.rs.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "pkm_model.h"

namespace pk8 {

inline constexpr std::size_t kStoredSize = 328;  // 0x148
inline constexpr std::size_t kPartySize = 344;   // 0x158

// nullopt se o tamanho nao e de PK8 ou o checksum nao fecha nem cifrado nem
// decifrado. Slot vazio (species 0) parseia normalmente — `empty()` decide.
std::optional<pkm::Pokemon> Parse(const std::uint8_t* data, std::size_t size);
std::optional<pkm::Pokemon> Parse(const std::vector<std::uint8_t>& data);

// Inverso do Parse (spec 063, G01): devolve o binario DECIFRADO, com o
// checksum de 0x06 recalculado. Quem cifra e o save writer (G02).
//
// ESCRITA CONSERVADORA (TD-01): parte de `p.raw` e sobrescreve SO os offsets
// que o Parse acima le. Padding e campos nao mapeados saem identicos.
// `p.raw` vazio => buffer de kStoredSize zerado (TD-02); esse caminho nunca
// foi validado contra o jogo.
std::vector<std::uint8_t> Write(const pkm::Pokemon& p);

}  // namespace pk8
