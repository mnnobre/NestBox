// Characteristic, tipos e memo da tela de summary (spec 103).
//
// Os casos de characteristic foram conferidos contra o PKHeX
// (EntityCharacteristic.cs): maior IV decide o grupo, empate resolve a partir
// de ec % 6 na ordem do save, e a frase dentro do grupo e (maior IV) % 5.

#include <cstdio>

#include "gen9_base_stats.h"
#include "move_types.h"
#include "summary_facts.h"

namespace sm = pokehome::summary;

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

void TestCharacteristic() {
  std::printf("characteristic (spec 103):\n");

  // Sem empate: Atk (indice 1) e o maior, 20 % 5 == 0 -> "Proud of its power".
  // E o caso do Pikachu da captura de referencia.
  const std::uint8_t atk[6] = {10, 20, 5, 5, 5, 5};
  Check(sm::CharacteristicIndex(atk, 0) == 5, "maior IV unico decide o grupo");
  Check(std::string(sm::CharacteristicText(5)) == "Proud of its power",
        "frase do grupo Atk com IV % 5 == 0");

  // Frase dentro do grupo: 31 % 5 == 1 -> segunda frase do grupo HP.
  const std::uint8_t hp31[6] = {31, 0, 0, 0, 0, 0};
  Check(sm::CharacteristicIndex(hp31, 0) == 1, "IV 31 escolhe frase indice 1");
  Check(std::string(sm::CharacteristicText(1)) == "Takes plenty of siestas",
        "frase do grupo HP com IV % 5 == 1");

  // Empate total (todos 31): comeca em ec % 6 e o primeiro empatado vence.
  const std::uint8_t all31[6] = {31, 31, 31, 31, 31, 31};
  Check(sm::CharacteristicIndex(all31, 0) == 0 * 5 + 1, "empate: ec%6==0 -> HP");
  Check(sm::CharacteristicIndex(all31, 3) == 3 * 5 + 1, "empate: ec%6==3 -> Spe");
  Check(sm::CharacteristicIndex(all31, 5) == 5 * 5 + 1, "empate: ec%6==5 -> SpD");

  // Empate parcial: ec%6 aponta um stat que NAO esta no empate; o proximo na
  // ordem circular que iguala o maximo vence (PKHeX faz exatamente isto).
  const std::uint8_t partial[6] = {5, 31, 5, 5, 31, 5};
  Check(sm::CharacteristicIndex(partial, 2) == 4 * 5 + 1,
        "empate parcial: a partir de Def, SpA vence antes de Atk");
  Check(sm::CharacteristicIndex(partial, 5) == 1 * 5 + 1,
        "empate parcial: a partir de SpD, Atk vence na volta");

  // Tudo zero: HP ganha (max 0, frase 0).
  const std::uint8_t zeros[6] = {0, 0, 0, 0, 0, 0};
  Check(sm::CharacteristicIndex(zeros, 0) == 0, "IVs zerados -> primeiro grupo");
}

void TestTypes() {
  std::printf("tipos por dex (tabela da spec 099, travada aqui):\n");
  using pokehome::modern::kTypeIds;
  Check(kTypeIds[25][0] == 13 && kTypeIds[25][1] == 0,
        "Pikachu: Electric puro");
  Check(kTypeIds[1][0] == 12 && kTypeIds[1][1] == 4,
        "Bulbasaur: Grass/Poison");
  Check(kTypeIds[6][0] == 10 && kTypeIds[6][1] == 3,
        "Charizard: Fire/Flying");

  // Tipo por golpe (icone das linhas do cartao) — os 4 do Pikachu da captura.
  using pokehome::modern::kMoveTypeIds;
  Check(kMoveTypeIds[84] == 13, "Thunder Shock: Electric");
  Check(kMoveTypeIds[39] == 1, "Tail Whip: Normal");
  Check(kMoveTypeIds[45] == 1, "Growl: Normal");
  Check(kMoveTypeIds[98] == 1, "Quick Attack: Normal");
}

void TestMemo() {
  std::printf("memo de encontro (spec 103):\n");

  // O caso da captura de referencia: fateful no HOME em 17/08/2026.
  const std::uint8_t d[3] = {26, 8, 17};
  Check(sm::BuildMemo(true, 0, d) ==
            "Seems to have had a fateful encounter in Pokemon HOME "
            "on 8/17/2026.",
        "fateful com origem desconhecida cai no Pokemon HOME");

  Check(sm::BuildMemo(false, 44, d) ==
            "Seems to have met in Pokemon Sword on 8/17/2026.",
        "encontro normal com jogo e data");

  const std::uint8_t none[3] = {0, 0, 0};
  Check(sm::BuildMemo(false, 4, none) ==
            "Seems to have met in Pokemon FireRed.",
        "gen3 sem data mostra so o jogo");
}

}  // namespace

int main() {
  TestCharacteristic();
  TestTypes();
  TestMemo();
  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("todos os testes passaram\n");
  return 0;
}
