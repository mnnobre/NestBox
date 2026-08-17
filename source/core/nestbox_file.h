// Formato de arquivo do NestBox (spec 027; multi-geracao na spec 090).
//
// Guarda os bytes CRUS de cada Pokemon, exatamente como estao no save. Nao
// serializa o struct parseado de proposito: ParseBoxPokemon le a word
// `origins` e guarda so o met_level, descartando Poke Ball, jogo de origem e
// idioma — e ribbons nunca sao lidas. A §7 da pesquisa exige que esses campos
// sobrevivam ao armazenamento, entao o unico jeito honesto e nao decodificar.
// Com cinco formatos modernos no core (pk8, pk9, pa8, pb8, pb7), a regra vale
// ainda mais: cada parser le uma fracao dos campos do seu formato.
//
// Layout:
//   0  magic      "NSTB" (4 bytes)
//   4  versao     u16, little-endian
//   6  caixas     u16
//   8  slots      u16   (por caixa)
//  10  slot_bytes u16   (v5; zero em v1..v4, que sempre usaram 80)
//  12  dados      caixas * slots * slot_bytes
//
// Cada slot da v5 tem kSlotBytes (384) bytes:
//   0  formato    u8   (SlotFormat; 0 = vazio)
//   1  reservado  u8   (zero)
//   2  tamanho    u16  little-endian, bytes reais do payload
//   4  payload    kSlotPayload (380) bytes CRUS do save de origem
//
// O Pokemon e guardado no formato em que estava no save de origem (D2 da spec
// 090). A conversao acontece so na saida, ao depositar num save — transfer.cpp
// ja faz isso. Nada e convertido na entrada.
//
// Slot vazio tem formato == kEmpty e payload zerado — o mesmo que o save usa
// (species == 0).
//
// Como o resto do core, isto nao depende de plataforma: recebe bytes, devolve
// bytes. O I/O de verdade fica na UI.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "moveset_memory.h"

namespace pokehome::nest {

// Slot fixo de 384 bytes: 4 de cabecalho + 380 de payload (D1 da spec 090).
// O maior formato conhecido e o pa8, com 376 bytes, entao cabe com folga.
// Fixo, e nao variavel, para At(box, slot) continuar sendo aritmetica de
// ponteiro pura — sem indice, sem varredura.
inline constexpr std::size_t kSlotHeaderBytes = 4;
inline constexpr std::size_t kSlotPayload = 380;
inline constexpr std::size_t kSlotBytes = kSlotHeaderBytes + kSlotPayload;

// Tamanho do slot nas versoes v1..v4: so gen3, sem cabecalho de formato.
inline constexpr std::size_t kLegacySlotBytes = 80;

inline constexpr std::size_t kHeaderBytes = 12;
inline constexpr std::uint8_t kMagic[4] = {'N', 'S', 'T', 'B'};

// Formato do que esta no slot. O valor e gravado em arquivo: nunca renumere,
// so acrescente no fim.
enum SlotFormat : std::uint8_t {
  kEmpty = 0,
  kGen3 = 1,   // 80 bytes
  kPk8 = 2,    // 344
  kPk9 = 3,    // 344
  kPa8 = 4,    // 376
  kPb8 = 5,    // 344
  kPb7 = 6,    // 260
};

// Versao do formato.
//
//   v1 — cabecalho + slots.
//   v2 — acrescenta, depois dos slots, o bitmap da dex global (spec 029).
//   v3 — acrescenta, depois da dex, os nomes das caixas (spec 030).
//   v4 — acrescenta, depois dos nomes, a memoria de moveset por (tracker,
//        jogo) — G11 da descoberta escrita-e-transferencia (spec 071).
//   v5 — slot de 384 bytes com cabecalho de formato, dex de 1025 bits e
//        slot_bytes no cabecalho do arquivo (spec 090).
//
// A v4 e a primeira secao de tamanho VARIAVEL, por isso entra por ultimo e
// carrega o proprio contador: nenhuma secao anterior muda de offset, e um
// arquivo v1..v3 continua sendo lido pelo mesmo caminho de sempre. A v5 mantem
// essa regra — os movesets seguem sendo a ultima secao.
//
// Versoes ANTERIORES sao lidas: um arquivo antigo e formato conhecido e
// completo, nao lixo, e recusa-lo apagaria o banco de quem atualizasse o app.
// As secoes que faltam entram vazias, e o slot de 80 bytes vira um slot v5 de
// formato kGen3 com os mesmos bytes. A recusa vale so para versao ACIMA da
// conhecida — essa vem de um app mais novo, e adivinhar o layout seria o erro
// (TD-01 da spec 029, TD-02 da spec 030).
inline constexpr std::uint16_t kVersion = 5;

// Dex global: um bit por especie ate a gen 9 (1..1025). O indice 0 nao e usado.
// Eram 386 (so gen 3) ate a v4; os bits novos entram zerados na migracao (D3 da
// spec 090).
inline constexpr std::size_t kDexBits = 1025;
inline constexpr std::size_t kDexBytes = (kDexBits + 8) / 8;  // 129

// Dex das versoes v1..v4: 386 bits em 49 bytes.
inline constexpr std::size_t kLegacyDexBits = 386;
inline constexpr std::size_t kLegacyDexBytes = (kLegacyDexBits + 8) / 8;  // 49

// Nome de caixa: bloco fixo, texto UTF-8 terminado em zero. Tamanho fixo para
// o offset continuar calculavel como as outras secoes (TD-01 da spec 030).
// O ultimo byte e sempre zero, entao cabem 23 bytes de texto.
inline constexpr std::size_t kBoxNameBytes = 24;

// Bytes reais de cada formato. Zero para formato desconhecido.
inline constexpr std::size_t FormatBytes(std::uint8_t fmt) {
  switch (fmt) {
    case kGen3: return 80;
    case kPk8: return 344;
    case kPk9: return 344;
    case kPa8: return 376;
    case kPb8: return 344;
    case kPb7: return 260;
    default: return 0;
  }
}

// --- Acesso ao slot --------------------------------------------------------
//
// Funcoes livres sobre o ponteiro de 384 bytes que At() devolve. Ficam fora de
// NestData porque quem mexe no slot ja tem o ponteiro na mao.

inline std::uint8_t SlotFormatOf(const std::uint8_t* slot) {
  return slot ? slot[0] : static_cast<std::uint8_t>(kEmpty);
}

// Tamanho gravado, saturado no que cabe no payload. Um arquivo adulterado com
// tamanho maior que o slot leria alem do bloco — daí o clamp.
inline std::size_t SlotSize(const std::uint8_t* slot) {
  if (!slot) return 0;
  const std::size_t n =
      static_cast<std::size_t>(slot[2]) | (static_cast<std::size_t>(slot[3]) << 8);
  return std::min(n, kSlotPayload);
}

inline const std::uint8_t* SlotPayload(const std::uint8_t* slot) {
  return slot ? slot + kSlotHeaderBytes : nullptr;
}

inline std::uint8_t* SlotPayload(std::uint8_t* slot) {
  return slot ? slot + kSlotHeaderBytes : nullptr;
}

// Slot vazio: formato kEmpty. Espelha BoxPokemon::empty() (species == 0).
inline bool SlotEmpty(const std::uint8_t* slot) {
  return !slot || slot[0] == kEmpty;
}

// Grava um Pokemon no slot. `n` maior que o payload e recusado — melhor nao
// gravar que gravar truncado, que viraria Pokemon corrompido no save de
// destino. Zera o resto do payload para nao deixar cauda do ocupante anterior.
inline bool SlotWrite(std::uint8_t* slot, std::uint8_t fmt,
                      const std::uint8_t* src, std::size_t n) {
  if (!slot || n > kSlotPayload) return false;
  slot[0] = fmt;
  slot[1] = 0;
  slot[2] = static_cast<std::uint8_t>(n & 0xFF);
  slot[3] = static_cast<std::uint8_t>(n >> 8);
  if (src && n) std::memcpy(slot + kSlotHeaderBytes, src, n);
  std::memset(slot + kSlotHeaderBytes + n, 0, kSlotPayload - n);
  return true;
}

inline void SlotClear(std::uint8_t* slot) {
  if (slot) std::memset(slot, 0, kSlotBytes);
}

// Conteudo do banco: um vetor plano de caixas * slots, cada um com kSlotBytes.
struct NestData {
  std::uint16_t boxes = 0;
  std::uint16_t slots = 0;
  std::vector<std::uint8_t> raw;  // boxes * slots * kSlotBytes

  // Dex global: especies que ja passaram pelo banco, mesmo que tenham saido
  // (spec 029). Sempre kDexBytes; um arquivo v1 entra com tudo zerado.
  std::uint8_t dex[kDexBytes] = {};

  // Nomes das caixas (spec 030). Um bloco de kBoxNameBytes por caixa; vazio
  // significa "sem nome", e a UI mostra o default. Arquivo v1/v2 entra vazio.
  std::vector<std::uint8_t> names;

  // Memoria de moveset por (tracker, jogo) — G11, spec 071. Arquivo v1..v3
  // entra vazia: um banco antigo simplesmente nunca memorizou nada, e o
  // proximo deposito comeca a preencher.
  moveset::Memory movesets;

  bool valid() const {
    return raw.size() == static_cast<std::size_t>(boxes) * slots * kSlotBytes;
  }

  // Marca uma especie como ja vista. Fora da faixa e ignorado — especie
  // desconhecida nao deve corromper o bitmap.
  void MarkSeen(int species) {
    if (species <= 0 || static_cast<std::size_t>(species) > kDexBits) return;
    dex[species / 8] |= static_cast<std::uint8_t>(1u << (species % 8));
  }

  bool Seen(int species) const {
    if (species <= 0 || static_cast<std::size_t>(species) > kDexBits) return false;
    return (dex[species / 8] & (1u << (species % 8))) != 0;
  }

  std::size_t SeenCount() const {
    std::size_t n = 0;
    for (std::size_t s = 1; s <= kDexBits; ++s) {
      if (Seen(static_cast<int>(s))) ++n;
    }
    return n;
  }

  // --- Nomes das caixas (spec 030) -----------------------------------------

  // Nome da caixa, ou vazio se nao houver. A leitura NAO confia no terminador
  // estar la: para no primeiro zero ou no fim do bloco, o que vier antes.
  // Um arquivo adulterado sem terminador leria alem do bloco.
  std::string BoxName(std::size_t box) const {
    if (box >= boxes) return "";
    const std::size_t off = box * kBoxNameBytes;
    if (off + kBoxNameBytes > names.size()) return "";
    std::string out;
    for (std::size_t i = 0; i < kBoxNameBytes; ++i) {
      const std::uint8_t c = names[off + i];
      if (c == 0) break;
      out.push_back(static_cast<char>(c));
    }
    return out;
  }

  // Grava o nome, truncando no limite. O bloco e sempre zerado antes, entao um
  // nome mais curto nao deixa cauda do anterior.
  void SetBoxName(std::size_t box, const std::string& name) {
    if (box >= boxes) return;
    if (names.size() < static_cast<std::size_t>(boxes) * kBoxNameBytes) {
      names.assign(static_cast<std::size_t>(boxes) * kBoxNameBytes, 0);
    }
    const std::size_t off = box * kBoxNameBytes;
    for (std::size_t i = 0; i < kBoxNameBytes; ++i) names[off + i] = 0;
    // kBoxNameBytes - 1: o ultimo byte fica sempre zero, garantindo o
    // terminador mesmo com nome no limite.
    std::size_t n = std::min(name.size(), kBoxNameBytes - 1);
    // Nao corta no meio de um caractere UTF-8: um multibyte partido viraria
    // sequencia invalida na tela. Recua enquanto o byte for continuacao
    // (10xxxxxx), o que so acontece se o corte caiu dentro de um caractere.
    while (n > 0 && n < name.size() &&
           (static_cast<std::uint8_t>(name[n]) & 0xC0) == 0x80) {
      --n;
    }
    for (std::size_t i = 0; i < n; ++i) {
      names[off + i] = static_cast<std::uint8_t>(name[i]);
    }
  }

  // Ponteiro para os kSlotBytes de um slot, ou nullptr fora da faixa.
  // Aritmetica pura, sem indice (D1 da spec 090).
  const std::uint8_t* At(std::size_t box, std::size_t slot) const {
    if (box >= boxes || slot >= slots) return nullptr;
    return raw.data() + (box * slots + slot) * kSlotBytes;
  }

  std::uint8_t* At(std::size_t box, std::size_t slot) {
    if (box >= boxes || slot >= slots) return nullptr;
    return raw.data() + (box * slots + slot) * kSlotBytes;
  }
};

inline NestData MakeEmpty(std::uint16_t boxes, std::uint16_t slots) {
  NestData d;
  d.boxes = boxes;
  d.slots = slots;
  d.raw.assign(static_cast<std::size_t>(boxes) * slots * kSlotBytes, 0);
  d.names.assign(static_cast<std::size_t>(boxes) * kBoxNameBytes, 0);
  return d;
}

// Serializa. O resultado e o conteudo exato do arquivo. Sempre v5.
inline std::vector<std::uint8_t> Encode(const NestData& d) {
  std::vector<std::uint8_t> out;
  if (!d.valid()) return out;

  const std::size_t names_len =
      static_cast<std::size_t>(d.boxes) * kBoxNameBytes;
  out.reserve(kHeaderBytes + d.raw.size() + kDexBytes + names_len);
  out.insert(out.end(), kMagic, kMagic + 4);
  out.push_back(static_cast<std::uint8_t>(kVersion & 0xFF));
  out.push_back(static_cast<std::uint8_t>(kVersion >> 8));
  out.push_back(static_cast<std::uint8_t>(d.boxes & 0xFF));
  out.push_back(static_cast<std::uint8_t>(d.boxes >> 8));
  out.push_back(static_cast<std::uint8_t>(d.slots & 0xFF));
  out.push_back(static_cast<std::uint8_t>(d.slots >> 8));
  // slot_bytes, no lugar da antiga reserva (TD-01 da spec 090). Zero era o que
  // as v1..v4 gravavam, e zero nao e tamanho valido — nao ha ambiguidade.
  out.push_back(static_cast<std::uint8_t>(kSlotBytes & 0xFF));
  out.push_back(static_cast<std::uint8_t>(kSlotBytes >> 8));
  out.insert(out.end(), d.raw.begin(), d.raw.end());
  // Seçao da dex global, depois dos slots (v2; 1025 bits desde a v5).
  out.insert(out.end(), d.dex, d.dex + kDexBytes);
  // Seçao dos nomes, depois da dex (v3). Sempre do tamanho cheio, mesmo que o
  // banco nunca tenha recebido nome — o offset precisa ser calculavel.
  out.insert(out.end(), d.names.begin(),
             d.names.begin() + std::min(names_len, d.names.size()));
  out.resize(kHeaderBytes + d.raw.size() + kDexBytes + names_len, 0);
  // Memoria de moveset, por ultimo (v4). Tamanho variavel, com o proprio
  // contador — por isso nao desloca nenhuma secao anterior. Continua sendo a
  // ultima secao na v5.
  const std::vector<std::uint8_t> mem = moveset::Encode(d.movesets);
  out.insert(out.end(), mem.begin(), mem.end());
  return out;
}

// Le. Devolve NestData com boxes == 0 quando o arquivo nao serve — arquivo
// ausente, corrompido ou de versao desconhecida sao todos "banco vazio", nunca
// dado interpretado errado.
//
// Arquivo v1..v4 e migrado na leitura: cada slot de 80 bytes vira um slot v5 de
// formato kGen3 com os mesmos 80 bytes de payload, e a dex de 49 bytes entra
// nos 49 primeiros dos 129, com os bits novos zerados. O banco de quem ja usa o
// app sobrevive inteiro — e o ponto da migracao.
inline NestData Decode(const std::vector<std::uint8_t>& bytes) {
  NestData d;
  if (bytes.size() < kHeaderBytes) return d;

  for (int i = 0; i < 4; ++i) {
    if (bytes[i] != kMagic[i]) return d;
  }

  const auto u16 = [&bytes](std::size_t off) -> std::uint16_t {
    return static_cast<std::uint16_t>(bytes[off] | (bytes[off + 1] << 8));
  };

  // Versao ACIMA da conhecida vem de um app mais novo: recusa, nao adivinha.
  // Versoes anteriores sao lidas (TD-01 da spec 029).
  const std::uint16_t version = u16(4);
  if (version == 0 || version > kVersion) return d;

  const std::uint16_t boxes = u16(6);
  const std::uint16_t slots = u16(8);

  // Tamanho do slot NO ARQUIVO. A v5 grava; v1..v4 gravavam zero na reserva, e
  // sempre usaram 80.
  const std::size_t file_slot =
      version >= 5 ? static_cast<std::size_t>(u16(10)) : kLegacySlotBytes;
  // Um arquivo v5 que diga outro tamanho de slot nao e legivel por este codigo:
  // o layout dele nao e o nosso. Recusa em vez de ler torto.
  if (file_slot != kSlotBytes && file_slot != kLegacySlotBytes) return d;

  const std::size_t file_dex =
      version >= 5 ? kDexBytes : kLegacyDexBytes;

  const std::size_t need =
      static_cast<std::size_t>(boxes) * slots * file_slot;

  // Truncado: recusa em vez de ler alem do buffer.
  if (bytes.size() < kHeaderBytes + need) return d;

  d.boxes = boxes;
  d.slots = slots;
  d.raw.assign(static_cast<std::size_t>(boxes) * slots * kSlotBytes, 0);

  if (file_slot == kSlotBytes) {
    std::copy(bytes.begin() + kHeaderBytes, bytes.begin() + kHeaderBytes + need,
              d.raw.begin());
  } else {
    // Migracao v1..v4: 80 bytes crus viram payload de um slot kGen3. Slot que
    // era todo zero continua vazio — la o species e zero, entao nao ha Pokemon.
    const std::size_t count = static_cast<std::size_t>(boxes) * slots;
    for (std::size_t i = 0; i < count; ++i) {
      const std::uint8_t* src = bytes.data() + kHeaderBytes + i * kLegacySlotBytes;
      bool empty = true;
      for (std::size_t k = 0; k < kLegacySlotBytes; ++k) {
        if (src[k] != 0) { empty = false; break; }
      }
      if (empty) continue;
      SlotWrite(d.raw.data() + i * kSlotBytes, kGen3, src, kLegacySlotBytes);
    }
  }

  // A dex global so existe da v2 em diante. Arquivo v1 entra com ela zerada —
  // quem carrega semeia com o conteudo do banco (spec 029).
  if (version >= 2) {
    const std::size_t off = kHeaderBytes + need;
    // Truncado na secao da dex: recusa o arquivo inteiro. Ler dex pela metade
    // daria historico errado, que e pior que banco vazio.
    if (bytes.size() < off + file_dex) return NestData{};
    // v1..v4: os 49 bytes antigos entram nos 49 primeiros dos 129. Como o
    // bitmap e indexado por numero de especie e a numeracao nao mudou, os bits
    // caem no lugar certo; o resto fica zerado (D3 da spec 090).
    for (std::size_t i = 0; i < file_dex; ++i) {
      d.dex[i] = bytes[off + i];
    }
  }

  // Nomes das caixas so existem da v3 em diante (spec 030).
  d.names.assign(static_cast<std::size_t>(boxes) * kBoxNameBytes, 0);
  const std::size_t names_len =
      static_cast<std::size_t>(boxes) * kBoxNameBytes;
  if (version >= 3) {
    const std::size_t off = kHeaderBytes + need + file_dex;
    if (bytes.size() < off + names_len) return NestData{};
    for (std::size_t i = 0; i < names_len; ++i) {
      d.names[i] = bytes[off + i];
    }
  }

  // Memoria de moveset so existe da v4 em diante (spec 071). Arquivo v1..v3
  // entra com ela vazia — e o mesmo tratamento que a dex e os nomes ganharam.
  if (version >= 4) {
    const std::size_t off = kHeaderBytes + need + file_dex + names_len;
    // Truncada: recusa o arquivo. Moveset restaurado pela metade escreveria
    // golpe errado num save, que e pior que memoria vazia.
    if (!moveset::Decode(bytes, off, &d.movesets)) return NestData{};
  }
  return d;
}

}  // namespace pokehome::nest
