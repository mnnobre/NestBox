// Regras de transferencia (spec 070, G07+G09 da descoberta
// escrita-e-transferencia).
//
// Uma pergunta so: "este Pokemon pode entrar neste jogo, e o que muda quando
// entra?". A fonte e a §7 de docs/pesquisa-pokemon-home.md.
//
// A logica mora aqui, e nao em src/ui/main.cpp, porque o gate do projeto e o
// ctest: regra de negocio dentro da UI e regra que ninguem testa.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game_species.h"
#include "pkm_model.h"

namespace pokehome::rules {

namespace cp = pokehome::compat;

enum class Verdict {
  kAllowed,   // entra sem ressalva
  kWarning,   // entra, mas algo muda (golpe que o destino nao conhece)
  kBlocked,   // nao entra
};

struct RuleResult {
  Verdict verdict = Verdict::kAllowed;
  // Legivel, para a tela mostrar direto. Vazio quando kAllowed.
  std::string reason;

  bool allowed() const { return verdict != Verdict::kBlocked; }
};

// O que a regra precisa saber sobre o SAVE DE DESTINO.
//
// TD-01 da spec 070: a lista de especies presentes, e nao o save inteiro. A
// unica regra que olha o destino e a do BDSP ("so 1 Lendario/Mitico de cada
// por save"), e para ela basta saber se aquela especie ja esta la. Passar o
// save acoplaria as regras ao formato de cada jogo.
//
// Quem chama monta a lista varrendo as caixas do destino (National Dex de
// cada Pokemon). Lista vazia = save vazio, que e o padrao seguro para quem
// so quer a regra geral.
//
struct SaveContext {
  std::vector<std::uint16_t> species_present;

  // Nivel do Pokemon que esta entrando — **opcional**, e essa e a diferenca
  // trazida pela spec 076 (pendencia P-03 da spec 070).
  //
  // Antes o campo era a UNICA fonte do nivel, e `0` significava
  // "desconhecido": a regra do Hyper Training do BDSP simplesmente nao
  // bloqueava, **em silencio**, para quem esquecesse de preencher. Era um
  // buraco de cobertura com cara de valor padrao.
  //
  // Agora o nivel DERIVA da exp do proprio Pokemon pela curva de crescimento
  // da especie (`species::LevelFromExp`) — que e de onde o jogo o tira. Este
  // campo virou override para quem ja tem o numero calculado; `0` deixou de
  // significar "sem regra" e passa a significar "derive voce mesmo".
  std::uint8_t level = 0;

  // O nivel efetivo: o override, se veio; a exp, se nao veio.
  std::uint8_t LevelOf(const pkm::Pokemon& p) const;

  bool Has(std::uint16_t dex) const;
};

// O Pokemon pode entrar em `dest`? Ordem: bloqueios primeiro, avisos depois
// (TD-03 — o veredito mais grave vence e a tela mostra um motivo so).
RuleResult CanTransfer(const pkm::Pokemon& p, cp::Game dest,
                       const SaveContext& ctx);

// ---------------------------------------------------------------------------
// G09 — os ajustes que a ENTRADA exige. Sao CALCULADOS, nunca gravados aqui:
// escrever no PKM e a transferencia end-to-end (G13).
// ---------------------------------------------------------------------------

struct Adjustments {
  // Scarlet/Violet: ao entrar, recebe Tera Type derivado do tipo primario
  // (§7). 0xFF = nao se aplica a este destino.
  std::uint8_t tera_type = 0xFF;

  // Legends Arceus: teto de cada effort level (GV) permitido, derivado do IV.
  // Vazio quando o destino nao e PLA.
  //
  // ATENCAO (TD-05): isto e o TETO, nao a conversao. A §7 fala em "converte
  // effort levels ↔ EVs/IVs", e essa formula NAO foi encontrada no PkHeX —
  // o PA8 guarda EV e GV em campos separados e nao ha nenhuma funcao de
  // conversao entre eles. Ver a pendencia no evidence-log da spec 070.
  std::vector<std::uint8_t> pla_max_effort_levels;

  // Primeiro golpe que o destino nao conhece, 0 se todos cabem. Espelha o
  // kWarning do veredito (spec 038).
  int missing_move = 0;
};

Adjustments AdjustmentsFor(const pkm::Pokemon& p, cp::Game dest);

// Teto de effort level (Ganbaru) de um IV, em Legends Arceus.
//
// Fonte: PKHeX.Core 25.12.21, GanbaruExtensions.GetBias — o teto e 10 menos
// um vies que so cresce a partir do IV 20. Medido por sonda, nao deduzido:
//   IV  0..19 -> 10    IV 20..25 -> 9    IV 26..30 -> 8    IV 31 -> 7
std::uint8_t MaxEffortLevel(std::uint8_t iv);

}  // namespace pokehome::rules
