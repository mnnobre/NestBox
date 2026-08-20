// Decisao pura do commit (spec 128).
//
// `CommitNestBox` (src/ui/main.cpp) concentra a logica mais critica do app —
// ordem de gravacao, rollback, conversao entre geracoes, memoria de moveset —
// e era a unica parte dela sem teste automatizado: tudo o que ele CHAMA tem
// ctest, mas o fio que costura nao tinha.
//
// Este modulo extrai a parte que DECIDE, sem nenhum I/O: recebe as alteracoes
// da sessao e devolve um PLANO — o que gravar no banco, o que gravar no save,
// ja convertido — ou um erro descritivo. Quem executa (e quem faz backup,
// escrita e rollback) continua sendo a UI, porque so la essas coisas podem
// falhar de verdade (TD-01 da spec).
//
// O que NAO esta aqui, de proposito:
//   * arquivo, borealis, dynamic_cast de fonte;
//   * a ORDEM de gravacao (banco -> backup -> save -> rollback). Ela e a regra
//     que elimina a janela de perda da spec 106 e continua na UI, onde as
//     falhas acontecem.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gen3_save.h"
#include "learnset.h"
#include "modern_box_view.h"
#include "moveset_memory.h"
#include "pkm_model.h"

namespace pokehome::commit {

// Uma alteracao pendente, como a sessao a entrega: de qual lado veio, e o
// conteudo novo do slot (vazio = limpar).
//
// `to_nest` substitui o `ref.source == kNestId` da UI: o plano nao precisa
// conhecer os ids dos paineis, so de que lado a alteracao cai.
struct Change {
  bool to_nest = false;
  std::size_t box = 0;
  std::size_t slot = 0;
  gen3::BoxPokemon mon;
};

// O que o save aberto aceita. `kNenhum` = nao ha save gravavel; alteracoes no
// lado do save viram erro.
enum class SaveKind : std::uint8_t { kNenhum, kGen3, kModerno };

// Tudo o que a decisao precisa saber do save aberto, ja resolvido pela UI.
// Passar os valores em vez das fontes e o que mantem este modulo livre de
// SaveSource/ModernSaveSource — e o que torna o teste possivel sem UI.
struct SaveInfo {
  SaveKind kind = SaveKind::kNenhum;

  // Formato de destino, quando `kind == kModerno`.
  pkm::Format formato = pkm::Format::kPK9;
  // Jogo do save, na memoria de moveset (para ApplyOnEntry/RestoreOnBank).
  moveset::Game jogo_ms = moveset::Game::kSwSh;
  // Treinador do save aberto. Vira o HT de quem chega de outro jogo (spec
  // 143); vazio deixa o `ht_name` como esta.
  std::string trainer_name;

  // Alvo da descida, quando `kind == kGen3`.
  learnset::Game learnset_gen3 = learnset::Game::kFireRed;
  // Codigo de origem gen3 (1..5) para a palavra de origins na descida.
  std::uint8_t origem_gen3 = 0;
};

// O plano: o que executar, ja decidido e convertido.
struct Plan {
  // Vazio = o plano e valido. Preenchido = abortar com esta mensagem, sem
  // gravar NADA (as conversoes rodam antes de qualquer escrita, exatamente
  // para que a falha custe zero).
  std::string error;

  // Escritas no banco do NestBox, na ordem. `mon` vazio = limpar o slot.
  std::vector<Change> nest_writes;

  // Alteracoes do save gen3, por indice linear (box * 30 + slot) — o formato
  // que `SaveSource::WriteChanges` espera.
  std::map<std::size_t, gen3::BoxPokemon> save_changes;

  // As mesmas alteracoes no formato do save moderno.
  std::vector<view::BoxChange> modern_changes;

  bool ok() const { return error.empty(); }
  bool touches_save() const {
    return !save_changes.empty() || !modern_changes.empty();
  }
};

// Monta o plano. `memory` E MUTADA: a conversao memoriza o moveset do jogo de
// origem, e essa memoria faz parte do que sera gravado no mesmo ciclo — por
// isso ela entra por ponteiro e nao por copia.
//
// Um erro devolve `Plan` com `error` preenchido e as listas em estado
// indefinido: quem chama deve testar `ok()` antes de qualquer escrita.
// O que todo Pokemon recebe ao ENTRAR num save moderno: tracker do HOME,
// reset/restauracao do moveset, PP e handler.
//
// Exposto (spec 143) porque QUALQUER coisa que escreva num save moderno tem
// de passar por aqui. A matriz de rotas chamava so `pkm::Convert` +
// `AjustesDeEntrada` e produzia exatamente os defeitos que esta spec
// corrigiu — um segundo caminho de escrita, o bug que ela existe para
// eliminar. Se voce esta gravando um Pokemon num save, chame isto.
void AplicaEntradaNoDestino(pkm::Pokemon& mon, const SaveInfo& save,
                            moveset::Memory* memory);

Plan BuildPlan(const std::vector<Change>& changes, const SaveInfo& save,
               moveset::Memory* memory);

}  // namespace pokehome::commit
