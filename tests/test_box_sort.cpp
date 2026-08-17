// Teste da ordenacao da lista (spec 024). Sem framework, como os demais.

#include <cstdio>
#include <string>
#include <vector>

#include "box_sort.h"

namespace bx = pokehome::box;

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

bx::SortEntry Mon(std::size_t idx, int dex, int level, const char* name) {
  bx::SortEntry e;
  e.index = idx;
  e.empty = false;
  e.dex = dex;
  e.level = level;
  e.name = name;
  return e;
}

bx::SortEntry Vazio(std::size_t idx) {
  bx::SortEntry e;
  e.index = idx;
  e.empty = true;
  return e;
}

// Lista com buracos no meio, que e o caso real de uma caixa usada.
std::vector<bx::SortEntry> Amostra() {
  return {
      Mon(0, 25, 50, "Pikachu"),
      Vazio(1),
      Mon(2, 6, 80, "Charizard"),
      Mon(3, 1, 5, "Bulbasaur"),
      Vazio(4),
      Mon(5, 150, 70, "Mewtwo"),
  };
}

void TestBox() {
  std::printf("ordem de caixa:\n");
  const auto items = Amostra();
  const auto order = bx::SortIndices(items, bx::SortBy::kBox);

  Check(order.size() == 6, "devolve todos os slots");
  const std::vector<std::size_t> esperado = {0, 1, 2, 3, 4, 5};
  Check(order == esperado, "ordem crua preservada, com os buracos no lugar");
}

void TestDex() {
  std::printf("ordem por dex:\n");
  const auto items = Amostra();
  const auto order = bx::SortIndices(items, bx::SortBy::kDex);

  // Bulbasaur(1) Charizard(6) Pikachu(25) Mewtwo(150), depois os dois vazios.
  const std::vector<std::size_t> esperado = {3, 2, 0, 5, 1, 4};
  Check(order == esperado, "crescente por dex, vazios no fim");
}

void TestLevel() {
  std::printf("ordem por nivel:\n");
  const auto items = Amostra();
  const auto order = bx::SortIndices(items, bx::SortBy::kLevel);

  // 5, 50, 70, 80.
  const std::vector<std::size_t> esperado = {3, 0, 5, 2, 1, 4};
  Check(order == esperado, "crescente por nivel, vazios no fim");
}

void TestSpecies() {
  std::printf("ordem por especie:\n");
  const auto items = Amostra();
  const auto order = bx::SortIndices(items, bx::SortBy::kSpecies);

  // Bulbasaur, Charizard, Mewtwo, Pikachu.
  const std::vector<std::size_t> esperado = {3, 2, 5, 0, 1, 4};
  Check(order == esperado, "alfabetica, vazios no fim");
}

void TestEstavel() {
  std::printf("estabilidade:\n");

  // Tres Pokemon de mesmo nivel: a ordem entre eles tem que ser a de caixa.
  std::vector<bx::SortEntry> items = {
      Mon(0, 25, 50, "Pikachu"),
      Mon(1, 6, 50, "Charizard"),
      Mon(2, 1, 50, "Bulbasaur"),
  };
  const auto order = bx::SortIndices(items, bx::SortBy::kLevel);
  const std::vector<std::size_t> esperado = {0, 1, 2};
  Check(order == esperado, "empate de nivel mantem a ordem de caixa");

  // O mesmo vale entre vazios.
  std::vector<bx::SortEntry> vazios = {Vazio(0), Vazio(1), Vazio(2)};
  const auto ordem_vazios = bx::SortIndices(vazios, bx::SortBy::kDex);
  Check(ordem_vazios == std::vector<std::size_t>({0, 1, 2}),
        "vazios mantem a ordem entre si");
}

void TestNaoAlteraFonte() {
  std::printf("a fonte nao e alterada:\n");

  auto items = Amostra();
  bx::SortIndices(items, bx::SortBy::kDex);
  bx::SortIndices(items, bx::SortBy::kLevel);

  Check(items[0].index == 0 && items[0].name == "Pikachu",
        "o vetor de entrada continua na ordem original");
  Check(items[3].name == "Bulbasaur", "nenhum item trocou de posicao");
}

void TestVazia() {
  std::printf("casos degenerados:\n");

  const std::vector<bx::SortEntry> nada;
  Check(bx::SortIndices(nada, bx::SortBy::kDex).empty(),
        "lista vazia nao estoura");

  const std::vector<bx::SortEntry> um = {Mon(0, 25, 50, "Pikachu")};
  Check(bx::SortIndices(um, bx::SortBy::kLevel).size() == 1,
        "lista de um elemento");

  // Todos vazios: qualquer criterio devolve a ordem crua.
  const std::vector<bx::SortEntry> todos = {Vazio(0), Vazio(1)};
  Check(bx::SortIndices(todos, bx::SortBy::kSpecies) ==
            std::vector<std::size_t>({0, 1}),
        "caixa toda vazia");
}

void TestCiclo() {
  std::printf("ciclo do botao Y:\n");

  bx::SortBy by = bx::SortBy::kBox;
  by = bx::NextSort(by);
  Check(by == bx::SortBy::kDex, "caixa -> dex");
  by = bx::NextSort(by);
  Check(by == bx::SortBy::kLevel, "dex -> nivel");
  by = bx::NextSort(by);
  Check(by == bx::SortBy::kSpecies, "nivel -> especie");
  by = bx::NextSort(by);
  Check(by == bx::SortBy::kBox, "especie -> caixa (fecha o ciclo)");

  Check(std::string(bx::SortName(bx::SortBy::kDex)) == "Dex",
        "o criterio tem nome para o rodape");
}

// --- Filtro (spec 025) ------------------------------------------------------

bx::SortEntry Shiny(std::size_t idx, int dex, int level, const char* name) {
  bx::SortEntry e = Mon(idx, dex, level, name);
  e.shiny = true;
  return e;
}

void TestFiltroShiny() {
  std::printf("filtro de shiny (spec 025):\n");

  const std::vector<bx::SortEntry> items = {
      Mon(0, 25, 50, "Pikachu"),
      Vazio(1),
      Shiny(2, 6, 80, "Charizard"),
      Mon(3, 1, 5, "Bulbasaur"),
      Shiny(4, 150, 70, "Mewtwo"),
  };

  // Sem filtro: tudo, na ordem crua.
  const auto todos = bx::FilterAndSort(items, bx::Filter::kNone, bx::SortBy::kBox);
  Check(todos.size() == 5, "sem filtro devolve todos os slots");

  // Só shiny: os dois, e o vazio some.
  const auto shiny = bx::FilterAndSort(items, bx::Filter::kShinyOnly,
                                       bx::SortBy::kBox);
  Check(shiny.size() == 2, "filtro reduz a lista aos shiny");
  Check(shiny == std::vector<std::size_t>({2, 4}),
        "devolve os indices ORIGINAIS dos shiny, na ordem de caixa");

  // Filtro + ordenacao compostos: por dex, Charizard(6) antes de Mewtwo(150).
  const auto por_dex = bx::FilterAndSort(items, bx::Filter::kShinyOnly,
                                         bx::SortBy::kDex);
  Check(por_dex == std::vector<std::size_t>({2, 4}), "filtra e ordena por dex");

  // Por nivel: Mewtwo(70) antes de Charizard(80) — inverte em relacao ao dex,
  // o que prova que a ordenacao roda DEPOIS do filtro, e nao e ignorada.
  const auto por_nivel = bx::FilterAndSort(items, bx::Filter::kShinyOnly,
                                           bx::SortBy::kLevel);
  Check(por_nivel == std::vector<std::size_t>({4, 2}),
        "filtra e ordena por nivel");
}

void TestFiltroSemResultado() {
  std::printf("filtro sem resultado (spec 025):\n");

  const std::vector<bx::SortEntry> items = {
      Mon(0, 25, 50, "Pikachu"),
      Vazio(1),
  };
  const auto r = bx::FilterAndSort(items, bx::Filter::kShinyOnly,
                                   bx::SortBy::kDex);
  Check(r.empty(), "nenhum shiny devolve lista vazia sem estourar");

  const std::vector<bx::SortEntry> nada;
  Check(bx::FilterAndSort(nada, bx::Filter::kShinyOnly, bx::SortBy::kBox).empty(),
        "lista vazia com filtro nao estoura");

  Check(std::string(bx::FilterName(bx::Filter::kShinyOnly)) == "So shiny",
        "o filtro tem nome para o rodape");
}

}  // namespace

int main() {
  TestBox();
  TestDex();
  TestLevel();
  TestSpecies();
  TestEstavel();
  TestNaoAlteraFonte();
  TestVazia();
  TestCiclo();
  TestFiltroShiny();
  TestFiltroSemResultado();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
