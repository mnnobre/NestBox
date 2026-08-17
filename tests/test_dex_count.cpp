// Teste da contagem de Pokedex sobre varias fontes (spec 026).

#include <cstdio>

#include "dex_count.h"

namespace dx = pokehome::dex;

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

void TestUniao() {
  std::printf("uniao de duas fontes (spec 026):\n");

  dx::DexTally t(386);
  Check(t.Count() == 0, "comeca zerada");

  t.Add(25, dx::kSave);   // Pikachu no save
  t.Add(6, dx::kNest);    // Charizard no NestBox
  Check(t.Count() == 2, "soma as duas fontes");

  // A MESMA especie nos dois paineis conta UMA vez — e o ponto da spec.
  t.Add(25, dx::kNest);
  Check(t.Count() == 2, "especie nos dois paineis nao conta duas vezes");

  Check(t.Has(25) && t.Has(6), "as duas especies estao registradas");
  Check(!t.Has(150), "especie ausente nao aparece");
}

void TestOrigem() {
  std::printf("origem por painel (spec 026):\n");

  dx::DexTally t(386);
  t.Add(25, dx::kSave);
  t.Add(6, dx::kNest);
  t.Add(1, dx::kSave);
  t.Add(1, dx::kNest);  // Bulbasaur nos dois

  Check(t.OriginOf(25) == dx::kSave, "Pikachu so no save");
  Check(t.OriginOf(6) == dx::kNest, "Charizard so no NestBox");
  Check((t.OriginOf(1) & dx::kSave) && (t.OriginOf(1) & dx::kNest),
        "Bulbasaur marcado nos dois");

  Check(t.CountFrom(dx::kSave) == 2, "duas especies vem do save");
  Check(t.CountFrom(dx::kNest) == 2, "duas especies vem do NestBox");
  Check(t.Count() == 3, "mas sao tres especies distintas no total");
}

void TestMovido() {
  std::printf("Pokemon movido entre paineis (spec 026):\n");

  // Simula o que a UI faz APOS a movimentacao: o slot do save esta vazio (nao
  // registra nada) e o do NestBox tem o Pokemon.
  dx::DexTally t(386);
  t.Add(25, dx::kNest);  // so o destino registra

  Check(t.Count() == 1, "movido continua contado uma vez");
  Check(t.Has(25), "e continua na dex — nao sumiu");
  Check(t.OriginOf(25) == dx::kNest, "agora consta como estando no NestBox");
}

void TestForaDaFaixa() {
  std::printf("robustez (spec 026):\n");

  dx::DexTally t(386);
  t.Add(0, dx::kSave);
  t.Add(-1, dx::kSave);
  t.Add(1025, dx::kSave);  // dex de gen9, fora da tabela gen3
  Check(t.Count() == 0, "dex fora da faixa nao entra nem estoura");

  Check(!t.Has(0) && !t.Has(-1) && !t.Has(9999),
        "consulta fora da faixa devolve falso");
  Check(t.OriginOf(9999) == dx::kNowhere, "origem fora da faixa e kNowhere");

  // Borda: o ultimo dex valido do gen3 tem que caber.
  t.Add(386, dx::kSave);
  Check(t.Has(386), "dex 386 (Deoxys, ultimo do gen3) cabe na tabela");
}

}  // namespace

int main() {
  TestUniao();
  TestOrigem();
  TestMovido();
  TestForaDaFaixa();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
