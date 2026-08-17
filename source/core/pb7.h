// Parser do PB7 — Pokemon Let's Go Pikachu/Eevee (spec 062, F05 da
// descoberta parsers-switch-completos).
//
// O mais diferente dos cinco formatos modernos: 260 bytes (box == party),
// cifra de bloco 0x38, sem ribbons, sem memorias, sem home tracker, e com
// os AVs (awakening values) que so existem aqui.
//
// Referencia: PkHeX (fonte de verdade) via pkm_rs/src/gen7_lgpe/pb7.rs.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "pkm_model.h"

namespace pb7 {

// Box e party tem o MESMO tamanho neste formato.
inline constexpr std::size_t kStoredSize = 260;  // 0x104
inline constexpr std::size_t kPartySize = 260;

std::optional<pkm::Pokemon> Parse(const std::uint8_t* data, std::size_t size);
std::optional<pkm::Pokemon> Parse(const std::vector<std::uint8_t>& data);

// Inverso do Parse (spec 063, G01) — binario DECIFRADO, checksum recalculado.
// Escrita conservadora: parte de `p.raw`, sobrescreve so o que o Parse le.
// Campos que o PB7 nao tem (ribbons, memorias, tracker) nao sao escritos.
// `p.raw` vazio => buffer zerado de kStoredSize (TD-02).
std::vector<std::uint8_t> Write(const pkm::Pokemon& p);

}  // namespace pb7
