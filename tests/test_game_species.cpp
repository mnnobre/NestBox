// Teste da matriz de compatibilidade por jogo (spec 034).
//
// Os casos abaixo conferem contra fatos publicos e conhecidos — o corte do
// "Dexit" em SwSh, a dex de Hisui, o que Let's Go aceita. Se o gerador ler o
// binario com o tamanho de entrada errado, os numeros saem plausiveis mas
// errados, e sao estes testes que pegam.

#include <cstdio>

#include <string>

#include "game_species.h"

namespace cp = pokehome::compat;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
  if (cond) {
    std::printf("  ok   %s\n", what);
  } else {
    std::printf("  FAIL %s\n", what);
    ++g_failures;
  }
}

void TestContagens() {
  std::printf("contagem por jogo (spec 034):\n");

  // Os numeros que provam que a extracao esta certa.
  Check(cp::SpeciesCount(cp::Game::kSwordShield) == 664,
        "SwSh tem 664 especies (o corte do Dexit)");
  // 242, nao 226: a spec 065 conferiu contra a API do PkHeX e achou que o
  // gerador da spec 034 contava so a forma 0, perdendo as 16 especies que so
  // existem em PLA como forma de Hisui (Growlithe, Sneasel, Decidueye...).
  Check(cp::SpeciesCount(cp::Game::kLegendsArceus) == 242,
        "Legends Arceus tem 242 (dex de Hisui, com as formas)");
  Check(cp::SpeciesCount(cp::Game::kFireRed) == 386,
        "FireRed tem 386 (bate com o gen3_personal.h do projeto)");
  Check(cp::SpeciesCount(cp::Game::kRedBlue) == 151,
        "Red/Blue tem 151 (Kanto)");
  Check(cp::SpeciesCount(cp::Game::kGoldSilver) == 251,
        "Gold/Silver tem 251 (Johto)");
  Check(cp::SpeciesCount(cp::Game::kLetsGo) == 153,
        "Let's Go tem 153 (Kanto + Meltan e Melmetal)");
  Check(cp::SpeciesCount(cp::Game::kBdsp) == 493,
        "BDSP tem 493 (Sinnoh)");
}

void TestSwSh() {
  std::printf("Sword/Shield (spec 034):\n");

  Check(cp::HasSpecies(cp::Game::kSwordShield, 1), "aceita Bulbasaur");
  Check(cp::HasSpecies(cp::Game::kSwordShield, 25), "aceita Pikachu");
  Check(cp::HasSpecies(cp::Game::kSwordShield, 888), "aceita Zacian");
  // O caso emblematico do Dexit: a linha do Chikorita ficou de fora.
  Check(!cp::HasSpecies(cp::Game::kSwordShield, 152), "recusa Chikorita");
}

void TestLegendsArceus() {
  std::printf("Legends: Arceus (spec 034):\n");

  Check(cp::HasSpecies(cp::Game::kLegendsArceus, 722), "aceita Rowlet");
  Check(cp::HasSpecies(cp::Game::kLegendsArceus, 493), "aceita Arceus");
  Check(!cp::HasSpecies(cp::Game::kLegendsArceus, 4), "recusa Charmander");
}

void TestLetsGo() {
  std::printf("Let's Go (spec 034):\n");

  Check(cp::HasSpecies(cp::Game::kLetsGo, 1), "aceita Bulbasaur");
  Check(cp::HasSpecies(cp::Game::kLetsGo, 151), "aceita Mew");
  Check(cp::HasSpecies(cp::Game::kLetsGo, 808), "aceita Meltan");
  Check(cp::HasSpecies(cp::Game::kLetsGo, 809), "aceita Melmetal");
  // Aqui a tabela do PKHeX herda o gen7 inteiro; sem a regra escrita a mao,
  // Chikorita passaria (TD-02 da spec 034).
  Check(!cp::HasSpecies(cp::Game::kLetsGo, 152), "recusa Chikorita");
  Check(!cp::HasSpecies(cp::Game::kLetsGo, 251), "recusa Celebi");
}

void TestGen3() {
  std::printf("gen3 (spec 034):\n");

  Check(cp::HasSpecies(cp::Game::kFireRed, 1), "FireRed aceita Bulbasaur");
  Check(cp::HasSpecies(cp::Game::kFireRed, 386), "FireRed aceita Deoxys");
  Check(!cp::HasSpecies(cp::Game::kFireRed, 387),
        "FireRed recusa Turtwig (gen4)");
  Check(cp::HasSpecies(cp::Game::kEmerald, 386), "Emerald aceita Deoxys");
}

void TestForaDaFaixa() {
  std::printf("robustez (spec 034):\n");

  Check(!cp::HasSpecies(cp::Game::kFireRed, 0), "dex 0 e recusado");
  Check(!cp::HasSpecies(cp::Game::kFireRed, -1), "dex negativo e recusado");
  Check(!cp::HasSpecies(cp::Game::kFireRed, 9999), "dex enorme e recusado");
  Check(!cp::HasSpecies(cp::Game::kFireRed, cp::kMaxDex + 1),
        "dex acima do maximo e recusado");
  Check(cp::HasSpecies(cp::Game::kScarletViolet, cp::kMaxDex) ||
            !cp::HasSpecies(cp::Game::kScarletViolet, cp::kMaxDex),
        "dex no limite nao estoura");

  Check(!cp::HasSpecies(cp::Game::kCount, 1), "jogo invalido e recusado");
  Check(std::string(cp::GameName(cp::Game::kCount)).empty(),
        "nome de jogo invalido e vazio");
  Check(cp::SpeciesCount(cp::Game::kCount) == 0,
        "contagem de jogo invalido e zero");
}

void TestNomes() {
  std::printf("nomes dos jogos (spec 034):\n");

  Check(std::string(cp::GameName(cp::Game::kFireRed)) == "FireRed",
        "FireRed tem nome");
  Check(std::string(cp::GameName(cp::Game::kSwordShield)) == "Sword/Shield",
        "SwSh tem nome");
  Check(static_cast<int>(cp::Game::kCount) == 23, "sao 23 jogos na matriz");
}

// A regra como a UI a usa: o painel de destino decide. O NestBox aceita tudo
// (e o cofre, como o HOME), e um save so aceita o que o jogo dele conhece.
void TestRegraDoDestino() {
  std::printf("regra do painel de destino (spec 034):\n");

  // Espelha BoxPanel::FitsInPanel: kCount significa "nao e um jogo".
  auto fits = [](cp::Game dest, int dex) {
    if (dest == cp::Game::kCount) return true;
    return cp::HasSpecies(dest, dex);
  };

  // NestBox aceita ate o que nenhum jogo do gen3 conhece.
  Check(fits(cp::Game::kCount, 906), "NestBox aceita Sprigatito (gen9)");
  Check(fits(cp::Game::kCount, 1), "NestBox aceita Bulbasaur");

  // Descer para um save gen3: so as 386.
  Check(fits(cp::Game::kFireRed, 386), "FireRed aceita Deoxys");
  Check(!fits(cp::Game::kFireRed, 906), "FireRed RECUSA Sprigatito");
  Check(!fits(cp::Game::kFireRed, 493), "FireRed recusa Arceus (gen4)");

  // O caso que motiva a spec: um Pokemon guardado no banco vindo de um jogo
  // novo nao pode descer para um save antigo.
  Check(!fits(cp::Game::kFireRed, 800), "FireRed recusa Necrozma (gen7)");
  Check(fits(cp::Game::kLegendsZA, 800) || !fits(cp::Game::kLegendsZA, 800),
        "ZA responde sem estourar");
}

// Os quatro jogos do gen3 tem a MESMA lista. E o que permite o app nao
// precisar distinguir qual deles o save e (decisao registrada na spec 034).
void TestGen3Equivalentes() {
  std::printf("jogos do gen3 sao equivalentes (spec 034):\n");

  int divergencias = 0;
  for (int dex = 1; dex <= 386; ++dex) {
    const bool fr = cp::HasSpecies(cp::Game::kFireRed, dex);
    if (fr != cp::HasSpecies(cp::Game::kLeafGreen, dex)) ++divergencias;
    if (fr != cp::HasSpecies(cp::Game::kRubySapphire, dex)) ++divergencias;
    if (fr != cp::HasSpecies(cp::Game::kEmerald, dex)) ++divergencias;
  }
  Check(divergencias == 0,
        "FR, LG, RS e E aceitam exatamente as mesmas especies");
}

}  // namespace

int main() {
  TestContagens();
  TestSwSh();
  TestLegendsArceus();
  TestLetsGo();
  TestGen3();
  TestForaDaFaixa();
  TestNomes();
  TestRegraDoDestino();
  TestGen3Equivalentes();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
