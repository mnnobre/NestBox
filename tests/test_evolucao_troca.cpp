// Teste da tabela de evolucao por troca (spec 146).
//
// A tabela e GERADA por `tools/pkhex-alpha --gera-troca`, e a geracao ja
// mordeu duas vezes:
//
//   1. `GetForward(dex, 0)` NAO valida o teto da arvore. Pedir dex 788 na
//      arvore Gen6 (que vai ate 721) devolvia lixo, e a tabela nascia com
//      "TapuFini -> Gourgeist". Por isso ha teste de que nada absurdo entrou.
//   2. Clamperl evolui para Huntail OU Gorebyss conforme o item segurado, e o
//      item nunca viaja (DEC-1). Duas linhas com a mesma base fariam a busca
//      binaria escolher uma em silencio; DEC-4 tirou as duas.
//
// A busca binaria depende da tabela estar ORDENADA e sem base repetida. Isso
// e invariante de geracao, nao de uso — se um dia a sonda emitir fora de
// ordem, `AlvoDaTroca` passa a errar silenciosamente. Por isso o teste
// confere a estrutura, nao so alguns acertos.

#include <cstdio>

#include "evolucao_troca.h"
#include "game_species.h"

namespace ev = pokehome::evo;
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

constexpr std::size_t kPares = sizeof(ev::kTrocaEvo) / sizeof(ev::kTrocaEvo[0]);

void TestParesConhecidos() {
  std::printf("pares medidos pela sonda (spec 146):\n");

  // O caso da spec: a sonda mediu Haunter -> Gengar com legal=true.
  Check(ev::AlvoDaTroca(93) == 94, "Haunter (93) evolui para Gengar (94)");
  Check(ev::AlvoDaTroca(67) == 68, "Machoke (67) evolui para Machamp (68)");
  Check(ev::AlvoDaTroca(64) == 65, "Kadabra (64) evolui para Alakazam (65)");
  Check(ev::AlvoDaTroca(61) == 186, "Poliwhirl (61) evolui para Politoed");
  Check(ev::AlvoDaTroca(708) == 709, "Phantump (708) evolui para Trevenant");

  // A DEC-1 disse que o item nao e requisito: as evolucoes por TradeHeldItem
  // entram junto com as de troca simples.
  Check(ev::AlvoDaTroca(123) == 212, "Scyther evolui (era TradeHeldItem)");
  Check(ev::AlvoDaTroca(137) == 233, "Porygon evolui (era TradeHeldItem)");
}

void TestQuemNaoEvolui() {
  std::printf("quem NAO evolui por troca:\n");

  Check(ev::AlvoDaTroca(1) == 0, "Bulbasaur nao evolui por troca");
  Check(ev::AlvoDaTroca(25) == 0, "Pikachu nao evolui por troca");
  Check(ev::AlvoDaTroca(94) == 0, "Gengar (ja evoluido) nao evolui de novo");

  // DEC-4: Clamperl tem DOIS alvos possiveis e ficou de fora inteiro. Se
  // voltar, a busca binaria passa a escolher um dos dois em silencio.
  Check(ev::AlvoDaTroca(366) == 0, "Clamperl fica de fora (ambiguo, DEC-4)");

  // TradeShelmetKarrablast exige troca casada, que o NestBox nao modela.
  Check(ev::AlvoDaTroca(588) == 0, "Karrablast fica de fora (troca casada)");
  Check(ev::AlvoDaTroca(616) == 0, "Shelmet fica de fora (troca casada)");
}

void TestForaDaFaixa() {
  std::printf("bordas (a busca binaria nao pode estourar):\n");

  Check(ev::AlvoDaTroca(0) == 0, "dex 0 devolve 0");
  Check(ev::AlvoDaTroca(-5) == 0, "dex negativo devolve 0");
  Check(ev::AlvoDaTroca(9999) == 0, "dex muito alto devolve 0");
  Check(ev::AlvoDaTroca(1025) == 0, "dex do teto devolve 0");
}

void TestEstruturaDaTabela() {
  std::printf("invariantes da tabela gerada:\n");

  Check(kPares == 22, "a sonda gerou 22 pares");

  // A busca binaria SO funciona ordenada. Base repetida faria dois alvos
  // competirem pela mesma chave.
  bool ordenada = true, sem_repetida = true;
  for (std::size_t i = 1; i < kPares; ++i) {
    if (ev::kTrocaEvo[i - 1].base > ev::kTrocaEvo[i].base) ordenada = false;
    if (ev::kTrocaEvo[i - 1].base == ev::kTrocaEvo[i].base) sem_repetida = false;
  }
  Check(ordenada, "ordenada por dex da base");
  Check(sem_repetida, "nenhuma base aparece duas vezes");

  // O bug do teto: alvo fora da faixa, ou base evoluindo para si mesma, sao
  // as formas que o lixo tomava.
  bool alvos_sensatos = true;
  for (std::size_t i = 0; i < kPares; ++i) {
    const auto& p = ev::kTrocaEvo[i];
    if (p.base == 0 || p.base > 1025) alvos_sensatos = false;
    if (p.alvo == 0 || p.alvo > 1025) alvos_sensatos = false;
    if (p.base == p.alvo) alvos_sensatos = false;
  }
  Check(alvos_sensatos, "todo par tem dex valido e base != alvo");

  // EvoluiPorTroca e AlvoDaTroca nao podem divergir em ponto nenhum.
  bool concordam = true;
  int achados = 0;
  for (int d = -10; d <= 1200; ++d) {
    const int alvo = ev::AlvoDaTroca(d);
    if (ev::EvoluiPorTroca(d) != (alvo != 0)) concordam = false;
    if (alvo != 0) ++achados;
  }
  Check(concordam, "EvoluiPorTroca concorda com AlvoDaTroca em toda a faixa");
  Check(achados == static_cast<int>(kPares),
        "varrer a dex inteira acha exatamente os pares da tabela");
}

void TestPresoNoJogoDeOrigem() {
  std::printf("o caso que a DEC-2 assumiu:\n");

  // A pergunta acontece na saida jogo -> NestBox, onde nao ha jogo de destino
  // para travar. Consequencia medida: evoluir pode impedir a volta ao jogo de
  // origem. Estes sao dois dos 52 casos, e existem de proposito — o dialogo
  // avisa em vez de bloquear.
  Check(cp::HasSpecies(cp::Game::kRedBlue, 79) &&
            !cp::HasSpecies(cp::Game::kRedBlue, 199),
        "Slowpoke existe em Red/Blue mas Slowking nao (nao volta)");
  Check(cp::HasSpecies(cp::Game::kLetsGo, 137) &&
            !cp::HasSpecies(cp::Game::kLetsGo, 233),
        "Porygon existe em Let's Go mas Porygon2 nao (nao volta)");

  // Nos modernos o par some junto, entao nao ha meio-Pokemon.
  Check(!cp::HasSpecies(cp::Game::kScarletViolet, 67) &&
            !cp::HasSpecies(cp::Game::kScarletViolet, 68),
        "SV nao tem nem Machoke nem Machamp (somem juntos)");

  // E o caso que sempre funciona: Gengar existe em todo lugar que tem Haunter.
  Check(cp::HasSpecies(cp::Game::kSwordShield, 94),
        "Gengar existe no SwSh (a evolucao da spec cabe)");
}

}  // namespace

int main() {
  TestParesConhecidos();
  TestQuemNaoEvolui();
  TestForaDaFaixa();
  TestEstruturaDaTabela();
  TestPresoNoJogoDeOrigem();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
