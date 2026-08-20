// Controle remoto da UI (spec 134) — modo `--script`.
//
// O agente nao ve a janela do app: a permissao de tela costuma ser negada, e
// por isso toda task visual ficava "pendente de conferencia humana". Este
// modulo abre um canal de comando: o roteiro dirige a UI, le o estado e
// AFIRMA o resultado com evidencia objetiva.
//
// Duas regras carregadas pelo desenho, e que nao devem ser desfeitas:
//
//   * O botao entra pelo caminho REAL de input (o canal em glfw_input.cpp, o
//     mesmo ponto por onde o teclado entra), nunca por chamada direta ao
//     handler. Um teste que chama o callback direto nao prova que o botao
//     esta ligado na tela.
//   * O `dump` le as MESMAS estruturas que o Refresh() usa para desenhar. Um
//     dump que serializa estado derivado daria falso-verde (TD-02).
//
// So existe no build desktop — no console seria superficie de risco sem uso
// (TD-01). O arquivo inteiro esta sob `#ifndef __SWITCH__`.
#pragma once

#ifndef __SWITCH__

#include <functional>
#include <string>

namespace nestbox::script {

// Estado da tela ativa, em JSON. Quem responde e a Activity do topo, lendo os
// proprios membros — ver `BoxActivity::DumpState()`.
//
// A Activity ativa se registra ao entrar e se desregistra ao sair; sem
// provedor o dump devolve so a identificacao da tela.
using StateProvider = std::function<std::string()>;

// Registra o provedor da tela que acabou de assumir. `name` aparece no dump
// como a chave `activity`, e e o que um `assert activity <nome>` compara.
void SetStateProvider(const char* name, StateProvider provider);

// Desfaz o registro, se `name` ainda for o provedor corrente. Telas empilhadas
// saem fora de ordem; comparar o nome evita que a de baixo apague a de cima.
void ClearStateProvider(const char* name);

// Comandos de TOQUE (spec 133): `tap <x> <y>` e
// `drag <x1> <y1> <x2> <y2>`, em coordenadas de tela do borealis.
//
// O dedo entra por `updateTouchStates` no glfw_input.cpp — o mesmo ponto por
// onde o toque real entra —, e a fase (START/STAY/END) continua sendo
// derivada pelo borealis a partir de `pressed`. O arrasto anda em passos: um
// salto da origem ao destino num frame seria lido como um pulo, e nao como o
// movimento continuo que o PanGesture mede.
//
// O que isto prova e o CAMINHO do gesto ate a acao. A sensacao do toque no
// console continua sendo conferencia do dono (TD-03 da spec 133).

// Nao ha comando `open`: o save entra como argumento normal do app, que e o
// caminho ja validado do fluxo do dono. Reabrir e um SEGUNDO roteiro sobre o
// mesmo arquivo — ver TD-04 da spec 134.

// Carrega o roteiro. Devolve false se o arquivo nao abrir ou tiver comando
// invalido — erro de sintaxe falha o roteiro antes de a UI subir, em vez de
// morrer no meio de um fluxo pela metade.
bool Load(const std::string& path);

// Ha um roteiro em execucao? O `main` so instala o driver se sim.
bool Active();

// Avanca o roteiro em um frame. Chamado uma vez por volta do mainLoop, entre
// os frames — nunca de dentro do borealis, para nao reentrar no laco.
//
// Devolve false quando o roteiro termina (por fim da lista ou por falha); o
// `main` entao encerra o app com o codigo de `ExitCode()`.
bool Step();

// 0 = todo comando passou. 1 = algum `assert` falhou, um comando errou, ou o
// roteiro estourou o limite de frames.
int ExitCode();

}  // namespace nestbox::script

#endif  // __SWITCH__
