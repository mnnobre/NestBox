// Backup de save de jogo (spec 032).
//
// Rede de seguranca exigida por spec/memory/produto.md antes de qualquer
// escrita em save: "sem rede de seguranca nao se escreve em save".
//
// Como o resto do core, isto e logica pura — nomes, verificacao e rotacao.
// O I/O fica na UI, que e quem conhece o cartao.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pokehome::backup {

// Quantos backups guardar por save. O gen3 tem 128 KB por arquivo, entao 8
// custa 1 MB — barato perto de perder um save.
inline constexpr std::size_t kMaxPerSave = 8;

// Um backup existente no cartao.
struct Entry {
  std::string filename;  // nome do arquivo, sem diretorio
  std::string save_name;  // save de origem
  std::string stamp;      // carimbo, ordenavel como texto
};

// Nome base do save, sem diretorio nem extensao. E o que agrupa os backups de
// um mesmo save.
inline std::string SaveName(const std::string& path) {
  const auto slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

// Nome do arquivo de backup: "<save>.<carimbo>.bak".
//
// O carimbo vem de fora (a UI tem o relogio) e precisa ser ordenavel como
// texto — "20260815-1642" serve. Isso permite achar o mais antigo sem parsear
// data.
inline std::string MakeFilename(const std::string& save_path,
                                const std::string& stamp) {
  return SaveName(save_path) + "." + stamp + ".bak";
}

// Le o nome de um arquivo de backup de volta para save + carimbo. Devolve
// false se o nome nao tem o formato esperado — arquivo alheio no diretorio nao
// deve virar entrada da lista.
inline bool ParseFilename(const std::string& filename, Entry* out) {
  const std::string suffix = ".bak";
  if (filename.size() <= suffix.size()) return false;
  if (filename.compare(filename.size() - suffix.size(), suffix.size(),
                       suffix) != 0) {
    return false;
  }
  const std::string base =
      filename.substr(0, filename.size() - suffix.size());
  const auto dot = base.find_last_of('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= base.size()) {
    return false;
  }
  if (out) {
    out->filename = filename;
    out->save_name = base.substr(0, dot);
    out->stamp = base.substr(dot + 1);
  }
  return true;
}

// Verifica um backup contra os bytes originais. Tamanho diferente ou qualquer
// byte diferente reprova.
//
// A verificacao existe porque um backup nao conferido da FALSA seguranca, que
// e pior que nao ter backup: muda o comportamento de quem confia nele
// (TD-01 da spec 032).
inline bool Verify(const std::vector<std::uint8_t>& original,
                   const std::vector<std::uint8_t>& readback) {
  return original.size() == readback.size() && original == readback;
}

// Quais backups apagar para caber no limite. Devolve os mais antigos,
// mantendo os `keep` mais recentes.
//
// `entries` sao os backups de UM save; a ordenacao e por carimbo, que e
// ordenavel como texto por construcao.
inline std::vector<std::string> ToRemove(std::vector<Entry> entries,
                                         std::size_t keep = kMaxPerSave) {
  std::vector<std::string> out;
  if (entries.size() <= keep) return out;

  std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.stamp < b.stamp; });

  const std::size_t excess = entries.size() - keep;
  out.reserve(excess);
  for (std::size_t i = 0; i < excess; ++i) {
    out.push_back(entries[i].filename);
  }
  return out;
}

// Carimbo "AAAAMMDD-hhmmss" para "DD/MM/AAAA hh:mm" (spec 037).
//
// O carimbo e ordenavel como texto de proposito, o que o torna ilegivel para
// quem le a tela de restauracao. Carimbo que nao bate com o formato volta cru:
// mostrar o nome real do arquivo diz a verdade, enquanto uma data inventada a
// partir de lixo faria o usuario escolher pelo motivo errado.
inline std::string HumanStamp(const std::string& stamp) {
  if (stamp.size() != 15 || stamp[8] != '-') return stamp;
  for (std::size_t i = 0; i < stamp.size(); ++i) {
    if (i == 8) continue;
    if (stamp[i] < '0' || stamp[i] > '9') return stamp;
  }
  return stamp.substr(6, 2) + "/" + stamp.substr(4, 2) + "/" +
         stamp.substr(0, 4) + " " + stamp.substr(9, 2) + ":" +
         stamp.substr(11, 2);
}

// Ordena do mais recente para o mais antigo — a ordem em que a tela lista.
//
// O mais recente primeiro porque e o que quase sempre se quer: desfazer a
// ultima escrita. O `dirent` devolve em ordem de diretorio, que nao e ordem
// nenhuma.
inline std::vector<Entry> NewestFirst(std::vector<Entry> entries) {
  std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.stamp > b.stamp; });
  return entries;
}

// Filtra as entradas de um save especifico.
inline std::vector<Entry> ForSave(const std::vector<Entry>& all,
                                  const std::string& save_path) {
  const std::string name = SaveName(save_path);
  std::vector<Entry> out;
  for (const Entry& e : all) {
    if (e.save_name == name) out.push_back(e);
  }
  return out;
}

}  // namespace pokehome::backup
