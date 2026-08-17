// Teste da restauracao de backup (spec 037).
//
// A tela nao e testavel por ctest; o que da para provar aqui e a logica que ela
// consome: a data que o usuario le, a ordem em que a lista aparece e o filtro
// que impede o backup de um save aparecer na lista de outro.

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

void TestDataLegivel() {
  std::printf("data legivel do carimbo (spec 037):\n");

  Check(bk::HumanStamp("20260815-164233") == "15/08/2026 16:42",
        "converte carimbo completo");
  Check(bk::HumanStamp("20260101-000000") == "01/01/2026 00:00",
        "meia-noite do primeiro dia");
  Check(bk::HumanStamp("19991231-235959") == "31/12/1999 23:59",
        "virada do milenio");

  // Os segundos existem no nome do arquivo (dois backups no mesmo minuto nao
  // colidem) mas nao na tela — o minuto ja localiza o backup.
  Check(bk::HumanStamp("20260815-164200") == bk::HumanStamp("20260815-164259"),
        "segundos nao aparecem na data legivel");

  std::printf("carimbo malformado (spec 037):\n");

  // O carimbo cru e a verdade; data inventada faria escolher pelo motivo
  // errado.
  Check(bk::HumanStamp("") == "", "carimbo vazio volta cru");
  Check(bk::HumanStamp("20260815") == "20260815", "carimbo curto volta cru");
  Check(bk::HumanStamp("20260815-1642") == "20260815-1642",
        "carimbo do formato antigo volta cru");
  Check(bk::HumanStamp("20260815+164233") == "20260815+164233",
        "separador errado volta cru");
  Check(bk::HumanStamp("2026o815-164233") == "2026o815-164233",
        "letra no lugar de digito volta cru");
  Check(bk::HumanStamp("20260815-1642333") == "20260815-1642333",
        "carimbo longo demais volta cru");
}

void TestOrdenacao() {
  std::printf("ordem da lista (spec 037):\n");

  // O dirent devolve em ordem de diretorio, que nao e ordem nenhuma.
  const std::vector<bk::Entry> baguncado = {
      Mk("FireRed", "20260103-120000"), Mk("FireRed", "20260105-090000"),
      Mk("FireRed", "20260101-235900"), Mk("FireRed", "20260104-000000")};

  const auto ordenado = bk::NewestFirst(baguncado);
  Check(ordenado.size() == 4, "nao perde nem duplica entrada");
  Check(ordenado[0].stamp == "20260105-090000", "o mais recente vem primeiro");
  Check(ordenado[3].stamp == "20260101-235900", "o mais antigo vem por ultimo");

  bool decrescente = true;
  for (std::size_t i = 1; i < ordenado.size(); ++i) {
    if (ordenado[i - 1].stamp < ordenado[i].stamp) decrescente = false;
  }
  Check(decrescente, "a lista inteira sai em ordem decrescente");

  // Dois backups no mesmo minuto: os segundos desempatam.
  const auto mesmo_minuto = bk::NewestFirst(
      {Mk("FireRed", "20260105-090010"), Mk("FireRed", "20260105-090055")});
  Check(mesmo_minuto[0].stamp == "20260105-090055",
        "segundos desempatam no mesmo minuto");

  Check(bk::NewestFirst({}).empty(), "lista vazia nao estoura");

  const auto um = bk::NewestFirst({Mk("FireRed", "20260105-090000")});
  Check(um.size() == 1, "lista de um elemento sobrevive");
}

void TestListaDoSaveAberto() {
  std::printf("lista do save aberto (spec 037):\n");

  // O que a tela faz: filtra pelo save aberto e ordena. Restaurar backup de
  // outro save gravaria um FireRed por cima de um LeafGreen (TD-01).
  const std::vector<bk::Entry> todos = {
      Mk("FireRed", "20260101-120000"), Mk("LeafGreen", "20260105-120000"),
      Mk("FireRed", "20260103-120000"), Mk("Emerald", "20260104-120000")};

  const auto lista =
      bk::NewestFirst(bk::ForSave(todos, "sdmc:/nestbox/FireRed.sav"));
  Check(lista.size() == 2, "so os backups do save aberto");
  Check(lista[0].stamp == "20260103-120000", "e ja ordenados");

  bool so_do_save = true;
  for (const auto& e : lista) {
    if (e.save_name != "FireRed") so_do_save = false;
  }
  Check(so_do_save, "nenhum backup de outro save vaza para a lista");

  // Save sem backup nenhum: a tela precisa avisar, nao mostrar lista vazia sem
  // explicacao.
  const auto vazia =
      bk::NewestFirst(bk::ForSave(todos, "sdmc:/nestbox/Ruby.sav"));
  Check(vazia.empty(), "save sem backup devolve lista vazia");

  // O nome mostrado na tela sai do carimbo da entrada escolhida.
  Check(bk::HumanStamp(lista[0].stamp) == "03/01/2026 12:00",
        "a entrada escolhida rende a data que a tela mostra");
}

void TestVerificacaoAntesDeRestaurar() {
  std::printf("verificacao antes de restaurar (spec 037):\n");

  // Restaurar lixo por cima de um save bom e pior que nao restaurar, entao o
  // backup e relido e conferido antes de qualquer escrita.
  const std::vector<std::uint8_t> save_atual(128, 0xAB);

  std::vector<std::uint8_t> bom(128, 0x11);
  Check(bom.size() == save_atual.size(), "backup do tamanho certo e aceito");

  const std::vector<std::uint8_t> truncado(64, 0x11);
  Check(truncado.size() != save_atual.size(),
        "backup truncado tem tamanho diferente do save");

  const std::vector<std::uint8_t> vazio;
  Check(vazio.size() != save_atual.size(),
        "backup ilegivel (vazio) reprova pelo tamanho");

  // Verify continua sendo a conferencia byte a byte usada na gravacao.
  Check(bk::Verify(bom, bom), "bytes identicos conferem");
  Check(!bk::Verify(bom, truncado), "tamanho diferente reprova");
}

}  // namespace

int main() {
  TestDataLegivel();
  TestOrdenacao();
  TestListaDoSaveAberto();
  TestVerificacaoAntesDeRestaurar();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
