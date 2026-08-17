// Gravacao atomica do NestBox por dupla de arquivos (spec 091).
//
// O problema: `fopen(..., "wb")` TRUNCA o arquivo antes do primeiro byte novo
// chegar. Uma queda de energia no meio do fwrite deixa o usuario com um banco
// pela metade — e, sendo arquivo unico, sem nenhuma copia boa. Com 400 caixas
// (spec 090) o payload passa de 4 MB, o que alarga essa janela.
//
// A solucao, decidida pelo dono: DUAS copias alternadas (bank.a / bank.b). O
// commit escreve sempre na INATIVA — a de geracao menor ou invalida. A ativa
// fica intacta o tempo todo. Se a energia cair no meio da escrita, a copia boa
// nunca foi tocada.
//
// Nao usa rename(), de proposito. O projeto ja pagou por essa licao: o
// comentario em src/ui/updater.cpp:358-364 registra que a arquitetura do
// Sphaira evita rename por causa da janela entre o delete e o rename, e que
// stdio sozinho nao basta no Switch — sem fsFsCommit a escrita pode nao chegar
// ao cartao.
//
// Envelope de cada arquivo (TD-01 da spec 091):
//
//    0  magic   "NBAB" (4 bytes)
//    4  geracao u64 LE — quem tem a maior E valida vence
//   12  crc32   u32 LE — do PAYLOAD, nao do cabecalho
//   16  payload = exatamente os bytes de nest::Encode()
//
// O cabecalho e EXTERNO ao payload de proposito: o formato v5 da spec 090 nao
// muda por causa desta spec. nest::Decode recebe o payload como sempre recebeu.
//
// Como o resto do core, isto nao depende de plataforma: recebe bytes, devolve
// bytes. O I/O de verdade (fopen/fwrite/fsFsCommit) fica na UI.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pokehome::nest::ab {

inline constexpr std::size_t kEnvelopeHeader = 16;
inline constexpr std::uint8_t kMagic[4] = {'N', 'B', 'A', 'B'};

// CRC32 (IEEE 802.3, o mesmo do zlib/PNG), sem tabela pre-computada: a tabela
// e montada na primeira chamada. Pega queda de energia, escrita parcial e bloco
// corrompido — que sao os modos de falha reais. NAO pega adulteracao
// proposital, e nao e o objetivo (TD-02 da spec 091).
inline std::uint32_t Crc32(const std::uint8_t* data, std::size_t n) {
  static std::uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      table[i] = c;
    }
    ready = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < n; ++i) {
    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

// O que se sabe de um dos dois arquivos depois de olhar para ele.
struct Slot {
  bool valid = false;          // magic ok, tamanho ok e CRC confere
  std::uint64_t generation = 0;
  std::vector<std::uint8_t> payload;  // os bytes do nest::Encode(), se valido
};

// Embrulha o payload v5 num envelope com geracao e CRC.
inline std::vector<std::uint8_t> Wrap(const std::vector<std::uint8_t>& payload,
                                      std::uint64_t generation) {
  std::vector<std::uint8_t> out;
  out.reserve(kEnvelopeHeader + payload.size());
  out.insert(out.end(), kMagic, kMagic + 4);
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((generation >> (i * 8)) & 0xFF));
  }
  const std::uint32_t crc = Crc32(payload.data(), payload.size());
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>((crc >> (i * 8)) & 0xFF));
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

// Abre um dos arquivos. Arquivo ausente, curto, de magic errado ou com CRC que
// nao bate volta `valid == false` — nunca payload interpretado errado. Um
// arquivo truncado no meio da escrita cai exatamente aqui: ou o CRC nao fecha,
// ou nem os 16 bytes de cabecalho chegaram.
inline Slot Unwrap(const std::vector<std::uint8_t>& bytes) {
  Slot s;
  if (bytes.size() < kEnvelopeHeader) return s;
  for (int i = 0; i < 4; ++i) {
    if (bytes[i] != kMagic[i]) return s;
  }
  std::uint64_t gen = 0;
  for (int i = 0; i < 8; ++i) {
    gen |= static_cast<std::uint64_t>(bytes[4 + i]) << (i * 8);
  }
  std::uint32_t crc = 0;
  for (int i = 0; i < 4; ++i) {
    crc |= static_cast<std::uint32_t>(bytes[12 + i]) << (i * 8);
  }
  const std::size_t n = bytes.size() - kEnvelopeHeader;
  if (Crc32(bytes.data() + kEnvelopeHeader, n) != crc) return s;

  s.valid = true;
  s.generation = gen;
  s.payload.assign(bytes.begin() + kEnvelopeHeader, bytes.end());
  return s;
}

// Qual arquivo vale para LER: o de maior geracao entre os validos.
//
//   os dois validos -> o de maior geracao
//   so um valido    -> esse (e o caso da queda de energia)
//   nenhum valido   -> false, e a UI comeca um banco novo vazio
//
// Devolve true e escreve em `out` quando ha um bom.
inline bool PickSource(const Slot& a, const Slot& b, const Slot** out) {
  if (a.valid && b.valid) {
    *out = (b.generation > a.generation) ? &b : &a;
    return true;
  }
  if (a.valid) { *out = &a; return true; }
  if (b.valid) { *out = &b; return true; }
  return false;
}

// Onde o proximo commit deve ESCREVER, e com que geracao.
//
// Sempre a copia inativa — a de geracao menor, ou a invalida. Nunca por cima da
// boa: e isso que garante que uma queda de energia no meio do commit deixe a
// anterior intacta.
//
// `write_b` sai true quando o destino e o arquivo B. A geracao nova e sempre
// maior que qualquer uma valida existente, para a proxima leitura escolher esta.
struct Target {
  bool write_b = false;
  std::uint64_t generation = 1;
};

inline Target PickTarget(const Slot& a, const Slot& b) {
  Target t;
  if (a.valid && b.valid) {
    // Escreve por cima da mais velha.
    t.write_b = (b.generation <= a.generation);
    t.generation = (a.generation > b.generation ? a.generation : b.generation) + 1;
    return t;
  }
  if (a.valid) {           // B invalido/ausente: escreve em B
    t.write_b = true;
    t.generation = a.generation + 1;
    return t;
  }
  if (b.valid) {           // A invalido/ausente: escreve em A
    t.write_b = false;
    t.generation = b.generation + 1;
    return t;
  }
  // Nenhum valido: comeca em A, geracao 1. E tambem o caso da instalacao que
  // so tinha o .nestbox legado (TD-03 da spec 091).
  t.write_b = false;
  t.generation = 1;
  return t;
}

}  // namespace pokehome::nest::ab
