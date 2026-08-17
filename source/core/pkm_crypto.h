// Cifra dos Pokemon modernos (spec 054) — comum a PB7 (LGPE), PK8 (SwSh),
// PB8 (BDSP), PA8 (PLA) e PK9 (SV).
//
// Formato do buffer (cifrado ou nao):
//   0x00 u32 encryption constant (EC) — fora da cifra
//   0x04 u16 sanity                   — fora da cifra
//   0x06 u16 checksum                 — fora da cifra
//   0x08 4 blocos de `block_size`, embaralhados por EC e cifrados
//   ...  resto (party stats), cifrado com o MESMO stream, sem embaralhar
//
// Referencia: PkHeX (fonte de verdade, decisao do dono na descoberta
// parsers-switch-completos) via OpenHome/pkm_rs/src/encryption.rs.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pkc {

// Tamanho de bloco por formato. O tamanho total do buffer nao deriva disto:
// party e o que sobrar depois de 8 + 4*bloco.
inline constexpr std::size_t kBlockPB7 = 0x38;  // LGPE (260 bytes de buffer)
inline constexpr std::size_t kBlockPK8 = 0x50;  // SwSh/BDSP/SV (328/344)
inline constexpr std::size_t kBlockPA8 = 0x58;  // PLA (360/376)

// Decifra in-place: desfaz o XOR do corpo inteiro e o embaralhamento dos
// 4 blocos. `size` >= 8 + 4*block_size; o excedente e party.
void Decrypt(std::uint8_t* data, std::size_t size, std::size_t block_size);

// Inverso exato de Decrypt (embaralha e aplica o XOR).
void Encrypt(std::uint8_t* data, std::size_t size, std::size_t block_size);

// Soma u16 dos bytes da regiao dos blocos (8 .. 8+4*block_size) de um buffer
// DECIFRADO — o valor que o formato guarda em 0x06.
std::uint16_t Checksum(const std::uint8_t* data, std::size_t block_size);

// O buffer parece decifrado? Compara o checksum calculado com o gravado.
// Colisao e possivel (1/65536) — o chamador que conheca o contexto (TD-01).
bool IsDecrypted(const std::uint8_t* data, std::size_t block_size);

}  // namespace pkc
