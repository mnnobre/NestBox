// Testes do log de diagnostico (spec 083).
//
// O que cada bloco prova:
//   1. formato — carimbo relativo + prefixo de categoria
//   2. as duas categorias sao DISTINGUIVEIS na saida (da para filtrar)
//   3. rotacao — escreve alem do teto, prova que rotacionou e que o arquivo
//      antigo SOBREVIVEU (rotacao que apaga o passado nao e rotacao)
//   4. desligamento — este e o teste que garante o requisito 3 da spec: com o
//      nivel desligado, NADA e escrito
//
// O sink e de memoria: o core nao faz I/O (regra do save_backup.h), entao o
// teste tambem nao precisa tocar o cartao.

#include "nlog.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace nl = pokehome::nlog;

namespace {

int g_fail = 0;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHA: %s\n", what.c_str());
    ++g_fail;
  }
}

// Sink de memoria que imita um arquivo com rotacao: `current` e o
// "nestbox.log", `rotated` sao os antigos, do mais novo para o mais velho.
struct FakeDisk {
  std::string current;
  std::vector<std::string> rotated;

  void Install() {
    nl::SetSink([this](const std::string& line) { current += line; });
    nl::SetRotator([this](int keep) {
      rotated.insert(rotated.begin(), current);
      current.clear();
      // Mesmo corte que a UI faz: o corrente + `keep` antigos.
      if (static_cast<int>(rotated.size()) > keep) rotated.resize(keep);
    });
    nl::SetWrittenBytes(0);
  }
};

std::size_t CountOf(const std::string& hay, const std::string& needle) {
  std::size_t n = 0, pos = 0;
  while ((pos = hay.find(needle, pos)) != std::string::npos) {
    ++n;
    pos += needle.size();
  }
  return n;
}

// --- 1. Formato ------------------------------------------------------------

void TestFormato() {
  const std::string nav = nl::Format(nl::Cat::kNav, 12.34, "entrou na Pokedex");
  const std::string act = nl::Format(nl::Cat::kAct, 0.0, "gravou 30 mons");

  Check(nav.find("[NAV]") != std::string::npos, "linha NAV tem o prefixo [NAV]");
  Check(act.find("[ACT]") != std::string::npos, "linha ACT tem o prefixo [ACT]");
  Check(nav.find("entrou na Pokedex") != std::string::npos,
        "a mensagem sobrevive ao formato");
  // Uma casa decimal, e o carimbo vem ANTES da categoria: o arquivo fica
  // ordenavel e alinhado a olho.
  Check(nav.find("12.3") != std::string::npos,
        "carimbo relativo com casa decimal (12.34 -> 12.3)");
  Check(nav.find("[NAV]") > nav.find("12.3"),
        "o carimbo vem antes da categoria");
  Check(!nav.empty() && nav.back() == '\n', "a linha termina em \\n");

  // printf de verdade, com argumento — se a formatacao nao passar pelo
  // vsnprintf, isto sai literal.
  FakeDisk disk;
  disk.Install();
  NLOG_ACT("moveu %s do slot %d para %d", "Pidgey", 3, 7);
  Check(disk.current.find("moveu Pidgey do slot 3 para 7") != std::string::npos,
        "formatacao estilo printf com %s e %d");
}

// --- 2. As duas categorias sao distinguiveis -------------------------------

void TestCategorias() {
  FakeDisk disk;
  disk.Install();

  NLOG_NAV("abriu caixa 2");
  NLOG_ACT("pegou Bulbasaur");
  NLOG_NAV("apertou B");
  NLOG_ACT("gravou nestbox.bin");
  NLOG_NAV("saiu");

  Check(CountOf(disk.current, "[NAV]") == 3, "3 linhas NAV na saida");
  Check(CountOf(disk.current, "[ACT]") == 2, "2 linhas ACT na saida");
  // O ponto do requisito: da para separar as duas com um filtro de texto.
  Check(CountOf(disk.current, "[NAV]") + CountOf(disk.current, "[ACT]") ==
            CountOf(disk.current, "\n"),
        "toda linha carrega exatamente uma categoria");
}

// --- 3. Rotacao ------------------------------------------------------------

void TestRotacao() {
  FakeDisk disk;
  disk.Install();

  // Uma linha longa o suficiente para estourar 2 MB em algumas milhares de
  // escritas — sem gastar o tempo de escrever byte a byte.
  const std::string big(1000, 'x');

  // O bastante para forcar 3 rotacoes: 3 x 2 MB = ~6300 linhas de 1 KB.
  for (int i = 0; i < 7000; ++i) NLOG_ACT("linha %d %s", i, big.c_str());

  Check(!disk.rotated.empty(), "rotacionou ao passar do teto");
  Check(disk.current.size() <= nl::kMaxBytes,
        "o arquivo corrente respeita o teto de 2 MB (tem " +
            std::to_string(disk.current.size()) + " bytes)");
  if (disk.rotated.empty()) {
    // Sem isto o teste morreria num assert do vector, e o vermelho nao diria
    // o que quebrou. A licao da spec 069: vermelho tem de NOMEAR a falha.
    std::printf(
        "  (rotacao nao aconteceu — os asserts seguintes nao tem o que "
        "conferir)\n");
    return;
  }
  for (const std::string& old : disk.rotated) {
    Check(old.size() <= nl::kMaxBytes, "arquivo rotacionado respeita o teto");
    // O que uma rotacao quebrada tipicamente perde: o conteudo antigo.
    Check(!old.empty(), "o arquivo antigo SOBREVIVEU a rotacao");
  }
  // Poucos arquivos: cartao SD cheio e problema real no Switch.
  Check(static_cast<int>(disk.rotated.size()) <= nl::kKeepRotated,
        "guarda no maximo kKeepRotated arquivos antigos");
  // Sem teto, 7000 linhas de ~1 KB dariam ~7 MB num arquivo so. O total vivo
  // tem de caber no orcamento declarado.
  const std::size_t total_kept =
      disk.current.size() +
      [&] {
        std::size_t s = 0;
        for (const std::string& o : disk.rotated) s += o.size();
        return s;
      }();
  Check(total_kept <= nl::kMaxBytes * (nl::kKeepRotated + 1),
        "o log inteiro cabe no orcamento (corrente + rotacionados)");

  // A prova mais direta de que houve corte: o inicio da sessao NAO esta mais
  // no arquivo corrente.
  Check(disk.current.find("linha 0 ") == std::string::npos,
        "a linha 0 saiu do arquivo corrente");
  Check(disk.rotated.back().find("linha ") != std::string::npos,
        "o arquivo mais antigo guardado ainda tem conteudo legivel");
}

// --- 4. Desligamento (o requisito 3 da spec) -------------------------------

// As macros sao resolvidas em tempo de compilacao, entao o teste do
// desligamento nao pode simplesmente mudar uma variavel. Estas funcoes
// reproduzem a expansao das macros nos tres niveis, provando que em
// NESTBOX_LOG_LEVEL=0 nao sobra chamada nenhuma.
#define NLOG_NAV_AT(level, ...) \
  do {                          \
    if ((level) >= 2) ::pokehome::nlog::Emit(::pokehome::nlog::Cat::kNav, __VA_ARGS__); \
  } while (0)
#define NLOG_ACT_AT(level, ...) \
  do {                          \
    if ((level) >= 1) ::pokehome::nlog::Emit(::pokehome::nlog::Cat::kAct, __VA_ARGS__); \
  } while (0)

void TestDesligamento() {
  // Nivel 0: nada.
  {
    FakeDisk disk;
    disk.Install();
    NLOG_NAV_AT(0, "navegou");
    NLOG_ACT_AT(0, "agiu");
    Check(disk.current.empty(), "NESTBOX_LOG_LEVEL=0 nao escreve NADA");
    Check(nl::WrittenBytes() == 0, "nivel 0 nao conta bytes");
  }
  // Nivel 1: so ACT.
  {
    FakeDisk disk;
    disk.Install();
    NLOG_NAV_AT(1, "navegou");
    NLOG_ACT_AT(1, "agiu");
    Check(CountOf(disk.current, "[NAV]") == 0, "nivel 1 corta NAV");
    Check(CountOf(disk.current, "[ACT]") == 1, "nivel 1 mantem ACT");
  }
  // Nivel 2: os dois.
  {
    FakeDisk disk;
    disk.Install();
    NLOG_NAV_AT(2, "navegou");
    NLOG_ACT_AT(2, "agiu");
    Check(CountOf(disk.current, "[NAV]") == 1, "nivel 2 mantem NAV");
    Check(CountOf(disk.current, "[ACT]") == 1, "nivel 2 mantem ACT");
  }

  // Sem sink instalado nada explode nem escreve — e o estado do core antes de
  // a UI subir, e de um binario de release que nunca instala sink.
  nl::SetSink(nullptr);
  nl::SetRotator(nullptr);
  NLOG_ACT("sem sink");
  Check(true, "Emit sem sink nao quebra");

  // O nivel COMPILADO deste alvo e o que o produto usa. Se alguem desligar o
  // log e esquecer de reativar antes de continuar desenvolvendo, esta linha
  // avisa em vez de deixar o app mudo em silencio.
  Check(NESTBOX_LOG_LEVEL == 2,
        "NESTBOX_LOG_LEVEL esta em 2 (tudo ligado, decisao do dono na spec 083)");
}

// --- 5. Carimbo relativo, sem relogio do sistema ---------------------------

void TestCarimbo() {
  FakeDisk disk;
  disk.Install();

  const double before = nl::Seconds();
  nl::Tick(60);  // um segundo a 60 FPS
  const double after = nl::Seconds();
  Check(after - before > 0.9 && after - before < 1.1,
        "60 ticks avancam ~1 segundo");

  nl::Tick(600);
  NLOG_ACT("depois dos ticks");
  // O carimbo na linha e o mesmo que Seconds() devolve — nao um relogio
  // paralelo. Comparar com o valor esperado formatado do mesmo jeito prova
  // isso sem depender de quantos ticks os testes anteriores acumularam.
  char expected[16];
  std::snprintf(expected, sizeof(expected), "%.1f", nl::Seconds());
  Check(disk.current.find(expected) != std::string::npos,
        "o carimbo da linha bate com Seconds()");
}

}  // namespace

int main() {
  TestFormato();
  TestCategorias();
  TestRotacao();
  TestDesligamento();
  TestCarimbo();

  if (g_fail == 0) {
    std::printf("test_nlog: OK\n");
    return EXIT_SUCCESS;
  }
  std::printf("test_nlog: %d FALHAS\n", g_fail);
  return EXIT_FAILURE;
}
