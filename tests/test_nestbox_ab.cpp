// Teste da gravacao atomica por dupla de arquivos (spec 091).
//
// O ponto central: uma queda de energia no meio do commit NAO pode deixar o
// usuario sem banco. O commit escreve sempre na copia inativa, entao a copia
// boa nunca esta sendo escrita quando a luz cai.

#include <cstdio>
#include <cstdint>
#include <vector>

#include "nestbox_ab.h"
#include "nestbox_file.h"

namespace nb = pokehome::nest;
namespace ab = pokehome::nest::ab;

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

// Um banco pequeno mas reconhecivel, para o payload nao ser trivial.
std::vector<std::uint8_t> PayloadDeTeste(std::uint8_t seed) {
  nb::NestData d = nb::MakeEmpty(4, 30);
  std::uint8_t rec[80];
  for (std::size_t i = 0; i < sizeof(rec); ++i) {
    rec[i] = static_cast<std::uint8_t>(seed + i);
  }
  nb::SlotWrite(d.At(0, 0), nb::kGen3, rec, sizeof(rec));
  d.MarkSeen(25);
  d.SetBoxName(0, "Banco");
  return nb::Encode(d);
}

void TestEnvelope() {
  std::printf("envelope: geracao e CRC (TD-01 da spec 091):\n");

  const auto payload = PayloadDeTeste(1);
  const auto arquivo = ab::Wrap(payload, 7);

  Check(arquivo.size() == ab::kEnvelopeHeader + payload.size(),
        "o arquivo e cabecalho de 16 bytes + payload");
  Check(arquivo[0] == 'N' && arquivo[1] == 'B' && arquivo[2] == 'A' &&
            arquivo[3] == 'B',
        "com o magic NBAB");

  const ab::Slot s = ab::Unwrap(arquivo);
  Check(s.valid, "e volta valido");
  Check(s.generation == 7, "com a geracao certa");
  Check(s.payload == payload, "e o payload IDENTICO, byte a byte");

  // O payload continua sendo lido pelo Decode da 090, sem saber da 091.
  const nb::NestData d = nb::Decode(s.payload);
  Check(d.boxes == 4 && d.BoxName(0) == "Banco" && d.Seen(25),
        "o payload continua sendo um .nestbox v5 normal");
}

void TestCrcPegaCorrupcao() {
  std::printf("CRC pega corrupcao (TD-02 da spec 091):\n");

  const auto payload = PayloadDeTeste(2);
  auto arquivo = ab::Wrap(payload, 3);

  // Um byte trocado no meio do payload.
  auto sujo = arquivo;
  sujo[ab::kEnvelopeHeader + 500] ^= 0xFF;
  Check(!ab::Unwrap(sujo).valid, "um byte trocado invalida o arquivo");

  // Truncado (o caso da queda de energia).
  auto curto = arquivo;
  curto.resize(curto.size() / 2);
  Check(!ab::Unwrap(curto).valid, "arquivo truncado e invalido");

  // Cabecalho incompleto.
  auto minusculo = arquivo;
  minusculo.resize(8);
  Check(!ab::Unwrap(minusculo).valid, "cabecalho incompleto e invalido");

  // Magic errado.
  auto mau_magic = arquivo;
  mau_magic[0] = 'X';
  Check(!ab::Unwrap(mau_magic).valid, "magic errado e invalido");

  // Vazio / ausente.
  Check(!ab::Unwrap({}).valid, "arquivo ausente e invalido");

  // O intacto continua valido.
  Check(ab::Unwrap(arquivo).valid, "e o arquivo intacto continua valido");
}

// O teste que a spec exige: queda de energia no meio do commit.
void TestQuedaDeEnergia() {
  std::printf("queda de energia no meio do commit (spec 091):\n");

  // Estado antes: A tem o banco bom, geracao 4.
  const auto bom = PayloadDeTeste(0x11);
  const auto arquivo_a = ab::Wrap(bom, 4);

  // O commit ia escrever em B (a inativa). A luz cai no meio: B fica com
  // metade dos bytes. A NAO foi tocada — e o ponto inteiro do desenho.
  auto arquivo_b = ab::Wrap(PayloadDeTeste(0x99), 5);
  arquivo_b.resize(arquivo_b.size() / 3);  // escrita interrompida

  const ab::Slot a = ab::Unwrap(arquivo_a);
  const ab::Slot b = ab::Unwrap(arquivo_b);
  Check(a.valid, "A continua valido (nunca foi tocado)");
  Check(!b.valid, "B ficou invalido (escrita interrompida)");

  const ab::Slot* fonte = nullptr;
  Check(ab::PickSource(a, b, &fonte), "a leitura ainda encontra um banco bom");
  Check(fonte == &a, "e escolhe o A, o integro");
  Check(fonte->payload == bom, "SEM PERDA: o payload volta byte a byte");

  const nb::NestData d = nb::Decode(fonte->payload);
  Check(d.boxes == 4 && d.BoxName(0) == "Banco",
        "e o banco recuperado abre normal");

  // O proximo commit tenta B de novo (o invalido), sem tocar no bom.
  const ab::Target t = ab::PickTarget(a, b);
  Check(t.write_b, "o proximo commit escreve em B de novo");
  Check(t.generation == 5, "com geracao maior que a do A");

  // Simetrico: se quem morreu foi o A, a leitura cai no B.
  auto a_corrompido = arquivo_a;
  a_corrompido[ab::kEnvelopeHeader + 10] ^= 0xFF;
  const ab::Slot a2 = ab::Unwrap(a_corrompido);
  const ab::Slot b2 = ab::Unwrap(ab::Wrap(bom, 9));
  const ab::Slot* fonte2 = nullptr;
  Check(ab::PickSource(a2, b2, &fonte2) && fonte2 == &b2,
        "com o A corrompido, a leitura cai no B");
  Check(ab::PickTarget(a2, b2).write_b == false,
        "e o proximo commit escreve por cima do A corrompido");

  // Os dois mortos: banco novo vazio, nao lixo interpretado.
  const ab::Slot morto;
  const ab::Slot* nada = nullptr;
  Check(!ab::PickSource(morto, morto, &nada),
        "com os dois invalidos nao ha fonte (a UI comeca banco vazio)");
  const ab::Target t0 = ab::PickTarget(morto, morto);
  Check(!t0.write_b && t0.generation == 1,
        "e o primeiro commit vai para o A, geracao 1");
}

// O outro teste que a spec exige: commits seguidos alternam de arquivo.
void TestAlternancia() {
  std::printf("dois commits seguidos alternam (spec 091):\n");

  // Comeca do zero, como uma instalacao nova (ou a migracao do legado).
  ab::Slot a, b;

  const ab::Target t1 = ab::PickTarget(a, b);
  Check(!t1.write_b && t1.generation == 1, "commit 1 -> A, geracao 1");
  a = ab::Unwrap(ab::Wrap(PayloadDeTeste(1), t1.generation));

  const ab::Target t2 = ab::PickTarget(a, b);
  Check(t2.write_b && t2.generation == 2, "commit 2 -> B, geracao 2");
  b = ab::Unwrap(ab::Wrap(PayloadDeTeste(2), t2.generation));

  const ab::Target t3 = ab::PickTarget(a, b);
  Check(!t3.write_b && t3.generation == 3, "commit 3 -> A de novo, geracao 3");
  a = ab::Unwrap(ab::Wrap(PayloadDeTeste(3), t3.generation));

  const ab::Target t4 = ab::PickTarget(a, b);
  Check(t4.write_b && t4.generation == 4, "commit 4 -> B, geracao 4");
  b = ab::Unwrap(ab::Wrap(PayloadDeTeste(4), t4.generation));

  // A leitura sempre pega o mais novo.
  const ab::Slot* fonte = nullptr;
  Check(ab::PickSource(a, b, &fonte) && fonte == &b,
        "a leitura pega o B, que e o mais recente");
  Check(fonte->generation == 4, "com a geracao 4");
  Check(fonte->payload == PayloadDeTeste(4), "e o conteudo do ultimo commit");

  // Dez commits seguidos: alterna sempre e a geracao so cresce.
  bool alterna = true, cresce = true;
  bool esperado_b = false;
  std::uint64_t anterior = 4;
  for (int i = 0; i < 10; ++i) {
    const ab::Target t = ab::PickTarget(a, b);
    if (t.generation != anterior + 1) cresce = false;
    esperado_b = !esperado_b;  // depois do commit 4 (em B), o proximo e A
    if (t.write_b == esperado_b) alterna = false;
    const auto novo = ab::Unwrap(ab::Wrap(PayloadDeTeste(
        static_cast<std::uint8_t>(10 + i)), t.generation));
    if (t.write_b) b = novo; else a = novo;
    anterior = t.generation;
  }
  Check(alterna, "dez commits seguidos alternam de arquivo, sempre");
  Check(cresce, "e a geracao cresce de um em um, sem repetir");
}

// A copia BOA nunca e o destino da escrita. E a invariante que sustenta tudo.
void TestNuncaEscrevePorCimaDoBom() {
  std::printf("o commit nunca escreve por cima da copia boa (spec 091):\n");

  bool sempre_seguro = true;
  ab::Slot a = ab::Unwrap(ab::Wrap(PayloadDeTeste(1), 1));
  ab::Slot b;

  for (int i = 0; i < 20; ++i) {
    const ab::Slot* fonte = nullptr;
    const bool tem_fonte = ab::PickSource(a, b, &fonte);
    const ab::Target t = ab::PickTarget(a, b);
    // Se ha uma copia boa, o destino do commit NAO pode ser ela.
    if (tem_fonte) {
      const ab::Slot* destino = t.write_b ? &b : &a;
      if (destino == fonte) sempre_seguro = false;
    }
    const auto novo = ab::Unwrap(ab::Wrap(PayloadDeTeste(
        static_cast<std::uint8_t>(i)), t.generation));
    if (t.write_b) b = novo; else a = novo;
  }
  Check(sempre_seguro,
        "em 20 commits, o destino NUNCA foi a copia que a leitura usaria");
}

void TestMigracaoDoLegado() {
  std::printf("instalacao com .nestbox legado (TD-03 da spec 091):\n");

  // O legado e um payload v5 solto, sem envelope. Ele nao passa no Unwrap...
  const auto legado = PayloadDeTeste(0x55);
  Check(!ab::Unwrap(legado).valid,
        "o arquivo legado nao e um arquivo A/B valido");

  // ...mas e lido pelo Decode normal, e vira bank.a geracao 1 no 1o commit.
  const nb::NestData d = nb::Decode(legado);
  Check(d.boxes == 4 && d.BoxName(0) == "Banco", "mas abre pelo Decode normal");

  const ab::Slot vazio;
  const ab::Target t = ab::PickTarget(vazio, vazio);
  Check(!t.write_b && t.generation == 1,
        "e o primeiro commit grava bank.a geracao 1");

  const ab::Slot a = ab::Unwrap(ab::Wrap(nb::Encode(d), t.generation));
  Check(a.valid && nb::Decode(a.payload).BoxName(0) == "Banco",
        "sem perder o conteudo do banco legado");
}

}  // namespace

int main() {
  TestEnvelope();
  TestCrcPegaCorrupcao();
  TestQuedaDeEnergia();
  TestAlternancia();
  TestNuncaEscrevePorCimaDoBom();
  TestMigracaoDoLegado();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
