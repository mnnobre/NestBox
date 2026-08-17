// Ordenacao da lista de Pokemon (spec 024).
//
// Como o box_move.h, isto e logica pura: recebe dados, devolve dados. Sem
// borealis, sem libnx — testavel por ctest sem abrir janela.
//
// A ordenacao e uma VISAO: devolve uma lista de indices, e a fonte continua
// intocada. Os slots nao se movem, so a ordem em que sao mostrados (TD-01).

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pokehome::box {

enum class SortBy {
  kBox,      // ordem crua dos slots — o padrao
  kDex,      // numero da national dex
  kLevel,    // nivel calculado
  kSpecies,  // nome da especie, alfabetico
};

// O que a ordenacao precisa saber de cada slot. A UI preenche isto lendo o que
// ja tem em maos; o core nao conhece BoxPokemon nem o parser.
struct SortEntry {
  std::size_t index = 0;  // posicao original, que e o que a UI usa depois
  bool empty = true;
  int dex = 0;
  int level = 0;
  std::string name;
  bool shiny = false;  // spec 025
};

// Filtros da lista. Bitmask para compor mais de um quando houver outros
// (idioma, Poke Ball...), sem trocar a assinatura de novo.
enum class Filter {
  kNone = 0,
  kShinyOnly = 1,
};

inline const char* FilterName(Filter f) {
  switch (f) {
    case Filter::kNone: return "Todos";
    case Filter::kShinyOnly: return "So shiny";
  }
  return "";
}

// Nome do criterio, para o rodape.
inline const char* SortName(SortBy by) {
  switch (by) {
    case SortBy::kBox: return "Caixa";
    case SortBy::kDex: return "Dex";
    case SortBy::kLevel: return "Nivel";
    case SortBy::kSpecies: return "Especie";
  }
  return "";
}

// Proximo criterio do ciclo, para o botao Y.
inline SortBy NextSort(SortBy by) {
  switch (by) {
    case SortBy::kBox: return SortBy::kDex;
    case SortBy::kDex: return SortBy::kLevel;
    case SortBy::kLevel: return SortBy::kSpecies;
    case SortBy::kSpecies: return SortBy::kBox;
  }
  return SortBy::kBox;
}

// Devolve os indices na ordem pedida.
//
// Em todo criterio que nao seja kBox, os slots VAZIOS vao para o fim: o motivo
// de ordenar e parar de varrer buraco.
//
// std::stable_sort e nao sort: empate preserva a ordem de caixa, entao a lista
// nao embaralha entre reordenacoes do mesmo criterio.
inline std::vector<std::size_t> SortIndices(const std::vector<SortEntry>& items,
                                            SortBy by) {
  std::vector<std::size_t> order;
  order.reserve(items.size());
  for (const auto& it : items) order.push_back(it.index);

  if (by == SortBy::kBox) return order;  // ordem crua, nada a fazer

  // Indexa por posicao no vetor, nao por SortEntry::index: os dois coincidem
  // no uso normal, mas depender disso quebraria com uma lista parcial.
  std::vector<std::size_t> pos(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) pos[i] = i;

  std::stable_sort(pos.begin(), pos.end(), [&](std::size_t a, std::size_t b) {
    const SortEntry& x = items[a];
    const SortEntry& y = items[b];

    // Vazio sempre depois de ocupado. Entre dois vazios, empate — o
    // stable_sort mantem a ordem de caixa.
    if (x.empty != y.empty) return !x.empty;
    if (x.empty) return false;

    switch (by) {
      case SortBy::kDex: return x.dex < y.dex;
      case SortBy::kLevel: return x.level < y.level;
      case SortBy::kSpecies: return x.name < y.name;
      case SortBy::kBox: break;  // tratado acima
    }
    return false;
  });

  std::vector<std::size_t> out;
  out.reserve(pos.size());
  for (std::size_t p : pos) out.push_back(items[p].index);
  return out;
}

// Filtra e ordena, nessa ordem (TD-01 da spec 025): ordenar 420 itens para
// depois descartar quase todos seria desperdicio.
//
// Diferente de SortIndices, aqui o resultado pode ser MENOR que a entrada — a
// UI precisa reagir com paginacao, contador e barra de posicao.
inline std::vector<std::size_t> FilterAndSort(
    const std::vector<SortEntry>& items, Filter filter, SortBy by) {
  if (filter == Filter::kNone) return SortIndices(items, by);

  std::vector<SortEntry> kept;
  kept.reserve(items.size());
  for (const auto& it : items) {
    if (it.empty) continue;  // filtro ativo nunca mostra buraco
    if (filter == Filter::kShinyOnly && !it.shiny) continue;
    kept.push_back(it);
  }
  return SortIndices(kept, by);
}

}  // namespace pokehome::box
