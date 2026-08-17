// Cifra de PARTY dos quatro formatos de bloco grande (spec 066).
//
// A lacuna que este teste fecha (registrada em 054 e 062):
//
// A spec 062 descobriu que a cifra tem DUAS passadas independentes — blocos
// e party, cada uma resemeada com o EC — e que `CryptBody` estava errado nos
// CINCO formatos. A correcao ficou provada so no PB7. Para PK8/PK9/PA8/PB8
// as fixtures existentes sao de tamanho party (344/376), mas estao gravadas
// **DECIFRADAS** (`DecryptedPartyData`): o parser cai no `IsDecrypted()` e
// **nunca chama `pkc::Decrypt`**. Nenhuma das duas passadas roda.
//
// As fixtures daqui (`party-*.pk8` etc, prefixo `party-`) saem dos saves
// REAIS do dono e sao `EncryptedPartyData` — cifradas de verdade. Cada uma
// vem com um `.dec` ao lado: os MESMOS bytes decifrados pelo PkHeX.
//
// O `.dec` e o que faz o teste valer. Comparar a nossa `Decrypt` com a nossa
// `Encrypt` nao prova nada — um erro simetrico nas duas passadas se cancela.
// Comparar contra o PkHeX e um oraculo externo.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pa8.h"
#include "pb8.h"
#include "pk8.h"
#include "pk9.h"
#include "pkm_crypto.h"

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  }
}

static std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
  // filesystem::path, nao string: no Windows o ifstream(string) usa a
  // codepage ANSI e nao acha os nomes com acento (Ethernatos).
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

// Onde as duas passadas se encontram: fim dos 4 blocos embaralhados.
static std::size_t BodyEnd(std::size_t block) { return 8 + 4 * block; }

// Uma fixture: prova a cifra nos dois sentidos contra o oraculo do PkHeX.
static void TestFixture(const std::filesystem::path& enc_path,
                        std::size_t block, std::size_t party_size,
                        bool (*parses)(const std::vector<std::uint8_t>&)) {
  const std::string t = enc_path.stem().string() + ": ";

  std::filesystem::path dec_path = enc_path;
  dec_path.replace_extension(".dec");
  const auto enc = ReadFile(enc_path);
  const auto dec = ReadFile(dec_path);
  if (enc.empty() || dec.empty()) {
    std::printf("FALHOU: %sfixture .%s/.dec ausente\n", t.c_str(),
                enc_path.extension().string().c_str());
    ++g_failures;
    return;
  }

  Check(enc.size() == party_size,
        t + "tamanho de party (" + std::to_string(enc.size()) + ")");
  Check(dec.size() == enc.size(), t + ".dec tem o mesmo tamanho");
  if (enc.size() != party_size || dec.size() != enc.size()) return;

  // A fixture precisa estar mesmo CIFRADA — senao o teste nao exercita nada.
  Check(!pkc::IsDecrypted(enc.data(), block),
        t + "fixture esta cifrada (se falhar, o teste nao prova a cifra)");
  Check(pkc::IsDecrypted(dec.data(), block), t + ".dec esta decifrado");

  // 1. Decrypt do buffer cifrado reproduz o do PkHeX, BYTE A BYTE, no buffer
  //    inteiro — inclusive a regiao de party, depois de 8 + 4*bloco.
  std::vector<std::uint8_t> ours = enc;
  pkc::Decrypt(ours.data(), ours.size(), block);
  Check(ours == dec, t + "Decrypt bate com o PkHeX byte a byte");

  // 2. O assert que a pendencia pedia: a regiao DEPOIS de 8+4*bloco (a
  //    segunda passada) tem de bater. Comparada a parte para que a falha
  //    aponte a passada certa, em vez de "os buffers diferem".
  const std::size_t body_end = BodyEnd(block);
  Check(body_end < enc.size(),
        t + "ha regiao de party depois de " + std::to_string(body_end));
  const bool party_ok =
      std::equal(ours.begin() + body_end, ours.end(), dec.begin() + body_end);
  Check(party_ok, t + "regiao de PARTY (>= " + std::to_string(body_end) +
                      ") bate com o PkHeX");

  // 3. A segunda passada faz alguma coisa: se a party do cifrado ja fosse
  //    igual a do decifrado, o assert acima passaria com uma Decrypt que
  //    ignora a regiao. Esta e a violacao plantada.
  const bool party_was_scrambled =
      !std::equal(enc.begin() + body_end, enc.end(), dec.begin() + body_end);
  Check(party_was_scrambled,
        t + "a party estava de fato cifrada (senao o teste e vacuo)");

  // 4. Roundtrip: Encrypt do decifrado reproduz o arquivo original.
  std::vector<std::uint8_t> back = dec;
  pkc::Encrypt(back.data(), back.size(), block);
  Check(back == enc, t + "Encrypt(dec) reproduz o cifrado byte a byte");

  // 5. O parser aceita o buffer CIFRADO — o caminho que o save real usa.
  Check(parses(enc), t + "parser le o buffer cifrado");
}

// O parser de cada formato, reduzido ao que este teste precisa saber.
static bool ParsesPk8(const std::vector<std::uint8_t>& b) {
  return pk8::Parse(b).has_value();
}
static bool ParsesPk9(const std::vector<std::uint8_t>& b) {
  return pk9::Parse(b).has_value();
}
static bool ParsesPa8(const std::vector<std::uint8_t>& b) {
  return pa8::Parse(b).has_value();
}
static bool ParsesPb8(const std::vector<std::uint8_t>& b) {
  return pb8::Parse(b).has_value();
}

int main() {
  struct Formato {
    const char* ext;
    std::size_t block;
    std::size_t party_size;
    bool (*parses)(const std::vector<std::uint8_t>&);
  };
  const Formato formatos[] = {
      {"pk8", pkc::kBlockPK8, 344, ParsesPk8},
      {"pk9", pkc::kBlockPK8, 344, ParsesPk9},
      {"pa8", pkc::kBlockPA8, pa8::kPartySize, ParsesPa8},
      {"pb8", pkc::kBlockPK8, 344, ParsesPb8},
  };

  int total = 0;
  for (const auto& f : formatos) {
    const std::filesystem::path dir(std::string(PKM_FIXTURES) + f.ext);
    int n = 0;
    // Varredura do diretorio, nao lista de nomes (os nomes tem acento).
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      const auto& p = entry.path();
      // So as fixtures de party CIFRADAS desta spec.
      if (p.extension() != std::string(".") + f.ext) continue;
      if (p.filename().string().rfind("party-", 0) != 0) continue;
      TestFixture(p, f.block, f.party_size, f.parses);
      ++n;
    }
    std::printf("%s: %d fixtures de party\n", f.ext, n);
    // Cada formato precisa ter pelo menos uma — senao a lacuna continua
    // aberta e o teste tem de dizer isso, nao passar em silencio.
    Check(n > 0, std::string(f.ext) + ": tem fixture de party cifrada");
    total += n;
  }

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("party: %d fixtures nos 4 formatos, tudo verde\n", total);
  return 0;
}
