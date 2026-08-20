#include "transfer_rules.h"

#include <algorithm>

#include "game_forms.h"
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

// Nivel minimo para o destino aceitar Hyper Training (spec 137, DEC-2 do
// dono). Antes era um "100" global preso dentro da regra do BDSP; o SV libera
// Hyper Training a partir do NIVEL 50, e o dono decidiu que a barreira e a do
// JOGO DE DESTINO, nao uma constante do HOME.
//
// Devolve 0 = "este jogo nao tem a mecanica, entao nao ha o que barrar". Vale
// para tudo antes da gen 7: o Hyper Training nasce em Sun/Moon, e num destino
// que nao o conhece a flag simplesmente nao viaja. Barrar ali seria inventar
// uma regra que o HOME nao tem — e reprovaria transferencia legal.
std::uint8_t MinHyperTrainingLevel(cp::Game dest) {
  switch (dest) {
    case cp::Game::kScarletViolet:
      return 50;
    case cp::Game::kSunMoon:
    case cp::Game::kUltraSunMoon:
    case cp::Game::kLetsGo:
    case cp::Game::kSwordShield:
    case cp::Game::kBdsp:
    case cp::Game::kLegendsArceus:
    case cp::Game::kLegendsZA:
      return 100;
    default:
      return 0;  // pre-gen7: a mecanica nao existe
  }
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

  // Regra GERAL, a outra metade (spec 137, lacuna L5): a FORMA tambem tem de
  // existir nos dados do destino. O HOME diz "especie E forma"; nos so
  // conferiamos a especie, entao um Alolan Vulpix entrava num save de BDSP —
  // o BDSP tem Vulpix, mas nao tem a forma no codigo do jogo (medido: as 66
  // entradas de forma do BDSP sao Unown/Arceus/Rotom/Deoxys e afins, nenhuma
  // regional). Idem um Zorua de Hisui indo para o SwSh.
  //
  // `HasForm` devolve true para jogo sem tabela medida (pre-Switch), entao
  // esta linha nunca barra por falta de dado — ver game_forms.h.
  if (!cp::HasForm(dest, dex, p.form)) {
    return {Verdict::kBlocked,
            "Esta forma de " + name + " nao existe em " + cp::GameName(dest)};
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
    // O Nincada ERA bloqueado aqui (spec 135, paridade cega com o HOME). A
    // spec 141 o LIBEROU, pelo criterio da DEC-2 do dono: o bloqueio oficial
    // e politica ANTI-CLONAGEM da Nintendo (o Nincada evolui em DOIS de uma
    // vez, e o servico marca o par como clone), nao limite do cartucho.
    //
    // MEDIDO: o BDSP tem Nincada (290), Ninjask (291) e Shedinja (292) nos
    // dados — `HasSpecies` e `HasForm` devolvem true para os tres. O jogo
    // suporta; quem recusava era a regra de negocio.
    //
    // Divergencia deliberada do HOME, na familia da DEC-1/DEC-2. Nao
    // "corrigir" achando que e bug.
    // Spinda: NAO ha bloqueio (spec 137, DEC-2 do dono). A spec 135 o
    // bloqueava copiando o HOME. A pesquisa mostrou que as pintas saem do
    // encryption constant/PID, que todo formato moderno carrega — o bug das
    // "pintas iguais" do HOME 3.0.0 era VISUAL e foi corrigido no 3.0.1; o
    // dado nunca se perdeu. Onde o jogo nao tem Spinda, o portao geral por
    // especie ja barra (medido: das 6 modernas, so o BDSP o tem).
  }

  if (dest == cp::Game::kLegendsArceus && p.can_gigantamax) {
    return {Verdict::kBlocked,
            "Legends: Arceus nao aceita Pokemon Gigantamax"};
  }

  // SV bloqueia o Gigantamax factor nas especies que ainda EVOLUEM
  // (spec 135): o SV nao conhece a flag, deixaria evoluir, e nasceria um
  // Gmax que nao existe (Raichu/Archaludon Gmax). LISTA, e nao criterio: o
  // Duraludon entrou por patch do HOME (v3.2.1), o que mostra que o servidor
  // mantem lista — se ela crescer, cresce aqui (TD-02 / D1 da pesquisa).
  if (dest == cp::Game::kScarletViolet && p.can_gigantamax) {
    static constexpr std::uint16_t kGmaxEvolving[] = {25, 52, 133, 884};
    for (std::uint16_t d : kGmaxEvolving) {
      if (dex == d) {
        return {Verdict::kBlocked,
                name + " Gigantamax nao entra em Scarlet/Violet"};
      }
    }
  }

  // Hyper Training: o nivel minimo e o do JOGO DE DESTINO (spec 137, DEC-2).
  // O nivel vem da exp pela curva de crescimento da especie (spec 076) quando
  // o chamador nao passa um — nao ha mais o caso "esqueceu de preencher, a
  // regra sumiu".
  if (AnyHyperTrained(p)) {
    const std::uint8_t min_level = MinHyperTrainingLevel(dest);
    if (min_level != 0 && ctx.LevelOf(p) < min_level) {
      return {Verdict::kBlocked,
              std::string(cp::GameName(dest)) + " so aceita Hyper Training a partir do nivel " +
                  std::to_string(min_level)};
    }
  }

  // --- Avisos ------------------------------------------------------------

  // TD-02: golpe ausente AVISA, nao bloqueia.
  //
  // A justificativa ORIGINAL (spec 038) era "o destino reseta o moveset por
  // conta propria". Isso foi MEDIDO em 2026-08-19 e e FALSO: um golpe que
  // nao existe na engine do PLA (id 903, de gen9) **mata o jogo** em ~11 s,
  // sem excecao no log. O jogo nao reseta nada — quem reseta o moveset e o
  // HOME, e aqui esse papel e do NestBox.
  //
  // O aviso continua correto, mas por outro motivo: `AplicaEntradaNoDestino`
  // (commit_plan.cpp) reescreve o moveset em TODA entrada, entao o golpe
  // invalido nunca chega ao save. Bloquear seria recusar uma transferencia
  // que o proprio app conserta.
  //
  // A trava que sustenta isso e o caminho unico de entrada. Se algum dia
  // houver um caminho de escrita que NAO passe por la, este aviso vira um
  // bug que trava o console do usuario.
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
