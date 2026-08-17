// Teste do backup de save (spec 032).
//
// A rede de seguranca exigida por produto.md antes de escrever em save. Um
// backup que nao confere e pior que nenhum: da falsa seguranca.

#include <cstdio>
#include <string>
#include <vector>

#include "save_backup.h"

namespace bk = pokehome::backup;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
  if (cond) {
    std::printf("  ok   %s\n", what);
  } else {
    std::printf("  FAIL %s\n", what);
    ++g_failures;
  }
}

bk::Entry Mk(const std::string& save, const std::string& stamp) {
  bk::Entry e;
  e.save_name = save;
  e.stamp = stamp;
  e.filename = save + "." + stamp + ".bak";
  return e;
}

void TestNome() {
  std::printf("nome do backup (spec 032):\n");

  Check(bk::SaveName("sdmc:/nestbox/Pokemon FireRed.sav") == "Pokemon FireRed",
        "tira diretorio e extensao");
  Check(bk::SaveName("C:\\saves\\Emerald.srm") == "Emerald",
        "funciona com barra invertida");
  Check(bk::SaveName("solto.sav") == "solto", "arquivo sem diretorio");
  Check(bk::SaveName("sem_extensao") == "sem_extensao", "arquivo sem extensao");

  const std::string f =
      bk::MakeFilename("sdmc:/nestbox/Pokemon FireRed.sav", "20260815-1642");
  Check(f == "Pokemon FireRed.20260815-1642.bak", "monta o nome do backup");

  // Dois backups do mesmo save em carimbos diferentes nao colidem.
  const std::string f2 =
      bk::MakeFilename("sdmc:/nestbox/Pokemon FireRed.sav", "20260815-1700");
  Check(f != f2, "carimbos diferentes geram nomes diferentes");
}

void TestParse() {
  std::printf("leitura do nome (spec 032):\n");

  bk::Entry e;
  Check(bk::ParseFilename("Pokemon FireRed.20260815-1642.bak", &e),
        "aceita nome bem formado");
  Check(e.save_name == "Pokemon FireRed", "extrai o save");
  Check(e.stamp == "20260815-1642", "extrai o carimbo");

  // Ida e volta.
  const std::string f = bk::MakeFilename("x/LeafGreen.sav", "20260101-0000");
  bk::Entry back;
  Check(bk::ParseFilename(f, &back) && back.save_name == "LeafGreen" &&
            back.stamp == "20260101-0000",
        "ida e volta preserva save e carimbo");

  // Arquivo alheio no diretorio nao vira entrada.
  Check(!bk::ParseFilename("qualquer.txt", &e), "recusa extensao errada");
  Check(!bk::ParseFilename("semcarimbo.bak", &e), "recusa nome sem carimbo");
  Check(!bk::ParseFilename(".bak", &e), "recusa nome vazio");
  Check(!bk::ParseFilename("", &e), "recusa string vazia");
}

void TestVerify() {
  std::printf("verificacao do backup (spec 032):\n");

  const std::vector<std::uint8_t> orig = {1, 2, 3, 4, 5};
  Check(bk::Verify(orig, orig), "bytes identicos passam");

  std::vector<std::uint8_t> diferente = orig;
  diferente[2] = 99;
  Check(!bk::Verify(orig, diferente), "um byte diferente reprova");

  std::vector<std::uint8_t> truncado = {1, 2, 3};
  Check(!bk::Verify(orig, truncado), "backup truncado reprova");

  std::vector<std::uint8_t> maior = {1, 2, 3, 4, 5, 6};
  Check(!bk::Verify(orig, maior), "backup maior reprova");

  const std::vector<std::uint8_t> vazio;
  Check(!bk::Verify(orig, vazio), "backup vazio reprova");
  Check(bk::Verify(vazio, vazio), "dois vazios conferem");
}

void TestRotacao() {
  std::printf("rotacao de backups (spec 032):\n");

  std::vector<bk::Entry> poucos = {Mk("FireRed", "20260101-0000"),
                                   Mk("FireRed", "20260102-0000")};
  Check(bk::ToRemove(poucos, 8).empty(), "abaixo do limite nao remove nada");
  Check(bk::ToRemove(poucos, 2).empty(), "exatamente no limite nao remove");

  // Cinco backups, guardar tres: remove os dois mais antigos.
  std::vector<bk::Entry> muitos = {
      Mk("FireRed", "20260105-0000"), Mk("FireRed", "20260101-0000"),
      Mk("FireRed", "20260103-0000"), Mk("FireRed", "20260102-0000"),
      Mk("FireRed", "20260104-0000")};
  const auto remover = bk::ToRemove(muitos, 3);
  Check(remover.size() == 2, "remove o excedente");
  Check(remover[0] == "FireRed.20260101-0000.bak", "remove o mais antigo");
  Check(remover[1] == "FireRed.20260102-0000.bak", "e o segundo mais antigo");

  // A entrada nao vem ordenada; a rotacao nao pode depender disso.
  bool mantem_recentes = true;
  for (const auto& f : remover) {
    if (f.find("20260105") != std::string::npos ||
        f.find("20260104") != std::string::npos ||
        f.find("20260103") != std::string::npos) {
      mantem_recentes = false;
    }
  }
  Check(mantem_recentes, "os mais recentes sao preservados");

  Check(bk::ToRemove({}, 8).empty(), "lista vazia nao estoura");
}

void TestFiltroPorSave() {
  std::printf("backups de um save so (spec 032):\n");

  const std::vector<bk::Entry> todos = {Mk("FireRed", "20260101-0000"),
                                        Mk("LeafGreen", "20260101-0000"),
                                        Mk("FireRed", "20260102-0000")};

  const auto fr = bk::ForSave(todos, "sdmc:/nestbox/FireRed.sav");
  Check(fr.size() == 2, "pega so os do save pedido");
  Check(fr[0].save_name == "FireRed", "e sao do save certo");

  const auto nada = bk::ForSave(todos, "sdmc:/nestbox/Emerald.sav");
  Check(nada.empty(), "save sem backup devolve lista vazia");

  // A rotacao de um save nao pode apagar backup de outro.
  const auto remover = bk::ToRemove(fr, 1);
  Check(remover.size() == 1, "rotacao considera so os do save");
  Check(remover[0].find("FireRed") != std::string::npos,
        "e nao toca no backup de outro save");
}

}  // namespace

int main() {
  TestNome();
  TestParse();
  TestVerify();
  TestRotacao();
  TestFiltroPorSave();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
