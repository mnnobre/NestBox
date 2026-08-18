// Helpers de escrita compartilhados pelos cinco serializadores (spec 063,
// G01). Os parsers repetem U16/U32/U64/Utf16ToUtf8 em cada .cpp porque sao
// privados; aqui a duplicacao seria de cinco copias identicas, entao o
// inverso mora num lugar so.
//
// Nao ha .cpp: sao funcoes pequenas o bastante para inline.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pkw {

inline void W16(std::uint8_t* d, std::size_t o, std::uint16_t v) {
  d[o] = static_cast<std::uint8_t>(v);
  d[o + 1] = static_cast<std::uint8_t>(v >> 8);
}

inline void W32(std::uint8_t* d, std::size_t o, std::uint32_t v) {
  d[o] = static_cast<std::uint8_t>(v);
  d[o + 1] = static_cast<std::uint8_t>(v >> 8);
  d[o + 2] = static_cast<std::uint8_t>(v >> 16);
  d[o + 3] = static_cast<std::uint8_t>(v >> 24);
}

// Float de 4 bytes (little-endian), como PA8/PB7 guardam altura e peso
// absolutos (spec 121). memcpy e o unico jeito legal em C++ de reinterpretar
// os bits sem violar aliasing.
inline void WF32(std::uint8_t* d, std::size_t o, float v) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &v, 4);
  d[o] = static_cast<std::uint8_t>(bits);
  d[o + 1] = static_cast<std::uint8_t>(bits >> 8);
  d[o + 2] = static_cast<std::uint8_t>(bits >> 16);
  d[o + 3] = static_cast<std::uint8_t>(bits >> 24);
}

inline float RF32(const std::uint8_t* d, std::size_t o) {
  const std::uint32_t bits = static_cast<std::uint32_t>(d[o]) |
                             (static_cast<std::uint32_t>(d[o + 1]) << 8) |
                             (static_cast<std::uint32_t>(d[o + 2]) << 16) |
                             (static_cast<std::uint32_t>(d[o + 3]) << 24);
  float v = 0.0f;
  std::memcpy(&v, &bits, 4);
  return v;
}

inline void W64(std::uint8_t* d, std::size_t o, std::uint64_t v) {
  W32(d, o, static_cast<std::uint32_t>(v));
  W32(d, o + 4, static_cast<std::uint32_t>(v >> 32));
}

// Grava UTF-8 -> UTF-16LE (BMP) no campo de `max_chars` code units
// (max_chars*2 bytes), seguido do terminador — e NADA depois dele.
//
// TD-05 (revisada na execucao, ver evidence-log): a versao original zerava o
// campo inteiro. As fixtures provaram que isso DESTROI dado: o slot do
// `rillaboom.pk8` guarda `Zach\0boom\0` — "Zach" terminado, e atras o rabo de
// um apelido anterior ("Rillaboom") que o jogo nunca limpou. O parser para no
// terminador e devolve "Zach", entao zerar o resto apagaria bytes reais que o
// modelo nao representa. Pela regra da escrita conservadora (TD-01), o que o
// parser nao le nao se toca: escrevemos o nome, o terminador, e paramos.
//
// O limite de escrita e `max_chars` inteiro, nao `max_chars - 1`: um nome que
// preenche o campo todo simplesmente nao tem terminador (e o que o formato
// faz), e reservar o slot truncaria seu ultimo caractere.
inline void WriteUtf16(std::uint8_t* d, std::size_t o, const std::string& s,
                       int max_chars) {
  int out = 0;
  for (std::size_t i = 0; i < s.size() && out < max_chars;) {
    const auto b0 = static_cast<std::uint8_t>(s[i]);
    std::uint32_t cp = 0;
    std::size_t len = 1;
    if (b0 < 0x80) {
      cp = b0;
    } else if ((b0 & 0xE0) == 0xC0) {
      len = 2;
      cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
      len = 3;
      cp = b0 & 0x0F;
    } else {
      // 4 bytes (fora do BMP) ou byte invalido: o parser nunca produz isso,
      // entao pular e mais seguro do que gravar meia sequencia.
      len = ((b0 & 0xF8) == 0xF0) ? 4 : 1;
      i += len;
      continue;
    }
    if (i + len > s.size()) break;
    for (std::size_t k = 1; k < len; ++k) {
      cp = (cp << 6) | (static_cast<std::uint8_t>(s[i + k]) & 0x3F);
    }
    i += len;
    if (cp > 0xFFFF) continue;  // fora do BMP: sem par substituto no formato
    W16(d, o + out * 2, static_cast<std::uint16_t>(cp));
    ++out;
  }
  // Terminador, se couber. Alem dele nao se escreve: os bytes que sobram sao
  // do buffer original (ver o comentario da TD-05 acima).
  if (out < max_chars) W16(d, o + out * 2, 0);
}

// Empacota as flags de hyper training: o modelo guarda em ordem FISICA
// (HP,Atk,Def,Spe,SpA,SpD), o binario em ordem de EXIBICAO. Inverso exato do
// mapa que os cinco parsers aplicam na leitura.
inline std::uint8_t PackHyperTrain(const bool (&physical)[6]) {
  static constexpr int kHtBitForPhysical[6] = {0, 1, 2, 5, 3, 4};
  std::uint8_t v = 0;
  for (int i = 0; i < 6; ++i) {
    if (physical[i]) v |= static_cast<std::uint8_t>(1u << kHtBitForPhysical[i]);
  }
  return v;
}

// Empacota IVs (6 x 5 bits = bits 0..29) + egg (30) + nicknamed (31). Os 32
// bits sao todos parseados, entao nada precisa vir do buffer original — este
// e o unico campo empacotado do formato sem bit obscuro.
inline std::uint32_t PackIvs(const std::uint8_t (&ivs)[6], bool is_egg,
                             bool is_nicknamed) {
  std::uint32_t v = 0;
  for (int i = 0; i < 6; ++i) {
    v |= static_cast<std::uint32_t>(ivs[i] & 0x1F) << (5 * i);
  }
  if (is_egg) v |= 0x4000'0000u;
  if (is_nicknamed) v |= 0x8000'0000u;
  return v;
}

}  // namespace pkw
