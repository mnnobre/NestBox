#include "save_sandbox.h"

#include <atomic>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace sandbox {
namespace {

// Normaliza sem exigir que o caminho exista (o destino ainda nao existe
// quando o guard roda).
fs::path Normalize(const fs::path& p) {
  std::error_code ec;
  fs::path out = fs::weakly_canonical(p, ec);
  return ec ? p.lexically_normal() : out;
}

}  // namespace

bool IsProtectedPath(const std::string& path) {
  const fs::path norm = Normalize(fs::path(path));
  // Procura o par de componentes consecutivos "switch-sim" / "saves". Comparar
  // a string inteira falharia com separador de Windows vs POSIX.
  bool seen_sim = false;
  for (const auto& part : norm) {
    const std::string name = part.string();
    if (seen_sim && name == "saves") return true;
    seen_sim = (name == "switch-sim");
  }
  return false;
}

std::optional<SaveSandbox> SaveSandbox::Create(const std::string& original) {
  std::error_code ec;
  const fs::path src = fs::path(original);
  if (!fs::is_regular_file(src, ec)) return std::nullopt;

  // Nome unico: o contador atomico sozinho e por PROCESSO, entao dois
  // executaveis de teste rodando em paralelo (ctest com -j, ou dois agentes)
  // colidiam em "nestbox-sandbox-0-0" e um deles falhava de forma
  // intermitente. O pid separa os processos; o contador separa os sandboxes
  // dentro de cada um.
  static std::atomic<unsigned> counter{0};
  const unsigned long pid =
      static_cast<unsigned long>(
#ifdef _WIN32
          _getpid()
#else
          getpid()
#endif
      );
  const fs::path root = fs::temp_directory_path(ec);
  if (ec) return std::nullopt;
  fs::path dir;
  for (int attempt = 0; attempt < 64; ++attempt) {
    dir = root / ("nestbox-sandbox-" + std::to_string(pid) + "-" +
                  std::to_string(counter.fetch_add(1)) + "-" +
                  std::to_string(attempt));
    if (fs::create_directory(dir, ec)) break;
    dir.clear();
  }
  if (dir.empty()) return std::nullopt;

  const fs::path dest = dir / src.filename();

  // Guard: se por qualquer motivo o destino cair dentro da arvore protegida,
  // aborta antes de copiar um unico byte.
  if (IsProtectedPath(dest.string())) {
    fs::remove_all(dir, ec);
    return std::nullopt;
  }

  // copy_file abre o original com ifstream (leitura). Nunca para escrita.
  if (!fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec)) {
    fs::remove_all(dir, ec);
    return std::nullopt;
  }
  // A copia herda o modo somente-leitura do original em alguns sistemas; quem
  // recebeu o sandbox precisa poder escrever nela.
  fs::permissions(dest, fs::perms::owner_write, fs::perm_options::add, ec);

  return SaveSandbox(dir.string(), dest.string());
}

void SaveSandbox::Cleanup() {
  if (dir_.empty()) return;
  std::error_code ec;
  fs::remove_all(fs::path(dir_), ec);
  dir_.clear();
  copy_.clear();
}

SaveSandbox::~SaveSandbox() { Cleanup(); }

SaveSandbox::SaveSandbox(SaveSandbox&& other) noexcept
    : dir_(std::move(other.dir_)), copy_(std::move(other.copy_)) {
  other.dir_.clear();
  other.copy_.clear();
}

SaveSandbox& SaveSandbox::operator=(SaveSandbox&& other) noexcept {
  if (this != &other) {
    Cleanup();
    dir_ = std::move(other.dir_);
    copy_ = std::move(other.copy_);
    other.dir_.clear();
    other.copy_.clear();
  }
  return *this;
}

}  // namespace sandbox
