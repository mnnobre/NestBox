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

#include "evolucao_troca.h"
#include "game_species.h"
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

  // Jogo do save aberto, na granularidade de `game_species.h` (spec 146).
  //
  // `jogo_ms` acima nao serve para isto: ele agrupa a familia GBA inteira em
  // `kGen3`, e o aviso de "nao volta para o jogo de origem" precisa do jogo
  // exato — Red/Blue perde Slowking, FireRed nao.
  //
  // `kCount` = desconhecido: o aviso e omitido em vez de chutar.
  compat::Game jogo_origem = compat::Game::kCount;

  // Alvo da descida, quando `kind == kGen3`.
  learnset::Game learnset_gen3 = learnset::Game::kFireRed;
  // Codigo de origem gen3 (1..5) para a palavra de origins na descida.
  std::uint8_t origem_gen3 = 0;
};

// Um Pokemon que PODE evoluir por troca ao ser guardado (spec 146).
//
// A evolucao por troca nao e estado no registro — e um EVENTO que o jogo
// dispara durante a troca, e o NestBox escreve direto no save. O jogo nunca
// reavalia. Sem oferecer aqui, um Haunter guardado fica elegivel para sempre
// e nunca evolui.
//
// DEC-2: a pergunta acontece na SAIDA jogo -> NestBox, nao na entrada de um
// jogo. Do contrario o dono teria de guardar, mandar para um jogo, e so entao
// evoluir — um passo a mais sem ganho.
//
// Consequencia assumida: aqui NAO existe jogo de destino para consultar,
// entao `HasSpecies` vira INFORMACAO (quais jogos aceitam o resultado), nao
// trava. Em 52 casos medidos evoluir impede a volta ao jogo de ORIGEM — e por
// isso `origem_aceita_alvo` existe: o dialogo avisa em vez de bloquear.
struct CandidatoEvolucao {
  std::size_t indice = 0;   // posicao em `Plan::nest_writes`
  int dex_base = 0;         // quem entra (ex.: 93, Haunter)
  int dex_alvo = 0;         // quem sai (ex.: 94, Gengar)

  // O jogo de onde ele saiu aceita o EVOLUIDO? Falso = evoluir prende o
  // Pokemon fora do jogo de origem. Nao impede: avisa.
  bool origem_aceita_alvo = true;
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

  // Quem pode evoluir por troca ao ser guardado (spec 146). SO e preenchido
  // no sentido jogo -> NestBox; guardar e a operacao que o jogo leria como
  // troca. A lista e uma OFERTA: o plano nao evolui ninguem sozinho, quem
  // aplica e `AplicaEvolucoes` depois da resposta do dono.
  std::vector<CandidatoEvolucao> candidatos_evolucao;

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

// Aplica as evolucoes que o dono ACEITOU, mutando `plan.nest_writes`.
//
// `aceitos` traz os indices de `plan.candidatos_evolucao` que devem evoluir —
// os demais entram intactos, que e a garantia de que a pergunta nao altera
// quem disse nao.
//
// Separado de `BuildPlan` de proposito: montar o plano e puro e roda sem
// interacao; evoluir depende da resposta humana. Juntar os dois obrigaria o
// BuildPlan a conhecer a UI.
void AplicaEvolucoes(Plan& plan, const std::vector<std::size_t>& aceitos);

}  // namespace pokehome::commit
