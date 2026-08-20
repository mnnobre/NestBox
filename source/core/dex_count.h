// Contagem de Pokedex sobre varias fontes (spec 026).
//
// Como box_move.h, isto e logica pura — testavel por ctest sem
// abrir janela.
//
// O motivo de existir: a dex contava so o save aberto e ignorava o NestBox,
// entao mover um Pokemon de um painel para o outro o fazia sumir da Pokedex.

#pragma once

#include <cstddef>
#include <vector>

namespace pokehome::dex {

// De onde uma especie foi vista nesta sessao. Bitmask: uma especie pode estar
// nos dois lugares ao mesmo tempo.
enum Origin : unsigned {
  kNowhere = 0,
  kSave = 1,
  kNest = 2,
};

// Registro por numero de dex. O indice e o numero nacional; a posicao 0 nao e
// usada (nao existe dex 0), o que evita subtrair 1 em todo acesso.
class DexTally {
 public:
  explicit DexTally(std::size_t max_dex) : seen_(max_dex + 1, kNowhere) {}

  // Marca uma especie como presente. Dex fora da faixa e ignorado em silencio:
  // fonte de outra geracao (ZA vai ate 1025) nao deve estourar a contagem.
  void Add(int dex, Origin from) {
    if (dex <= 0 || static_cast<std::size_t>(dex) >= seen_.size()) return;
    seen_[dex] = static_cast<Origin>(seen_[dex] | from);
  }

  bool Has(int dex) const {
    if (dex <= 0 || static_cast<std::size_t>(dex) >= seen_.size()) return false;
    return seen_[dex] != kNowhere;
  }

  Origin OriginOf(int dex) const {
    if (dex <= 0 || static_cast<std::size_t>(dex) >= seen_.size()) return kNowhere;
    return seen_[dex];
  }

  // Quantas especies distintas foram registradas. A uniao e natural: uma
  // especie presente nos dois paineis ocupa uma posicao so.
  std::size_t Count() const {
    std::size_t n = 0;
    for (Origin o : seen_) {
      if (o != kNowhere) ++n;
    }
    return n;
  }

  std::size_t CountFrom(Origin from) const {
    std::size_t n = 0;
    for (Origin o : seen_) {
      if (o & from) ++n;
    }
    return n;
  }

 private:
  std::vector<Origin> seen_;
};

}  // namespace pokehome::dex
