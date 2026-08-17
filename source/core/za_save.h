// Leitura do save do Pokémon Legends Z-A (gen9).
//
// Formato reimplementado a partir do estudo do PKHeX (referência de formato,
// não de código — ver spec 016, TD-01), validado camada a camada contra um
// save real antes de virar C++:
//
//   arquivo  = payload ⊕ xorpad estático  ‖  SHA256(intro ‖ payload ‖ outro)
//   payload  = sequência de blocos SCBlock, cada um cifrado por XorShift32
//              semeado pela própria chave do bloco
//   caixas   = bloco 0x0D66012C: 32 caixas × 30 slots × 408 bytes
//   registro = 344 bytes (party gen8): corpo cifrado por LCRNG semeado pelo
//              EncryptionConstant e embaralhado em 4 blocos de 80 bytes
//
// Este módulo é leitura pura, sem dependência de plataforma — como o gen3.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace za {

inline constexpr std::size_t kBoxCount = 32;
inline constexpr std::size_t kSlotsPerBox = 30;
inline constexpr std::size_t kSlotSize = 408;   // 344 do registro + 64 de gap
inline constexpr std::size_t kRecordSize = 344; // formato party do gen8
inline constexpr std::size_t kPartySlots = 6;
inline constexpr std::size_t kPartySlotSize = 480;  // 344 + 136 de gap

// O mínimo que a UI mostra hoje. O restante do registro entra por demanda.
struct ZaPokemon {
  std::uint16_t species = 0;
  std::uint8_t level = 0;
  std::string nickname;  // UTF-8, convertido do UTF-16 do save

  bool empty() const { return species == 0; }
};

// Dados do treinador, do bloco 0xE3E89BD1.
struct ZaTrainer {
  std::string name;
  std::uint16_t tid = 0, sid = 0;
  std::uint32_t play_seconds = 0;
};

struct ZaSave {
  // Blocos já decifrados; os registros individuais ainda estão com o crypto
  // próprio (LCRNG + shuffle), desfeito na leitura de cada slot.
  std::vector<std::uint8_t> box_data;
  std::vector<std::uint8_t> party_data;
  ZaTrainer trainer;
  std::size_t count = 0;  // Pokemon nas caixas, contado na abertura
};

// Reconhece e abre um save do Z-A: confere o hash SwishCrypto, desfaz o
// xorpad e extrai o bloco das caixas. std::nullopt se o arquivo não for um
// save deste formato.
std::optional<ZaSave> ParseZaSave(const std::vector<std::uint8_t>& file);

// Lê um slot. std::nullopt para slot vazio.
std::optional<ZaPokemon> ReadZaBoxPokemon(const ZaSave& save, std::size_t box,
                                          std::size_t slot);

// Lê um slot da party (0..5). std::nullopt para slot vazio.
std::optional<ZaPokemon> ReadZaPartyPokemon(const ZaSave& save,
                                            std::size_t slot);

// Nome da espécie gen9 (1..1025); "?" fora da faixa.
std::string ZaSpeciesName(std::uint16_t species);

}  // namespace za
