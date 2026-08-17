// Testes do SaveSandbox (spec 064, G04).
//
// O teste mais importante do arquivo e TestOriginalNuncaMuda: ele hasheia o
// save REAL antes, roda o ciclo inteiro do sandbox (copia, escrita agressiva
// na copia, destruicao) e hasheia de novo. Se o hash mudar, a spec falhou no
// unico ponto que nao admite falha.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "save_sandbox.h"
#include "sha256.h"

namespace fs = std::filesystem;

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

static std::string HexDigest(const std::vector<std::uint8_t>& data) {
  const sha256::Digest d = sha256::Hash(data.data(), data.size());
  static const char* kHex = "0123456789abcdef";
  std::string out;
  for (std::uint8_t b : d) {
    out.push_back(kHex[b >> 4]);
    out.push_back(kHex[b & 0x0F]);
  }
  return out;
}

// O teste que sustenta toda a onda de escrita.
static void TestOriginalNuncaMuda(const std::string& name,
                                  const std::string& original) {
  const std::vector<std::uint8_t> before = ReadFile(original);
  if (before.empty()) {
    std::printf("FALHOU: save ausente: %s\n", original.c_str());
    ++g_failures;
    return;
  }
  const std::string hash_before = HexDigest(before);
  const auto mtime_before = fs::last_write_time(fs::path(original));

  std::string copy_path;
  {
    auto sb = sandbox::SaveSandbox::Create(original);
    Check(sb.has_value(), name + ": sandbox criado");
    if (!sb) return;
    copy_path = sb->path();

    Check(fs::exists(fs::path(copy_path)), name + ": a copia existe");
    Check(fs::path(copy_path) != fs::path(original),
          name + ": a copia NAO e o original");
    Check(!sandbox::IsProtectedPath(copy_path),
          name + ": a copia esta fora da arvore protegida");
    Check(ReadFile(copy_path) == before,
          name + ": a copia e byte-identica ao original");

    // Escrita agressiva na copia — e exatamente para isso que ela existe.
    {
      std::ofstream out(copy_path, std::ios::binary | std::ios::trunc);
      out << "LIXO DE TESTE";
    }
    Check(ReadFile(copy_path) != before, name + ": a copia foi de fato escrita");
  }

  // RAII: o diretorio temporario some junto com o objeto.
  Check(!fs::exists(fs::path(copy_path)),
        name + ": a copia sumiu ao destruir o sandbox");

  const std::vector<std::uint8_t> after = ReadFile(original);
  Check(HexDigest(after) == hash_before,
        name + ": SHA256 do ORIGINAL identico antes e depois (" + hash_before +
            ")");
  Check(fs::last_write_time(fs::path(original)) == mtime_before,
        name + ": mtime do ORIGINAL inalterado");
}

// Violacao plantada: o guard tem de acusar o vermelho sozinho.
static void TestGuardDeCaminho() {
  Check(sandbox::IsProtectedPath(std::string(SIM_SAVES) +
                                 "01008DB008C2C000/Amaral/main"),
        "guard reconhece a arvore de saves do simulador");
  Check(sandbox::IsProtectedPath(std::string(SIM_SAVES) + "qualquer/coisa.bin"),
        "guard reconhece um caminho novo dentro da arvore");
  Check(!sandbox::IsProtectedPath(
            (fs::temp_directory_path() / "nestbox" / "main").string()),
        "guard libera um caminho fora da arvore");
  // Caminho relativo com ".." que reentra na arvore: normalizacao tem de pegar.
  Check(sandbox::IsProtectedPath(std::string(SIM_SAVES) +
                                 "01008DB008C2C000/../0100A3D008C5C000/x"),
        "guard normaliza '..' antes de decidir");
}

static void TestOriginalInexistente() {
  auto sb = sandbox::SaveSandbox::Create(
      (fs::temp_directory_path() / "nao-existe-nestbox-064.bin").string());
  Check(!sb.has_value(), "Create recusa um original inexistente");
}

// Dois sandboxes do mesmo save nao colidem, e mover nao apaga cedo demais.
static void TestIsolamentoEMove(const std::string& original) {
  auto a = sandbox::SaveSandbox::Create(original);
  auto b = sandbox::SaveSandbox::Create(original);
  Check(a && b, "dois sandboxes simultaneos");
  if (!a || !b) return;
  Check(a->dir() != b->dir(), "cada sandbox tem diretorio proprio");

  std::string moved_path;
  {
    sandbox::SaveSandbox moved = std::move(*a);
    moved_path = moved.path();
    Check(fs::exists(fs::path(moved_path)), "move preserva a copia");
  }
  Check(!fs::exists(fs::path(moved_path)), "destruir o movido limpa a copia");
}

int main() {
  const std::string shield =
      std::string(SIM_SAVES) + "01008DB008C2C000/Amaral/main";

  TestOriginalNuncaMuda("Shield", shield);
  TestOriginalNuncaMuda("Scarlet",
                        std::string(SIM_SAVES) + "0100A3D008C5C000/Amaral/main");
  TestOriginalNuncaMuda("BDSP", std::string(SIM_SAVES) +
                                    "0100000011D90000/Amaral/SaveData.bin");
  TestGuardDeCaminho();
  TestOriginalInexistente();
  TestIsolamentoEMove(shield);

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("save_sandbox: tudo verde\n");
  return 0;
}
