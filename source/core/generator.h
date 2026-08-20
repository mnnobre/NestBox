// Gerador de Pokemon — o nucleo de regra da tela (spec 144).
//
// O QUE ESTE MODULO E, E O QUE ELE NAO E (TD-06):
//
// O projeto JA tem um gerador consistente. Quando um Pokemon sai da NestBox
// para outro jogo, ele e RECRIADO no formato de destino:
//
//     pkm::Convert          reescreve o layout
//     pkm::AjustesDeEntrada corrige o que o jogo de destino espera
//     commit::AplicaEntradaNoDestino  atribui tracker, handler, moveset, PP
//     <fmt>::Write          serializa
//
// A UNICA diferenca entre aquele caminho e este e DE ONDE VEM OS CAMPOS: la
// vem de um Pokemon existente, aqui vem da tela. Tudo o mais e o mesmo
// problema, e reimplementa-lo significaria manter dois codigos e corrigir
// cada bug duas vezes.
//
// Por isso este modulo NAO contem conversao de formato, NAO contem ajuste de
// entrada e NAO chama `Write`. Ele so:
//
//   1. guarda o estado do formulario           (GeneratorState)
//   2. responde "isto esta coerente?"          (Verify)
//   3. sabe consertar o que apontou            (ApplyFix)
//   4. monta um pkm::Pokemon a partir do molde (Build)
//
// e entrega ao caminho existente. Um `Build()` que chamasse `Write` por conta
// propria pularia tracker e handler — exatamente os dois campos que a
// spec 143 teve de consertar — e repetiria o bug do ovo por um caminho novo.
// Medido: evidence-log da spec 144, secao "o nosso Convert tem um bug?".
//
// Se algum caso do gerador nao for coberto pelo caminho existente, isso e
// REPORTADO ao dono, nao resolvido com codigo paralelo.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "learnset.h"
#include "personal_tables.h"
#include "pkm_model.h"

namespace pokehome::generator {

inline constexpr std::uint16_t kEvTotalMax = 510;
inline constexpr std::uint8_t kEvStatMax = 252;
inline constexpr std::uint8_t kIvMax = 31;

// As 4 secoes da tela. `Issue::section` usa isto para a aba certa acender.
enum class Section : std::uint8_t {
  kEspecie = 0,
  kOrigem = 1,
  kStats = 2,
  kGolpes = 3,
};

// Severidade. A tela pinta vermelho/ambar e o botao Criar so trava no
// vermelho — aviso nao bloqueia (regra da tela no mock v6).
enum class Severity : std::uint8_t { kErro, kAviso };

// Um problema encontrado. `code` e estavel para teste e para ApplyFix;
// `reason` e o texto que a tela mostra.
struct Issue {
  std::string code;
  std::string reason;
  Severity severity = Severity::kErro;
  Section section = Section::kEspecie;
  // Rotulo curto da correcao sugerida ("Trocar para Blaze"), vazio quando o
  // problema nao tem conserto automatico.
  std::string fix_label;
};

// O estado do formulario — um campo por controle da tela.
struct GeneratorState {
  // --- Especie
  std::uint16_t dex = 25;   // National Dex
  std::uint8_t form = 0;
  std::uint8_t level = 5;
  bool shiny = false;
  std::uint8_t gender = 0;  // 0=M 1=F 2=sem genero
  std::uint8_t nature = 0;
  std::uint8_t ability_slot = 1;  // 1, 2 ou 4 (oculta) — convencao do PkHeX
  std::uint16_t held_item = 0;
  std::uint8_t ball = 4;    // Poke Ball

  // --- Origem
  personal::Jogo jogo = personal::Jogo::kSV;
  std::uint8_t met_level = 5;
  std::string ot_name = "NestBox";
  std::uint16_t tid = 0;
  std::uint16_t sid = 0;
  std::uint8_t met_year = 26, met_month = 1, met_day = 1;  // ano-2000
  std::uint8_t language = 2;  // ENG

  // --- Stats
  std::uint8_t ivs[6] = {0, 0, 0, 0, 0, 0};  // HP,Atk,Def,Spe,SpA,SpD
  std::uint8_t evs[6] = {0, 0, 0, 0, 0, 0};

  // --- Golpes
  std::uint16_t moves[4] = {0, 0, 0, 0};
};

// O formato em que aquele jogo guarda seus Pokemon.
pkm::Format FormatOf(personal::Jogo jogo);

// A entrada da tabela personal para (dex, form) naquele jogo. nullptr se a
// especie nao existe la — que e como o gerador sabe recusar.
const personal::EntryFull* PersonalOf(const GeneratorState& s);

// --- Orcamento de EV -------------------------------------------------------
// O jogo nao aceita passar de 510 no total nem de 252 por stat. A tela nao
// deixa digitar e reclamar depois: o campo respeita o teto na entrada, e o
// excedente e devolvido na hora.

std::uint16_t EvTotal(const GeneratorState& s);

// Poe `valor` no EV do indice `i` respeitando os dois tetos. Devolve o valor
// que REALMENTE ficou — a tela usa a diferenca para avisar o jogador.
std::uint8_t SetEv(GeneratorState& s, int i, int valor);

// Idem para IV (teto 31, sem orcamento total).
std::uint8_t SetIv(GeneratorState& s, int i, int valor);

// --- O mapa por jogo -------------------------------------------------------
//
// O jogo escolhido decide TRES coisas independentes: o formato do registro, a
// tabela personal e a tabela de learnset. Os tres enums nao estao na mesma
// ordem, entao o mapeamento e explicito.
//
// Exportado (spec 145) porque o gerador de LOTE (tests/make_batch.cpp) tomava
// as mesmas decisoes por conta propria, com ternarios `kPA8 ? PLA : BDSP`.
// Isso so funciona com dois jogos: Z-A e SV compartilham o formato kPK9, e o
// formato deixa de distinguir. Duas copias da mesma tabela divergem em
// silencio — a licao do bug do ovo (spec 143).
struct MapaJogo {
  personal::Jogo jogo;
  pkm::Format formato;
  learnset::Game learnset;
  bool tem_learnset;         // nem todo jogo tem tabela de learnset no repo
  std::uint8_t origin_game;  // GameVersion do PkHeX, gravado no registro
};

// nullptr se o jogo nao esta mapeado.
const MapaJogo* Mapa(personal::Jogo jogo);

// --- Verificacao -----------------------------------------------------------
// Duas fontes, deliberadamente separadas:
//
//   1. `legality::CheckLegality` sobre o Pokemon montado — coerencia interna,
//      a mesma regua que o resto do app usa (spec 079).
//   2. as checagens abaixo, que o FORMULARIO conhece e o verificador nao:
//      elas dependem do jogo de origem escolhido, informacao que nao existe
//      dentro de um pkm ja montado.
//
// O que NAO esta aqui: validacao de ENCONTRO (metodo, local, piso de IV por
// encontro, disponibilidade de habilidade oculta). Exige o encounter DB do
// PkHeX, que docs/pesquisa-verificador-legalidade.md concluiu ser inviavel
// portar. Fingir essa checagem seria pior que nao te-la.
std::vector<Issue> Verify(const GeneratorState& s);

// O nivel de encontro do MOLDE daquele jogo — o unico par (local, nivel) que
// sabemos ser valido, porque o PkHeX aprovou o molde inteiro.
//
// Existe para a tela poder AVISAR: mudar o nivel de encontro faz o PkHeX
// acusar "Unable to match an encounter from origin game", porque o local
// herdado do molde so tem encontro naquele nivel. Sem o encounter DB nao
// temos como oferecer os outros pares validos — mas temos como dizer qual e
// o unico que conhecemos, em vez de deixar o jogador descobrir depois.
// Devolve 0 se nao houver molde para o jogo.
std::uint8_t MetLevelDoMolde(personal::Jogo jogo);

// Os golpes que a especie aprende por nivel naquele jogo, ate `nivel`.
// Preenche `out[4]` e devolve quantos (0..4) — a mesma fonte que `ApplyFix`
// usa, para a lista da tela nunca oferecer um golpe que a verificacao
// reprovaria em seguida. 0 se o jogo nao tem tabela de learnset.
int GolpesAteNivel(const GeneratorState& s, std::uint8_t nivel,
                   std::uint16_t out[4]);

// Aplica a correcao sugerida daquele `code`. Devolve false se o code nao tem
// conserto automatico (ou nao existe) — a tela nao oferece o botao nesse caso.
bool ApplyFix(GeneratorState& s, const std::string& code);

// --- Montagem --------------------------------------------------------------

// Monta o Pokemon a partir do MOLDE do formato de destino (TD-04) e dos
// campos da tela.
//
// O molde e o ponto de partida porque `Write()` parte de `p.raw` e, com raw
// vazio, deixa em zero todo offset que o `Parse` ainda nao mapeia. Onde a
// transferencia recebe um registro real de entrada, o gerador recebe o molde.
//
// Devolve nullopt se a especie nao existe no jogo escolhido ou se nao ha
// molde para o formato — montar do zero e o bug que TD-04 existe para impedir.
//
// O QUE ESTA FUNCAO NAO FAZ, de proposito: nao converte formato, nao aplica
// ajustes de entrada e nao serializa. Quem deposita chama, na ordem,
// `pkm::AjustesDeEntrada` e `commit::AplicaEntradaNoDestino` — o mesmo
// caminho da transferencia (TD-06).
std::optional<pkm::Pokemon> Build(const GeneratorState& s);

}  // namespace pokehome::generator
