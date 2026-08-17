// Parser do PB8 — Brilliant Diamond / Shining Pearl (spec 061, F07 da
// descoberta parsers-switch-completos).
//
// Mesmo envelope do PK8 (328 stored / 344 party, cifra de bloco 0x50) e, na
// pratica, o MESMO layout de offsets — conferido byte a byte contra os JSONs
// do PkHeX nas fixtures extraidas do save real de BDSP. As diferencas sao de
// SEMANTICA, nao de posicao; estao comentadas no .cpp.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "pkm_model.h"

namespace pb8 {

inline constexpr std::size_t kStoredSize = 328;  // 0x148
inline constexpr std::size_t kPartySize = 344;   // 0x158

// nullopt se o tamanho nao e de PB8 ou o buffer nao decifra. Slot vazio
// (species 0) parseia normalmente — `empty()` decide.
std::optional<pkm::Pokemon> Parse(const std::uint8_t* data, std::size_t size);
std::optional<pkm::Pokemon> Parse(const std::vector<std::uint8_t>& data);

// Inverso do Parse (spec 063, G01) — binario DECIFRADO, checksum recalculado.
// Escrita conservadora: parte de `p.raw`, sobrescreve so o que o Parse le. Os
// bytes de dinamax (144) e TR (295..308), que o BDSP nao usa e o parser nao
// mapeia, saem intactos. `p.raw` vazio => buffer zerado (TD-02).
std::vector<std::uint8_t> Write(const pkm::Pokemon& p);

}  // namespace pb8
