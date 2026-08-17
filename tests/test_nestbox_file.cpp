// Teste do formato de arquivo do NestBox (spec 027).
//
// O ponto central: os 80 bytes crus tem que sobreviver a ida e volta sem
// perder nada — e por isso que o formato guarda bytes, e nao o struct
// parseado (que descarta Poke Ball, origem, idioma e ribbons).

#include <cstdio>
#include <cstring>
#include <vector>

#include "gen3_save.h"
#include "nestbox_file.h"

namespace nb = pokehome::nest;
namespace g3 = pokehome::gen3;

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

// Preenche um slot gen3 com bytes reconheciveis, incluindo os offsets que o
// parser gen3 descarta hoje (a word `origins` em 0x02 do bloco de misc).
void PreencheSlot(std::uint8_t* s, std::uint8_t seed) {
  std::uint8_t rec[80];
  for (std::size_t i = 0; i < sizeof(rec); ++i) {
    rec[i] = static_cast<std::uint8_t>(seed + i);
  }
  nb::SlotWrite(s, nb::kGen3, rec, sizeof(rec));
}

// Preenche um slot de formato moderno com bytes reconheciveis.
void PreencheSlotFmt(std::uint8_t* s, std::uint8_t fmt, std::uint8_t seed) {
  const std::size_t n = nb::FormatBytes(fmt);
  std::vector<std::uint8_t> rec(n);
  for (std::size_t i = 0; i < n; ++i) {
    rec[i] = static_cast<std::uint8_t>(seed + i * 3);
  }
  nb::SlotWrite(s, fmt, rec.data(), n);
}

void TestIdaEVolta() {
  std::printf("ida e volta (spec 027):\n");

  nb::NestData d = nb::MakeEmpty(10, 30);
  Check(d.valid(), "banco vazio e valido");
  Check(d.raw.size() == 10u * 30u * nb::kSlotBytes,
        "tamanho bate com caixas x slots x 384");

  PreencheSlot(d.At(0, 0), 1);
  PreencheSlot(d.At(3, 17), 200);
  PreencheSlot(d.At(9, 29), 77);  // ultimo slot da ultima caixa

  const auto bytes = nb::Encode(d);
  // v4: cabecalho + slots + dex global + nomes das caixas + memoria de moveset
  // (spec 071). A memoria vazia ainda ocupa os 4 bytes do proprio contador.
  const std::size_t esperado = nb::kHeaderBytes + d.raw.size() +
                               nb::kDexBytes + d.boxes * nb::kBoxNameBytes + 4;
  Check(bytes.size() == esperado,
        "arquivo = cabecalho + dados + dex + nomes + movesets");

  const nb::NestData back = nb::Decode(bytes);
  Check(back.boxes == 10 && back.slots == 30, "cabecalho volta certo");
  Check(back.raw == d.raw, "TODOS os bytes voltam identicos");

  // Confere slot a slot, que e o que importa na pratica.
  bool iguais = true;
  for (std::size_t i = 0; i < nb::kSlotBytes; ++i) {
    if (back.At(3, 17)[i] != d.At(3, 17)[i]) iguais = false;
  }
  Check(iguais, "os bytes de um slot no meio sobrevivem");
  Check(nb::SlotPayload(back.At(9, 29))[0] == 77,
        "o ultimo slot da ultima caixa sobrevive");
}

void TestVazios() {
  std::printf("slots vazios (spec 027):\n");

  nb::NestData d = nb::MakeEmpty(2, 30);
  PreencheSlot(d.At(0, 5), 42);

  const nb::NestData back = nb::Decode(nb::Encode(d));
  Check(nb::SlotEmpty(back.At(0, 0)), "slot nao usado continua vazio");
  Check(!nb::SlotEmpty(back.At(0, 5)), "slot usado nao e vazio");
  Check(nb::SlotEmpty(back.At(1, 29)), "ultimo slot vazio continua vazio");
}

void TestArquivoRuim() {
  std::printf("arquivo invalido e recusado (spec 027):\n");

  // Ausente / vazio.
  Check(nb::Decode({}).boxes == 0, "arquivo vazio vira banco vazio");

  // Magic errado.
  std::vector<std::uint8_t> ruim = nb::Encode(nb::MakeEmpty(2, 30));
  ruim[0] = 'X';
  Check(nb::Decode(ruim).boxes == 0, "magic errado e recusado");

  // Versao desconhecida: nao adivinha o layout.
  std::vector<std::uint8_t> futuro = nb::Encode(nb::MakeEmpty(2, 30));
  futuro[4] = 99;
  Check(nb::Decode(futuro).boxes == 0, "versao desconhecida e recusada");

  // Truncado no meio dos dados: nao pode ler alem do buffer.
  std::vector<std::uint8_t> curto = nb::Encode(nb::MakeEmpty(10, 30));
  curto.resize(nb::kHeaderBytes + 100);
  Check(nb::Decode(curto).boxes == 0, "arquivo truncado e recusado");

  // So o cabecalho, sem dados.
  std::vector<std::uint8_t> so_cab = nb::Encode(nb::MakeEmpty(10, 30));
  so_cab.resize(nb::kHeaderBytes);
  Check(nb::Decode(so_cab).boxes == 0, "cabecalho sem dados e recusado");

  // Cabecalho cortado ao meio.
  std::vector<std::uint8_t> meio = {'N', 'S', 'T'};
  Check(nb::Decode(meio).boxes == 0, "cabecalho incompleto e recusado");
}

void TestForaDaFaixa() {
  std::printf("acesso fora da faixa (spec 027):\n");

  nb::NestData d = nb::MakeEmpty(10, 30);
  Check(d.At(10, 0) == nullptr, "caixa fora da faixa devolve nullptr");
  Check(d.At(0, 30) == nullptr, "slot fora da faixa devolve nullptr");
  Check(d.At(999, 999) == nullptr, "bem fora da faixa devolve nullptr");
  Check(nb::SlotEmpty(nullptr), "slot nulo conta como vazio");

  const nb::NestData vazio;
  Check(vazio.At(0, 0) == nullptr, "banco nao inicializado nao estoura");
}

void TestBancoZerado() {
  std::printf("banco de tamanho zero (spec 027):\n");

  const nb::NestData d = nb::MakeEmpty(0, 0);
  Check(d.valid(), "banco 0x0 e valido (so o cabecalho)");
  const auto bytes = nb::Encode(d);
  // + 4: o contador da memoria de moveset, secao v4 da spec 071. Sem entrada
  // nenhuma ela e so o contador zerado.
  Check(bytes.size() == nb::kHeaderBytes + nb::kDexBytes + 4,
        "gera cabecalho + dex + contador de movesets, sem slots");
  Check(nb::Decode(bytes).boxes == 0, "e volta como banco vazio");
}

// --- Ida e volta com o parser de verdade (spec 028) -------------------------
//
// O teste acima prova que os BYTES sobrevivem. Este prova que os bytes
// guardados sao SUFICIENTES: reparsear devolve o mesmo Pokemon.

void TestReparse() {
  std::printf("bytes guardados reconstroem o Pokemon (spec 028):\n");

  // Monta um registro de 80 bytes com PID/OT reconheciveis. Nao precisa ser um
  // Pokemon "legal": o que importa e o parser devolver a mesma coisa nas duas
  // pontas.
  std::vector<std::uint8_t> rec(80, 0);
  // personality (0x00) e ot_id (0x04), little-endian.
  const std::uint32_t pid = 0xABCD1234;
  const std::uint32_t ot = 0x11223344;
  for (int i = 0; i < 4; ++i) {
    rec[i] = static_cast<std::uint8_t>((pid >> (i * 8)) & 0xFF);
    rec[4 + i] = static_cast<std::uint8_t>((ot >> (i * 8)) & 0xFF);
  }
  // Bytes do bloco criptografado: valores arbitrarios, so para nao ser tudo
  // zero — inclusive na regiao de `origins`, que o parser descarta.
  for (std::size_t i = 32; i < rec.size(); ++i) {
    rec[i] = static_cast<std::uint8_t>(i * 7);
  }

  const g3::BoxPokemon direto = g3::ParseBoxPokemonRecord(rec.data());

  // Passa pelo arquivo: guarda os bytes, serializa, le de volta, reparseia.
  nb::NestData d = nb::MakeEmpty(1, 30);
  nb::SlotWrite(d.At(0, 0), nb::kGen3, rec.data(), rec.size());
  const nb::NestData back = nb::Decode(nb::Encode(d));
  const g3::BoxPokemon depois =
      g3::ParseBoxPokemonRecord(nb::SlotPayload(back.At(0, 0)));

  Check(direto.personality == depois.personality, "personality sobrevive");
  Check(direto.ot_id == depois.ot_id, "ot_id sobrevive");
  Check(direto.species == depois.species, "especie sobrevive");
  Check(direto.experience == depois.experience, "experiencia sobrevive");
  Check(direto.met_level == depois.met_level, "met_level sobrevive");
  Check(direto.nickname == depois.nickname, "apelido sobrevive");

  bool ivs_iguais = true;
  for (int i = 0; i < 6; ++i) {
    if (direto.ivs[i] != depois.ivs[i]) ivs_iguais = false;
  }
  Check(ivs_iguais, "IVs sobrevivem");

  // O ponto da spec 027: os bytes que o parser NAO decodifica tambem voltam.
  // A word `origins` fica em 0x02 do bloco de misc; aqui basta comparar o
  // registro inteiro, que e o que garante Poke Ball, origem, idioma e ribbons.
  bool bytes_iguais = true;
  for (std::size_t i = 0; i < rec.size(); ++i) {
    if (rec[i] != nb::SlotPayload(back.At(0, 0))[i]) bytes_iguais = false;
  }
  Check(bytes_iguais,
        "os 80 bytes originais voltam — inclusive os campos nao parseados");

  // E o parser guarda os bytes crus dentro do proprio struct.
  bool raw_guardado = true;
  for (std::size_t i = 0; i < rec.size(); ++i) {
    if (direto.raw[i] != rec[i]) raw_guardado = false;
  }
  Check(raw_guardado, "BoxPokemon::raw traz os 80 bytes do slot");
}

void TestRawVazio() {
  std::printf("slot vazio (spec 028):\n");

  std::vector<std::uint8_t> zeros(80, 0);
  const g3::BoxPokemon vazio = g3::ParseBoxPokemonRecord(zeros.data());
  Check(vazio.empty(), "registro zerado vira Pokemon vazio");

  bool raw_zerado = true;
  for (std::size_t i = 0; i < zeros.size(); ++i) {
    if (vazio.raw[i] != 0) raw_zerado = false;
  }
  Check(raw_zerado, "raw de slot vazio fica zerado");
}

// --- Dex global e compatibilidade de versao (spec 029) ---------------------

// Monta um arquivo v1: cabecalho com versao 1 e SEM a secao da dex.
std::vector<std::uint8_t> ArquivoV1(std::uint16_t boxes, std::uint16_t slots) {
  std::vector<std::uint8_t> out;
  out.insert(out.end(), nb::kMagic, nb::kMagic + 4);
  out.push_back(1);  // versao 1
  out.push_back(0);
  out.push_back(static_cast<std::uint8_t>(boxes & 0xFF));
  out.push_back(static_cast<std::uint8_t>(boxes >> 8));
  out.push_back(static_cast<std::uint8_t>(slots & 0xFF));
  out.push_back(static_cast<std::uint8_t>(slots >> 8));
  out.push_back(0);
  out.push_back(0);
  // Slot de 80 bytes: o tamanho da v1, nao o da v5.
  out.resize(nb::kHeaderBytes +
             static_cast<std::size_t>(boxes) * slots * nb::kLegacySlotBytes);
  return out;
}

void TestBitmap() {
  std::printf("bitmap da dex global (spec 029):\n");

  nb::NestData d = nb::MakeEmpty(10, 30);
  Check(d.SeenCount() == 0, "comeca sem nada visto");

  d.MarkSeen(25);
  d.MarkSeen(1);
  Check(d.Seen(25) && d.Seen(1), "marca e consulta");
  Check(!d.Seen(2), "especie nao marcada continua falsa");
  Check(d.SeenCount() == 2, "conta as marcadas");

  // Marcar duas vezes nao conta duas vezes.
  d.MarkSeen(25);
  Check(d.SeenCount() == 2, "marcar de novo nao duplica");

  // Bordas da faixa. Desde a spec 090 a dex vai ate 1025, nao mais ate 386.
  d.MarkSeen(386);
  Check(d.Seen(386), "dex 386 (ultimo do gen3) cabe");
  d.MarkSeen(1025);
  Check(d.Seen(1025), "dex 1025 (ultimo do gen9) tambem cabe agora");
  d.MarkSeen(0);
  d.MarkSeen(-5);
  d.MarkSeen(1026);
  Check(d.SeenCount() == 4, "fora da faixa nao marca nem estoura");
  Check(!d.Seen(0) && !d.Seen(1026), "consulta fora da faixa e falsa");
}

void TestDexIdaEVolta() {
  std::printf("dex global sobrevive ao arquivo (spec 029):\n");

  nb::NestData d = nb::MakeEmpty(10, 30);
  d.MarkSeen(25);
  d.MarkSeen(150);
  d.MarkSeen(386);
  PreencheSlot(d.At(0, 0), 9);

  const nb::NestData back = nb::Decode(nb::Encode(d));
  Check(back.boxes == 10, "os slots continuam vindo");
  Check(back.Seen(25) && back.Seen(150) && back.Seen(386),
        "as especies vistas voltam");
  Check(back.SeenCount() == 3, "a contagem bate");
  Check(!back.Seen(4), "o que nao foi marcado continua nao marcado");
}

void TestHistoricoPersiste() {
  std::printf("historico sobrevive a saida do Pokemon (spec 029):\n");

  // Deposita, marca, e depois ESVAZIA o slot — o caso que a spec existe para
  // cobrir: a dex da sessao deixa de contar, a global nao.
  nb::NestData d = nb::MakeEmpty(2, 30);
  PreencheSlot(d.At(0, 0), 33);
  d.MarkSeen(25);

  nb::SlotClear(d.At(0, 0));  // Pokemon retirado

  const nb::NestData back = nb::Decode(nb::Encode(d));
  Check(nb::SlotEmpty(back.At(0, 0)), "o slot ficou vazio");
  Check(back.Seen(25), "mas a especie continua na dex global");
}

void TestLeV1() {
  std::printf("compatibilidade com a v1 (spec 029):\n");

  // Arquivo v1 com um Pokemon guardado.
  std::vector<std::uint8_t> v1 = ArquivoV1(10, 30);
  for (std::size_t i = 0; i < nb::kLegacySlotBytes; ++i) {
    v1[nb::kHeaderBytes + i] = static_cast<std::uint8_t>(i + 1);
  }

  const nb::NestData d = nb::Decode(v1);
  Check(d.boxes == 10 && d.slots == 30, "arquivo v1 e ACEITO, nao recusado");
  Check(!nb::SlotEmpty(d.At(0, 0)), "os Pokemon do banco v1 sobrevivem");
  Check(nb::SlotFormatOf(d.At(0, 0)) == nb::kGen3,
        "o slot v1 vira formato gen3 na migracao");
  Check(nb::SlotPayload(d.At(0, 0))[0] == 1, "os bytes do slot v1 estao certos");
  Check(d.SeenCount() == 0, "a dex global entra vazia");

  // Regravar promove para v2, mantendo o conteudo.
  const nb::NestData v2 = nb::Decode(nb::Encode(d));
  Check(!nb::SlotEmpty(v2.At(0, 0)), "regravar mantem o banco");
}

void TestRecusaVersaoNova() {
  std::printf("versao futura e recusada (spec 029):\n");

  std::vector<std::uint8_t> futuro = nb::Encode(nb::MakeEmpty(2, 30));
  futuro[4] = 99;  // v99: app mais novo
  Check(nb::Decode(futuro).boxes == 0, "versao acima da conhecida e recusada");

  std::vector<std::uint8_t> zero = nb::Encode(nb::MakeEmpty(2, 30));
  zero[4] = 0;
  Check(nb::Decode(zero).boxes == 0, "versao 0 e recusada");
}

void TestV2Truncada() {
  std::printf("v2 truncada na dex (spec 029):\n");

  std::vector<std::uint8_t> bytes = nb::Encode(nb::MakeEmpty(2, 30));
  // Corta no meio da secao da dex: ler pela metade daria historico errado.
  bytes.resize(bytes.size() - 10);
  Check(nb::Decode(bytes).boxes == 0, "v2 sem a dex inteira e recusada");
}

// --- Nomes de caixa (spec 030) ---------------------------------------------

// Monta um arquivo v2 de verdade: cabecalho + slots de 80 bytes + dex de 49,
// SEM a secao de nomes. Nao da para partir de um Encode() e rebaixar a versao —
// o Encode grava v5, com slot de 384 e dex de 129.
std::vector<std::uint8_t> ArquivoV2(std::uint16_t boxes, std::uint16_t slots) {
  std::vector<std::uint8_t> out = ArquivoV1(boxes, slots);
  out[4] = 2;  // versao 2
  out.resize(out.size() + nb::kLegacyDexBytes, 0);
  return out;
}

void TestNomes() {
  std::printf("nomes de caixa (spec 030):\n");

  nb::NestData d = nb::MakeEmpty(10, 30);
  Check(d.BoxName(0).empty(), "caixa nova nao tem nome");

  d.SetBoxName(0, "Lendarios");
  d.SetBoxName(3, "Shinies");
  Check(d.BoxName(0) == "Lendarios", "grava e le o nome");
  Check(d.BoxName(3) == "Shinies", "nomes de caixas diferentes nao se misturam");
  Check(d.BoxName(1).empty(), "caixa sem nome continua vazia");

  // Regravar por cima nao deixa cauda do nome anterior.
  d.SetBoxName(0, "Ovos");
  Check(d.BoxName(0) == "Ovos", "nome mais curto nao deixa resto do antigo");

  // Limite: 23 caracteres + terminador.
  const std::string longo(50, 'A');
  d.SetBoxName(5, longo);
  Check(d.BoxName(5).size() == nb::kBoxNameBytes - 1,
        "nome longo e truncado no limite");
  Check(d.BoxName(5) == std::string(nb::kBoxNameBytes - 1, 'A'),
        "o truncamento mantem o comeco do nome");

  // Fora da faixa nao estoura.
  d.SetBoxName(99, "Nada");
  Check(d.BoxName(99).empty(), "caixa fora da faixa devolve vazio");

  // UTF-8: o corte nao pode partir um caractere multibyte ao meio.
  // "ç" = 2 bytes (0xC3 0xA7). 11 desses = 22 bytes; o 12o cruzaria o limite
  // de 23 e teria de ser descartado inteiro, nao pela metade.
  std::string acentos;
  for (int i = 0; i < 15; ++i) acentos += "\xC3\xA7";
  d.SetBoxName(2, acentos);
  const std::string lido = d.BoxName(2);
  Check(lido.size() % 2 == 0, "corte nao parte o caractere UTF-8 ao meio");
  Check(lido.size() <= nb::kBoxNameBytes - 1, "e respeita o limite");
  bool continuacao_solta = false;
  for (std::size_t i = 0; i < lido.size(); i += 2) {
    if ((static_cast<std::uint8_t>(lido[i]) & 0xC0) == 0x80) {
      continuacao_solta = true;
    }
  }
  Check(!continuacao_solta, "nenhum byte de continuacao ficou orfao");
}

void TestNomesIdaEVolta() {
  std::printf("nomes sobrevivem ao arquivo (spec 030):\n");

  nb::NestData d = nb::MakeEmpty(10, 30);
  d.SetBoxName(0, "Lendarios");
  d.SetBoxName(9, "Ultima");
  d.MarkSeen(25);
  PreencheSlot(d.At(2, 3), 60);

  const nb::NestData back = nb::Decode(nb::Encode(d));
  Check(back.BoxName(0) == "Lendarios", "o nome volta");
  Check(back.BoxName(9) == "Ultima", "o nome da ultima caixa volta");
  Check(back.BoxName(4).empty(), "caixa sem nome continua sem nome");
  Check(back.Seen(25), "a dex global continua vindo junto");
  Check(!nb::SlotEmpty(back.At(2, 3)), "os slots continuam vindo");
}

void TestLeV2SemNomes() {
  std::printf("compatibilidade com a v2 (spec 030):\n");

  std::vector<std::uint8_t> v2 = ArquivoV2(10, 30);
  // Poe um Pokemon e marca a dex, para provar que nada disso se perde.
  for (std::size_t i = 0; i < nb::kLegacySlotBytes; ++i) {
    v2[nb::kHeaderBytes + i] = static_cast<std::uint8_t>(i + 7);
  }

  const nb::NestData d = nb::Decode(v2);
  Check(d.boxes == 10, "arquivo v2 e ACEITO");
  Check(!nb::SlotEmpty(d.At(0, 0)), "os Pokemon do banco v2 sobrevivem");
  Check(d.BoxName(0).empty(), "os nomes entram vazios");

  // Regravar promove para v3 sem perder nada.
  nb::NestData promovido = d;
  promovido.SetBoxName(0, "Novo");
  const nb::NestData v3 = nb::Decode(nb::Encode(promovido));
  Check(!nb::SlotEmpty(v3.At(0, 0)), "promover para v3 mantem o banco");
  Check(v3.BoxName(0) == "Novo", "e o nome novo e gravado");
}

void TestV3Truncada() {
  std::printf("v3 truncada nos nomes (spec 030):\n");

  std::vector<std::uint8_t> bytes = nb::Encode(nb::MakeEmpty(10, 30));
  bytes.resize(bytes.size() - 10);
  Check(nb::Decode(bytes).boxes == 0, "v3 sem os nomes inteiros e recusada");
}

void TestNomeSemTerminador() {
  std::printf("nome sem terminador (spec 030):\n");

  // Bloco cheio de bytes nao-zero: a leitura tem que parar no fim do bloco em
  // vez de continuar pelo proximo.
  nb::NestData d = nb::MakeEmpty(3, 30);
  for (std::size_t i = 0; i < nb::kBoxNameBytes; ++i) d.names[i] = 'B';
  Check(d.BoxName(0).size() == nb::kBoxNameBytes,
        "le no maximo o bloco inteiro, sem invadir o proximo");
  Check(d.BoxName(1).empty(), "a caixa seguinte continua vazia");
}

// --- v5: slot multi-formato, 400 caixas, dex 1025 (spec 090) ---------------

void TestSlotCabecalho() {
  std::printf("cabecalho do slot (D1 da spec 090):\n");

  Check(nb::kSlotBytes == 384, "o slot tem 384 bytes");
  Check(nb::kSlotHeaderBytes + nb::kSlotPayload == nb::kSlotBytes,
        "4 de cabecalho + 380 de payload");
  Check(nb::FormatBytes(nb::kPa8) <= nb::kSlotPayload,
        "o maior formato conhecido (pa8, 376B) cabe no payload");

  nb::NestData d = nb::MakeEmpty(2, 30);
  std::uint8_t* s = d.At(0, 0);
  Check(nb::SlotEmpty(s), "slot novo nasce vazio");
  Check(nb::SlotFormatOf(s) == nb::kEmpty, "com formato kEmpty");

  std::vector<std::uint8_t> rec(344, 0xAB);
  Check(nb::SlotWrite(s, nb::kPk9, rec.data(), rec.size()),
        "grava um pk9 no slot");
  Check(nb::SlotFormatOf(s) == nb::kPk9, "o formato fica gravado");
  Check(nb::SlotSize(s) == 344, "o tamanho real fica gravado");
  Check(!nb::SlotEmpty(s), "e o slot deixa de ser vazio");
  Check(nb::SlotPayload(s)[0] == 0xAB && nb::SlotPayload(s)[343] == 0xAB,
        "o payload esta la inteiro");
  Check(nb::SlotPayload(s)[344] == 0,
        "e o resto do payload fica zerado");

  // Payload maior que o slot e RECUSADO. Gravar truncado viraria Pokemon
  // corrompido no save de destino — pior que nao gravar.
  std::vector<std::uint8_t> grande(nb::kSlotPayload + 1, 0xFF);
  Check(!nb::SlotWrite(s, nb::kPk9, grande.data(), grande.size()),
        "payload maior que o slot e recusado");
  Check(nb::SlotFormatOf(s) == nb::kPk9 && nb::SlotSize(s) == 344,
        "e o slot anterior nao e corrompido pela tentativa");

  // Regravar com formato menor nao deixa cauda do anterior.
  std::vector<std::uint8_t> pequeno(80, 0x11);
  nb::SlotWrite(s, nb::kGen3, pequeno.data(), pequeno.size());
  Check(nb::SlotSize(s) == 80 && nb::SlotPayload(s)[100] == 0,
        "regravar menor zera a cauda do ocupante anterior");

  nb::SlotClear(s);
  Check(nb::SlotEmpty(s), "SlotClear esvazia");
}

void TestFormatosMistos() {
  std::printf("round-trip v5 com formatos mistos (spec 090):\n");

  nb::NestData d = nb::MakeEmpty(6, 30);
  PreencheSlot(d.At(0, 0), 5);                    // gen3, cheio
  PreencheSlotFmt(d.At(1, 10), nb::kPk9, 0x20);   // pk9, cheio
  PreencheSlotFmt(d.At(2, 29), nb::kPa8, 0x30);   // pa8, o maior, cheio
  PreencheSlotFmt(d.At(3, 0), nb::kPb7, 0x40);    // pb7, o menor moderno
  // As caixas 4 e 5 ficam vazias de proposito.

  const auto bytes = nb::Encode(d);
  Check(bytes[4] == 5, "o arquivo e gravado como v5");
  Check(bytes[10] == (nb::kSlotBytes & 0xFF) && bytes[11] == (nb::kSlotBytes >> 8),
        "slot_bytes vai no cabecalho, no lugar da antiga reserva (TD-01)");

  const nb::NestData back = nb::Decode(bytes);
  Check(back.boxes == 6 && back.slots == 30, "o cabecalho volta certo");
  Check(back.raw == d.raw, "TODOS os bytes de TODOS os slots voltam identicos");

  Check(nb::SlotFormatOf(back.At(0, 0)) == nb::kGen3 &&
            nb::SlotSize(back.At(0, 0)) == 80,
        "o slot gen3 volta com formato e tamanho");
  Check(nb::SlotFormatOf(back.At(1, 10)) == nb::kPk9 &&
            nb::SlotSize(back.At(1, 10)) == 344,
        "o slot pk9 volta com formato e tamanho");
  Check(nb::SlotFormatOf(back.At(2, 29)) == nb::kPa8 &&
            nb::SlotSize(back.At(2, 29)) == 376,
        "o slot pa8 volta com formato e tamanho");
  Check(nb::SlotFormatOf(back.At(3, 0)) == nb::kPb7 &&
            nb::SlotSize(back.At(3, 0)) == 260,
        "o slot pb7 volta com formato e tamanho");

  // Os bytes do pa8 (o maior) conferidos um a um.
  bool pa8_ok = true;
  for (std::size_t i = 0; i < 376; ++i) {
    if (nb::SlotPayload(back.At(2, 29))[i] !=
        static_cast<std::uint8_t>(0x30 + i * 3)) {
      pa8_ok = false;
    }
  }
  Check(pa8_ok, "os 376 bytes do pa8 sobrevivem um a um");

  Check(nb::SlotEmpty(back.At(4, 0)) && nb::SlotEmpty(back.At(5, 29)),
        "os slots vazios continuam vazios");
}

void TestDex1025() {
  std::printf("dex de 1025 bits (D3 da spec 090):\n");

  Check(nb::kDexBits == 1025, "a dex vai ate 1025");
  Check(nb::kDexBytes == 129, "o que da 129 bytes");

  nb::NestData d = nb::MakeEmpty(2, 30);
  d.MarkSeen(1);      // Bulbasaur
  d.MarkSeen(386);    // ultimo do gen3, a antiga borda
  d.MarkSeen(387);    // o primeiro que NAO cabia antes
  d.MarkSeen(1025);   // Pecharunt, a nova borda
  Check(d.SeenCount() == 4, "as quatro marcam");

  d.MarkSeen(1026);
  d.MarkSeen(0);
  d.MarkSeen(-1);
  Check(d.SeenCount() == 4, "fora da faixa nao marca nem estoura");

  const nb::NestData back = nb::Decode(nb::Encode(d));
  Check(back.Seen(1) && back.Seen(386) && back.Seen(387) && back.Seen(1025),
        "todas voltam do arquivo");
  Check(back.SeenCount() == 4, "e a contagem bate");
  Check(!back.Seen(500), "o que nao foi marcado continua nao marcado");
}

void Test400Caixas() {
  std::printf("400 caixas (D5 da spec 090):\n");

  nb::NestData d = nb::MakeEmpty(400, 30);
  Check(d.valid(), "banco de 400 caixas e valido");
  Check(d.raw.size() == 400u * 30u * nb::kSlotBytes,
        "o tamanho bate: 400 x 30 x 384");

  PreencheSlot(d.At(0, 0), 1);
  PreencheSlotFmt(d.At(399, 29), nb::kPk8, 0x77);  // ultimo slot da ultima caixa
  d.SetBoxName(399, "Fim");

  const nb::NestData back = nb::Decode(nb::Encode(d));
  Check(back.boxes == 400, "as 400 caixas voltam");
  Check(nb::SlotFormatOf(back.At(399, 29)) == nb::kPk8 &&
            nb::SlotPayload(back.At(399, 29))[0] == 0x77,
        "o ultimo slot da ultima caixa sobrevive");
  Check(back.BoxName(399) == "Fim", "e o nome da ultima caixa tambem");
  Check(back.At(400, 0) == nullptr, "a caixa 400 continua fora da faixa");
}

// Monta um arquivo v4 COMPLETO a mao, do jeito que o encoder v4 gravava:
// slots de 80 bytes, dex de 49, nomes de 24 por caixa, movesets no fim.
std::vector<std::uint8_t> ArquivoV4(std::uint16_t boxes, std::uint16_t slots) {
  std::vector<std::uint8_t> out = ArquivoV1(boxes, slots);
  out[4] = 4;  // versao 4
  out.resize(out.size() + nb::kLegacyDexBytes, 0);
  out.resize(out.size() + static_cast<std::size_t>(boxes) * nb::kBoxNameBytes, 0);
  // Contador de movesets: zero (a secao v4 vazia sao 4 bytes).
  out.resize(out.size() + 4, 0);
  return out;
}

void TestMigracaoV4ByteAByte() {
  std::printf("migracao v4 -> v5 byte a byte (spec 090):\n");

  const std::uint16_t boxes = 10, slots = 30;
  std::vector<std::uint8_t> v4 = ArquivoV4(boxes, slots);

  // Preenche TRES slots espalhados com bytes reconheciveis, e guarda copia do
  // que foi escrito para comparar depois.
  const std::size_t idx[3] = {0, 5 * 30 + 17, 9 * 30 + 29};
  std::uint8_t esperado[3][80];
  for (int k = 0; k < 3; ++k) {
    for (std::size_t i = 0; i < nb::kLegacySlotBytes; ++i) {
      const std::uint8_t b = static_cast<std::uint8_t>(k * 61 + i + 1);
      v4[nb::kHeaderBytes + idx[k] * nb::kLegacySlotBytes + i] = b;
      esperado[k][i] = b;
    }
  }

  // Dex antiga: marca 25, 150 e 386 (a borda do gen3).
  const std::size_t dex_off =
      nb::kHeaderBytes + static_cast<std::size_t>(boxes) * slots * nb::kLegacySlotBytes;
  for (int sp : {25, 150, 386}) {
    v4[dex_off + sp / 8] |= static_cast<std::uint8_t>(1u << (sp % 8));
  }

  // Nomes: caixa 0 e caixa 9.
  const std::size_t names_off = dex_off + nb::kLegacyDexBytes;
  const char n0[] = "Lendarios";
  const char n9[] = "Ultima";
  for (std::size_t i = 0; i < sizeof(n0) - 1; ++i) {
    v4[names_off + i] = static_cast<std::uint8_t>(n0[i]);
  }
  for (std::size_t i = 0; i < sizeof(n9) - 1; ++i) {
    v4[names_off + 9 * nb::kBoxNameBytes + i] = static_cast<std::uint8_t>(n9[i]);
  }

  const nb::NestData d = nb::Decode(v4);
  Check(d.boxes == boxes && d.slots == slots, "o arquivo v4 ABRE");

  // 1) Os slots, byte a byte.
  bool slots_ok = true;
  for (int k = 0; k < 3; ++k) {
    const std::uint8_t* got = d.At(idx[k] / slots, idx[k] % slots);
    if (nb::SlotFormatOf(got) != nb::kGen3) slots_ok = false;
    if (nb::SlotSize(got) != 80) slots_ok = false;
    for (std::size_t i = 0; i < 80; ++i) {
      if (nb::SlotPayload(got)[i] != esperado[k][i]) slots_ok = false;
    }
  }
  Check(slots_ok, "TODOS os bytes de cada slot v4 sobrevivem, como formato gen3");

  // 2) A dex.
  Check(d.Seen(25) && d.Seen(150) && d.Seen(386), "a dex antiga volta inteira");
  Check(d.SeenCount() == 3, "sem marcar nada a mais");
  Check(!d.Seen(387) && !d.Seen(1025),
        "e os bits novos (387..1025) entram zerados");

  // 3) Os nomes.
  Check(d.BoxName(0) == "Lendarios" && d.BoxName(9) == "Ultima",
        "os nomes das caixas voltam");
  Check(d.BoxName(5).empty(), "caixa sem nome continua sem nome");

  // 4) A memoria de moveset, vazia no arquivo, continua vazia.
  Check(d.movesets.size() == 0, "a memoria de moveset entra vazia");

  // 5) Regravar promove para v5 sem perder NADA.
  const auto v5 = nb::Encode(d);
  Check(v5[4] == 5, "regravar grava v5");
  const nb::NestData back = nb::Decode(v5);

  bool promovido_ok = true;
  for (int k = 0; k < 3; ++k) {
    const std::uint8_t* got = back.At(idx[k] / slots, idx[k] % slots);
    for (std::size_t i = 0; i < 80; ++i) {
      if (nb::SlotPayload(got)[i] != esperado[k][i]) promovido_ok = false;
    }
  }
  Check(promovido_ok, "e o v5 gravado preserva os mesmos bytes");
  Check(back.Seen(25) && back.Seen(150) && back.Seen(386) &&
            back.SeenCount() == 3,
        "preserva a dex");
  Check(back.BoxName(0) == "Lendarios" && back.BoxName(9) == "Ultima",
        "preserva os nomes");
}

void TestRecusaV6() {
  std::printf("versao acima da v5 e recusada (spec 090):\n");

  auto v6 = nb::Encode(nb::MakeEmpty(2, 30));
  v6[4] = 6;
  Check(nb::Decode(v6).boxes == 0, "v6 e recusada — nao adivinha o layout");

  auto v5 = nb::Encode(nb::MakeEmpty(2, 30));
  Check(nb::Decode(v5).boxes == 2, "mas a v5 continua abrindo");

  // slot_bytes que este codigo nao sabe ler tambem e recusado: o layout do
  // arquivo nao e o nosso, e ler torto poria Pokemon em slots errados.
  auto estranho = nb::Encode(nb::MakeEmpty(2, 30));
  estranho[10] = 0x00;
  estranho[11] = 0x04;  // 1024 bytes por slot
  Check(nb::Decode(estranho).boxes == 0, "slot_bytes desconhecido e recusado");
}

}  // namespace

int main() {
  TestIdaEVolta();
  TestVazios();
  TestArquivoRuim();
  TestForaDaFaixa();
  TestBancoZerado();
  TestReparse();
  TestRawVazio();
  TestBitmap();
  TestDexIdaEVolta();
  TestHistoricoPersiste();
  TestLeV1();
  TestRecusaVersaoNova();
  TestV2Truncada();
  TestNomes();
  TestNomesIdaEVolta();
  TestLeV2SemNomes();
  TestV3Truncada();
  TestNomeSemTerminador();
  TestSlotCabecalho();
  TestFormatosMistos();
  TestDex1025();
  Test400Caixas();
  TestMigracaoV4ByteAByte();
  TestRecusaV6();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
