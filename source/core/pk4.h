// Formato PK4 — Diamond/Pearl/Platinum/HGSS (spec 126, G4-F01).
//
// 136 bytes armazenados (0x88), 236 no party (0xEC). Mesma filosofia do
// gen3: 4 blocos de 32 bytes embaralhados pelo PID e cifrados — mas a cifra
// e um LCRNG semeado pelo CHECKSUM (nao um XOR de chave fixa), e os nomes
// usam o charset proprio da gen4 (gen4_charset.h), nao UTF-16.
//
// Parse aceita cifrado ou em claro (detecta pelo checksum) e devolve o raw
// EM CLARO na ordem canonica, como os parsers modernos. Write devolve os
// bytes em claro — quem grava num save cifra na saida (G4-F02).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "pkm_model.h"

namespace pk4 {

inline constexpr std::size_t kStoredSize = 136;
inline constexpr std::size_t kPartySize = 236;

std::optional<pkm::Pokemon> Parse(const std::uint8_t* data, std::size_t size);
std::optional<pkm::Pokemon> Parse(const std::vector<std::uint8_t>& data);

std::vector<std::uint8_t> Write(const pkm::Pokemon& p);

}  // namespace pk4
