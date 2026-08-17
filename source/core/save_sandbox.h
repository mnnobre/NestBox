// Sandbox de save (spec 064, G04 da descoberta escrita-e-transferencia).
//
// Regra unica e inegociavel: o save ORIGINAL nunca e aberto para escrita.
// Quem quer exercitar escrita opera na copia que este objeto devolve.
//
// O original e lido uma vez, em modo binario de leitura, e a copia vai para um
// diretorio temporario proprio. O destrutor apaga o diretorio inteiro (RAII),
// entao um teste que aborta no meio nao deixa lixo.
//
// Defesa em profundidade: Create() recusa se o destino cair dentro da arvore
// de saves do simulador. A prova real de nao-destruicao e o teste de SHA256
// (tests/test_save_sandbox.cpp), nao este guard.
//
// Mora no core, e nao em tests/, porque G13 (transferencia com commit atomico)
// vai precisar do mesmo "escreve em tmp e so entao troca" no produto — TD-01.
#pragma once

#include <optional>
#include <string>

namespace sandbox {

// Um diretorio temporario com a copia de um save. Move-only.
class SaveSandbox {
 public:
  // Copia `original` para um diretorio temporario novo. Devolve nullopt se o
  // original nao existe, se a copia falha, ou se o destino cairia dentro da
  // arvore de saves protegida.
  static std::optional<SaveSandbox> Create(const std::string& original);

  ~SaveSandbox();
  SaveSandbox(SaveSandbox&&) noexcept;
  SaveSandbox& operator=(SaveSandbox&&) noexcept;
  SaveSandbox(const SaveSandbox&) = delete;
  SaveSandbox& operator=(const SaveSandbox&) = delete;

  // Caminho da COPIA — o unico que pode ser aberto para escrita.
  const std::string& path() const { return copy_; }
  // Diretorio temporario que sera apagado na destruicao.
  const std::string& dir() const { return dir_; }

 private:
  SaveSandbox(std::string dir, std::string copy)
      : dir_(std::move(dir)), copy_(std::move(copy)) {}
  void Cleanup();

  std::string dir_;
  std::string copy_;
};

// Verdadeiro se `path` esta dentro de um diretorio "saves" sob "switch-sim".
// Exposto para o teste plantar a violacao sem precisar de um save real.
bool IsProtectedPath(const std::string& path);

}  // namespace sandbox
