// Testes da cifra de Pokemon moderno (spec 054).
//
// As fixtures sao arquivos DECIFRADOS gerados pelo PkHeX (via OpenHome).
// Provas: 1) o checksum gravado bate com o calculado — se o algoritmo de
// checksum ou os offsets estivessem errados, nenhuma fixture passaria;
// 2) Encrypt e Decrypt sao inversos exatos byte a byte; 3) Encrypt muda os
// bytes de verdade (planta a violacao: uma cifra identidade passaria em 2).
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "pkm_crypto.h"

static int g_failures = 0;

static void Check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what);
    ++g_failures;
  }
}

static std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

// Roda as tres provas numa fixture. `block` conforme o formato.
static void TestFixture(const std::string& rel, std::size_t block) {
  const std::string path = std::string(PKM_FIXTURES) + rel;
  std::vector<std::uint8_t> plain = ReadFile(path);
  if (plain.empty()) {
    std::printf("FALHOU: fixture ausente: %s\n", path.c_str());
    ++g_failures;
    return;
  }

  // 1) checksum da fixture decifrada bate com o gravado em 0x06.
  Check(pkc::IsDecrypted(plain.data(), block),
        (rel + ": checksum da fixture bate").c_str());

  // 2) roundtrip exato.
  std::vector<std::uint8_t> buf = plain;
  pkc::Encrypt(buf.data(), buf.size(), block);
  const bool changed =
      std::memcmp(buf.data() + 8, plain.data() + 8, buf.size() - 8) != 0;

  // 3) a cifra muda os bytes (uma implementacao identidade nao passa aqui).
  Check(changed, (rel + ": Encrypt altera o corpo").c_str());

  pkc::Decrypt(buf.data(), buf.size(), block);
  Check(buf == plain, (rel + ": Encrypt->Decrypt reproduz o original").c_str());
}

int main() {
  // PK8 (SwSh) — bloco 0x50, buffer de party 344.
  TestFixture("pk8/bouffalant-shiny.pk8", pkc::kBlockPK8);
  TestFixture("pk8/cinderace-mint-nature.pk8", pkc::kBlockPK8);
  TestFixture("pk8/glastrier.pk8", pkc::kBlockPK8);
  TestFixture("pk8/mienshao.pk8", pkc::kBlockPK8);
  TestFixture("pk8/mr-mime-galar.pk8", pkc::kBlockPK8);
  TestFixture("pk8/rillaboom.pk8", pkc::kBlockPK8);

  // PK9 (SV) — mesmo bloco 0x50.
  TestFixture("pk9/0058-01 - Growlithe - 8A3926835D1D.pk9", pkc::kBlockPK8);
  TestFixture("pk9/0128-01 - Tauros - 3ACEA55CFD17.pk9", pkc::kBlockPK8);
  TestFixture("pk9/0922 - Pawmo - E119DEA69C8F.pk9", pkc::kBlockPK8);

  // PA8 (PLA) — bloco 0x58, buffers 360 (stored) e 376 (party).
  TestFixture("pa8/dialga.pa8", pkc::kBlockPA8);
  TestFixture("pa8/luxray.pa8", pkc::kBlockPA8);
  TestFixture("pa8/479 - Rotom - F588F21C779E.pa8", pkc::kBlockPA8);

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("pkm_crypto: tudo verde\n");
  return 0;
}
