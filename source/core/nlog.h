// Log de diagnostico (spec 083).
//
// O agente que desenvolve este projeto NAO ve a tela. Sem log, todo defeito
// relatado pelo dono chega como memoria ("acho que apertei B e sumiu"), que e
// a pior fonte de verdade possivel num app que mexe em save de jogo. Com log,
// o rastro fala.
//
// Duas categorias, e a distincao e o que torna o arquivo legivel:
//
//   [NAV]  o que o USUARIO fez — entrou na Pokedex, apertou B, trocou de caixa
//          com R, moveu o cursor. Responde "como cheguei aqui". Volumoso.
//   [ACT]  o que o APP fez com o dado — moveu do slot X para Y, gravou no
//          cartao, criou backup, falhou ao ler. Responde "o que aconteceu com
//          meus Pokemon". Enxuto e critico.
//
// Filtrar depois e so `grep '\[ACT\]'`.
//
// ---------------------------------------------------------------------------
// COMO DESLIGAR NA HORA DO RELEASE — UMA LINHA
// ---------------------------------------------------------------------------
//
//   Troque o `#define NESTBOX_LOG_LEVEL 2` abaixo por `0`.
//   (ou passe -DNESTBOX_LOG_LEVEL=0 no CMake, que tem precedencia)
//
//   2 = ACT + NAV (tudo — o que o dono pediu enquanto testa)
//   1 = so ACT    (so o que mexeu em dado)
//   0 = nada      (as chamadas viram no-op; nenhum arquivo e criado)
//
// Nao ha um segundo lugar para procurar. Este e o ponto unico, de proposito:
// o requisito da spec 083 e que ninguem precise cacar chamadas espalhadas
// antes de liberar a versao.
//
// ---------------------------------------------------------------------------
//
// I/O NAO MORA AQUI. Como o resto do core (ver save_backup.h: "o I/O fica na
// UI, que e quem conhece o cartao"), este modulo so formata, conta bytes e
// decide QUANDO rotacionar. Quem abre arquivo e a UI, por um sink instalado
// em `SetSink`.
//
// O carimbo e RELATIVO ao inicio da sessao e contado em FRAMES, nao no relogio
// do sistema: `armGetSystemTick()` (CNTVCT_EL0) derruba o app no Ryujinx, e
// toda API de tempo do borealis desce por ele. A UI chama `Tick()` uma vez por
// volta do mainLoop. Ver TD-02 da spec 083.

#pragma once

#include <cstddef>
#include <functional>
#include <string>

#ifndef NESTBOX_LOG_LEVEL
#define NESTBOX_LOG_LEVEL 2
#endif

namespace pokehome::nlog {

// Teto por arquivo. 2 MB e barato num cartao de Switch e ainda cabe inteiro na
// leitura de um agente; sem teto, uma sessao longa em modo NAV enche o cartao.
inline constexpr std::size_t kMaxBytes = 2 * 1024 * 1024;

// Quantos arquivos sobrevivem: o corrente + kKeepRotated antigos.
inline constexpr int kKeepRotated = 2;

enum class Cat { kNav, kAct };

// Recebe uma linha ja formatada e terminada em '\n'. A UI instala o dela.
using Sink = std::function<void(const std::string&)>;

// Pedido de rotacao: o core avisa que o arquivo corrente estourou o teto e a
// UI faz o renomeio (ela e quem conhece o cartao). `keep` e quantos antigos
// manter.
using Rotator = std::function<void(int keep)>;

void SetSink(Sink sink);
void SetRotator(Rotator rotator);

// Bytes ja escritos no arquivo corrente. A UI informa o tamanho de um log
// preexistente ao abrir, senao a rotacao reiniciaria a contagem a cada
// execucao e o arquivo cresceria sem limite entre sessoes.
void SetWrittenBytes(std::size_t bytes);
std::size_t WrittenBytes();

// Uma volta do mainLoop. `frames` permite adiantar mais de um de uma vez.
void Tick(unsigned frames = 1);

// Segundos desde o inicio da sessao, a 60 FPS.
double Seconds();

// Formata e entrega ao sink. Nao chame direto — use Nav()/Act(), que somem
// quando NESTBOX_LOG_LEVEL desliga a categoria.
// `gnu_printf`, nao `printf`: no MinGW o `printf` do atributo segue as regras do
// MSVCRT antigo, que NAO conhece %zu — e o gate -Wall -Wextra reprovava toda
// linha que imprime um tamanho. O runtime aqui e o ucrt, cujo vsnprintf aceita
// %zu normalmente. Trocar o %zu por cast seria mascarar o aviso corrigindo o
// lado errado.
void Emit(Cat cat, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(gnu_printf, 2, 3)))
#endif
    ;

// Monta a linha sem entregar a lugar nenhum. Existe para o teste conferir o
// formato sem depender do sink.
std::string Format(Cat cat, double seconds, const std::string& message);

#if NESTBOX_LOG_LEVEL >= 2
#define NLOG_NAV(...) ::pokehome::nlog::Emit(::pokehome::nlog::Cat::kNav, __VA_ARGS__)
#else
#define NLOG_NAV(...) ((void)0)
#endif

#if NESTBOX_LOG_LEVEL >= 1
#define NLOG_ACT(...) ::pokehome::nlog::Emit(::pokehome::nlog::Cat::kAct, __VA_ARGS__)
#else
#define NLOG_ACT(...) ((void)0)
#endif

}  // namespace pokehome::nlog
