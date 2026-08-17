#include "transfer_rules.h"

#include <algorithm>

#include "game_moves.h"
#include "gen9_species.h"
#include "pkm_convert.h"
#include "species_facts.h"

namespace pokehome::rules {
namespace sf = pokehome::species;

namespace {

// Nome da especie para o texto do motivo. A tabela do gen9 cobre as 1025.
std::string SpeciesName(int dex) {
  if (dex <= 0 || dex >= za::kGen9SpeciesCount) return "Este Pokemon";
  return za::kGen9Species[dex];
}

// O parceiro do Let's Go — o Pikachu/Eevee que anda no ombro. §7: "o parceiro
// nao transfere".
//
// Como se reconhece: no PkHeX e PB7.IsStarter, e a sonda da spec 070 mostrou
// que ele e exatamente Pikachu na forma 8 ou Eevee na forma 1 (as formas
// "partner"). Nenhum bit dedicado no binario — e a propria forma.
// `dex` e National Dex (ver CanTransfer): o parceiro so existe em PB7, onde
// interno == nacional, mas a regra nao deve depender disso.
bool IsLetsGoPartner(int dex, const pkm::Pokemon& p) {
  return (dex == 25 && p.form == 8) || (dex == 133 && p.form == 1);
}

bool AnyHyperTrained(const pkm::Pokemon& p) {
  return std::any_of(p.hyper_trained.begin(), p.hyper_trained.end(),
                     [](bool v) { return v; });
}

}  // namespace

bool SaveContext::Has(std::uint16_t dex) const {
  return std::find(species_present.begin(), species_present.end(), dex) !=
         species_present.end();
}

std::uint8_t SaveContext::LevelOf(const pkm::Pokemon& p) const {
  // Override explicito vence: quem ja calculou o nivel (a tela mostra) nao
  // precisa que o recalculemos.
  if (level != 0) return level;
  return sf::LevelFromExp(static_cast<int>(p.species), p.exp);
}

std::uint8_t MaxEffortLevel(std::uint8_t iv) {
  // GanbaruExtensions.TrueMax = 10, menos GetBias(iv). Valores medidos por
  // sonda contra o PKHeX.Core 25.12.21 (ver evidence-log da spec 070).
  if (iv >= 31) return 7;
  if (iv >= 26) return 8;
  if (iv >= 20) return 9;
  return 10;
}

RuleResult CanTransfer(const pkm::Pokemon& p, cp::Game dest,
                       const SaveContext& ctx) {
  if (dest == cp::Game::kCount) return {};  // sem destino: nada a checar

  // Slot vazio nao e transferencia.
  if (p.empty()) return {};

  // National Dex, NAO `p.species` (spec 076).
  //
  // O parser do PK9 guarda o INDICE INTERNO do gen9: o Pawmo e 955 no binario
  // e 922 na dex nacional (spec 069). Todas as tabelas consultadas aqui —
  // `HasSpecies`, `IsLegendary`, `Type1`, `kGrowthRate`, `kFusableSpecies` —
  // sao indexadas por National Dex. Ler `p.species` direto fazia cada regra
  // consultar a linha errada para todo PK9 cujo interno difere do nacional, e
  // nenhum checksum acusa isso.
  //
  // Uma linha so, no topo, porque todas as regras derivam dela — corrigir em
  // cada regra seria o mesmo bug esperando para voltar na proxima.
  const int dex = static_cast<int>(pkm::NationalDex(p));
  const std::string name = SpeciesName(dex);

  // --- Bloqueios ---------------------------------------------------------

  // Ovo nao transfere (§4/§7): o HOME so enxerga Pokemon.
  if (p.is_egg) {
    return {Verdict::kBlocked, "Ovos nao podem ser transferidos"};
  }

  // Let's Go: o parceiro fica. Vale mesmo indo para o outro Let's Go.
  if (IsLetsGoPartner(dex, p)) {
    return {Verdict::kBlocked, "O parceiro do Let's Go nao pode ser transferido"};
  }

  // Fusao tem de ser desfeita ANTES de depositar (§7). Spec 076, pendencia
  // P-02 da spec 070: `HasSpecies` opera por ESPECIE, e a especie base do
  // Kyurem/Necrozma/Calyrex existe no destino — entao a regra geral deixava o
  // fundido entrar sem que nada acusasse. E por forma, e a forma e o que
  // distingue.
  //
  // Vale para TODO destino, inclusive os que tem a forma nos dados (SwSh e SV
  // tem): a regra do HOME nao e "o destino conhece a forma", e "o HOME nao
  // aceita o fundido no deposito".
  if (sf::IsFusedForm(dex, p.form)) {
    return {Verdict::kBlocked,
            name + " precisa ser separado antes de ser transferido"};
  }

  // Regra GERAL (§7): a especie tem de existir nos dados do destino. Nao
  // depende do progresso da Pokedex do jogador.
  if (!cp::HasSpecies(dest, dex)) {
    return {Verdict::kBlocked,
            name + " nao existe em " + cp::GameName(dest)};
  }

  if (dest == cp::Game::kBdsp) {
    // So 1 exemplar de cada Lendario/Mitico por save.
    if (sf::IsLegendary(dex) && ctx.Has(p.species)) {
      return {Verdict::kBlocked,
              "BDSP so aceita um " + name + " por save"};
    }
    // Bloqueia Gmax Pikachu/Eevee/Meowth. Na pratica o BDSP nao tem
    // Gigantamax nenhum: a regra vale para qualquer especie marcada.
    if (p.can_gigantamax) {
      return {Verdict::kBlocked, "BDSP nao aceita Pokemon Gigantamax"};
    }
    // Hyper Training abaixo do lv100 nao entra. O nivel vem da exp pela curva
    // de crescimento da especie (spec 076) quando o chamador nao passa um —
    // nao ha mais o caso "esqueceu de preencher, a regra sumiu".
    if (AnyHyperTrained(p) && ctx.LevelOf(p) < 100) {
      return {Verdict::kBlocked,
              "BDSP so aceita Hyper Training no nivel 100"};
    }
  }

  if (dest == cp::Game::kLegendsArceus && p.can_gigantamax) {
    return {Verdict::kBlocked,
            "Legends: Arceus nao aceita Pokemon Gigantamax"};
  }

  // --- Avisos ------------------------------------------------------------

  // TD-02: golpe ausente AVISA, nao bloqueia (spec 038, §7 — o destino
  // reseta o moveset por conta propria).
  const int missing = cp::MissingMoveIn(dest, p.moves.data(), p.moves.size());
  if (missing != 0) {
    return {Verdict::kWarning,
            "Um golpe nao existe em " + std::string(cp::GameName(dest)) +
                " e sera trocado"};
  }

  return {};
}

Adjustments AdjustmentsFor(const pkm::Pokemon& p, cp::Game dest) {
  Adjustments a;
  if (dest == cp::Game::kCount || p.empty()) return a;

  a.missing_move = cp::MissingMoveIn(dest, p.moves.data(), p.moves.size());

  // §7: ao entrar em Scarlet/Violet, recebe Tera Type derivado do tipo
  // primario. A ordem de tipos do PkHeX e a mesma do byte de tera no PK9.
  if (dest == cp::Game::kScarletViolet) {
    // National Dex: `kType1` e indexado por ela, e o PK9 guarda o interno.
    a.tera_type = sf::Type1(static_cast<int>(pkm::NationalDex(p)));
  }

  // §7: PLA converte effort levels ↔ EVs/IVs. A conversao em si nao foi
  // encontrada no PkHeX (TD-05) — o que existe e o TETO por IV, e e so
  // isso que devolvemos.
  if (dest == cp::Game::kLegendsArceus) {
    a.pla_max_effort_levels.reserve(p.ivs.size());
    for (std::uint8_t iv : p.ivs) a.pla_max_effort_levels.push_back(MaxEffortLevel(iv));
  }

  return a;
}

}  // namespace pokehome::rules
