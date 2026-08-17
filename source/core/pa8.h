// Parser do PA8 — Pokemon Legends: Arceus (spec 060, F08 da descoberta).
//
// Mesmo envelope do PK8/PK9 (EC/sanity/checksum nos 8 primeiros bytes, 4
// blocos embaralhados), porem com bloco de cifra 0x58 em vez de 0x50 — dai os
// 360/376 bytes contra os 328/344 dos irmaos.
//
// NAO existe referencia Rust do PA8 no repo: os offsets foram levantados
// empiricamente contra as 7 fixtures JSON do PkHeX (spec 056), que e a fonte
// de verdade do projeto. Os que as fixtures nao exercitam estao marcados no
// enum e listados no evidence-log como nao verificados (TD-02).
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "pkm_model.h"

namespace pa8 {

inline constexpr std::size_t kStoredSize = 360;  // 0x168
inline constexpr std::size_t kPartySize = 376;   // 0x178

std::optional<pkm::Pokemon> Parse(const std::uint8_t* data, std::size_t size);
std::optional<pkm::Pokemon> Parse(const std::vector<std::uint8_t>& data);

// Inverso do Parse (spec 063, G01) — binario DECIFRADO, checksum recalculado.
// Escrita conservadora: parte de `p.raw`, sobrescreve so o que o Parse le.
// ATENCAO: 20 dos offsets deste formato sao derivados por analogia e NAO
// verificados (TD-02 da spec 060). Preserva-los so vale enquanto o modelo nao
// os altera; alterar um campo marcado [~] escreve num offset possivelmente
// errado. `p.raw` vazio => buffer zerado (TD-02 da 063).
std::vector<std::uint8_t> Write(const pkm::Pokemon& p);

}  // namespace pa8
