// Movimentacao de Pokemon entre caixas — camada de overlay, em memoria.
//
// Como o gen3_save, este header NAO depende de libnx nem do borealis: recebe
// dados, devolve dados. E o que permite testar pegar/soltar/trocar no `ctest`,
// sem abrir janela (ver spec 019).
//
// Modelo (o mesmo do Pokemon HOME, §4.8 da pesquisa): a sessao inteira e
// provisoria. Nada e gravado em disco aqui — o buffer do save nunca e tocado.
// As alteracoes vivem num mapa `{(fonte, caixa, slot) -> conteudo}` consultado
// na leitura. Descartar tudo e limpar o mapa.

#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "gen3_save.h"

namespace pokehome::box {

// Endereco de um slot. A fonte e identificada por um inteiro (0 = painel
// esquerdo, 1 = direito, na tela de duas caixas) em vez de um ponteiro: mantem
// este header livre de qualquer tipo da UI.
struct SlotRef {
  int source = 0;
  std::size_t box = 0;
  std::size_t slot = 0;

  // Necessario para usar SlotRef como chave de std::map.
  bool operator<(const SlotRef& o) const {
    return std::tie(source, box, slot) < std::tie(o.source, o.box, o.slot);
  }
  bool operator==(const SlotRef& o) const {
    return source == o.source && box == o.box && slot == o.slot;
  }
};

using Pokemon = gen3::BoxPokemon;

// Modos de cursor, como no Pokemon HOME (§5 da pesquisa). O que muda e o que
// o botao A faz (spec 031).
enum class CursorMode {
  kMove,    // pega e solta; sobre ocupado NAO faz nada
  kSwap,    // pega e, sobre ocupado, troca os dois
  kSelect,  // marca varios e move em bloco
};

inline const char* CursorModeName(CursorMode m) {
  switch (m) {
    case CursorMode::kMove: return "Mover";
    case CursorMode::kSwap: return "Trocar";
    case CursorMode::kSelect: return "Selecionar";
  }
  return "";
}

// ZR avanca no ciclo, ZL volta.
inline CursorMode NextMode(CursorMode m) {
  switch (m) {
    case CursorMode::kMove: return CursorMode::kSwap;
    case CursorMode::kSwap: return CursorMode::kSelect;
    case CursorMode::kSelect: return CursorMode::kMove;
  }
  return CursorMode::kMove;
}

inline CursorMode PrevMode(CursorMode m) {
  switch (m) {
    case CursorMode::kMove: return CursorMode::kSelect;
    case CursorMode::kSwap: return CursorMode::kMove;
    case CursorMode::kSelect: return CursorMode::kSwap;
  }
  return CursorMode::kMove;
}

// Um slot vazio e representado por um Pokemon com species == 0, que e como o
// proprio formato gen3 marca "vazio" (gen3_save.h:66).
inline bool IsEmpty(const Pokemon& p) { return p.species == 0; }

// Estado de uma sessao de movimentacao.
//
// Uso: a UI pergunta `Get(ref, original)` para desenhar cada celula, passando o
// que a fonte original devolveria. O overlay decide se ha alteracao pendente.
class MoveSession {
 public:
  // Conteudo efetivo de um slot: a alteracao pendente, se houver; senao o que
  // a fonte original tem.
  //
  // Devolve por VALOR, nao por referencia. `BoxSource::At()` devolve um
  // TEMPORARIO, e a chamada natural e
  //
  //     auto mon = session.Get(ref, fonte->At(box, slot));
  //
  // Com retorno por referencia, o `original` morre no fim da expressao e o
  // chamador copia de memoria liberada quando o slot NAO tem alteracao
  // pendente.
  //
  // Foi o crash que o dono relatou NO CONSOLE em 2026-08-21: mover um Haunter
  // para o NestBox, evoluir e abrir o detalhe fechava o app; fechar e reabrir
  // "resolvia", porque ai a leitura vinha da fonte sem passar por aqui.
  //
  // A copia e barata perto de um use-after-free. E ele NAO reproduz em
  // qualquer maquina: depende de o alocador reusar a memoria liberada, e isso
  // muda entre Switch, emulador e PC. Nao adianta procurar um teste que falhe
  // aqui — o `ctest` passava com o defeito no lugar.
  Pokemon Get(const SlotRef& ref, const Pokemon& original) const {
    auto it = changes_.find(ref);
    return it == changes_.end() ? original : it->second;
  }

  bool Holding() const { return held_.has_value(); }

  // Substitui o conteudo de um slot JA alterado nesta sessao (spec 146).
  //
  // Existe para a evolucao por troca: quando o dono aceita evoluir, o Pokemon
  // que acabou de ser solto no NestBox vira o evoluido — ainda no overlay,
  // sem nada gravado. Trocar aqui e o que faz a caixa mostrar o Gengar na
  // hora, e o commit gravar o Gengar depois.
  //
  // NAO cria alteracao onde nao havia: sem entrada em `changes_` a chamada e
  // ignorada. Assim isto nao vira uma porta para escrever em slot que o
  // usuario nao tocou.
  void Replace(const SlotRef& ref, const Pokemon& mon) {
    auto it = changes_.find(ref);
    if (it == changes_.end()) return;
    it->second = mon;
  }

  // O Pokemon na mao. So chamar com Holding() == true.
  const Pokemon& Held() const { return *held_; }

  // De onde o Pokemon na mao saiu — para o cancelamento saber onde devolver.
  const SlotRef& HeldFrom() const { return held_from_; }

  // Pega o Pokemon de `ref`. `current` e o conteudo efetivo daquele slot (ja
  // passado por Get). Falha se a mao ja estiver cheia ou o slot vazio.
  bool Pick(const SlotRef& ref, const Pokemon& current) {
    if (Holding() || IsEmpty(current)) return false;
    // Copia antes de escrever, pelo mesmo motivo documentado em Drop():
    // `current` pode ser uma referencia para dentro de changes_.
    held_ = current;  // std::optional copia por valor
    held_from_ = ref;
    changes_[ref] = Pokemon{};  // origem fica vazia
    return true;
  }

  // Solta no slot `ref`. Se `current` estiver ocupado, TROCA os dois de lugar
  // e a mao esvazia. Se estiver vazio, move e a mao esvazia.
  //
  // A troca FECHA (spec 087): o ocupante do destino vai para o slot de onde o
  // Pokemon da mao saiu (`held_from_`), e nao para a mao. Ate a spec 086 o
  // ocupante ficava na mao, o que obrigava a um segundo gesto para larga-lo e
  // — se o jogador o soltasse na origem, como e natural — produzia o mesmo
  // resultado por dois passos. Um gesto, uma troca.
  //
  // `accepts` e a resposta de BoxSource::CanAccept() do destino — uma fonte
  // somente leitura recusa receber.
  // `allow_swap` = false recusa soltar sobre slot ocupado, em vez de trocar.
  // E o que diferencia o modo Mover do modo Trocar (spec 031).
  bool Drop(const SlotRef& ref, const Pokemon& current, bool accepts,
            bool allow_swap = true) {
    if (!Holding() || !accepts) return false;
    if (!allow_swap && !IsEmpty(current)) return false;
    // Soltar no proprio slot de origem: desfaz o gesto, sem trocar nada com
    // ninguem. Sem este caso o codigo abaixo escreveria o ocupante por cima do
    // Pokemon da mao no MESMO slot, e um dos dois sumiria.
    if (ref == held_from_) {
      changes_[ref] = *held_;
      held_.reset();
      return true;
    }

    // COPIA, nao referencia. `current` costuma vir de Get(), que devolve uma
    // referencia para dentro de changes_ — e a escrita logo abaixo sobrescreve
    // exatamente essa entrada. Ler `current` depois disso devolveria o valor
    // recem-escrito, e a troca nunca fecharia (o teste "a mao esvazia e a
    // troca fecha" pegou isso).
    const Pokemon previous = current;

    changes_[ref] = *held_;
    // O destino de quem estava aqui: o slot de onde o da mao saiu. Vazio nao
    // precisa de escrita — Pick() ja deixou a origem vazia no overlay.
    if (!IsEmpty(previous)) changes_[held_from_] = previous;
    held_.reset();
    return true;
  }

  // Devolve o Pokemon da mao ao slot de onde saiu.
  bool Cancel() {
    if (!Holding()) return false;
    changes_[held_from_] = *held_;
    held_.reset();
    return true;
  }

  // Quantos Pokemon a fonte `source` tem, considerando as alteracoes.
  // `original` e a contagem que a fonte relata sem overlay; `original_at`
  // devolve o que a fonte tinha num slot (para saber se a alteracao encheu ou
  // esvaziou aquele lugar).
  //
  // Percorre o mapa inteiro: ele tem no maximo o numero de slots mexidos na
  // sessao (dezenas), nao o tamanho das caixas.
  template <typename OriginalAt>
  std::size_t Count(int source, std::size_t original,
                    OriginalAt original_at) const {
    std::size_t n = original;
    for (const auto& [ref, mon] : changes_) {
      if (ref.source != source) continue;
      const bool was_empty = IsEmpty(original_at(ref));
      const bool is_empty = IsEmpty(mon);
      if (was_empty && !is_empty) ++n;
      if (!was_empty && is_empty) --n;
    }
    return n;
  }

  // --- Multissselecao (spec 021) -------------------------------------------
  //
  // Um conjunto de slots marcados, movidos juntos. Independente da "mao": ou
  // se segura um Pokemon, ou se marca um bloco.

  // Marca/desmarca. Slot vazio nao entra — marcar o nada nao faz sentido e
  // atrapalharia a contagem do bloco.
  void ToggleSelect(const SlotRef& ref, const Pokemon& current) {
    if (IsEmpty(current)) return;
    auto it = selected_.find(ref);
    if (it == selected_.end()) {
      selected_.insert(ref);
    } else {
      selected_.erase(it);
    }
  }

  bool IsSelected(const SlotRef& ref) const {
    return selected_.count(ref) > 0;
  }

  std::size_t SelectedCount() const { return selected_.size(); }

  void ClearSelection() { selected_.clear(); }

  const std::set<SlotRef>& selection() const { return selected_; }

  // Move o bloco marcado para `dest_source`/`dest_box`, ocupando os slots
  // livres a partir de `dest_slot` em diante.
  //
  // `slot_count` e quantos slots a caixa de destino tem. `effective_at` devolve
  // o conteudo efetivo de um slot do destino (fonte + overlay), para saber o
  // que esta livre. `source_at` faz o mesmo para as origens.
  //
  // Se nao couber tudo, move o que couber e MANTEM marcados os que sobraram —
  // ver TD-02 da spec 021. Devolve quantos foram movidos.
  template <typename EffectiveAt>
  std::size_t MoveSelection(int dest_source, std::size_t dest_box,
                            std::size_t dest_slot, std::size_t slot_count,
                            bool accepts, EffectiveAt effective_at) {
    if (selected_.empty() || !accepts) return 0;

    // Copia o conteudo antes de escrever: as origens serao esvaziadas, e
    // escrever no overlay invalida o que effective_at devolveria depois.
    std::vector<std::pair<SlotRef, Pokemon>> pending;
    pending.reserve(selected_.size());
    for (const SlotRef& ref : selected_) {
      pending.emplace_back(ref, effective_at(ref));
    }

    std::size_t moved = 0;
    std::size_t slot = dest_slot;
    for (const auto& [from, mon] : pending) {
      // Procura o proximo slot livre no destino.
      while (slot < slot_count) {
        const SlotRef to{dest_source, dest_box, slot};
        if (IsEmpty(effective_at(to))) break;
        ++slot;
      }
      if (slot >= slot_count) break;  // acabou o espaco

      const SlotRef to{dest_source, dest_box, slot};
      changes_[to] = mon;
      changes_[from] = Pokemon{};
      selected_.erase(from);
      ++moved;
      ++slot;
    }
    return moved;
  }

  // Move um bloco PRESERVANDO A FORMA (spec 088), ancorado no canto superior
  // esquerdo da area de origem.
  //
  // Diferente do MoveSelection acima em duas regras que o dono trocou:
  //   - a forma e preservada: um buraco no meio do retangulo continua buraco
  //     no destino, em vez de o bloco ser compactado nos livres;
  //   - e TUDO-OU-NADA: se um so slot do destino estiver ocupado, ou se a
  //     forma passar da borda da caixa, nada se move.
  //
  // `cols`/`rows` sao a geometria da caixa de destino. `top_left` e o slot
  // onde o canto superior esquerdo da area vai pousar. `shape` traz os
  // deslocamentos (dr, dc) relativos a esse canto, ja sem os vazios.
  //
  // Devolve quantos foram movidos: 0 = recusou, e nesse caso nada foi escrito.
  template <typename EffectiveAt>
  std::size_t MoveBlock(int dest_source, std::size_t dest_box,
                        std::size_t dest_row, std::size_t dest_col,
                        std::size_t rows, std::size_t cols, bool accepts,
                        const std::vector<std::pair<SlotRef, std::pair<int, int>>>& shape,
                        EffectiveAt effective_at) {
    if (shape.empty() || !accepts) return 0;

    // As origens sao destino de ninguem alem delas mesmas: um slot que faz
    // parte do bloco nao conta como "ocupado" ao conferir o destino, senao
    // mover um bloco duas casas para o lado seria sempre recusado por ele
    // mesmo.
    std::set<SlotRef> origens;
    for (const auto& [from, _] : shape) origens.insert(from);

    // CONFERE TUDO antes de escrever — tudo-ou-nada de verdade.
    std::vector<std::pair<SlotRef, SlotRef>> plano;  // (de, para)
    plano.reserve(shape.size());
    for (const auto& [from, delta] : shape) {
      const long r = static_cast<long>(dest_row) + delta.first;
      const long c = static_cast<long>(dest_col) + delta.second;
      if (r < 0 || c < 0 || r >= static_cast<long>(rows) ||
          c >= static_cast<long>(cols)) {
        return 0;  // a forma passou da borda da caixa
      }
      const SlotRef to{dest_source, dest_box,
                       static_cast<std::size_t>(r) * cols +
                           static_cast<std::size_t>(c)};
      if (!origens.count(to) && !IsEmpty(effective_at(to))) {
        return 0;  // tem alguem no caminho que nao e do proprio bloco
      }
      plano.emplace_back(from, to);
    }

    // Copia o conteudo ANTES de escrever: as origens serao esvaziadas, e
    // escrever no overlay invalida o que effective_at devolveria depois.
    std::vector<Pokemon> carga;
    carga.reserve(plano.size());
    for (const auto& [from, to] : plano) carga.push_back(effective_at(from));

    // Esvazia todas as origens primeiro, depois preenche os destinos. Nesta
    // ordem um destino que era origem de outro do mesmo bloco nao e apagado
    // depois de preenchido.
    for (const auto& [from, to] : plano) changes_[from] = Pokemon{};
    for (std::size_t i = 0; i < plano.size(); ++i) {
      changes_[plano[i].second] = carga[i];
    }
    selected_.clear();
    return plano.size();
  }

  // Ha algo pendente? A tela usa para decidir se avisa ao sair.
  bool Dirty() const { return !changes_.empty() || Holding(); }

  // Descarta tudo — o equivalente a sair do HOME sem salvar.
  void Discard() {
    changes_.clear();
    held_.reset();
    selected_.clear();
  }

  const std::map<SlotRef, Pokemon>& changes() const { return changes_; }

 private:
  std::map<SlotRef, Pokemon> changes_;
  std::optional<Pokemon> held_;
  SlotRef held_from_;
  std::set<SlotRef> selected_;
};

}  // namespace pokehome::box
