// Testes do SwishCrypto (spec 055) contra os SAVES REAIS do simulador.
//
// So leitura: o guardrail do projeto proibe escrever em save real. O
// roundtrip acontece inteiro em memoria.
//
// Provas por save: 1) hash de integridade valido — SHA256 + intro/outro +
// XOR pad certos, senao nada bate; 2) o arquivo inteiro parseia em blocos
// ate o ultimo byte; 3) Encrypt(Decrypt(x)) == x byte a byte; 4) violacao
// plantada: flip de um byte derruba o hash.
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "sha256.h"
#include "swish_crypto.h"

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  }
}

static std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

// Vetor conhecido do FIPS: SHA256("abc").
static void TestSha256() {
  static const std::uint8_t kAbc[3] = {'a', 'b', 'c'};
  static const std::uint8_t kExpected[32] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
      0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
      0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
  };
  const sha256::Digest d = sha256::Hash(kAbc, 3);
  bool ok = true;
  for (int i = 0; i < 32; ++i) ok = ok && d[i] == kExpected[i];
  Check(ok, "SHA256(\"abc\") bate com o vetor do FIPS");
}

static void TestSave(const std::string& name, const std::string& rel) {
  const std::string path = std::string(SIM_SAVES) + rel;
  const std::vector<std::uint8_t> file = ReadFile(path);
  if (file.empty()) {
    std::printf("FALHOU: save ausente: %s\n", path.c_str());
    ++g_failures;
    return;
  }

  Check(swc::HashIsValid(file), name + ": hash de integridade valido");

  const auto blocks = swc::Decrypt(file);
  Check(blocks.has_value(), name + ": decifra em blocos ate o fim");
  if (!blocks) return;
  Check(blocks->size() > 100, name + ": quantidade plausivel de blocos (" +
                                  std::to_string(blocks->size()) + ")");

  const std::vector<std::uint8_t> again = swc::Encrypt(*blocks);
  Check(again == file, name + ": Encrypt(Decrypt(x)) == x byte a byte");

  // Violacao plantada: a protecao precisa acusar o vermelho.
  std::vector<std::uint8_t> tampered = file;
  tampered[tampered.size() / 2] ^= 0xFF;
  Check(!swc::HashIsValid(tampered),
        name + ": um byte flipado derruba o hash");
}

int main() {
  TestSha256();

  TestSave("Shield", "01008DB008C2C000/Amaral/main");
  TestSave("Sword", "0100ABF008968000/Amaral/main");
  TestSave("Scarlet", "0100A3D008C5C000/Amaral/main");
  TestSave("Legends Arceus", "01001F5010DFA000/Amaral/main");

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("swish_crypto: tudo verde\n");
  return 0;
}
