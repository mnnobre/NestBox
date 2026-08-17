// Teste das regras de descoberta de save data (spec 039).
//
// Estas regras rodam nos DOIS caminhos — no console e no simulador de PC. Se
// divergirem, o teste no PC deixa de dizer alguma coisa sobre o console, que e
// o proposito inteiro da spec.

#include <cstdio>
#include <string>

#include "switch_sim.h"
#include "switch_titles.h"

namespace sim = pokehome::sim;

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

void TestAppId() {
  std::printf("nome de diretorio <-> ApplicationId (spec 039):\n");

  std::uint64_t id = 0;
  // FireRed em ingles — o titulo que o dono tem no console.
  Check(sim::ParseAppId("0100554023408000", &id) && id == 0x0100554023408000ULL,
        "hex de 16 digitos vira o id certo");
  Check(sim::ParseAppId("0100554023408000", nullptr), "aceita out nulo");

  // Minusculas tambem: quem cria a pasta a mao nao vai respeitar o caso.
  Check(sim::ParseAppId("0100554023408000", &id) &&
            sim::ParseAppId("0100554023408000", &id),
        "aceita maiusculas");
  std::uint64_t lower = 0;
  Check(sim::ParseAppId("0100554023408abc", &lower) &&
            lower == 0x0100554023408ABCULL,
        "aceita minusculas");

  // Pasta alheia dentro da raiz nao pode virar um jogo.
  Check(!sim::ParseAppId("naoehex", &id), "recusa nome nao-hex");
  Check(!sim::ParseAppId("010055402340800", &id), "recusa 15 digitos");
  Check(!sim::ParseAppId("01005540234080000", &id), "recusa 17 digitos");
  Check(!sim::ParseAppId("", &id), "recusa vazio");
  Check(!sim::ParseAppId("0100554023408g00", &id), "recusa digito invalido");

  // Ida e volta.
  Check(sim::AppIdToDir(0x0100554023408000ULL) == "0100554023408000",
        "id vira nome de diretorio");
  std::uint64_t back = 0;
  Check(sim::ParseAppId(sim::AppIdToDir(0x01001F5010DFA000ULL), &back) &&
            back == 0x01001F5010DFA000ULL,
        "ida e volta preserva o id");
}

void TestTituloConhecido() {
  std::printf("id conhecido na tabela de titulos (spec 039):\n");

  // A tabela do projeto e a mesma que o console consulta.
  auto conhecido = [](std::uint64_t id) {
    for (int i = 0; i < nestbox::kSwitchTitleCount; ++i) {
      if (nestbox::kSwitchTitles[i].app_id == id) return true;
    }
    return false;
  };

  std::uint64_t id = 0;
  sim::ParseAppId("0100554023408000", &id);
  Check(conhecido(id), "FireRed (ingles) esta na tabela");

  sim::ParseAppId("01001F5010DFA000", &id);
  Check(conhecido(id), "Legends: Arceus esta na tabela");

  // Um id valido mas que nao e jogo Pokemon: a pasta existe, o app ignora.
  sim::ParseAppId("0123456789ABCDEF", &id);
  Check(!conhecido(id), "id fora da tabela e ignorado");
}

void TestArquivoInterno() {
  std::printf("qual arquivo serve (spec 039):\n");

  // Save de GBA: 128 KB exatos.
  Check(sim::IsGbaSave("Pokemon FireRed.sav", 131072),
        "sav de 131072 bytes e save de GBA");
  Check(sim::IsGbaSave("qualquer.srm", 131072), "srm tambem serve");

  // Tamanho errado nao passa. Um arquivo maior NAO e um save gen3 com lixo no
  // fim: o formato tem duas metades de tamanho fixo.
  Check(!sim::IsGbaSave("Pokemon FireRed.sav", 131071), "1 byte a menos recusa");
  Check(!sim::IsGbaSave("Pokemon FireRed.sav", 131073), "1 byte a mais recusa");
  Check(!sim::IsGbaSave("Pokemon FireRed.sav", 0), "arquivo vazio recusa");
  Check(!sim::IsGbaSave("leiame.txt", 131072), "extensao errada recusa");

  // SwishCrypto: o `main` passa de 1 MB.
  Check(sim::IsMainSave("main", 0x200000), "main de 2 MB e SwishCrypto");
  Check(!sim::IsMainSave("main", 1024), "main pequeno demais recusa");
  Check(!sim::IsMainSave("outro", 0x200000), "nome desconhecido recusa");
  Check(!sim::IsMainSave("main", sim::kMainMinBytes),
        "exatamente no limite recusa (o teste e >)");
  // O piso precisa ficar ABAIXO do menor save real conhecido, senao exclui
  // save de verdade — foi o que aconteceu com 1 MB (spec 042).
  Check(sim::kMainMinBytes < 979108,
        "o piso cabe abaixo do menor save observado (BDSP, 979108)");

  // Nem todo save de Switch se chama "main" (spec 042). Conferido em saves
  // reais: BDSP e Unity, Let's Go tem engine propria.
  Check(sim::IsMainSave("SaveData.bin", 979108),
        "SaveData.bin e save de BDSP");
  Check(sim::IsMainSave("savedata.bin", 1048576),
        "savedata.bin e save de Let's Go");
  Check(!sim::IsMainSave("SaveData.bin", 1024),
        "o teste de tamanho vale para todos os nomes");

  // O caso que impede aceitar "qualquer arquivo grande": o backup tem o MESMO
  // tamanho do save, e abrir o backup no lugar dele seria pior que nao achar.
  Check(!sim::IsMainSave("backup", 0x200000),
        "backup NAO e save, mesmo com o tamanho certo");
  Check(!sim::IsMainSave("Backup.bin", 979108),
        "Backup.bin do BDSP tambem nao");
}

void TestExtensoes() {
  std::printf("extensoes de save (spec 039):\n");

  Check(sim::HasSaveExtension("a.sav"), ".sav");
  Check(sim::HasSaveExtension("a.srm"), ".srm");
  Check(sim::HasSaveExtension("a.sps"), ".sps");
  Check(sim::HasSaveExtension("a.dsv"), ".dsv");
  Check(!sim::HasSaveExtension("a.txt"), ".txt nao");
  Check(!sim::HasSaveExtension("a.savx"), "extensao no meio nao conta");
  Check(!sim::HasSaveExtension(".sav"), "so a extensao, sem nome, nao conta");
  Check(!sim::HasSaveExtension(""), "vazio nao estoura");
}

}  // namespace

int main() {
  TestAppId();
  TestTituloConhecido();
  TestArquivoInterno();
  TestExtensoes();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
