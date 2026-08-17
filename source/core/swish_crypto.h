// SwishCrypto (spec 055) — cifra e integridade dos saves de SwSh, PLA e SV.
//
// Arquivo = payload ‖ SHA256(INTRO ‖ payload ‖ OUTRO). O payload passa por um
// XOR de pad estatico e vira uma sequencia de SCBlocks tipados, cada um
// cifrado por um xorshift semeado pela propria key.
//
// Referencia: PkHeX SwishCrypto.cs (fonte de verdade da descoberta
// parsers-switch-completos), via OpenHome/pkm_rs.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace swc {

// Um bloco decifrado. `type` e o id cru do formato:
//   1..3 bool (o proprio type e o valor; payload vazio)
//   8..17 escalar (1/2/4/8 bytes)
//   4 object, 5 array (subtype preenchido)
struct ScBlock {
  std::uint32_t key = 0;
  std::uint8_t type = 0;
  std::uint8_t subtype = 0;
  std::vector<std::uint8_t> data;
};

// O hash dos 32 bytes finais confere com o payload?
bool HashIsValid(const std::vector<std::uint8_t>& file);

// Decifra o arquivo inteiro em blocos. nullopt se o hash nao bate ou um
// bloco tem tipo invalido (arquivo corrompido ou nao e um save destes jogos).
std::optional<std::vector<ScBlock>> Decrypt(
    const std::vector<std::uint8_t>& file);

// Recifra os blocos no formato de arquivo (XOR + hash novo). A ordem dos
// blocos e a dada — para roundtrip exato, preserve a ordem da leitura.
std::vector<std::uint8_t> Encrypt(const std::vector<ScBlock>& blocks);

// Busca linear por key. nullptr se ausente.
const ScBlock* Find(const std::vector<ScBlock>& blocks, std::uint32_t key);

}  // namespace swc
