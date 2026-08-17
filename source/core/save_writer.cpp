#include "save_writer.h"

#include <cstring>

#include "pa8.h"
#include "pb7.h"
#include "pb8.h"
#include "pk8.h"
#include "pk9.h"
#include "pkm_crypto.h"
#include "swish_crypto.h"

namespace savew {
namespace {

// --- Layout de cada jogo ---------------------------------------------------
//
// Todos os numeros abaixo foram conferidos EMPIRICAMENTE contra os saves reais
// do dono (evidence-log da spec 068, §2): para cada um, a contagem de Pokemon
// lida bate exatamente com a que o tools/pkhex-verify reporta.

// SwSh, SV e PLA guardam as caixas num SCBlock; BDSP e LGPE, em offset fixo.
// `stride` e a distancia entre slots; `record` e quanto do slot o parser do
// formato de fato consome. Nos cinco primeiros jogos os dois coincidem, mas no
// Z-A o stride (408) tem 64 bytes de GAP alem do registro (344) — e os
// Parse()/Write() dos formatos recusam tamanho que nao seja o do registro.
// Manter os dois separados e o que impede o gap de virar dado.
struct ScLayout {
  std::uint32_t key;
  std::size_t boxes, slots, stride, record;
};
constexpr ScLayout kSwSh{0x0D66012C, 32, 30, 344, 344};
// O bloco do SV tem 990 slots (33 caixas), mas o PkHeX so expoe 32: a ultima
// e area de trabalho do jogo, nao uma caixa do jogador. Lemos as 32 que ele
// conta — a 33a fica intocada, e preservada byte a byte pela escrita
// conservadora.
constexpr ScLayout kSV{0x0D66012C, 32, 30, 344, 344};
constexpr ScLayout kPLA{0x47E1CEAB, 32, 30, 360, 360};
// Z-A (spec 080). MEDIDO pela sonda tools/pkhex-za contra o save real, nao
// derivado do SwSh/SV apesar de a key ser a mesma — a licao da spec 066. A
// sonda localizou o EncryptionConstant do slot 0 no offset 0 do bloco e o do
// slot 1 em 408, e o bloco tem 391680 bytes = 960 x 408 EXATOS, sem resto.
// O registro em si e PK9 (328/344, cifra de bloco 0x50); os 64 bytes de gap
// que sobram do stride ficam intocados, porque PutRecord so escreve o que o
// Write() devolveu.
constexpr ScLayout kZA{0x0D66012C, 32, 30, 408, 344};

// BDSP (SaveData.bin, 979108 bytes). A party mora em 0x14098 (6 x 344) e os
// nomes de caixa em 0x148aa; nada disso e tocado.
constexpr std::size_t kBdspBoxOffset = 0x14ef4;
constexpr std::size_t kBdspStride = 344;  // party size do PB8, nao 328
constexpr std::size_t kBdspBoxes = 40, kBdspSlots = 30;
constexpr std::size_t kBdspHashOffset = 0xe9818;  // MD5 de 16 bytes
constexpr std::size_t kBdspSize = 979108;

// LGPE (savedata.bin, 0x100000 bytes). NAO sao 40 caixas fisicas: e uma lista
// achatada e COMPACTADA de 1000 slots. O PkHeX a apresenta como 40x25.
constexpr std::size_t kLgpeBoxOffset = 0x5c00;
constexpr std::size_t kLgpeStride = 260;
constexpr std::size_t kLgpeSlotTotal = 1000;
constexpr std::size_t kLgpeBoxes = 40, kLgpeSlots = 25;
constexpr std::size_t kLgpePcSize = 0x3f7a0;
constexpr std::size_t kLgpeHeaderOffset = 0x5a00;
constexpr std::size_t kLgpeHeaderSize = 0x12;
constexpr std::size_t kLgpeSize = 0x100000;

// Header e PC sao os blocos 8 e 9 da tabela oficial (spec 073). Os checksums
// deles nao tem constante propria: saem do laco que recalcula os 21. Estes
// asserts impedem que o layout de caixa aqui e a tabela divirjam em silencio.
static_assert(kLgpeBoxOffset == kLgpeBlocks[kLgpeBlockPc].offset, "");
static_assert(kLgpePcSize == kLgpeBlocks[kLgpeBlockPc].size, "");
static_assert(kLgpeHeaderOffset == kLgpeBlocks[kLgpeBlockHeader].offset, "");
static_assert(kLgpeHeaderSize == kLgpeBlocks[kLgpeBlockHeader].size, "");

ScLayout LayoutOf(Game g) {
  switch (g) {
    case Game::kSwSh: return kSwSh;
    case Game::kSV: return kSV;
    case Game::kZA: return kZA;
    default: return kPLA;
  }
}

bool IsScGame(Game g) {
  return g == Game::kSwSh || g == Game::kSV || g == Game::kPLA ||
         g == Game::kZA;
}

std::optional<pkm::Pokemon> ParseFor(Game g, const std::uint8_t* p,
                                     std::size_t n) {
  switch (g) {
    case Game::kSwSh: return pk8::Parse(p, n);
    case Game::kSV: case Game::kZA: return pk9::Parse(p, n);
    case Game::kPLA: return pa8::Parse(p, n);
    case Game::kBDSP: return pb8::Parse(p, n);
    case Game::kLGPE: return pb7::Parse(p, n);
  }
  return std::nullopt;
}

std::vector<std::uint8_t> WriteFor(Game g, const pkm::Pokemon& m) {
  switch (g) {
    case Game::kSwSh: return pk8::Write(m);
    case Game::kSV: case Game::kZA: return pk9::Write(m);
    case Game::kPLA: return pa8::Write(m);
    case Game::kBDSP: return pb8::Write(m);
    case Game::kLGPE: return pb7::Write(m);
  }
  return {};
}

// O bloco de cifra do formato — o save guarda os registros CIFRADOS, e os
// Write() da spec 063 devolvem decifrado.
std::size_t CryptoBlockOf(Game g) {
  switch (g) {
    case Game::kPLA: return pkc::kBlockPA8;
    case Game::kLGPE: return pkc::kBlockPB7;
    default: return pkc::kBlockPK8;
  }
}

// Grava um registro no slot, cifrado, respeitando o tamanho do REGISTRO. O que
// sobra do stride (o "gap" entre registros, 64 bytes no Z-A) fica como estava
// — escrita conservadora ate no espaco morto.
void PutRecord(std::uint8_t* dst, std::size_t record, Game g,
               const pkm::Pokemon& mon) {
  std::vector<std::uint8_t> rec = WriteFor(g, mon);
  if (rec.empty()) return;
  if (rec.size() > record) rec.resize(record);
  pkc::Encrypt(rec.data(), rec.size(), CryptoBlockOf(g));
  std::memcpy(dst, rec.data(), rec.size());
}

void PutU16(std::uint8_t* p, std::uint16_t v) {
  p[0] = std::uint8_t(v);
  p[1] = std::uint8_t(v >> 8);
}

// --- MD5 (BDSP) ------------------------------------------------------------
// Implementacao compacta; o core nao depende de biblioteca de plataforma, do
// mesmo jeito que sha256.cpp.

struct Md5Ctx {
  std::uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;

  static std::uint32_t Rot(std::uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
  }

  void Block(const std::uint8_t* p) {
    static const std::uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
        0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
        0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
        0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
        0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7,
                              12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9,
                              14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16,
                              23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
      m[i] = std::uint32_t(p[i * 4]) | (std::uint32_t(p[i * 4 + 1]) << 8) |
             (std::uint32_t(p[i * 4 + 2]) << 16) |
             (std::uint32_t(p[i * 4 + 3]) << 24);
    }
    std::uint32_t A = a, B = b, C = c, D = d;
    for (int i = 0; i < 64; ++i) {
      std::uint32_t f;
      int g;
      if (i < 16) { f = (B & C) | (~B & D); g = i; }
      else if (i < 32) { f = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
      else if (i < 48) { f = B ^ C ^ D; g = (3 * i + 5) % 16; }
      else { f = C ^ (B | ~D); g = (7 * i) % 16; }
      const std::uint32_t tmp = D;
      D = C;
      C = B;
      B = B + Rot(A + f + K[i] + m[g], S[i]);
      A = tmp;
    }
    a += A; b += B; c += C; d += D;
  }
};

}  // namespace

void Md5(const std::uint8_t* data, std::size_t size, std::uint8_t out[16]) {
  Md5Ctx ctx;
  std::size_t i = 0;
  for (; i + 64 <= size; i += 64) ctx.Block(data + i);

  // Cauda + padding (0x80, zeros, tamanho em bits LE de 64 bits).
  std::uint8_t tail[128] = {};
  const std::size_t rest = size - i;
  std::memcpy(tail, data + i, rest);
  tail[rest] = 0x80;
  const std::size_t total = rest + 1 <= 56 ? 64 : 128;
  const std::uint64_t bits = std::uint64_t(size) * 8;
  for (int k = 0; k < 8; ++k) tail[total - 8 + k] = std::uint8_t(bits >> (k * 8));
  ctx.Block(tail);
  if (total == 128) ctx.Block(tail + 64);

  const std::uint32_t words[4] = {ctx.a, ctx.b, ctx.c, ctx.d};
  for (int w = 0; w < 4; ++w)
    for (int k = 0; k < 4; ++k) out[w * 4 + k] = std::uint8_t(words[w] >> (k * 8));
}

// CRC-16/ARC: poly refletido 0xA001, init 0, sem inversao final. E a mesma
// tabela que o OpenHome cola como 256 constantes (`SeedTableInvert`) —
// gera-la e mais curto e nao admite erro de digitacao.
std::uint16_t Crc16Arc(const std::uint8_t* data, std::size_t size) {
  std::uint16_t crc = 0;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b)
      crc = (crc & 1) ? std::uint16_t((crc >> 1) ^ 0xA001) : std::uint16_t(crc >> 1);
  }
  return crc;
}

std::vector<std::uint8_t> BagRegion(const SaveData& sd) {
  std::vector<std::uint8_t> region;
  if (sd.game == Game::kLGPE) {
    if (sd.file.size() < kLgpeBagOffset + kLgpeBagBlockSize) return {};
    region.assign(sd.file.begin() + kLgpeBagOffset,
                  sd.file.begin() + kLgpeBagOffset + kLgpeBagBlockSize);
  } else if (sd.game == Game::kBDSP) {
    // Save flat: a bag esta em claro, como no LGPE, so que num offset.
    if (sd.file.size() < kBdspBagOffset + kBdspBagSize) return {};
    region.assign(sd.file.begin() + kBdspBagOffset,
                  sd.file.begin() + kBdspBagOffset + kBdspBagSize);
  } else if (sd.game == Game::kSwSh || sd.game == Game::kSV) {
    const auto blocks = swc::Decrypt(sd.file);
    if (!blocks) return {};
    const swc::ScBlock* b =
        swc::Find(*blocks, sd.game == Game::kSV ? kSvBagBlock : kSwShBagBlock);
    if (!b) return {};
    region = b->data;
  } else {
    return {};
  }
  // Aplica o que ja foi enfileirado: sem isso, duas chamadas seguidas de
  // AddItemToBag para o mesmo item leriam as duas a contagem ORIGINAL e a
  // segunda sobrescreveria a primeira.
  for (const SaveData::RawPatch& p : sd.bag_patches) {
    if (p.offset + p.bytes.size() > region.size()) continue;
    std::memcpy(region.data() + p.offset, p.bytes.data(), p.bytes.size());
  }
  return region;
}

const char* GameName(Game g) {
  switch (g) {
    case Game::kSwSh: return "Sword/Shield";
    case Game::kSV: return "Scarlet/Violet";
    case Game::kPLA: return "Legends Arceus";
    case Game::kBDSP: return "BDSP";
    case Game::kLGPE: return "Let's Go";
    case Game::kZA: return "Legends Z-A";
  }
  return "?";
}

std::size_t SaveData::Count() const {
  std::size_t n = 0;
  for (const Slot& s : box)
    if (s.present) ++n;
  return n;
}

const SaveData::Slot& SaveData::At(std::size_t box_i, std::size_t slot_i) const {
  static const Slot kEmpty;
  const std::size_t i = box_i * slots_per_box + slot_i;
  if (box_i >= box_count || slot_i >= slots_per_box || i >= box.size())
    return kEmpty;
  return box[i];
}

bool SaveData::Set(std::size_t box_i, std::size_t slot_i,
                   const pkm::Pokemon& mon) {
  const std::size_t i = box_i * slots_per_box + slot_i;
  if (box_i >= box_count || slot_i >= slots_per_box || i >= box.size())
    return false;
  box[i].mon = mon;
  box[i].present = !mon.empty();
  dirty_[i] = true;
  return true;
}

namespace {

// Preenche os slots a partir de um buffer de caixas contiguo.
// `stride` anda de slot em slot; `record` e quanto o parser le de cada um.
// Passar o stride ao parser quebraria o Z-A: o pk9::Parse recusa tamanho que
// nao seja 328 nem 344, entao 408 devolveria nullopt em TODOS os 960 slots.
void LoadSlots(SaveData& sd, const std::uint8_t* base, std::size_t stride,
               std::size_t record, std::size_t n) {
  sd.box.resize(n);
  sd.dirty_.assign(n, false);
  for (std::size_t i = 0; i < n; ++i) {
    auto mon = ParseFor(sd.game, base + i * stride, record);
    if (mon && !mon->empty()) {
      sd.box[i].present = true;
      sd.box[i].mon = std::move(*mon);
    }
  }
}

std::optional<SaveData> LoadSc(const std::vector<std::uint8_t>& file, Game g) {
  const ScLayout L = LayoutOf(g);
  const auto blocks = swc::Decrypt(file);
  if (!blocks) return std::nullopt;
  const swc::ScBlock* b = swc::Find(*blocks, L.key);
  const std::size_t need = L.boxes * L.slots * L.stride;
  if (!b || b->data.size() < need) return std::nullopt;

  SaveData sd;
  sd.game = g;
  sd.file = file;
  sd.box_count = L.boxes;
  sd.slots_per_box = L.slots;
  LoadSlots(sd, b->data.data(), L.stride, L.record, L.boxes * L.slots);
  return sd;
}

std::optional<SaveData> LoadFixed(const std::vector<std::uint8_t>& file, Game g) {
  const bool bdsp = g == Game::kBDSP;
  const std::size_t base = bdsp ? kBdspBoxOffset : kLgpeBoxOffset;
  const std::size_t stride = bdsp ? kBdspStride : kLgpeStride;
  const std::size_t boxes = bdsp ? kBdspBoxes : kLgpeBoxes;
  const std::size_t slots = bdsp ? kBdspSlots : kLgpeSlots;
  if (file.size() != (bdsp ? kBdspSize : kLgpeSize)) return std::nullopt;
  if (base + boxes * slots * stride > file.size()) return std::nullopt;

  SaveData sd;
  sd.game = g;
  sd.file = file;
  sd.box_count = boxes;
  sd.slots_per_box = slots;
  LoadSlots(sd, file.data() + base, stride, stride, boxes * slots);
  return sd;
}

}  // namespace

std::optional<SaveData> Load(const std::vector<std::uint8_t>& file, Game game) {
  return IsScGame(game) ? LoadSc(file, game) : LoadFixed(file, game);
}

std::optional<SaveData> Load(const std::vector<std::uint8_t>& file) {
  // BDSP e LGPE se reconhecem pelo tamanho exato.
  if (file.size() == kBdspSize) return LoadFixed(file, Game::kBDSP);
  if (file.size() == kLgpeSize) return LoadFixed(file, Game::kLGPE);
  // Os tres de SCBlock: PLA tem uma key de caixa propria; SwSh e SV
  // compartilham a key e se separam pelo formato do registro, que e o que o
  // Parse de cada um aceita.
  const auto blocks = swc::Decrypt(file);
  if (!blocks) return std::nullopt;
  if (swc::Find(*blocks, kPLA.key)) return LoadSc(file, Game::kPLA);
  const swc::ScBlock* b = swc::Find(*blocks, kSwSh.key);
  if (!b) return std::nullopt;
  // SwSh, SV e Z-A compartilham a key da caixa, e PK8/PK9 tem o mesmo layout
  // de registro — contar Pokemon nao separa os tres (os parsers aceitam os
  // tres saves). O que separa e o TAMANHO do bloco, distinto nos tres e
  // medido em cada save real:
  //
  //   SwSh 330240 = 960 x 344   (32 caixas)
  //   SV   340560 = 990 x 344   (33 caixas; a 33a e area de trabalho do jogo)
  //   Z-A  391680 = 960 x 408   (32 caixas, stride 408 — sonda tools/pkhex-za)
  //
  // O Z-A e o maior e nao colide com nenhum dos dois, entao a comparacao
  // exata e suficiente e nao depende de ordem de teste.
  constexpr std::size_t kSwShBlockSize = 32 * 30 * 344;   // 330240
  constexpr std::size_t kZaBlockSize = 32 * 30 * 408;     // 391680
  if (b->data.size() == kZaBlockSize) return LoadSc(file, Game::kZA);
  return LoadSc(file, b->data.size() <= kSwShBlockSize ? Game::kSwSh : Game::kSV);
}

std::vector<std::uint8_t> Save(const SaveData& sd) {
  // Parte SEMPRE do arquivo original: e o que faz o roundtrip byte-identico
  // valer por construcao, e nao por acaso de reserializacao.
  std::vector<std::uint8_t> out = sd.file;

  if (IsScGame(sd.game)) {
    const ScLayout L = LayoutOf(sd.game);
    auto blocks = swc::Decrypt(out);
    if (!blocks) return {};
    // Encontra o bloco pela key e sobrescreve so os slots sujos.
    for (swc::ScBlock& b : *blocks) {
      if (b.key != L.key) continue;
      for (std::size_t i = 0; i < sd.box.size(); ++i) {
        if (!sd.dirty()[i]) continue;
        std::uint8_t* dst = b.data.data() + i * L.stride;
        if (sd.box[i].present) {
          PutRecord(dst, L.record, sd.game, sd.box[i].mon);
        } else {
          // Zera so o REGISTRO: o gap do stride nao e nosso para limpar.
          std::memset(dst, 0, L.record);
        }
      }
      break;
    }
    // Patches fora das caixas (bag, spec 072): vao para o bloco da bag, que e
    // outro SCBlock. O Encrypt logo abaixo refaz o SHA256 por cima de tudo.
    if (!sd.bag_patches.empty() &&
        (sd.game == Game::kSwSh || sd.game == Game::kSV)) {
      const std::uint32_t bag_key =
          sd.game == Game::kSV ? kSvBagBlock : kSwShBagBlock;
      for (swc::ScBlock& b : *blocks) {
        if (b.key != bag_key) continue;
        for (const SaveData::RawPatch& p : sd.bag_patches) {
          if (p.offset + p.bytes.size() > b.data.size()) continue;
          std::memcpy(b.data.data() + p.offset, p.bytes.data(), p.bytes.size());
        }
        break;
      }
    }
    // Encrypt refaz o XOR e o SHA256 de integridade do arquivo inteiro.
    return swc::Encrypt(*blocks);
  }

  if (sd.game == Game::kBDSP) {
    for (std::size_t i = 0; i < sd.box.size(); ++i) {
      if (!sd.dirty()[i]) continue;
      std::uint8_t* dst = out.data() + kBdspBoxOffset + i * kBdspStride;
      if (sd.box[i].present)
        PutRecord(dst, kBdspStride, sd.game, sd.box[i].mon);
      else
        std::memset(dst, 0, kBdspStride);
    }
    // Patches da bag (spec 074). Entram ANTES do MD5 logo abaixo — que e
    // exatamente por que a bag do BDSP nao precisou de digest novo: o hash de
    // 0xE9818 cobre o arquivo inteiro e ja existe desde a spec 068.
    for (const SaveData::RawPatch& p : sd.bag_patches) {
      const std::size_t at = kBdspBagOffset + p.offset;
      if (at + p.bytes.size() > kBdspBagOffset + kBdspBagSize) continue;
      std::memcpy(out.data() + at, p.bytes.data(), p.bytes.size());
    }
    // MD5 do arquivo INTEIRO com os 16 bytes do proprio hash zerados.
    std::memset(out.data() + kBdspHashOffset, 0, 16);
    std::uint8_t digest[16];
    Md5(out.data(), out.size(), digest);
    std::memcpy(out.data() + kBdspHashOffset, digest, 16);
    return out;
  }

  // LGPE. A lista e COMPACTADA e a party a referencia por indice, entao a
  // ordem dos slots nao pode mudar: escrevemos no lugar e nada mais.
  for (std::size_t i = 0; i < sd.box.size(); ++i) {
    if (!sd.dirty()[i]) continue;
    std::uint8_t* dst = out.data() + kLgpeBoxOffset + i * kLgpeStride;
    if (sd.box[i].present)
      PutRecord(dst, kLgpeStride, sd.game, sd.box[i].mon);
    else
      std::memset(dst, 0, kLgpeStride);
  }
  // Patches da bag (spec 072). No LGPE ela esta em claro no arquivo e tem
  // CHECKSUM PROPRIO, recalculado logo abaixo — sem isso o save fica
  // corrompido de um jeito que o PkHeX NAO acusa na leitura.
  for (const SaveData::RawPatch& p : sd.bag_patches) {
    const std::size_t at = kLgpeBagOffset + p.offset;
    if (at + p.bytes.size() > kLgpeBagOffset + kLgpeBagBlockSize) continue;
    std::memcpy(out.data() + at, p.bytes.data(), p.bytes.size());
  }
  // Recalcula os 21 blocos, nao so os tres que escrevemos (spec 073). Bloco
  // intocado devolve o CRC que ja estava gravado — e por isso que o roundtrip
  // G03 continua byte-identico. Escrever num bloco novo (treinador, dex) passa
  // a ser seguro por construcao, em vez de depender de alguem lembrar de somar
  // um recalculo aqui.
  for (std::size_t id = 0; id < kLgpeBlockCount; ++id) {
    const LgpeBlock& b = kLgpeBlocks[id];
    if (b.offset + b.size > out.size()) continue;
    PutU16(out.data() + LgpeChecksumOffset(id),
           Crc16Arc(out.data() + b.offset, b.size));
  }
  return out;
}

}  // namespace savew
