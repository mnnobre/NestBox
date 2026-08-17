#include "bag_writer.h"

#include <cstring>

#include "bag_tables.h"

namespace bagw {
namespace {

// --- Os dois mundos --------------------------------------------------------
//
// SwSh/LGPE: lista ESPARSA de u32 auto-descritivo (id e count no mesmo slot).
// SV/BDSP:   tabela DENSA de 16 bytes indexada pelo id; o slot nao diz quem e.
enum class Kind { kNone, kSparse, kDense };

Kind KindOf(savew::Game g) {
  switch (g) {
    case savew::Game::kSwSh:
    case savew::Game::kLGPE: return Kind::kSparse;
    case savew::Game::kSV:
    case savew::Game::kBDSP: return Kind::kDense;
    // PLA: sem held item, sem escrita de bag. Ver o cabecalho.
    default: return Kind::kNone;
  }
}

// --- As pouches -----------------------------------------------------------
//
// Offset dentro da regiao da bag e quantidade de slots. Todos saidos da sonda
// (`tools/pkhex-bag dump`, campo `offset` de InventoryPouch lido por reflexao
// porque e `internal` nesta versao), e conferidos pelo `probe`: o offset onde
// o PkHeX escreve ao mexer no primeiro item de cada pouch bate com o valor
// abaixo.
//
// KeyItems e TMHMs ficam de FORA de proposito: MaxCount = 1 neles, e Pokemon
// nao segura key item nem TM. Incluir seria abrir caminho para escrever 999
// numa pouch que so aceita 1.
struct Pouch {
  std::size_t offset;
  std::size_t slots;
};

// SwSh — SCBlock 0x1177C2C4 (4856 bytes).
constexpr Pouch kSwSh[] = {
    {0x000, 60},   // Medicine
    {0x0F0, 30},   // Balls
    {0x168, 20},   // BattleItems
    {0x1B8, 80},   // Berries
    {0x2F8, 550},  // Items
    {0xED8, 100},  // Treasure
    {0x1068, 100}, // Candy
};

// LGPE — em claro no arquivo, a partir de 0.
constexpr Pouch kLgpe[] = {
    {0x000, 60},   // Medicine
    {0x2A0, 200},  // Candy
    {0x5C0, 150},  // ZCrystals
    {0x818, 50},   // Balls
    {0x8E0, 150},  // BattleItems
    {0xB38, 150},  // Items
};

struct Layout {
  const Pouch* pouches;
  std::size_t n;
};

Layout LayoutOf(savew::Game g) {
  if (g == savew::Game::kSwSh) return {kSwSh, sizeof(kSwSh) / sizeof(*kSwSh)};
  if (g == savew::Game::kLGPE) return {kLgpe, sizeof(kLgpe) / sizeof(*kLgpe)};
  return {nullptr, 0};
}

// O slot: um u32 auto-descritivo. bits 0..14 = id, bits 15..29 = quantidade.
// Os bits 30..31 sao flags do jogo (novo/favorito) e NAO sao tocados — escrita
// conservadora, a mesma regra da spec 063.
constexpr std::uint32_t kIdMask = 0x7FFF;
constexpr int kCountShift = 15;
constexpr std::uint32_t kCountMask = 0x7FFF;

std::uint32_t GetU32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

void PutU32(std::uint8_t* p, std::uint32_t v) {
  p[0] = std::uint8_t(v);
  p[1] = std::uint8_t(v >> 8);
  p[2] = std::uint8_t(v >> 16);
  p[3] = std::uint8_t(v >> 24);
}

std::uint16_t IdOf(std::uint32_t slot) { return std::uint16_t(slot & kIdMask); }
std::uint16_t CountOfSlot(std::uint32_t slot) {
  return std::uint16_t((slot >> kCountShift) & kCountMask);
}
// Preserva os bits altos (flags) do slot original.
std::uint32_t MakeSlot(std::uint32_t original, std::uint16_t id,
                       std::uint16_t count) {
  const std::uint32_t flags = original & ~(kIdMask | (kCountMask << kCountShift));
  return flags | (std::uint32_t(id) & kIdMask) |
         ((std::uint32_t(count) & kCountMask) << kCountShift);
}

// --- A tabela densa (SV e BDSP) -------------------------------------------
//
// O slot mora sempre em `id * 16`, e o que muda entre os dois jogos e o que
// cada campo significa. A base do BDSP (0x563C) ja foi descontada por
// BagRegion, entao aqui os dois comecam em 0.
constexpr std::size_t kDenseStride = 16;

std::size_t DenseSlots(savew::Game g) {
  // SV: 48000/16 = 3000. BDSP: 0xBB80/16 = 3000. Iguais, mas cada um vem da
  // sua propria constante em vez de um numero solto compartilhado.
  return g == savew::Game::kSV ? 3000 : savew::kBdspBagSize / kDenseStride;
}

// Um item existe na tabela? Os dois jogos guardam esse fato em campos
// diferentes, e em ambos a contagem sozinha NAO basta:
//
//   SV    Pouch = 0xFFFFFFFF marca slot invalido (item que o jogador nao tem).
//   BDSP  SortOrder = 0 significa "nao adquirido" — o proprio PkHeX descarta
//         o item na leitura, mesmo com Count > 0.
bool DenseHas(savew::Game g, const std::uint8_t* s) {
  if (g == savew::Game::kSV)
    return GetU32(s) != 0xFFFFFFFFu && GetU32(s + 4) != 0;
  return GetU32(s) != 0 && (std::uint16_t(s[12]) | (std::uint16_t(s[13]) << 8)) != 0;
}

std::uint32_t DenseCount(savew::Game g, const std::uint8_t* s) {
  return GetU32(s + (g == savew::Game::kSV ? 4 : 0));
}

// O maior SortOrder gravado na tabela do BDSP. Item novo entra em max+1: um
// valor maior que qualquer SortOrder de qualquer pouch e, portanto, valido
// como "ultimo da lista" na pouch que ele acabar ocupando. Ver spec 074 TD-02.
std::uint16_t MaxSortOrder(const std::vector<std::uint8_t>& region) {
  std::uint16_t max = 0;
  for (std::size_t at = 0; at + kDenseStride <= region.size(); at += kDenseStride) {
    const std::uint16_t so =
        std::uint16_t(region[at + 12]) | std::uint16_t(region[at + 13] << 8);
    if (so > max) max = so;
  }
  return max;
}

}  // namespace

bool Supported(savew::Game g) { return KindOf(g) != Kind::kNone; }

std::vector<Item> ReadBag(const savew::SaveData& sd) {
  std::vector<Item> out;
  if (KindOf(sd.game) == Kind::kNone) return out;
  const std::vector<std::uint8_t> region = savew::BagRegion(sd);
  if (region.empty()) return out;

  if (KindOf(sd.game) == Kind::kDense) {
    const std::size_t n = DenseSlots(sd.game);
    for (std::size_t id = 1; id < n; ++id) {
      const std::size_t at = id * kDenseStride;
      if (at + kDenseStride > region.size()) break;
      const std::uint8_t* s = region.data() + at;
      if (!DenseHas(sd.game, s)) continue;
      const std::uint32_t c = DenseCount(sd.game, s);
      out.push_back({std::uint16_t(id),
                     std::uint16_t(c > 0xFFFF ? 0xFFFF : c)});
    }
    return out;
  }

  const Layout L = LayoutOf(sd.game);
  for (std::size_t p = 0; p < L.n; ++p) {
    for (std::size_t i = 0; i < L.pouches[p].slots; ++i) {
      const std::size_t at = L.pouches[p].offset + i * 4;
      if (at + 4 > region.size()) break;
      const std::uint32_t slot = GetU32(region.data() + at);
      const std::uint16_t id = IdOf(slot);
      if (id != 0) out.push_back({id, CountOfSlot(slot)});
    }
  }
  return out;
}

std::uint16_t CountOf(const savew::SaveData& sd, std::uint16_t item_id) {
  for (const Item& it : ReadBag(sd))
    if (it.id == item_id) return it.count;
  return 0;
}

bool AddItemToBag(savew::SaveData& sd, std::uint16_t item_id,
                  std::uint16_t qtd) {
  if (item_id == 0 || qtd == 0) return false;
  if (KindOf(sd.game) == Kind::kNone) return false;
  std::vector<std::uint8_t> region = savew::BagRegion(sd);
  if (region.empty()) return false;

  // --- Tabela densa: o id E o endereco, nao ha o que varrer nem slot para
  // ocupar. "Item novo" aqui significa slot que existe mas esta marcado como
  // nao-possuido, e o que muda e so o campo que marca isso.
  if (KindOf(sd.game) == Kind::kDense) {
    const std::size_t at = std::size_t(item_id) * kDenseStride;
    if (item_id >= DenseSlots(sd.game) || at + kDenseStride > region.size())
      return false;
    std::uint8_t* s = region.data() + at;
    const bool novo = !DenseHas(sd.game, s);

    const std::uint32_t atual = novo ? 0 : DenseCount(sd.game, s);
    const std::uint32_t soma = atual + qtd;
    const std::uint32_t total = soma > kMaxCount ? kMaxCount : soma;

    // Parte dos bytes ORIGINAIS do slot e sobrescreve so o que muda — a mesma
    // escrita conservadora da spec 063. Em item que ja existe isso preserva
    // Flags/Favorite/SortOrder que o jogo gravou.
    std::uint8_t bytes[kDenseStride];
    std::memcpy(bytes, s, kDenseStride);

    if (sd.game == savew::Game::kSV) {
      if (novo) {
        // O slot zerado diria "Medicine" para qualquer item: a pouch tem de
        // vir da tabela. Item que o SV nao conhece e recusado em vez de
        // gravado numa pouch inventada.
        const std::uint8_t pouch = pokehome::bag::SvPouchOf(item_id);
        if (pouch == pokehome::bag::kPouchNone) return false;
        PutU32(bytes, pouch);
        PutU32(bytes + 8, 5);  // IsNew|IsObtained, o que o PkHeX grava
        PutU32(bytes + 12, 0);
      }
      PutU32(bytes + 4, total);
    } else {
      PutU32(bytes, total);
      if (novo) {
        // SortOrder = 0 e "nao adquirido": sem isto o item some da bag mesmo
        // com contagem. Ver spec 074, TD-02.
        const std::uint32_t so = std::uint32_t(MaxSortOrder(region)) + 1;
        if (so > 0xFFFF) return false;
        bytes[12] = std::uint8_t(so);
        bytes[13] = std::uint8_t(so >> 8);
        PutU32(bytes + 4, 0);  // IsNew INVERTIDO: 0 = novo
        PutU32(bytes + 8, 0);  // IsFavorite
      }
    }
    sd.bag_patches.push_back({at, {bytes, bytes + kDenseStride}});
    return true;
  }

  const Layout L = LayoutOf(sd.game);

  // Primeira varredura: o item ja esta na bag? Incrementar o slot existente e
  // obrigatorio — criar um segundo slot com o mesmo id deixa a bag com item
  // duplicado, que o jogo nao produz.
  std::size_t empty_at = 0;
  bool has_empty = false;
  for (std::size_t p = 0; p < L.n; ++p) {
    for (std::size_t i = 0; i < L.pouches[p].slots; ++i) {
      const std::size_t at = L.pouches[p].offset + i * 4;
      if (at + 4 > region.size()) break;
      const std::uint32_t slot = GetU32(region.data() + at);
      const std::uint16_t id = IdOf(slot);
      if (id == item_id) {
        const std::uint32_t soma = std::uint32_t(CountOfSlot(slot)) + qtd;
        const std::uint16_t novo =
            std::uint16_t(soma > kMaxCount ? kMaxCount : soma);
        std::uint8_t bytes[4];
        PutU32(bytes, MakeSlot(slot, item_id, novo));
        sd.bag_patches.push_back({at, {bytes, bytes + 4}});
        return true;
      }
      if (id == 0 && !has_empty) { empty_at = at; has_empty = true; }
    }
  }

  // Item novo: ocupa o primeiro slot vazio. Sem slot vazio a bag esta cheia —
  // e recusar e melhor que sobrescrever item do jogador.
  if (!has_empty) return false;
  const std::uint16_t novo = qtd > kMaxCount ? kMaxCount : qtd;
  std::uint8_t bytes[4];
  PutU32(bytes, MakeSlot(GetU32(region.data() + empty_at), item_id, novo));
  sd.bag_patches.push_back({empty_at, {bytes, bytes + 4}});
  return true;
}

bool WithdrawHeldItem(savew::SaveData& sd, std::size_t box_i,
                      std::size_t slot_i) {
  const savew::SaveData::Slot& s = sd.At(box_i, slot_i);
  if (!s.present) return false;
  const std::uint16_t item = s.mon.held_item;
  if (item == 0) return false;

  // A ordem importa: se a bag recusar (cheia), o Pokemon NAO pode perder o
  // item — "nunca se perde" e literalmente a regra da §7.
  if (!AddItemToBag(sd, item, 1)) return false;

  pkm::Pokemon mon = s.mon;
  mon.held_item = 0;
  return sd.Set(box_i, slot_i, mon);
}

}  // namespace bagw
