// Teste da compatibilidade de golpes por jogo (spec 038).
//
// Os casos conferem contra fatos publicos: os tetos de ID por geracao e golpes
// emblematicos de cada uma. Se o gerador resolver o enum Move com o offset
// errado, os tetos saem plausiveis mas deslocados, e sao estes testes que
// pegam — a mesma disciplina da spec 034.
//
// O ponto central: golpe AVISA, especie BLOQUEIA. Sao consultas independentes.

#include <cstdio>

#include <cstdint>

#include "game_moves.h"
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

// Os tetos por geracao sao publicos e verificaveis: e o ultimo golpe que cada
// geracao introduziu.
void TestTetosPorGeracao() {
  std::printf("teto de move ID por geracao (spec 038):\n");

  Check(cp::MaxMoveId(cp::Game::kRedBlue) == 165, "gen1 vai ate 165");
  Check(cp::MaxMoveId(cp::Game::kGoldSilver) == 251, "gen2 vai ate 251");
  Check(cp::MaxMoveId(cp::Game::kFireRed) == 354, "gen3 vai ate 354");
  Check(cp::MaxMoveId(cp::Game::kDiamondPearl) == 467, "gen4 vai ate 467");
  Check(cp::MaxMoveId(cp::Game::kBlackWhite) == 559, "gen5 vai ate 559");
  Check(cp::MaxMoveId(cp::Game::kXY) == 617, "X/Y vai ate 617");
  // ORAS acrescentou golpes sobre X/Y — os tetos nao podem ser iguais.
  Check(cp::MaxMoveId(cp::Game::kOmegaAlpha) == 621, "ORAS vai ate 621");
  Check(cp::MaxMoveId(cp::Game::kLetsGo) == 742,
        "Let's Go vai ate 742 (Double Iron Bash)");
  Check(cp::MaxMoveId(cp::Game::kSwordShield) == 826,
        "SwSh vai ate 826 (Eerie Spell, DLC 2)");

  // Os quatro jogos do gen3 compartilham o teto, como compartilham a lista de
  // especies (spec 034).
  Check(cp::MaxMoveId(cp::Game::kRubySapphire) ==
            cp::MaxMoveId(cp::Game::kFireRed),
        "RS e FR tem o mesmo teto");
  Check(cp::MaxMoveId(cp::Game::kEmerald) == cp::MaxMoveId(cp::Game::kLeafGreen),
        "E e LG tem o mesmo teto");
}

// Um golpe do gen1 e o caso mais forte: nenhuma geracao removeu golpe, entao
// Pound (1) tem de existir nos 23 jogos.
void TestGolpeAntigoExisteEmTodoJogo() {
  std::printf("golpe do gen1 existe em todo jogo (spec 038):\n");

  int faltando = 0;
  for (int g = 0; g < static_cast<int>(cp::Game::kCount); ++g) {
    const cp::Game game = static_cast<cp::Game>(g);
    if (!cp::HasMove(game, 1)) ++faltando;    // Pound
    if (!cp::HasMove(game, 165)) ++faltando;  // Struggle, o ultimo do gen1
  }
  Check(faltando == 0, "Pound e Struggle existem nos 23 jogos");
}

void TestGolpeFuturoNaoExisteEmJogoAntigo() {
  std::printf("golpe de geracao futura (spec 038):\n");

  // Close Combat (370) e do gen4: nao existe em nenhum jogo do gen3.
  Check(!cp::HasMove(cp::Game::kFireRed, 370),
        "FireRed recusa Close Combat (gen4)");
  Check(cp::HasMove(cp::Game::kDiamondPearl, 370),
        "Diamond/Pearl conhece Close Combat");

  // Um golpe do gen3 e desconhecido no gen1/2, mas conhecido dai pra frente.
  Check(!cp::HasMove(cp::Game::kRedBlue, 354), "Red/Blue recusa golpe do gen3");
  Check(!cp::HasMove(cp::Game::kCrystal, 354), "Crystal recusa golpe do gen3");
  Check(cp::HasMove(cp::Game::kEmerald, 354), "Emerald conhece golpe do gen3");
  Check(cp::HasMove(cp::Game::kScarletViolet, 354),
        "Scarlet/Violet ainda conhece golpe do gen3");

  // Malignant Chain (919) e do gen9: so os jogos mais novos o conhecem.
  Check(cp::HasMove(cp::Game::kScarletViolet, 919), "SV conhece o golpe 919");
  Check(!cp::HasMove(cp::Game::kSwordShield, 919), "SwSh recusa o golpe 919");
  Check(!cp::HasMove(cp::Game::kLegendsArceus, 919), "PLA recusa o golpe 919");
}

void TestSlotVazio() {
  std::printf("slot vazio (spec 038):\n");

  // Golpe 0 e slot vazio, nao golpe inexistente. Se gerasse aviso, todo
  // Pokemon com menos de 4 golpes ficaria amarelo — o aviso viraria ruido.
  Check(cp::HasMove(cp::Game::kRedBlue, 0), "golpe 0 nunca gera aviso");
  Check(cp::HasMove(cp::Game::kFireRed, 0), "golpe 0 vale em FireRed tambem");

  const std::uint16_t vazio[4] = {0, 0, 0, 0};
  Check(cp::MissingMoveIn(cp::Game::kRedBlue, vazio, 4) == 0,
        "moveset todo vazio nao gera aviso");
}

void TestForaDaFaixa() {
  std::printf("robustez (spec 038):\n");

  Check(!cp::HasMove(cp::Game::kFireRed, -1), "golpe negativo e recusado");
  Check(!cp::HasMove(cp::Game::kFireRed, 9999), "golpe enorme e recusado");
  Check(!cp::HasMove(cp::Game::kScarletViolet, cp::kMaxMoveId + 1),
        "golpe acima do maximo e recusado");
  Check(cp::HasMove(cp::Game::kLegendsZA, cp::kMaxMoveId),
        "golpe no limite e aceito pelo jogo mais novo");

  Check(!cp::HasMove(cp::Game::kCount, 1), "jogo invalido e recusado");
  Check(cp::MaxMoveId(cp::Game::kCount) == 0, "teto de jogo invalido e zero");

  const std::uint16_t moves[4] = {370, 0, 0, 0};
  Check(cp::MissingMoveIn(cp::Game::kCount, moves, 4) == 0,
        "jogo invalido (NestBox) nao gera aviso");
  Check(cp::MissingMoveIn(cp::Game::kFireRed, nullptr, 4) == 0,
        "ponteiro nulo nao estoura");
  Check(cp::MissingMoveIn(cp::Game::kFireRed, moves, 0) == 0,
        "contagem zero nao estoura");
}

void TestMissingMoveIn() {
  std::printf("MissingMoveIn (spec 038):\n");

  // Tudo do gen1: cabe em qualquer lugar.
  const std::uint16_t antigos[4] = {1, 33, 52, 165};
  Check(cp::MissingMoveIn(cp::Game::kFireRed, antigos, 4) == 0,
        "moveset do gen1 nao gera aviso em FireRed");
  Check(cp::MissingMoveIn(cp::Game::kRedBlue, antigos, 4) == 0,
        "moveset do gen1 nao gera aviso em Red/Blue");

  // O golpe ausente e encontrado em qualquer uma das 4 posicoes.
  const std::uint16_t pos0[4] = {370, 1, 33, 52};
  const std::uint16_t pos1[4] = {1, 370, 33, 52};
  const std::uint16_t pos2[4] = {1, 33, 370, 52};
  const std::uint16_t pos3[4] = {1, 33, 52, 370};
  Check(cp::MissingMoveIn(cp::Game::kFireRed, pos0, 4) == 370,
        "acha o golpe ausente na posicao 0");
  Check(cp::MissingMoveIn(cp::Game::kFireRed, pos1, 4) == 370,
        "acha o golpe ausente na posicao 1");
  Check(cp::MissingMoveIn(cp::Game::kFireRed, pos2, 4) == 370,
        "acha o golpe ausente na posicao 2");
  Check(cp::MissingMoveIn(cp::Game::kFireRed, pos3, 4) == 370,
        "acha o golpe ausente na posicao 3");

  // Devolve o PRIMEIRO ausente, para o rodape ter algo determinstico a dizer.
  const std::uint16_t dois[4] = {1, 370, 400, 52};
  Check(cp::MissingMoveIn(cp::Game::kFireRed, dois, 4) == 370,
        "devolve o primeiro golpe ausente, nao o ultimo");

  // O mesmo moveset nao gera aviso num jogo que conhece os golpes.
  Check(cp::MissingMoveIn(cp::Game::kDiamondPearl, pos0, 4) == 0,
        "o mesmo moveset nao avisa em Diamond/Pearl");
}

// A assimetria que motiva a spec: as duas consultas sao independentes. Um
// Pokemon pode ter especie valida e golpe ausente (aviso, move) ou especie
// invalida (bloqueio, nao move).
void TestAvisoNaoEhBloqueio() {
  std::printf("golpe avisa, especie bloqueia (spec 038):\n");

  // Bulbasaur existe em FireRed; um golpe do gen4 nao.
  const int bulbasaur = 1;
  const std::uint16_t com_gen4[4] = {1, 370, 0, 0};

  Check(cp::HasSpecies(cp::Game::kFireRed, bulbasaur),
        "a especie cabe em FireRed (nao bloqueia)");
  Check(cp::MissingMoveIn(cp::Game::kFireRed, com_gen4, 4) == 370,
        "mas o golpe gera aviso");

  // O contrario: especie de gen9 em FireRed bloqueia, independente do golpe.
  const int sprigatito = 906;
  const std::uint16_t so_gen1[4] = {1, 33, 0, 0};
  Check(!cp::HasSpecies(cp::Game::kFireRed, sprigatito),
        "especie do gen9 e bloqueada em FireRed");
  Check(cp::MissingMoveIn(cp::Game::kFireRed, so_gen1, 4) == 0,
        "e o moveset dela pode nem gerar aviso — sao regras separadas");

  // No NestBox nada avisa nem bloqueia: e o cofre, como o HOME.
  Check(cp::MissingMoveIn(cp::Game::kCount, com_gen4, 4) == 0,
        "NestBox nao avisa de golpe");
}

}  // namespace

int main() {
  TestTetosPorGeracao();
  TestGolpeAntigoExisteEmTodoJogo();
  TestGolpeFuturoNaoExisteEmJogoAntigo();
  TestSlotVazio();
  TestForaDaFaixa();
  TestMissingMoveIn();
  TestAvisoNaoEhBloqueio();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
