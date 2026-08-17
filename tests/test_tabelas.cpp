// Teste das tabelas de dado extraidas do PkHeX (spec 065).
//
// Tres tabelas, tres lacunas da descoberta escrita-e-transferencia:
//   1. indice interno do gen9 -> National Dex   (pendencia da spec 059)
//   2. learnset por nivel de PLA e BDSP         (G12)
//   3. especies por jogo                        (G07)
//
// O teste da tabela 1 nao usa numero escrito a mao: ele le AS 16 FIXTURES do
// PK9, tira o indice interno do binario e o National Dex do JSON do PkHeX, e
// exige que a tabela ligue um ao outro. Se a tabela estiver deslocada, todas
// as fixtures reprovam de uma vez.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "game_species.h"
#include "gen9_species_id.h"
#include "learnset.h"
#include "pk9.h"

namespace cp = pokehome::compat;
namespace g9 = pokehome::gen9;
namespace ls = pokehome::learnset;

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
  if (ok) {
    std::printf("  ok   %s\n", what.c_str());
  } else {
    std::printf("  FAIL %s\n", what.c_str());
    ++g_failures;
  }
}

void Eq(long long ours, long long theirs, const std::string& what) {
  if (ours == theirs) {
    std::printf("  ok   %s\n", what.c_str());
  } else {
    std::printf("  FAIL %s — nosso %lld, esperado %lld\n", what.c_str(), ours,
                theirs);
    ++g_failures;
  }
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
  // filesystem::path, nao string: uma das fixtures tem a estrela do shiny no
  // nome e o ifstream(string) usa a codepage ANSI no Windows.
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

// Mesmo leitor de JSON achatado dos testes de parser.
std::map<std::string, std::string> ReadFlatJson(
    const std::filesystem::path& path) {
  std::map<std::string, std::string> out;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    const auto q1 = line.find('"');
    if (q1 == std::string::npos) continue;
    const auto q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos) continue;
    const auto colon = line.find(':', q2);
    if (colon == std::string::npos) continue;
    std::string value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') value.erase(0, 1);
    while (!value.empty() && (value.back() == ',' || value.back() == ' ')) {
      value.pop_back();
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    out[line.substr(q1 + 1, q2 - q1 - 1)] = value;
  }
  return out;
}

long long AsInt(const std::map<std::string, std::string>& j,
                const std::string& key) {
  const auto it = j.find(key);
  if (it == j.end()) return -1;
  if (it->second == "true") return 1;
  if (it->second == "false") return 0;
  try {
    return std::stoll(it->second);
  } catch (...) {
    return -1;
  }
}

// --------------------------------------------------------------------------
// 1. Conversao gen9 — provada contra as 16 fixtures reais.
// --------------------------------------------------------------------------

void TestConversaoGen9CasoDocumentado() {
  std::printf("gen9: o caso documentado na spec 059:\n");

  // O binario do Pawmo guarda 955; a dex nacional dele e 922.
  Eq(g9::ToNational(955), 922, "interno 955 -> nacional 922 (Pawmo)");
  Eq(g9::ToInternal(922), 955, "nacional 922 -> interno 955 (Pawmo)");

  // Gen 1-8 nao foi renumerado: a identidade vale ate onde o gen9 nao mexeu.
  Eq(g9::ToNational(25), 25, "Pikachu e 25 nos dois");
  Eq(g9::ToNational(1), 1, "Bulbasaur e 1 nos dois");
}

void TestConversaoGen9Robustez() {
  std::printf("gen9: robustez:\n");

  Eq(g9::ToNational(0), 0, "interno 0 devolve 0");
  Eq(g9::ToInternal(0), 0, "nacional 0 devolve 0");
  Eq(g9::ToNational(g9::kMaxInternalId + 1), 0, "interno alem do maximo -> 0");
  Eq(g9::ToInternal(g9::kMaxNationalDex + 1), 0, "nacional alem do maximo -> 0");
  Eq(g9::ToNational(65535), 0, "interno absurdo -> 0");
}

// A ida e a volta tem de fechar para TODA especie do gen9. Se a tabela
// estiver deslocada em qualquer ponto, este teste pega.
void TestConversaoGen9Roundtrip() {
  std::printf("gen9: roundtrip nacional -> interno -> nacional:\n");

  int checados = 0, quebrados = 0;
  for (std::uint16_t dex = 1; dex <= g9::kMaxNationalDex; ++dex) {
    const std::uint16_t interno = g9::ToInternal(dex);
    if (interno == 0) continue;  // especie fora do gen9
    ++checados;
    if (g9::ToNational(interno) != dex) ++quebrados;
  }
  Check(quebrados == 0, "roundtrip fecha em todas as " +
                            std::to_string(checados) + " especies do gen9");
  Check(checados > 900, "a tabela cobre o gen9 inteiro (" +
                            std::to_string(checados) + " especies)");
}

// O teste central da lacuna 1: para cada fixture, o binario da o indice
// interno e o JSON do PkHeX da o National Dex. A tabela tem de ligar os dois.
void TestConversaoGen9ContraFixtures() {
  std::printf("gen9: conversao contra as fixtures reais do PkHeX:\n");

  const std::filesystem::path dir(std::string(PKM_FIXTURES) + "pk9");
  if (!std::filesystem::exists(dir)) {
    std::printf("  FAIL diretorio de fixtures ausente: %s\n",
                dir.string().c_str());
    ++g_failures;
    return;
  }

  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".pk9") continue;

    std::filesystem::path json = entry.path();
    json.replace_extension(".json");
    const std::filesystem::path json_path =
        std::filesystem::path(PKHEX_JSON) / "pk9" / json.filename();

    const auto bytes = ReadFile(entry.path());
    const auto j = ReadFlatJson(json_path);
    const std::string base = entry.path().stem().string();
    if (bytes.empty() || j.empty()) {
      std::printf("  FAIL fixture/json ausente para %s\n", base.c_str());
      ++g_failures;
      continue;
    }

    const auto parsed = pk9::Parse(bytes);
    if (!parsed) {
      std::printf("  FAIL %s nao parseia\n", base.c_str());
      ++g_failures;
      continue;
    }

    // p.species e o indice INTERNO (o que o binario guarda).
    const std::uint16_t interno = parsed->species;
    const long long dex_pkhex = AsInt(j, "Species");

    Eq(g9::ToNational(interno), dex_pkhex,
       base + ": interno " + std::to_string(interno) + " -> dex do PkHeX");
    // E a volta tambem, para o mesmo Pokemon.
    Eq(g9::ToInternal(static_cast<std::uint16_t>(dex_pkhex)), interno,
       base + ": dex do PkHeX -> interno " + std::to_string(interno));
    ++count;
  }

  // Nao fixamos o numero exato: outra spec pode acrescentar fixture, e o
  // teste nao deve quebrar por isso. O que importa e que TODAS foram
  // conferidas e que o conjunto nao encolheu.
  Check(count >= 16, "todas as fixtures do PK9 foram conferidas (" +
                         std::to_string(count) + ", minimo 16)");
}

// --------------------------------------------------------------------------
// 2. Learnset por nivel.
// --------------------------------------------------------------------------

// Ids de golpe nacionais, os mesmos de gen3_moves.h.
constexpr std::uint16_t kTackle = 33;
constexpr std::uint16_t kGrowl = 45;
constexpr std::uint16_t kVineWhip = 22;
constexpr std::uint16_t kScratch = 10;
constexpr std::uint16_t kEmber = 52;

void TestLearnsetCasosConhecidos() {
  std::printf("learnset: casos conhecidos dos iniciais:\n");

  // BDSP e um remake de Sinnoh mas contem os iniciais de Kanto.
  Eq(ls::LevelOf(ls::Game::kBdsp, 1, 0, kTackle), 1,
     "BDSP: Bulbasaur aprende Tackle no nivel 1");
  Eq(ls::LevelOf(ls::Game::kBdsp, 1, 0, kGrowl), 1,
     "BDSP: Bulbasaur aprende Growl no nivel 1");
  Eq(ls::LevelOf(ls::Game::kBdsp, 1, 0, kVineWhip), 3,
     "BDSP: Bulbasaur aprende Vine Whip no nivel 3");
  Eq(ls::LevelOf(ls::Game::kBdsp, 4, 0, kScratch), 1,
     "BDSP: Charmander aprende Scratch no nivel 1");
  Eq(ls::LevelOf(ls::Game::kBdsp, 4, 0, kEmber), 4,
     "BDSP: Charmander aprende Ember no nivel 4");

  // O ponto da tabela existir: a MESMA especie aprende o MESMO golpe em
  // niveis diferentes conforme a engine. Cyndaquil e Ember:
  Eq(ls::LevelOf(ls::Game::kBdsp, 155, 0, kEmber), 10,
     "BDSP: Cyndaquil aprende Ember no nivel 10");
  Eq(ls::LevelOf(ls::Game::kLegendsArceus, 155, 0, kEmber), 6,
     "PLA: Cyndaquil aprende Ember no nivel 6 (engine diferente)");
}

void TestLearnsetNaoVazio() {
  std::printf("learnset: tabelas povoadas:\n");

  const ls::Entry* e = nullptr;
  std::size_t n = 0;

  Check(ls::Find(ls::Game::kBdsp, 1, 0, &e, &n) && n > 5,
        "BDSP: Bulbasaur tem learnset com varios golpes");
  Check(ls::Find(ls::Game::kLegendsArceus, 155, 0, &e, &n) && n > 5,
        "PLA: Cyndaquil tem learnset com varios golpes");

  // Nenhuma entrada do indice pode apontar para faixa vazia ou invertida.
  int vazias = 0, invalidas = 0;
  for (int g = 0; g < static_cast<int>(ls::Game::kCount); ++g) {
    const ls::Table& t = ls::kTables[g];
    for (std::size_t i = 0; i < t.index_count; ++i) {
      if (t.index[i].end <= t.index[i].begin) ++vazias;
      if (t.index[i].end > t.entry_count) ++invalidas;
    }
  }
  Check(vazias == 0, "nenhuma faixa vazia no indice");
  Check(invalidas == 0, "nenhuma faixa aponta fora do vetor de pares");

  // Todo golpe tem id valido e nivel plausivel.
  int golpes_zero = 0, niveis_altos = 0;
  for (int g = 0; g < static_cast<int>(ls::Game::kCount); ++g) {
    const ls::Table& t = ls::kTables[g];
    for (std::size_t i = 0; i < t.entry_count; ++i) {
      if (t.entries[i].move == 0) ++golpes_zero;
      if (t.entries[i].level > 100) ++niveis_altos;
    }
  }
  Check(golpes_zero == 0, "nenhum golpe com id 0");
  Check(niveis_altos == 0, "nenhum nivel acima de 100");
}

// As especies das fixtures do PK9 que existem em PLA/BDSP tem de ter
// learnset — se a tabela viesse vazia, o reset de moveset seria silencioso.
void TestLearnsetDasFixtures() {
  std::printf("learnset: especies das fixtures que existem em PLA/BDSP:\n");

  const ls::Entry* e = nullptr;
  std::size_t n = 0;

  // Growlithe (58) e Sneasel (215) sao das fixtures e existem em PLA — mas
  // como forma de Hisui. Munchlax (446) e Tauros (128) existem em BDSP.
  Check(ls::Find(ls::Game::kBdsp, 446, 0, &e, &n) && n > 0,
        "BDSP: Munchlax (fixture 0446) tem learnset");
  Check(ls::Find(ls::Game::kBdsp, 128, 0, &e, &n) && n > 0,
        "BDSP: Tauros (fixture 0128) tem learnset");
  Check(ls::Find(ls::Game::kLegendsArceus, 58, 1, &e, &n) && n > 0,
        "PLA: Growlithe de Hisui (fixture 0058-01) tem learnset");

  // Especie de gen9 nao existe em nenhum dos dois.
  Check(!ls::Find(ls::Game::kBdsp, 922, 0, &e, &n),
        "BDSP nao tem learnset de Pawmo (gen9)");
  Check(!ls::Find(ls::Game::kLegendsArceus, 922, 0, &e, &n),
        "PLA nao tem learnset de Pawmo (gen9)");
}

// A razao de a tabela existir: os 4 golpes ao resetar por nivel.
void TestResetPorNivel() {
  std::printf("learnset: os 4 golpes ao resetar por nivel:\n");

  std::uint16_t moves[4] = {0, 0, 0, 0};

  // Bulbasaur nivel 1 em BDSP: so Tackle e Growl.
  int n = ls::MovesAtLevel(ls::Game::kBdsp, 1, 0, 1, moves);
  Eq(n, 2, "BDSP: Bulbasaur nivel 1 tem 2 golpes");
  Check((moves[0] == kTackle && moves[1] == kGrowl) ||
            (moves[0] == kGrowl && moves[1] == kTackle),
        "BDSP: Bulbasaur nivel 1 sabe Tackle e Growl");

  // Nivel 50: quatro golpes, todos aprendidos ate ali.
  n = ls::MovesAtLevel(ls::Game::kBdsp, 1, 0, 50, moves);
  Eq(n, 4, "BDSP: Bulbasaur nivel 50 tem os 4 golpes");
  Check(ls::LevelOf(ls::Game::kBdsp, 1, 0, moves[3]) <= 50,
        "BDSP: o ultimo golpe foi aprendido ate o nivel 50");

  // Especie que nao existe no jogo devolve 0 e zera a saida.
  n = ls::MovesAtLevel(ls::Game::kBdsp, 922, 0, 50, moves);
  Eq(n, 0, "BDSP: Pawmo (gen9) nao devolve golpe nenhum");
  Eq(moves[0], 0, "a saida foi zerada");

  // Jogo invalido nao estoura.
  n = ls::MovesAtLevel(ls::Game::kCount, 1, 0, 50, moves);
  Eq(n, 0, "jogo invalido devolve 0");
}

// --------------------------------------------------------------------------
// 3. Especies por jogo — os 6 nativos do Switch (G07).
// --------------------------------------------------------------------------

void TestEspeciesPorJogoContagem() {
  std::printf("especies por jogo: contagens dos 6 jogos do Switch:\n");

  // Fatos publicos. LGPE, SwSh, BDSP e SV ja batiam na spec 034; PLA e Z-A
  // foram corrigidos nesta spec (ver evidence-log).
  Eq(cp::SpeciesCount(cp::Game::kLetsGo), 153, "Let's Go: 153");
  Eq(cp::SpeciesCount(cp::Game::kSwordShield), 664,
     "Sword/Shield: 664 (o corte do Dexit)");
  Eq(cp::SpeciesCount(cp::Game::kBdsp), 493, "BDSP: 493 (Sinnoh)");
  Eq(cp::SpeciesCount(cp::Game::kLegendsArceus), 242,
     "Legends: Arceus: 242 (a dex de Hisui inteira, com as formas)");
  Eq(cp::SpeciesCount(cp::Game::kScarletViolet), 733, "Scarlet/Violet: 733");
  Eq(cp::SpeciesCount(cp::Game::kLegendsZA), 364, "Legends: Z-A: 364");
}

// kGameSpeciesCount e uma constante escrita ao lado do bitmap: as duas podem
// divergir em silencio. Descoberto ao plantar a violacao 3 (evidence-log), em
// que apagar um bit deixava a contagem intacta. Vale para os 23 jogos.
void TestContagemBateComOBitmap() {
  std::printf("a contagem declarada bate com o bitmap:\n");

  for (int g = 0; g < static_cast<int>(cp::Game::kCount); ++g) {
    const auto game = static_cast<cp::Game>(g);
    std::size_t contados = 0;
    for (int dex = 1; dex <= cp::kMaxDex; ++dex) {
      if (cp::HasSpecies(game, dex)) ++contados;
    }
    Eq(static_cast<long long>(contados),
       static_cast<long long>(cp::SpeciesCount(game)),
       std::string(cp::GameName(game)) + ": bits contados == contagem");
  }
}

void TestEspeciesPorJogoDentroEFora() {
  std::printf("especies por jogo: casos de dentro e de fora:\n");

  // De fora: especie de gen9 em jogo antigo.
  Check(!cp::HasSpecies(cp::Game::kLetsGo, 922),
        "LGPE RECUSA Pawmo (gen9)");
  Check(!cp::HasSpecies(cp::Game::kLetsGo, 906),
        "LGPE RECUSA Sprigatito (gen9)");
  Check(!cp::HasSpecies(cp::Game::kBdsp, 922), "BDSP recusa Pawmo (gen9)");
  Check(!cp::HasSpecies(cp::Game::kSwordShield, 152),
        "SwSh recusa Chikorita (o Dexit)");
  Check(!cp::HasSpecies(cp::Game::kBdsp, 494), "BDSP recusa Victini (gen5)");

  // De dentro.
  Check(cp::HasSpecies(cp::Game::kLetsGo, 25), "LGPE aceita Pikachu");
  Check(cp::HasSpecies(cp::Game::kLetsGo, 809), "LGPE aceita Melmetal");
  Check(cp::HasSpecies(cp::Game::kScarletViolet, 922), "SV aceita Pawmo");
  Check(cp::HasSpecies(cp::Game::kBdsp, 493), "BDSP aceita Arceus");
  Check(cp::HasSpecies(cp::Game::kSwordShield, 888), "SwSh aceita Zacian");

  // A correcao desta spec: PLA aceita as especies que so existem la como
  // forma de Hisui. Antes da correcao o header dizia que nao.
  Check(cp::HasSpecies(cp::Game::kLegendsArceus, 58),
        "PLA aceita Growlithe (so existe como forma de Hisui)");
  Check(cp::HasSpecies(cp::Game::kLegendsArceus, 215),
        "PLA aceita Sneasel");
  Check(cp::HasSpecies(cp::Game::kLegendsArceus, 724),
        "PLA aceita Decidueye (forma de Hisui)");
  Check(!cp::HasSpecies(cp::Game::kLegendsArceus, 4),
        "PLA recusa Charmander");

  // A outra correcao: Z-A nao tem a linha do Dipplin nem os cachorros de
  // Kitakami, apesar de o header antigo afirmar que sim.
  Check(!cp::HasSpecies(cp::Game::kLegendsZA, 1011),
        "Z-A recusa Dipplin");
  Check(!cp::HasSpecies(cp::Game::kLegendsZA, 1014),
        "Z-A recusa Okidogi");
}

// A tabela por jogo e a de conversao tem de concordar: tudo que SV aceita
// tem indice interno, e vice-versa.
void TestCoerenciaEntreTabelas() {
  std::printf("coerencia entre a tabela por jogo e a conversao do gen9:\n");

  int sem_interno = 0;
  for (int dex = 1; dex <= cp::kMaxDex; ++dex) {
    if (!cp::HasSpecies(cp::Game::kScarletViolet, dex)) continue;
    if (g9::ToInternal(static_cast<std::uint16_t>(dex)) == 0) ++sem_interno;
  }
  Check(sem_interno == 0,
        "toda especie que SV aceita tem indice interno no gen9");
}

}  // namespace

int main() {
  TestConversaoGen9CasoDocumentado();
  TestConversaoGen9Robustez();
  TestConversaoGen9Roundtrip();
  TestConversaoGen9ContraFixtures();

  TestLearnsetCasosConhecidos();
  TestLearnsetNaoVazio();
  TestLearnsetDasFixtures();
  TestResetPorNivel();

  TestEspeciesPorJogoContagem();
  TestEspeciesPorJogoDentroEFora();
  TestCoerenciaEntreTabelas();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
