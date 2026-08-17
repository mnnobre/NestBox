// Save writer por jogo (spec 068, G02+G03 da descoberta escrita-e-transferencia).
//
// Le e regrava as CAIXAS dos cinco saves modernos. O criterio que sustenta o
// modulo inteiro e o G03, o **portao de seguranca do epico**:
//
//     Save(Load(x)) == x   byte a byte, sem alterar nada
//
// Se regravar sem mudar nada ja muda bytes, qualquer transferencia futura
// estaria corrompendo dado em silencio. Por isso o writer e CONSERVADOR no
// mesmo sentido da spec 063: ele parte do arquivo ORIGINAL e sobrescreve
// somente os slots de caixa cujo Pokemon o chamador de fato trocou.
//
// A PARTY FICA FORA (decisao do dono, 2026-08-16; descoberta §"A party fica
// FORA"). O HOME so enxerga as caixas. Nem a leitura nem a escrita tocam a
// regiao de party — inclusive no LGPE, onde a party referencia a lista de
// armazenamento por INDICE e mexer na ordem a corromperia.
//
// Consequencia registrada: o save de Legends Arceus tem 0 Pokemon nas caixas
// e 1 na party. Para nos ele e um save VAZIO, e isso e o comportamento
// correto, nao um bug.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pkm_model.h"

namespace savew {

// Qual jogo, e portanto qual layout de caixa e qual esquema de integridade.
enum class Game : std::uint8_t {
  kSwSh,  // Sword/Shield   — SCBlock 0x0D66012C, 32x30, slot 344 (PK8)
  kSV,    // Scarlet/Violet — SCBlock 0x0D66012C, 32x30, slot 344 (PK9)
  kPLA,   // Legends Arceus — SCBlock 0x47E1CEAB, 32x30, slot 360 (PA8)
  kBDSP,  // Brilliant Diamond/Shining Pearl — offset fixo, 40x30, slot 344
  kLGPE,  // Let's Go — lista compactada de 1000 slots de 260 bytes (PB7)
  kZA,    // Legends Z-A — SCBlock 0x0D66012C, 32x30, slot 408 (registro PK9
          // de 344 + 64 de gap). Medido pela sonda tools/pkhex-za, nao
          // derivado do SwSh/SV apesar de compartilhar a key (spec 080).
};

const char* GameName(Game g);

// Um save aberto. `file` e o arquivo ORIGINAL inteiro: e dele que o Save()
// parte, o que torna o roundtrip byte-identico possivel por construcao.
struct SaveData {
  Game game{};
  std::vector<std::uint8_t> file;

  // Um slot de caixa. `present` distingue "slot vazio" de "Pokemon".
  struct Slot {
    bool present = false;
    pkm::Pokemon mon;
  };
  // Achatado: box * slots_per_box + slot.
  std::vector<Slot> box;

  std::size_t box_count = 0;
  std::size_t slots_per_box = 0;

  std::size_t Count() const;
  const Slot& At(std::size_t box_i, std::size_t slot_i) const;
  // Troca o Pokemon de um slot. Marca o slot como sujo — so os sujos sao
  // reescritos por Save(), o que preserva byte a byte tudo o que nao mudou.
  bool Set(std::size_t box_i, std::size_t slot_i, const pkm::Pokemon& mon);

  const std::vector<bool>& dirty() const { return dirty_; }

  // Sobrescritas fora da regiao de caixas — hoje so a BAG (spec 072). O
  // Save() as aplica no lugar certo de cada formato: no LGPE o offset e do
  // arquivo, no SwSh e do SCBLOCO da bag (o arquivo esta cifrado, entao nao
  // existe offset de arquivo para escrever).
  //
  // Ficam aqui, e nao num campo do bag_writer, porque o Save() e quem
  // recalcula a integridade — e integridade quebrada e o que corrompe o save.
  struct RawPatch {
    std::size_t offset = 0;  // dentro do bloco da bag (SwSh) ou do arquivo (LGPE)
    std::vector<std::uint8_t> bytes;
  };
  std::vector<RawPatch> bag_patches;

  // Publico porque o Load precisa dimensiona-lo; quem escreve no save passa
  // por Set(), que e o unico caminho que marca sujeira.
  std::vector<bool> dirty_;
};

// Reconhece o formato pelo tamanho e pela estrutura e abre as caixas.
// nullopt se o arquivo nao e um save de nenhum dos cinco jogos, ou se a
// integridade nao fecha (SCBlock com hash quebrado, etc).
std::optional<SaveData> Load(const std::vector<std::uint8_t>& file);
// Variante para quando o chamador ja sabe qual jogo espera.
std::optional<SaveData> Load(const std::vector<std::uint8_t>& file, Game game);

// Devolve o arquivo completo, com todo checksum/hash de integridade
// recalculado. Sem nenhum Set(), o resultado e byte-identico a `sd.file` —
// esse e o teste G03.
std::vector<std::uint8_t> Save(const SaveData& sd);

// --- Integridade, exposta para o teste plantar a violacao -----------------
// (Um gate verde nao prova que o recalculo existe; o teste precisa poder
// quebra-lo e exigir o vermelho — rules/evidence.md.)

// --- Bag (spec 072) --------------------------------------------------------
// Onde mora o inventario de cada jogo. Descoberto sondando o PkHeX
// (tools/pkhex-bag `probe`), nao por analogia.

// SCBlock que guarda a bag do SwSh (4856 bytes = 1214 slots de u32).
constexpr std::uint32_t kSwShBagBlock = 0x1177C2C4;
// SV (spec 074): SCBlock 0x21C9BD44, 48000 bytes = 3000 slots de 16 bytes,
// tabela DENSA indexada pelo id do item.
constexpr std::uint32_t kSvBagBlock = 0x21C9BD44;
// BDSP (spec 074): save flat, tabela densa a partir de 0x563C, 0xBB80 bytes.
// A integridade e o MD5 de 0xE9818 que o Save() ja recalcula desde a spec 068
// — a bag entra ANTES dele, e por isso nao exige digest novo.
constexpr std::size_t kBdspBagOffset = 0x563C;
constexpr std::size_t kBdspBagSize = 0xBB80;
// No LGPE a bag esta em claro no arquivo, comecando em 0, e tem CHECKSUM
// PROPRIO em 0xB861A — descoberto pela sonda: alterar um item pelo PkHeX muda
// 2 bytes na bag e 2 bytes ali. Sem recalcula-lo, o save fica corrompido de um
// jeito que o PkHeX NAO acusa na leitura (contexto-tecnico.md).
// Confirmado pela pesquisa da spec 073: a bag e o BLOCO 0 da tabela oficial do
// PkHeX, e o 0xB861A e a formula `0xB861A + 8*0`. As constantes saem da tabela
// (kLgpeBlocks, mais abaixo) para nao existir um segundo lugar com o numero.
constexpr std::size_t kLgpeBagOffset = 0;
constexpr std::size_t kLgpeBagChecksum = 0xB861A;
// A regiao coberta pelo CRC nao e a bag "logica": e o BLOCO do save que a
// contem. Tamanho achado por FORCA BRUTA exigindo que o CRC recalculado bata
// com o gravado (0xD6E3) — o mesmo criterio que ja valida header e PC.
constexpr std::size_t kLgpeBagBlockSize = 0xD90;

// Os bytes da bag, decifrados. Vazio se o jogo nao tem bag suportada.
std::vector<std::uint8_t> BagRegion(const SaveData& sd);

// --- Os 21 blocos do LGPE (spec 073) ---------------------------------------
// O save de Let's Go tem 21 blocos, cada um com CRC-16/ARC proprio num rodape
// em 0xB8600. O Save() recalcula TODOS — nao so os tres que o produto escreve
// hoje (bag, header, PC). Motivo: o PkHeX NAO valida integridade de LGPE na
// leitura (contexto-tecnico.md), entao uma spec futura que escreva num bloco
// sem saber que ele tem CRC gravaria save corrompido sem nenhum oraculo
// acusar. Recalcular os 21 custa uma passada de CRC em 1 MiB e elimina a
// classe de bug inteira.
//
// Fonte: PKHeX.Core/Saves/Access/SaveBlockAccessor7b.cs (array BlockInfoGG) e
// BlockInfo3DS.cs (classe BlockInfo7b). Levantada em
// docs/pesquisa-blocos-lgpe.md, que conferiu os 21 contra o save real: 21/21.
struct LgpeBlock {
  std::size_t offset, size;
};

// Base do rodape (boGG = 0xB8800 - 0x200) e a formula do offset do checksum do
// bloco de ID k: bo + 0x14 + k*8 + 6.
constexpr std::size_t kLgpeFooter = 0xB8600;
constexpr std::size_t LgpeChecksumOffset(std::size_t id) {
  return kLgpeFooter + 0x14 + id * 8 + 6;
}

// Indexada pelo ID do bloco — a ordem E o ID, que a formula acima consome.
constexpr LgpeBlock kLgpeBlocks[] = {
    {0x00000, 0x00D90},  //  0 MyItem — a bag (spec 072)
    {0x00E00, 0x00200},  //  1 _01 (desconhecido do PkHeX)
    {0x01000, 0x00168},  //  2 MyStatus — treinador (TID/SID, nome)
    {0x01200, 0x01800},  //  3 EventWork
    {0x02A00, 0x020E8},  //  4 Zukan — Pokedex
    {0x04C00, 0x00930},  //  5 Misc
    {0x05600, 0x00004},  //  6 ConfigSave
    {0x05800, 0x00130},  //  7 PlayerGeoLocation
    {0x05A00, 0x00012},  //  8 PokeListHeader — o nosso "header" (spec 068)
    {0x05C00, 0x3F7A0},  //  9 PokeListPokemon — o nosso "PC" (spec 068)
    {0x45400, 0x00008},  // 10 PlayTime
    {0x45600, 0x00E90},  // 11 WB7Record
    {0x46600, 0x010A4},  // 12 CaptureRecord
    {0x47800, 0x000F0},  // 13 Daycare
    {0x47A00, 0x06010},  // 14 Coordinates
    {0x4DC00, 0x00200},  // 15 _15
    {0x4DE00, 0x00098},  // 16 FashionPlayer
    {0x4E000, 0x00068},  // 17 FashionStarter
    {0x4E200, 0x69780},  // 18 GoParkEntities
    {0xB7A00, 0x000B0},  // 19 GoGoParkNames
    {0xB7C00, 0x00940},  // 20 _20
};
constexpr std::size_t kLgpeBlockCount =
    sizeof(kLgpeBlocks) / sizeof(kLgpeBlocks[0]);

// IDs dos blocos que o produto de fato ESCREVE — o resto do codigo os nomeia,
// e as constantes de offset/CRC abaixo saem daqui em vez de repetir numeros.
constexpr std::size_t kLgpeBlockBag = 0;
constexpr std::size_t kLgpeBlockHeader = 8;
constexpr std::size_t kLgpeBlockPc = 9;

// As constantes da bag (declaradas acima, onde o resto do codigo as le) sao o
// bloco 0. Amarradas aqui para que divergir vire erro de compilacao, e nao um
// save corrompido — o unico lugar onde os numeros da bag podem mudar e a tabela.
static_assert(kLgpeBagOffset == kLgpeBlocks[kLgpeBlockBag].offset, "");
static_assert(kLgpeBagBlockSize == kLgpeBlocks[kLgpeBlockBag].size, "");
static_assert(kLgpeBagChecksum == LgpeChecksumOffset(kLgpeBlockBag), "");

// CRC-16/ARC (poly refletido 0xA001, init 0) — os checksums do LGPE.
std::uint16_t Crc16Arc(const std::uint8_t* data, std::size_t size);
// MD5 de 16 bytes — o hash de integridade do BDSP.
void Md5(const std::uint8_t* data, std::size_t size, std::uint8_t out[16]);

}  // namespace savew
