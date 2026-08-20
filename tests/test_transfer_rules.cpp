// Regras de transferencia (spec 070, G07+G09).
//
// Cada regra da §7 da pesquisa tem caso POSITIVO e NEGATIVO. Um teste que so
// prova o bloqueio nao distingue "a regra funciona" de "tudo esta bloqueado";
// e um que so prova o allowed nao distingue "a regra funciona" de "nada
// bloqueia" — que e exatamente a violacao plantada desta spec.

#include <cstdio>
#include <string>

#include "game_species.h"
#include "pkm_convert.h"
#include "pkm_model.h"
#include "species_facts.h"
#include "transfer_rules.h"

namespace cp = pokehome::compat;
namespace tr = pokehome::rules;
namespace sf = pokehome::species;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
  if (cond) {
    std::printf("  ok   %s\n", what.c_str());
  } else {
    std::printf("  FAIL %s\n", what.c_str());
    ++g_failures;
  }
}

// Bloqueado E com motivo legivel — um bloqueio sem reason nao serve para a
// tela, entao os dois andam juntos.
void CheckBlocked(const tr::RuleResult& r, const std::string& what) {
  Check(r.verdict == tr::Verdict::kBlocked && !r.reason.empty(),
        what + "  [reason: \"" + r.reason + "\"]");
}

void CheckAllowed(const tr::RuleResult& r, const std::string& what) {
  Check(r.verdict == tr::Verdict::kAllowed, what);
}

// Um Pokemon identificado por NATIONAL DEX.
//
// O formato e PK8 de proposito (spec 076). A versao original usava kPK9, e
// isso era incoerente com o proprio modelo: o parser do PK9 guarda o INDICE
// INTERNO do gen9, nao a National Dex (spec 069) — o Pawmo e 955 no binario e
// 922 na dex. Enfiar uma National Dex num campo declarado como interno criava
// um Pokemon que nao existe, e escondia que `CanTransfer` estava consultando
// as tabelas com a grandeza errada.
//
// Nos outros quatro formatos species E National Dex, entao PK8 diz o que o
// teste quer dizer. O caminho do PK9 tem teste proprio, em TestPk9UsaDexNacional.
pkm::Pokemon Mon(std::uint16_t species, std::uint8_t form = 0) {
  pkm::Pokemon p;
  p.format = pkm::Format::kPK8;
  p.species = species;
  p.form = form;
  return p;
}

// O mesmo, no formato PK9: `species` aqui e o INDICE INTERNO do gen9.
pkm::Pokemon MonPk9Interno(std::uint16_t interno, std::uint8_t form = 0) {
  pkm::Pokemon p;
  p.format = pkm::Format::kPK9;
  p.species = interno;
  p.form = form;
  return p;
}

const tr::SaveContext kEmpty{};

// --------------------------------------------------------------------------

void TestRegraGeral() {
  std::printf("regra geral — a especie existe nos dados do destino (§7):\n");

  // Positivo: Pikachu esta em todo jogo moderno.
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kScarletViolet, kEmpty),
               "Pikachu entra em Scarlet/Violet");
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kSwordShield, kEmpty),
               "Pikachu entra em Sword/Shield");

  // Negativo: Chikorita ficou de fora do corte do Dexit em SwSh.
  //
  // A primeira versao deste teste usava Bulbasaur, supondo que os iniciais de
  // Kanto tivessem caido no Dexit. Caiu vermelho: SwSh TEM Bulbasaur. Sonda
  // contra o PkHeX (PersonalTable.SWSH.IsSpeciesInGame) achou o caso real.
  Check(!cp::HasSpecies(cp::Game::kSwordShield, 152),
        "premissa: SwSh nao tem Chikorita (Dexit)");
  CheckBlocked(tr::CanTransfer(Mon(152), cp::Game::kSwordShield, kEmpty),
               "Chikorita NAO entra em Sword/Shield");

  // O motivo tem de nomear a especie e o jogo — e o que a tela mostra.
  const auto r = tr::CanTransfer(Mon(152), cp::Game::kSwordShield, kEmpty);
  Check(r.reason.find("Chikorita") != std::string::npos &&
            r.reason.find("Sword") != std::string::npos,
        "o motivo nomeia a especie e o jogo");
}

void TestSpec065() {
  std::printf("as duas correcoes da spec 065:\n");

  // Growlithe so existe em PLA como forma de Hisui. O gerador antigo contava
  // so a forma 0 e o app RECUSAVA — era bug, nao regra.
  CheckAllowed(tr::CanTransfer(Mon(58, 1), cp::Game::kLegendsArceus, kEmpty),
               "Growlithe de Hisui ENTRA em Legends: Arceus");

  // Dipplin nao esta em Z-A (a spec 065 tirou as especies 1011-1016).
  CheckBlocked(tr::CanTransfer(Mon(1011), cp::Game::kLegendsZA, kEmpty),
               "Dipplin NAO entra em Legends: Z-A");
}

void TestOvo() {
  std::printf("ovo nao transfere (§4/§7):\n");

  auto egg = Mon(25);
  egg.is_egg = true;

  // Bloqueado em TODO destino, inclusive onde a especie cabe.
  CheckBlocked(tr::CanTransfer(egg, cp::Game::kScarletViolet, kEmpty),
               "ovo bloqueado em Scarlet/Violet");
  CheckBlocked(tr::CanTransfer(egg, cp::Game::kBdsp, kEmpty),
               "ovo bloqueado em BDSP");
  CheckBlocked(tr::CanTransfer(egg, cp::Game::kLetsGo, kEmpty),
               "ovo bloqueado em Let's Go");

  // Negativo: o MESMO Pokemon sem a flag entra.
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kScarletViolet, kEmpty),
               "o mesmo Pokemon SEM a flag de ovo entra");
}

void TestLetsGo() {
  std::printf("Let's Go — parceiro e dex de Kanto (§7):\n");

  // O parceiro e Pikachu forma 8 / Eevee forma 1 (sonda no PB7.IsStarter).
  CheckBlocked(tr::CanTransfer(Mon(25, 8), cp::Game::kLetsGo, kEmpty),
               "Pikachu parceiro (forma 8) NAO transfere");
  CheckBlocked(tr::CanTransfer(Mon(133, 1), cp::Game::kLetsGo, kEmpty),
               "Eevee parceiro (forma 1) NAO transfere");
  // Vale para qualquer destino, nao so para o outro Let's Go.
  CheckBlocked(tr::CanTransfer(Mon(25, 8), cp::Game::kScarletViolet, kEmpty),
               "o parceiro tambem nao vai para Scarlet/Violet");

  // Negativo: Pikachu comum (forma 0) e Eevee comum (forma 0) transferem.
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kLetsGo, kEmpty),
               "Pikachu comum ENTRA em Let's Go");
  CheckAllowed(tr::CanTransfer(Mon(133), cp::Game::kLetsGo, kEmpty),
               "Eevee comum ENTRA em Let's Go");

  // A dex do Let's Go e Kanto + Meltan/Melmetal.
  CheckAllowed(tr::CanTransfer(Mon(151), cp::Game::kLetsGo, kEmpty),
               "Mew (Kanto) entra em Let's Go");
  CheckAllowed(tr::CanTransfer(Mon(808), cp::Game::kLetsGo, kEmpty),
               "Meltan entra em Let's Go");
  CheckBlocked(tr::CanTransfer(Mon(152), cp::Game::kLetsGo, kEmpty),
               "Chikorita (fora de Kanto) NAO entra em Let's Go");
  CheckBlocked(tr::CanTransfer(Mon(448), cp::Game::kLetsGo, kEmpty),
               "Lucario (fora de Kanto) NAO entra em Let's Go");
}

void TestBdspLendario() {
  std::printf("BDSP — 1 exemplar de cada Lendario/Mitico por save (§7):\n");

  Check(sf::IsLegendary(150), "premissa: Mewtwo esta classificado como lendario");
  Check(!sf::IsLegendary(25), "premissa: Pikachu NAO e lendario");

  // Primeiro Mewtwo: o save nao tem nenhum.
  CheckAllowed(tr::CanTransfer(Mon(150), cp::Game::kBdsp, kEmpty),
               "o PRIMEIRO Mewtwo entra no BDSP");

  // Segundo: o save ja tem um.
  tr::SaveContext com_mewtwo;
  com_mewtwo.species_present = {150};
  CheckBlocked(tr::CanTransfer(Mon(150), cp::Game::kBdsp, com_mewtwo),
               "o SEGUNDO Mewtwo e bloqueado no BDSP");

  // A regra e por especie: outro lendario ainda entra.
  CheckAllowed(tr::CanTransfer(Mon(144), cp::Game::kBdsp, com_mewtwo),
               "Articuno entra mesmo com um Mewtwo no save");

  // E nao vale para nao-lendarios: dez Pikachus podem coexistir.
  tr::SaveContext com_pikachu;
  com_pikachu.species_present = {25};
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kBdsp, com_pikachu),
               "um SEGUNDO Pikachu entra (a regra so vale para lendarios)");

  // E so o BDSP: o mesmo caso em outro jogo passa.
  CheckAllowed(tr::CanTransfer(Mon(150), cp::Game::kScarletViolet, com_mewtwo),
               "o segundo Mewtwo entra em Scarlet/Violet (regra so do BDSP)");
}

void TestBdspGigantamax() {
  std::printf("BDSP e PLA bloqueiam Gigantamax (§7):\n");

  auto gmax = Mon(25);
  gmax.can_gigantamax = true;

  CheckBlocked(tr::CanTransfer(gmax, cp::Game::kBdsp, kEmpty),
               "Gmax Pikachu NAO entra no BDSP");
  CheckBlocked(tr::CanTransfer(gmax, cp::Game::kLegendsArceus, kEmpty),
               "Gmax Pikachu NAO entra em Legends: Arceus");

  // Negativo: o mesmo Pikachu sem a flag entra nos dois.
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kBdsp, kEmpty),
               "Pikachu sem Gmax entra no BDSP");
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kLegendsArceus, kEmpty),
               "Pikachu sem Gmax entra em Legends: Arceus");

  // E Gmax nao bloqueia onde o Gigantamax existe.
  CheckAllowed(tr::CanTransfer(gmax, cp::Game::kSwordShield, kEmpty),
               "Gmax Pikachu ENTRA em Sword/Shield");
}

void TestBdspHyperTraining() {
  std::printf("BDSP — Hyper Training abaixo do lv100 nao entra (§7):\n");

  auto ht = Mon(25);
  ht.hyper_trained[0] = true;

  tr::SaveContext lv50;
  lv50.level = 50;
  tr::SaveContext lv100;
  lv100.level = 100;

  CheckBlocked(tr::CanTransfer(ht, cp::Game::kBdsp, lv50),
               "Hyper Training no lv50 e bloqueado no BDSP");
  CheckAllowed(tr::CanTransfer(ht, cp::Game::kBdsp, lv100),
               "Hyper Training no lv100 ENTRA no BDSP");

  // Negativo: sem Hyper Training, o lv50 passa.
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kBdsp, lv50),
               "sem Hyper Training, o lv50 entra no BDSP");

  // E so o BDSP restringe.
  CheckAllowed(tr::CanTransfer(ht, cp::Game::kScarletViolet, lv50),
               "Hyper Training no lv50 entra em Scarlet/Violet");

  // --- spec 076, P-03: o nivel DERIVA da exp quando nao vem de fora --------
  //
  // Ate a spec 076 esta chamada — sem `level` no contexto — passava batido: a
  // regra do Hyper Training simplesmente NAO rodava, em silencio. Era o
  // buraco de cobertura. Agora o nivel sai da exp do proprio Pokemon.
  //
  // A derivacao, OBSERVADA DIRETO. Sem este assert o buraco volta em
  // silencio: com `level = 0` o antigo comportamento tambem BLOQUEIA um
  // Pokemon de exp 0 — nao porque derivou lv1, mas porque a regra sumiu e o
  // proximo bloqueio pegou. Os dois comportamentos dao o mesmo veredito no
  // caso negativo, entao so o veredito nao distingue: e preciso perguntar
  // qual nivel o contexto calculou.
  Check(kEmpty.LevelOf(ht) == 1,
        "sem level no contexto, LevelOf deriva da exp: exp 0 -> lv " +
            std::to_string(kEmpty.LevelOf(ht)));
  auto ht_meio = ht;
  ht_meio.exp = 125000;  // lv50 na curva MediumFast (medido no PkHeX)
  Check(kEmpty.LevelOf(ht_meio) == 50,
        "exp 125.000 (Pikachu) -> lv " + std::to_string(kEmpty.LevelOf(ht_meio)) +
            " (esperado 50)");

  CheckBlocked(tr::CanTransfer(ht, cp::Game::kBdsp, kEmpty),
               "sem level no contexto, o nivel vem da exp (exp 0 = lv1, bloqueia)");

  // O lv50 DERIVADO tambem bloqueia — com o buraco antigo, `level` seria 0 e
  // este caso passaria como "allowed".
  CheckBlocked(tr::CanTransfer(ht_meio, cp::Game::kBdsp, kEmpty),
               "Hyper Training no lv50 DERIVADO da exp e bloqueado no BDSP");

  // Positivo do mesmo caminho: exp de lv100 na curva do Pikachu (MediumFast,
  // 1.000.000) entra sem que ninguem informe o nivel.
  auto ht100 = ht;
  ht100.exp = 1000000;
  CheckAllowed(tr::CanTransfer(ht100, cp::Game::kBdsp, kEmpty),
               "exp de lv100 derivada ENTRA no BDSP sem level no contexto");

  // Um passo abaixo do lv100 ainda bloqueia — prova que o corte esta no
  // limiar certo, e nao em "exp grande passa".
  auto ht99 = ht;
  ht99.exp = 1000000 - 1;
  CheckBlocked(tr::CanTransfer(ht99, cp::Game::kBdsp, kEmpty),
               "exp de 999.999 (lv99 na curva MediumFast) ainda bloqueia");

  // A curva importa: 1.000.000 de exp e lv100 no Pikachu (MediumFast) mas
  // NAO no Bulbasaur (MediumSlow, que exige 1.059.860). Se a tabela fosse
  // indexada errado — ou fosse uma curva so para todo mundo — os dois dariam
  // o mesmo veredito.
  auto bulba = Mon(1);
  bulba.hyper_trained[0] = true;
  bulba.exp = 1000000;
  CheckBlocked(tr::CanTransfer(bulba, cp::Game::kBdsp, kEmpty),
               "1.000.000 de exp NAO e lv100 no Bulbasaur (curva MediumSlow)");
  auto bulba100 = bulba;
  bulba100.exp = 1059860;
  CheckAllowed(tr::CanTransfer(bulba100, cp::Game::kBdsp, kEmpty),
               "1.059.860 de exp E lv100 no Bulbasaur");

  // O override explicito continua vencendo a derivacao: quem ja tem o numero
  // calculado passa, e a exp e ignorada.
  tr::SaveContext override100;
  override100.level = 100;
  CheckAllowed(tr::CanTransfer(ht, cp::Game::kBdsp, override100),
               "level explicito vence a exp (exp 0 + level 100 = entra)");
}

// A tabela de curva de crescimento, direto (spec 076, P-03).
//
// O oraculo NAO e a funcao testada (licao da spec 069): os limiares abaixo
// foram medidos no PkHeX (Experience.GetEXP) e estao escritos a mao aqui.
void TestLevelFromExp() {
  std::printf("nivel derivado da exp — a tabela de curvas (spec 076):\n");

  // As seis curvas, no lv100. Valores do PkHeX, transcritos do probe.
  struct Caso { int dex; const char* nome; std::uint32_t exp100; };
  const Caso casos[] = {
      {25, "Pikachu (MediumFast, curva 0)", 1000000},
      {1, "Bulbasaur (MediumSlow, curva 3)", 1059860},
      {150, "Mewtwo (Slow, curva 5)", 1250000},
  };
  for (const Caso& c : casos) {
    Check(sf::LevelFromExp(c.dex, c.exp100) == 100,
          std::string(c.nome) + ": exp " + std::to_string(c.exp100) + " = lv100");
    Check(sf::LevelFromExp(c.dex, c.exp100 - 1) == 99,
          std::string(c.nome) + ": um a menos = lv99");
  }

  // As curvas sao DIFERENTES entre si — se a tabela tivesse uma linha so
  // replicada, este assert cairia.
  Check(sf::LevelFromExp(25, 1000000) != sf::LevelFromExp(1, 1000000),
        "a mesma exp da niveis diferentes em curvas diferentes");

  // Bordas.
  Check(sf::LevelFromExp(25, 0) == 1, "exp 0 = nivel 1");
  Check(sf::LevelFromExp(25, 999999999u) == 100, "exp absurda satura no lv100");
  Check(sf::LevelFromExp(0, 1000000) == 0, "especie invalida devolve 0, nao 1");
  Check(sf::LevelFromExp(9999, 1000000) == 0, "dex acima do maximo devolve 0");

  // Monotonicidade nas 1025 especies: o nivel nunca DECRESCE quando a exp
  // cresce. Uma tabela com linha fora de ordem quebraria aqui.
  int nao_monotonicas = 0;
  for (int dex = 1; dex <= 1025; ++dex) {
    std::uint8_t anterior = 0;
    for (std::uint32_t exp = 0; exp <= 1700000; exp += 7919) {  // primo
      const std::uint8_t lv = sf::LevelFromExp(dex, exp);
      if (lv < anterior) ++nao_monotonicas;
      anterior = lv;
    }
  }
  Check(nao_monotonicas == 0,
        "nas 1025 especies o nivel cresce junto com a exp (" +
            std::to_string(nao_monotonicas) + " violacoes)");

  // A curva tem de VARIAR entre especies. Se `kGrowthRate` fosse zerada — o
  // falso-verde classico da spec 066 — todas as especies cairiam na curva 0 e
  // este assert cairia.
  int distintos = 0;
  std::uint8_t visto[256] = {};
  for (int dex = 1; dex <= 1025; ++dex) {
    const std::uint8_t lv = sf::LevelFromExp(dex, 1000000);
    if (!visto[lv]) { visto[lv] = 1; ++distintos; }
  }
  Check(distintos >= 4,
        "1.000.000 de exp da >= 4 niveis distintos nas 1025 especies (" +
            std::to_string(distintos) + ") — prova que kGrowthRate nao e uniforme");
}

// Fusoes (spec 076, P-02 da spec 070).
//
// A §7 manda desfazer a fusao antes de depositar. A regra geral do
// `HasSpecies` NAO pegava isso: ela e por ESPECIE, e a especie base existe no
// destino. E por FORMA.
void TestFusoes() {
  std::printf("fusoes precisam ser desfeitas antes de depositar (§7, P-02):\n");

  // Premissa que faz o teste valer: a especie base ESTA no destino. Sem isso
  // o bloqueio seria da regra geral, e o teste passaria sem a regra nova.
  Check(cp::HasSpecies(cp::Game::kScarletViolet, 646),
        "premissa: Kyurem existe em Scarlet/Violet");
  Check(cp::HasSpecies(cp::Game::kScarletViolet, 800),
        "premissa: Necrozma existe em Scarlet/Violet");
  Check(cp::HasSpecies(cp::Game::kScarletViolet, 898),
        "premissa: Calyrex existe em Scarlet/Violet");

  struct Fusao { std::uint16_t dex; std::uint8_t form; const char* nome; };
  const Fusao fundidos[] = {
      {646, 1, "Kyurem-Black"},   {646, 2, "Kyurem-White"},
      {800, 1, "Necrozma-Dusk"},  {800, 2, "Necrozma-Dawn"},
      {898, 1, "Calyrex-Ice"},    {898, 2, "Calyrex-Shadow"},
  };

  // NEGATIVO: o fundido nao entra, em nenhum destino.
  for (const Fusao& f : fundidos) {
    CheckBlocked(tr::CanTransfer(Mon(f.dex, f.form), cp::Game::kScarletViolet, kEmpty),
                 std::string(f.nome) + " NAO entra em Scarlet/Violet");
    CheckBlocked(tr::CanTransfer(Mon(f.dex, f.form), cp::Game::kSwordShield, kEmpty),
                 std::string(f.nome) + " NAO entra em Sword/Shield");
  }

  // POSITIVO: a forma BASE — a fusao ja desfeita — entra normalmente. E o
  // caso que distingue "a regra funciona" de "Kyurem esta todo bloqueado".
  CheckAllowed(tr::CanTransfer(Mon(646, 0), cp::Game::kScarletViolet, kEmpty),
               "Kyurem SOLTO (forma 0) entra em Scarlet/Violet");
  CheckAllowed(tr::CanTransfer(Mon(800, 0), cp::Game::kScarletViolet, kEmpty),
               "Necrozma normal (forma 0) entra em Scarlet/Violet");
  CheckAllowed(tr::CanTransfer(Mon(898, 0), cp::Game::kScarletViolet, kEmpty),
               "Calyrex sozinho (forma 0) entra em Scarlet/Violet");

  // O motivo diz o que fazer, nao so que deu errado.
  const auto r = tr::CanTransfer(Mon(646, 1), cp::Game::kScarletViolet, kEmpty);
  Check(r.reason.find("Kyurem") != std::string::npos &&
            r.reason.find("separado") != std::string::npos,
        "o motivo nomeia a especie e diz que precisa separar: \"" + r.reason + "\"");

  // A regra e ESTREITA: forma != 0 em especie nao-fusionavel nao bloqueia.
  // Sem este assert, "bloqueia toda forma alternativa" passaria igual.
  CheckAllowed(tr::CanTransfer(Mon(25, 1), cp::Game::kScarletViolet, kEmpty),
               "Pikachu forma 1 (nao e fusao) entra normalmente");
  CheckAllowed(tr::CanTransfer(Mon(479, 1), cp::Game::kScarletViolet, kEmpty),
               "Rotom forma 1 (forma alternativa, nao fusao) entra");

  // E so a forma 1/2: a forma 0 das fusionaveis ja passou acima, e uma forma
  // alta da mesma especie nao e fusao conhecida.
  Check(!sf::IsFusedForm(646, 0), "Kyurem forma 0 nao e fusao");
  Check(sf::IsFusedForm(646, 1) && sf::IsFusedForm(646, 2),
        "Kyurem formas 1 e 2 sao fusao");
  Check(!sf::IsFusedForm(25, 1), "Pikachu forma 1 nao e fusao");
}

void TestGolpeAusenteAvisa() {
  std::printf("golpe inexistente no destino AVISA, nao bloqueia (spec 038):\n");

  // Um golpe do gen9 (id alto) num destino que so conhece ids baixos.
  auto p = Mon(25);
  p.moves[0] = 900;  // acima do teto do BDSP

  const auto r = tr::CanTransfer(p, cp::Game::kBdsp, kEmpty);
  Check(r.verdict == tr::Verdict::kWarning,
        "golpe ausente gera kWarning (nao kBlocked)");
  Check(r.allowed(), "o kWarning ainda deixa passar");
  Check(!r.reason.empty(), "o aviso tem motivo legivel: \"" + r.reason + "\"");

  // Negativo: golpe que o destino conhece nao gera aviso nenhum.
  auto ok = Mon(25);
  ok.moves[0] = 1;  // Pound existe em todo jogo
  CheckAllowed(tr::CanTransfer(ok, cp::Game::kBdsp, kEmpty),
               "golpe conhecido nao gera aviso");

  // E o bloqueio vence o aviso (TD-03): especie ausente + golpe ausente.
  auto both = Mon(152);  // Chikorita, fora do SwSh
  both.moves[0] = 900;
  const auto r2 = tr::CanTransfer(both, cp::Game::kSwordShield, kEmpty);
  Check(r2.verdict == tr::Verdict::kBlocked,
        "bloqueio vence aviso quando os dois valem");
}

void TestTeraType() {
  std::printf("SV — tera type derivado do tipo primario (§7):\n");

  // 12 = Electric, 9 = Fire, 11 = Grass na ordem de tipos do PkHeX.
  const auto pika = tr::AdjustmentsFor(Mon(25), cp::Game::kScarletViolet);
  Check(pika.tera_type == 12,
        "Pikachu recebe tera Electric (12), tem " +
            std::to_string(pika.tera_type));

  const auto chari = tr::AdjustmentsFor(Mon(6), cp::Game::kScarletViolet);
  Check(chari.tera_type == 9,
        "Charizard recebe tera Fire (9), tem " +
            std::to_string(chari.tera_type));

  const auto bulba = tr::AdjustmentsFor(Mon(1), cp::Game::kScarletViolet);
  Check(bulba.tera_type == 11,
        "Bulbasaur recebe tera Grass (11), tem " +
            std::to_string(bulba.tera_type));

  // Sempre igual ao tipo primario da tabela — a regra, nao tres casos soltos.
  bool todos = true;
  for (int dex = 1; dex <= 1025; ++dex) {
    const auto a = tr::AdjustmentsFor(Mon(static_cast<std::uint16_t>(dex)),
                                      cp::Game::kScarletViolet);
    if (a.tera_type != sf::Type1(dex)) todos = false;
  }
  Check(todos, "nas 1025 especies o tera type e o tipo primario");

  // Negativo: destino que nao e SV nao atribui tera type.
  const auto fora = tr::AdjustmentsFor(Mon(25), cp::Game::kBdsp);
  Check(fora.tera_type == 0xFF,
        "destino BDSP nao atribui tera type (0xFF)");
}

void TestEffortLevels() {
  std::printf("PLA — teto de effort level por IV (TD-05 da spec 070):\n");

  auto p = Mon(25);
  p.ivs = {0, 20, 26, 31, 15, 25};

  const auto a = tr::AdjustmentsFor(p, cp::Game::kLegendsArceus);
  Check(a.pla_max_effort_levels.size() == 6, "seis tetos, um por stat");

  // Medido contra GanbaruExtensions do PKHeX (TrueMax 10 menos GetBias).
  Check(tr::MaxEffortLevel(0) == 10, "IV 0 -> teto 10");
  Check(tr::MaxEffortLevel(19) == 10, "IV 19 -> teto 10");
  Check(tr::MaxEffortLevel(20) == 9, "IV 20 -> teto 9");
  Check(tr::MaxEffortLevel(25) == 9, "IV 25 -> teto 9");
  Check(tr::MaxEffortLevel(26) == 8, "IV 26 -> teto 8");
  Check(tr::MaxEffortLevel(30) == 8, "IV 30 -> teto 8");
  Check(tr::MaxEffortLevel(31) == 7, "IV 31 -> teto 7");

  Check(a.pla_max_effort_levels[3] == 7, "o IV 31 do exemplo devolve 7");

  // Negativo: destino que nao e PLA nao devolve teto nenhum.
  const auto fora = tr::AdjustmentsFor(p, cp::Game::kScarletViolet);
  Check(fora.pla_max_effort_levels.empty(),
        "destino Scarlet/Violet nao devolve effort levels");
}

void TestSlotVazio() {
  std::printf("bordas:\n");

  CheckAllowed(tr::CanTransfer(pkm::Pokemon{}, cp::Game::kBdsp, kEmpty),
               "slot vazio nao e transferencia (allowed, sem motivo)");
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kCount, kEmpty),
               "destino kCount (NestBox) aceita tudo");
}

}  // namespace

// As regras consultam as tabelas por National Dex; o PK9 guarda o interno
// (spec 069). Antes da spec 076 `CanTransfer` lia `p.species` cru, entao para
// todo PK9 cujo interno difere do nacional cada regra consultava a LINHA
// ERRADA — e nenhum checksum acusa isso.
//
// A amostragem aqui e escolhida para EXERCITAR a diferenca (licao da spec
// 069): so servem especies em que interno != nacional.
void TestPk9UsaDexNacional() {
  std::printf("PK9: as regras usam National Dex, nao o indice interno (spec 076):\n");

  // Pawmo: interno 955, nacional 922. O numero e o mesmo par medido contra o
  // PkHeX na spec 069.
  const std::uint16_t kPawmoInterno = 955, kPawmoNacional = 922;
  auto pawmo = MonPk9Interno(kPawmoInterno);
  Check(pkm::NationalDex(pawmo) == kPawmoNacional,
        "premissa: interno 955 -> nacional 922 (interno != nacional)");

  // O tera type tem de ser o do Pawmo NACIONAL. Lendo o interno, a tabela
  // devolveria o tipo de outra especie inteiramente.
  const auto a = tr::AdjustmentsFor(pawmo, cp::Game::kScarletViolet);
  Check(a.tera_type == sf::Type1(kPawmoNacional),
        "tera type do PK9 vem da linha NACIONAL (" +
            std::to_string(a.tera_type) + " == " +
            std::to_string(sf::Type1(kPawmoNacional)) + ")");
  Check(sf::Type1(kPawmoInterno) != sf::Type1(kPawmoNacional),
        "premissa: as duas linhas dao tipos DIFERENTES — o assert acima "
        "distingue as duas leituras");

  // --- a regra GERAL pelo caminho do PK9 ---------------------------------
  //
  // Este bloco existe porque a primeira versao do teste NAO pegava a leitura
  // errada dentro de `CanTransfer`: ela so conferia o tera type, que sai de
  // `AdjustmentsFor` — outra funcao, outro caminho. Plantar a violacao no
  // `dex` do `CanTransfer` deixava a suite VERDE.
  //
  // O caso discriminante e uma especie cujo INTERNO cai numa linha da tabela
  // com resposta OPOSTA. Sneasler: nacional 903, existe em Legends: Arceus;
  // o interno dele nao. Lendo o interno, o Pokemon e barrado por engano.
  CheckAllowed(tr::CanTransfer(pawmo, cp::Game::kScarletViolet, kEmpty),
               "Pawmo (PK9, interno 955) entra em Scarlet/Violet");

  // Varredura: quantas especies do gen9 dao vereditos DIFERENTES conforme se
  // leia o interno ou o nacional? Se der 0, este teste nao consegue provar
  // nada e diz isso.
  //
  // O destino tem de ser escolhido com cuidado, e a primeira tentativa
  // ensinou por que: com SwSh a varredura deu **0** — toda especie de interno
  // divergente e do gen9, ausente do SwSh nas DUAS leituras, entao o veredito
  // coincide e o caso nao discrimina nada. O teste falou isso em vez de
  // passar em silencio, que era o ponto da trava.
  int discriminantes = 0;
  std::uint16_t exemplo_nac = 0, exemplo_int = 0;
  cp::Game alvo = cp::Game::kLegendsZA;
  for (std::uint16_t nac = 1; nac <= 1025; ++nac) {
    const std::uint16_t interno = pkm::SpeciesForFormat(nac, pkm::Format::kPK9);
    if (interno == 0 || interno == nac || interno > 1025) continue;
    if (cp::HasSpecies(alvo, nac) != cp::HasSpecies(alvo, interno)) {
      if (!exemplo_nac) { exemplo_nac = nac; exemplo_int = interno; }
      ++discriminantes;
    }
  }
  Check(discriminantes > 0,
        "ha " + std::to_string(discriminantes) +
            " especie(s) do gen9 em que ler o interno inverte o veredito de "
            "HasSpecies — o caso abaixo so existe por causa delas");

  // O caso concreto, com a premissa explicita nos dois lados.
  if (exemplo_nac) {
    const bool nacional_entra = cp::HasSpecies(alvo, exemplo_nac);
    Check(nacional_entra != cp::HasSpecies(alvo, exemplo_int),
          "premissa: dex " + std::to_string(exemplo_nac) + " (interno " +
              std::to_string(exemplo_int) + ") tem veredito OPOSTO nas duas leituras");
    const auto r2 =
        tr::CanTransfer(MonPk9Interno(exemplo_int), alvo, kEmpty);
    Check(r2.allowed() == nacional_entra,
          "CanTransfer de um PK9 segue a linha NACIONAL (dex " +
              std::to_string(exemplo_nac) + ": allowed=" +
              std::string(r2.allowed() ? "true" : "false") + ", esperado " +
              std::string(nacional_entra ? "true" : "false") + ")");
  }

  // O bloqueio de fusao tambem vale pelo caminho do PK9.
  //
  // HONESTIDADE SOBRE O QUE ESTE CASO COBRE: as tres especies fusionaveis
  // (646/800/898) tem interno == nacional no gen9 — medido, nao suposto (o
  // assert abaixo trava isso). Ou seja, este par NAO discrimina as duas
  // leituras; ele cobre o bloqueio de fusao chegando por PK9, e nada mais.
  // Quem discrimina a traducao e o Pawmo, acima.
  const std::uint16_t kCalyrexInterno = pkm::SpeciesForFormat(898, pkm::Format::kPK9);
  Check(kCalyrexInterno == 898,
        "medido: Calyrex tem interno == nacional (898) — por isso este caso "
        "nao discrimina a traducao, so o bloqueio");
  CheckBlocked(tr::CanTransfer(MonPk9Interno(kCalyrexInterno, 1),
                               cp::Game::kScarletViolet, kEmpty),
               "Calyrex-Ice vindo de PK9 e bloqueado");
  CheckAllowed(tr::CanTransfer(MonPk9Interno(kCalyrexInterno, 0),
                               cp::Game::kScarletViolet, kEmpty),
               "Calyrex forma 0 vindo de PK9 entra");

  // Quantas das 1025 tem interno != nacional? Se uma regeneracao futura fizer
  // as duas grandezas coincidirem, este teste fica CEGO — e diz isso, em vez
  // de passar em silencio (a trava da spec 069).
  int divergentes = 0;
  for (std::uint16_t nac = 1; nac <= 1025; ++nac) {
    const std::uint16_t interno = pkm::SpeciesForFormat(nac, pkm::Format::kPK9);
    if (interno != 0 && interno != nac) ++divergentes;
  }
  Check(divergentes >= 100,
        "ha >= 100 especies com interno != nacional (" +
            std::to_string(divergentes) +
            ") — se cair a zero, este teste virou decorativo");
}

// Paridade de bloqueios com o HOME (spec 135). As lacunas vieram da
// pesquisa de mecanicas por jogo (docs/pesquisa-mecanicas-por-jogo.md).
static void TestParidadeSpec135() {
  std::printf("=== spec 135: paridade de bloqueios ===\n");
  const tr::SaveContext kEmpty{};

  // SV: Gmax que ainda evolui NAO entra — Pikachu(25), Meowth(52),
  // Eevee(133), Duraludon(884), a lista do HOME v3.2.1.
  for (std::uint16_t dex : {25, 52, 133, 884}) {
    pkm::Pokemon g = Mon(dex);
    g.can_gigantamax = true;
    CheckBlocked(tr::CanTransfer(g, cp::Game::kScarletViolet, kEmpty),
                 "Gmax " + std::to_string(dex) + " nao entra no SV");
  }
  // Contra-caso 1: os MESMOS sem a flag entram.
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kScarletViolet, kEmpty),
               "Pikachu sem Gmax entra no SV");
  // Contra-caso 2: Gmax de especie FINAL entra (Charizard 6) — a flag fica
  // retida, inerte; o HOME so bloqueia quem evolui.
  {
    pkm::Pokemon zard = Mon(6);
    zard.can_gigantamax = true;
    CheckAllowed(tr::CanTransfer(zard, cp::Game::kScarletViolet, kEmpty),
                 "Charizard Gmax (final) entra no SV");
  }

  // BDSP: o Nincada(290) era bloqueado pela spec 135 (paridade cega com o
  // HOME) e foi LIBERADO pela 141 — o bloqueio oficial e politica
  // anti-clonagem, e o BDSP tem as tres especies da linha nos dados.
  // O caso virou CONTRA-caso: ele entra de qualquer origem.
  {
    pkm::Pokemon nin = Mon(290);
    nin.origin_game = 44;  // SwSh
    CheckAllowed(tr::CanTransfer(nin, cp::Game::kBdsp, kEmpty),
                 "Nincada de FORA entra no BDSP (spec 141: o jogo o tem)");
    nin.origin_game = 48;  // BD
    CheckAllowed(tr::CanTransfer(nin, cp::Game::kBdsp, kEmpty),
                 "Nincada do proprio BDSP tambem, claro");
    nin.origin_game = 44;
    CheckAllowed(tr::CanTransfer(nin, cp::Game::kSwordShield, kEmpty),
                 "Nincada entra no SwSh normalmente");
    // A trava que CONTINUA valendo e a tecnica: no SV a especie nao existe.
    CheckBlocked(tr::CanTransfer(nin, cp::Game::kScarletViolet, kEmpty),
                 "Nincada NAO entra no SV — ali a especie nao existe (tecnico)");
  }

  // Spinda: o bloqueio da spec 135 foi REVERTIDO na spec 137 (DEC-2 do dono).
  // Ver TestSpindaLiberadoSpec137.
}


// ---------------------------------------------------------------------------
// spec 137 - L5: o portao confere ESPECIE **E FORMA**.
//
// Todo caso tem contra-caso: uma regra que bloqueia tudo "passa" sem provar
// nada. Aqui o contra-caso e sempre a MESMA especie na forma base, que tem de
// continuar entrando - se ela quebrar, o bloqueio nao e da forma, e da
// especie, e a regra nova nao esta fazendo o que diz.
//
// Todos os vereditos abaixo saem da tabela MEDIDA contra o PkHeX
// (PersonalTable.X.IsPresentInGame), nunca de texto pesquisado.
// ---------------------------------------------------------------------------
static void TestPortaoPorFormaSpec137() {
  std::printf("=== spec 137: o portao confere a FORMA ===\n");
  const tr::SaveContext kEmpty{};

  // O caso da pesquisa: o BDSP tem Vulpix(37), mas nao tem a forma de Alola.
  CheckBlocked(tr::CanTransfer(Mon(37, 1), cp::Game::kBdsp, kEmpty),
               "Alolan Vulpix NAO entra no BDSP");
  // CONTRA-CASO: o Vulpix normal entra. Sem ele, "bloqueia tudo" passaria.
  CheckAllowed(tr::CanTransfer(Mon(37, 0), cp::Game::kBdsp, kEmpty),
               "Vulpix normal ENTRA no BDSP");
  // E a forma de Alola entra onde ela existe (SwSh e SV) - prova que o
  // bloqueio e por DESTINO, nao "forma 1 e proibida".
  CheckAllowed(tr::CanTransfer(Mon(37, 1), cp::Game::kSwordShield, kEmpty),
               "Alolan Vulpix entra no SwSh");
  CheckAllowed(tr::CanTransfer(Mon(37, 1), cp::Game::kScarletViolet, kEmpty),
               "Alolan Vulpix entra no SV");

  // Zorua de Hisui(570 forma 1): o SwSh tem Zorua, mas nao a forma de Hisui.
  CheckBlocked(tr::CanTransfer(Mon(570, 1), cp::Game::kSwordShield, kEmpty),
               "Hisuian Zorua NAO entra no SwSh");
  CheckAllowed(tr::CanTransfer(Mon(570, 0), cp::Game::kSwordShield, kEmpty),
               "Zorua normal ENTRA no SwSh");
  // CONTRA-CASOS: onde a forma de Hisui existe, ela entra.
  CheckAllowed(tr::CanTransfer(Mon(570, 1), cp::Game::kLegendsArceus, kEmpty),
               "Hisuian Zorua entra no PLA");
  CheckAllowed(tr::CanTransfer(Mon(570, 1), cp::Game::kScarletViolet, kEmpty),
               "Hisuian Zorua entra no SV");
  // O avesso, medido: no PLA existe SO a forma de Hisui - o Zorua de Unova
  // nao entra. Prova que a tabela nao e "forma 0 sempre vale".
  CheckBlocked(tr::CanTransfer(Mon(570, 0), cp::Game::kLegendsArceus, kEmpty),
               "Zorua de Unova (forma 0) NAO entra no PLA");
  // Idem Growlithe(58): o PLA so tem o de Hisui.
  CheckBlocked(tr::CanTransfer(Mon(58, 0), cp::Game::kLegendsArceus, kEmpty),
               "Growlithe de Kanto NAO entra no PLA");
  CheckAllowed(tr::CanTransfer(Mon(58, 1), cp::Game::kLegendsArceus, kEmpty),
               "Hisuian Growlithe entra no PLA");

  // Tauros de Paldea(128 forma 1): o SwSh TEM Tauros, entao so o portao novo
  // pode barra-lo. Este caso e o mais forte da bateria — o "Galarian Ponyta
  // no SV" que estava aqui antes passava pelo motivo ERRADO (o SV nem tem
  // Ponyta, entao o portao velho, por especie, ja o barrava; o teste
  // continuaria verde com a regra nova apagada).
  CheckBlocked(tr::CanTransfer(Mon(128, 1), cp::Game::kSwordShield, kEmpty),
               "Paldean Tauros NAO entra no SwSh (que TEM Tauros)");
  CheckAllowed(tr::CanTransfer(Mon(128, 0), cp::Game::kSwordShield, kEmpty),
               "Tauros normal ENTRA no SwSh");
  CheckAllowed(tr::CanTransfer(Mon(128, 1), cp::Game::kScarletViolet, kEmpty),
               "Paldean Tauros entra no SV");

  // Formas NAO-regionais tambem valem: o BDSP tem as 27 do Unown e as 5 do
  // Rotom, entao elas passam. Se a regra so soubesse de regionais, isto
  // quebraria.
  CheckAllowed(tr::CanTransfer(Mon(201, 26), cp::Game::kBdsp, kEmpty),
               "Unown forma 26 entra no BDSP");
  CheckAllowed(tr::CanTransfer(Mon(479, 5), cp::Game::kBdsp, kEmpty),
               "Rotom forma 5 entra no BDSP");
  // E uma forma inexistente da MESMA especie e barrada: o Unown tem 28
  // formas (0..27), a 28 nao existe em jogo nenhum.
  CheckBlocked(tr::CanTransfer(Mon(201, 28), cp::Game::kBdsp, kEmpty),
               "Unown forma 28 (inexistente) NAO entra no BDSP");

  // Jogo pre-Switch nao tem tabela de forma medida: o portao nao pode barrar
  // por falta de dado. Vulpix entra no Emerald normalmente.
  CheckAllowed(tr::CanTransfer(Mon(37, 0), cp::Game::kEmerald, kEmpty),
               "sem tabela medida (Emerald), a forma nao bloqueia");
}

// ---------------------------------------------------------------------------
// spec 137 - DEC-2: Spinda LIBERADO.
//
// A spec 135 bloqueava Spinda no BDSP copiando o HOME. O dono decidiu o
// contrario com a pesquisa: as pintas saem do encryption constant/PID, que
// todo formato moderno carrega, e o bug das "pintas iguais" do HOME 3.0.0 era
// VISUAL (corrigido no 3.0.1) - o dado nunca se perdeu.
// ---------------------------------------------------------------------------
static void TestSpindaLiberadoSpec137() {
  std::printf("=== spec 137: Spinda liberado (DEC-2) ===\n");
  const tr::SaveContext kEmpty{};

  CheckAllowed(tr::CanTransfer(Mon(327), cp::Game::kBdsp, kEmpty),
               "Spinda ENTRA no BDSP (bloqueio da spec 135 revertido)");
  CheckAllowed(tr::CanTransfer(Mon(327), cp::Game::kEmerald, kEmpty),
               "Spinda entra no Emerald");

  // CONTRA-CASO: onde o jogo NAO tem a especie, o portao geral ja barra -
  // e por isso a regra propria do Spinda nao faz falta. Medido: das 6
  // modernas, so o BDSP tem Spinda nos dados.
  CheckBlocked(tr::CanTransfer(Mon(327), cp::Game::kSwordShield, kEmpty),
               "Spinda nao entra no SwSh (o portao por especie barra)");
  CheckBlocked(tr::CanTransfer(Mon(327), cp::Game::kScarletViolet, kEmpty),
               "Spinda nao entra no SV (o portao por especie barra)");
  CheckBlocked(tr::CanTransfer(Mon(327), cp::Game::kLegendsArceus, kEmpty),
               "Spinda nao entra no PLA (o portao por especie barra)");
}

// ---------------------------------------------------------------------------
// spec 137 - DEC-2: Hyper Training por JOGO.
//
// O SV aceita a partir do nivel 50; os demais que tem a mecanica exigem 100.
// Antes havia um "100" fixo dentro da regra do BDSP.
// ---------------------------------------------------------------------------
static void TestHyperTrainingPorJogoSpec137() {
  std::printf("=== spec 137: Hyper Training por jogo (DEC-2) ===\n");

  auto ht = Mon(25);  // Pikachu, presente em todos os destinos usados aqui
  ht.hyper_trained[0] = true;

  tr::SaveContext lv49; lv49.level = 49;
  tr::SaveContext lv50; lv50.level = 50;
  tr::SaveContext lv99; lv99.level = 99;
  tr::SaveContext lv100; lv100.level = 100;

  // SV: a barreira e 50. O par 49/50 fixa a BORDA - sem ele, "50 passa" nao
  // distingue a regra nova de nenhuma regra.
  CheckBlocked(tr::CanTransfer(ht, cp::Game::kScarletViolet, lv49),
               "Hyper Training lv49 NAO entra no SV");
  CheckAllowed(tr::CanTransfer(ht, cp::Game::kScarletViolet, lv50),
               "Hyper Training lv50 ENTRA no SV (a regra nova)");

  // BDSP e SwSh: continuam em 100, e a borda 99/100 prova que sao 100 mesmo.
  CheckBlocked(tr::CanTransfer(ht, cp::Game::kBdsp, lv99),
               "Hyper Training lv99 NAO entra no BDSP");
  CheckAllowed(tr::CanTransfer(ht, cp::Game::kBdsp, lv100),
               "Hyper Training lv100 entra no BDSP");
  CheckBlocked(tr::CanTransfer(ht, cp::Game::kSwordShield, lv50),
               "Hyper Training lv50 NAO entra no SwSh (exige 100)");
  CheckAllowed(tr::CanTransfer(ht, cp::Game::kSwordShield, lv100),
               "Hyper Training lv100 entra no SwSh");

  // CONTRA-CASO obrigatorio: sem a flag, o lv49 passa em toda parte. Sem
  // isto, um bloqueio geral por nivel se disfarcaria de regra de HT.
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kScarletViolet, lv49),
               "sem Hyper Training, lv49 entra no SV");
  CheckAllowed(tr::CanTransfer(Mon(25), cp::Game::kBdsp, lv49),
               "sem Hyper Training, lv49 entra no BDSP");

  // Destino sem a mecanica (pre-gen7) nao barra: a flag nem viaja para la.
  CheckAllowed(tr::CanTransfer(ht, cp::Game::kEmerald, lv49),
               "Hyper Training lv49 entra no Emerald (sem a mecanica)");
}


int main() {
  std::printf("regras de transferencia (spec 070)\n\n");

  TestRegraGeral();
  TestSpec065();
  TestOvo();
  TestLetsGo();
  TestBdspLendario();
  TestBdspGigantamax();
  TestBdspHyperTraining();
  TestGolpeAusenteAvisa();
  TestTeraType();
  TestEffortLevels();
  TestSlotVazio();
  // spec 076 — as pendencias residuais P-02 e P-03 da spec 070.
  TestFusoes();
  TestLevelFromExp();
  TestPk9UsaDexNacional();
  TestParidadeSpec135();
  TestPortaoPorFormaSpec137();
  TestSpindaLiberadoSpec137();
  TestHyperTrainingPorJogoSpec137();

  std::printf("\n%s\n", g_failures == 0 ? "TUDO OK"
                                        : "FALHAS ACIMA");
  return g_failures == 0 ? 0 : 1;
}
