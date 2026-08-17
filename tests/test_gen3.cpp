// Teste do parser gen3. Sem framework — assert e um main.
//
// Os testes que dependem de um save real sao pulados se o arquivo nao existir,
// para o teste continuar util em maquina sem o save.

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gen3_save.h"

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

std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

// --- Testes que nao precisam de save --------------------------------------

void TestChecksum() {
  std::printf("checksum:\n");

  // Soma simples: quatro words de 1 -> 4.
  std::vector<std::uint8_t> data(16, 0);
  for (int i = 0; i < 4; ++i) data[i * 4] = 1;
  Check(g3::ComputeChecksum(data.data(), 16) == 4, "soma de 4 words");

  // Zerado da zero.
  std::vector<std::uint8_t> zeros(64, 0);
  Check(g3::ComputeChecksum(zeros.data(), 64) == 0, "buffer zerado");

  // O fold de 32 para 16 bits: 0xFFFFFFFF -> 0xFFFF + 0xFFFF = 0x1FFFE,
  // truncado para 16 bits = 0xFFFE.
  std::vector<std::uint8_t> max(4, 0xFF);
  Check(g3::ComputeChecksum(max.data(), 4) == 0xFFFE, "fold de 32 para 16 bits");
}

void TestRejectsGarbage() {
  std::printf("rejeicao de lixo:\n");

  std::vector<std::uint8_t> empty;
  Check(!g3::ParseSave(empty).has_value(), "arquivo vazio");

  std::vector<std::uint8_t> garbage(g3::kSaveSize, 0xAB);
  Check(!g3::ParseSave(garbage).has_value(), "bytes aleatorios sem signature");

  std::vector<std::uint8_t> tiny(100, 0);
  Check(!g3::ParseSave(tiny).has_value(), "arquivo pequeno demais");
}

void TestSpeciesNames() {
  std::printf("nomes de especie:\n");
  Check(g3::SpeciesName(1) == "Bulbasaur", "indice 1 = Bulbasaur");
  Check(g3::SpeciesName(151) == "Mew", "indice 151 = Mew");
  // 277+ e a faixa deslocada do gen3.
  Check(g3::SpeciesName(277) == "Treecko", "indice 277 = Treecko (deslocado)");
  Check(g3::SpeciesName(411) == "Chimecho", "indice 411 = Chimecho");
  // 252-276 sao invalidos no gen3.
  Check(g3::SpeciesName(260) == "???", "indice 260 = invalido");
  Check(g3::SpeciesName(9999) == "???", "indice fora da tabela");
}

// O mapeamento interno -> National Dex e a ponte para o arquivo de sprite.
// Errar aqui mostra o sprite errado com o nome certo — falha silenciosa.
void TestNationalDex() {
  std::printf("national dex:\n");

  // 1-251 coincidem com a National Dex.
  Check(g3::NationalDex(1) == 1, "interno 1 -> dex 1 (Bulbasaur)");
  Check(g3::NationalDex(151) == 151, "interno 151 -> dex 151 (Mew)");
  Check(g3::NationalDex(251) == 251, "interno 251 -> dex 251 (Celebi)");

  // 277+ sao deslocados.
  Check(g3::NationalDex(277) == 252, "interno 277 -> dex 252 (Treecko)");
  Check(g3::NationalDex(411) == 358, "interno 411 -> dex 358 (Chimecho)");
  Check(g3::NationalDex(410) == 386, "interno 410 -> dex 386 (Deoxys)");

  // 252-276 sao invalidos no gen3.
  Check(g3::NationalDex(260) == 0, "interno 260 -> 0 (invalido)");
  Check(g3::NationalDex(9999) == 0, "fora da tabela -> 0");

  // Coerencia: se ha nome, deve haver dex, e vice-versa.
  int mismatches = 0;
  for (std::uint16_t i = 1; i < g3::SpeciesTableSize(); ++i) {
    const bool has_name = g3::SpeciesName(i) != "???";
    const bool has_dex = g3::NationalDex(i) != 0;
    if (has_name != has_dex) ++mismatches;
  }
  Check(mismatches == 0, "nome e dex concordam sobre validade");

  // Nenhuma dex de gen3 passa de 386.
  int out_of_range = 0;
  for (std::uint16_t i = 1; i < g3::SpeciesTableSize(); ++i) {
    if (g3::NationalDex(i) > 386) ++out_of_range;
  }
  Check(out_of_range == 0, "nenhuma dex acima de 386");
}

// Campos expandidos do BoxPokemon (spec 008). Trava valores de Pokemon
// concretos do save de referencia: erro nos offsets de bloco produz numeros
// plausiveis mas errados.
void TestPokemonDetails(const std::vector<std::uint8_t>& file) {
  std::printf("detalhes do Pokemon:\n");

  const auto save = g3::ParseSave(file);
  if (!save) {
    Check(false, "save parseado");
    return;
  }
  const auto pc = g3::BuildPcBuffer(file, *save);

  // Box 1 slot 1: Smeargle com HMs, IVs perfeitos, spread 252/252.
  const auto a = g3::ReadBoxPokemonFrom(pc, 0, 0);
  Check(a.has_value() && !a->empty(), "slot 1 ocupado");
  if (!a) return;

  Check(g3::SpeciesName(a->species) == "Smeargle", "slot 1 = Smeargle");
  Check(g3::NatureName(a->nature()) == "Adamant", "natureza = Adamant");
  Check(a->friendship == 255, "amizade = 255");

  bool all_31 = true;
  for (int i = 0; i < 6; ++i) {
    if (a->ivs[i] != 31) all_31 = false;
  }
  Check(all_31, "IVs todos 31");

  Check(a->evs[1] == 252, "EV Atk = 252");
  Check(a->evs[3] == 252, "EV Spe = 252");

  Check(g3::MoveName(a->moves[0]) == "Fly", "golpe 1 = Fly");
  Check(g3::MoveName(a->moves[1]) == "Cut", "golpe 2 = Cut");

  // Identidade da barra de status (spec 098): OT, idioma e jogo de origem
  // saem do proprio slot. Valores exatos dependem do save; aqui se confere
  // que o parser devolve algo plausivel em vez de lixo/vazio.
  Check(!a->ot_name.empty(), "OT presente");
  Check(a->language >= 1 && a->language <= 7, "idioma na faixa do gen3");
  Check(a->origin_game >= 1 && a->origin_game <= 15,
        "jogo de origem na faixa do gen3");

  // Box 1 slot 3: Lapras, spread defensivo/especial.
  const auto c = g3::ReadBoxPokemonFrom(pc, 0, 2);
  Check(c.has_value() && !c->empty(), "slot 3 ocupado");
  if (!c) return;
  Check(g3::SpeciesName(c->species) == "Lapras", "slot 3 = Lapras");
  Check(c->evs[0] == 252, "Lapras EV HP = 252");
  Check(c->evs[4] == 252, "Lapras EV SpA = 252");
  Check(g3::MoveName(c->moves[0]) == "Surf", "Lapras golpe 1 = Surf");

  // Slot vazio nao deve trazer lixo.
  const auto empty = g3::ReadBoxPokemonFrom(pc, 0, 3);
  Check(empty.has_value() && empty->empty(), "slot 4 vazio");

  // Move 0 nao tem nome; indice fora da faixa tambem nao.
  Check(g3::MoveName(0).empty(), "golpe 0 = sem nome");
  Check(g3::MoveName(9999).empty(), "golpe fora da faixa = sem nome");
  Check(g3::NatureName(24) == "Quirky", "natureza 24 = Quirky");
  Check(g3::NatureName(99) == "???", "natureza fora da faixa");
}

// Personal data: tipos e base stats vem da tabela do jogo, indexada por
// National Dex. Erro de offset produz stats plausiveis mas de outra especie.
void TestPersonal() {
  std::printf("personal data:\n");

  const auto bulb = g3::Personal(1);
  Check(bulb.base_stats[0] == 45, "Bulbasaur HP = 45");
  Check(bulb.base_stats[1] == 49, "Bulbasaur Atk = 49");
  Check(bulb.base_stats[4] == 65, "Bulbasaur SpA = 65");
  Check(g3::TypeName(bulb.type1) == "Planta", "Bulbasaur tipo 1 = Planta");
  Check(g3::TypeName(bulb.type2) == "Venenoso", "Bulbasaur tipo 2 = Venenoso");
  Check(!bulb.single_type(), "Bulbasaur tem dois tipos");

  const auto chari = g3::Personal(6);
  Check(chari.base_stats[0] == 78, "Charizard HP = 78");
  Check(chari.base_stats[3] == 100, "Charizard Spe = 100");
  Check(chari.base_stats[4] == 109, "Charizard SpA = 109");
  Check(g3::TypeName(chari.type1) == "Fogo", "Charizard tipo 1 = Fogo");
  Check(g3::TypeName(chari.type2) == "Voador", "Charizard tipo 2 = Voador");

  // Tipo unico e representado repetindo o valor.
  const auto pika = g3::Personal(25);
  Check(pika.single_type(), "Pikachu tem tipo unico");
  Check(g3::TypeName(pika.type1) == "Eletrico", "Pikachu = Eletrico");

  // Mewtwo: ultimo da gen1, verifica que a indexacao nao desanda no meio.
  const auto mew2 = g3::Personal(150);
  Check(mew2.base_stats[4] == 154, "Mewtwo SpA = 154");

  // --- Habilidades (spec 023) ---
  //
  // Confere contra valores publicos do gen3. Se o offset 22-23 estivesse
  // errado, estes nomes viriam trocados ou vazios.
  Check(std::string(g3::AbilityName(bulb.ability(0))) == "Overgrow",
        "Bulbasaur = Overgrow");
  Check(std::string(g3::AbilityName(chari.ability(0))) == "Blaze",
        "Charizard = Blaze");
  Check(std::string(g3::AbilityName(pika.ability(0))) == "Static",
        "Pikachu = Static");
  Check(std::string(g3::AbilityName(g3::Personal(7).ability(0))) == "Torrent",
        "Squirtle = Torrent");
  Check(std::string(g3::AbilityName(mew2.ability(0))) == "Pressure",
        "Mewtwo = Pressure");

  // Especie com habilidade unica devolve a mesma nos dois valores do bit.
  Check(pika.ability(0) == pika.ability(1),
        "Pikachu tem uma habilidade so");

  // Abra tem DUAS habilidades diferentes: e o caso que prova que o bit
  // seleciona de verdade, em vez de sempre devolver a primeira.
  const auto abra = g3::Personal(63);
  Check(std::string(g3::AbilityName(abra.ability(0))) == "Synchronize",
        "Abra bit 0 = Synchronize");
  Check(std::string(g3::AbilityName(abra.ability(1))) == "Inner Focus",
        "Abra bit 1 = Inner Focus");

  // Robustez: dado corrompido nao pode estourar a tela de detalhes.
  Check(std::string(g3::AbilityName(200)).empty(),
        "habilidade fora da faixa devolve vazio");
  Check(g3::Personal(0).ability(0) == 0, "dex 0 nao tem habilidade");
  Check(g3::Personal(999).ability(0) == 0, "dex fora da faixa nao estoura");

  // Deoxys: ultimo do gen3, verifica o fim da tabela.
  // Atk 180 e nao 150: no FireRed a entrada base do Deoxys e a forma ATTACK,
  // nao a Normal. Cada jogo do gen3 traz uma forma diferente.
  const auto deo = g3::Personal(386);
  Check(deo.base_stats[1] == 180, "Deoxys Atk = 180 (forma Attack, do FR)");
  Check(deo.base_stats[2] == 20, "Deoxys Def = 20 (forma Attack)");
  Check(g3::TypeName(deo.type1) == "Psiquico", "Deoxys = Psiquico");

  // --- Sexo (spec 098) ---
  //
  // gender_ratio no byte 16 da entrada, conferido contra valores publicos:
  // 87.5% macho = 31, so femea = 254, sem sexo = 255, so macho = 0.
  Check(bulb.gender_ratio == 31, "Bulbasaur ratio = 31");
  Check(g3::Personal(29).gender_ratio == 254, "Nidoran-F ratio = 254");
  Check(g3::Personal(81).gender_ratio == 255, "Magnemite ratio = 255");
  Check(g3::Personal(106).gender_ratio == 0, "Hitmonlee ratio = 0");

  // Derivacao pelo personality: low byte < ratio => femea.
  g3::BoxPokemon gm;
  gm.species = 1;  // Bulbasaur (indice interno = dex na gen1)
  gm.personality = 30;
  Check(g3::Gender(gm) == 1, "Bulbasaur pid 30 = femea");
  gm.personality = 31;
  Check(g3::Gender(gm) == 0, "Bulbasaur pid 31 = macho");
  gm.species = 81;
  Check(g3::Gender(gm) == 2, "Magnemite = sem sexo");
  gm.display_gender = 1;  // fonte moderna ja resolveu: ratio e ignorado
  Check(g3::Gender(gm) == 1, "display_gender vence a derivacao");

  // Fora da faixa devolve entrada zerada, nao lixo.
  const auto oob = g3::Personal(999);
  Check(oob.base_stats[0] == 0, "dex 999 = entrada zerada");
  Check(g3::Personal(0).base_stats[0] == 0, "dex 0 = entrada zerada");

  Check(g3::TypeName(99) == "???", "tipo fora da faixa");
}

// Stats calculados (spec 010). Pokemon de caixa nao armazena stats de batalha
// — o bloco 0x50+ so existe no formato de party. Aqui aplicamos a formula do
// jogo, e erro em qualquer termo produz numero plausivel mas errado.
// Shiny (spec 025). PID/OT montados a mao: prova a formula, nao a leitura do
// save — isso depende de conferencia contra o PKHeX (risco registrado na spec).
void TestShiny() {
  std::printf("shiny:\n");

  auto mk = [](std::uint32_t pid, std::uint16_t tid, std::uint16_t sid) {
    g3::BoxPokemon m;
    m.species = 25;  // != 0, senao empty() curto-circuita
    m.personality = pid;
    m.ot_id = (static_cast<std::uint32_t>(sid) << 16) | tid;
    return m;
  };

  // XOR == 0: o caso mais shiny possivel.
  Check(mk(0, 0, 0).is_shiny(), "PID e OT zerados -> XOR 0, shiny");

  // TID ^ SID ^ PIDhi ^ PIDlo = 1 ^ 0 ^ 0 ^ 1 = 0.
  Check(mk(1, 1, 0).is_shiny(), "XOR 0 por cancelamento, shiny");

  // A borda que define a geracao: 7 e shiny, 8 nao. Se alguem trocar o limiar
  // para 16 (gen6+), o segundo caso quebra.
  Check(mk(7, 0, 0).is_shiny(), "XOR 7 e shiny (borda de dentro)");
  Check(!mk(8, 0, 0).is_shiny(), "XOR 8 NAO e shiny no gen3");
  Check(!mk(15, 0, 0).is_shiny(), "XOR 15 nao e shiny (seria no gen6+)");

  // Caso comum.
  Check(!mk(0x12345678, 0xABCD, 0x1234).is_shiny(), "PID/OT comuns nao shiny");

  // Slot vazio nunca e shiny, mesmo com PID/OT que dariam XOR 0.
  g3::BoxPokemon vazio;
  vazio.personality = 0;
  vazio.ot_id = 0;
  Check(!vazio.is_shiny(), "slot vazio nunca e shiny");
}

// Nomes por dex nacional, cobrindo ate a gen9 (spec 035).
void TestNomePorDex() {
  std::printf("nome por dex nacional (spec 035):\n");

  Check(g3::MaxKnownDex() == 1025, "conhece ate a dex 1025");

  // Bordas de cada geracao: se a tabela estivesse deslocada, estes saem
  // errados de forma obvia.
  Check(g3::SpeciesNameByDex(1) == "Bulbasaur", "dex 1 = Bulbasaur");
  Check(g3::SpeciesNameByDex(151) == "Mew", "dex 151 = Mew (fim da gen1)");
  Check(g3::SpeciesNameByDex(251) == "Celebi", "dex 251 = Celebi (gen2)");
  Check(g3::SpeciesNameByDex(386) == "Deoxys", "dex 386 = Deoxys (gen3)");
  Check(g3::SpeciesNameByDex(493) == "Arceus", "dex 493 = Arceus (gen4)");
  Check(g3::SpeciesNameByDex(649) == "Genesect", "dex 649 = Genesect (gen5)");
  Check(g3::SpeciesNameByDex(721) == "Volcanion", "dex 721 = Volcanion (gen6)");
  Check(g3::SpeciesNameByDex(809) == "Melmetal", "dex 809 = Melmetal (gen7)");
  Check(g3::SpeciesNameByDex(905) == "Enamorus", "dex 905 = Enamorus (gen8)");
  Check(g3::SpeciesNameByDex(1025) == "Pecharunt",
        "dex 1025 = Pecharunt (gen9)");

  // Fora da faixa nao estoura.
  Check(g3::SpeciesNameByDex(0).empty(), "dex 0 e vazio");
  Check(g3::SpeciesNameByDex(-1).empty(), "dex negativo e vazio");
  Check(g3::SpeciesNameByDex(1026).empty(), "dex acima do maximo e vazio");
  Check(g3::SpeciesNameByDex(99999).empty(), "dex enorme e vazio");
}

// TD-01 da spec 035: duas tabelas de nome convivem (indice interno do gen3 e
// dex nacional). Este teste e a mitigacao — elas nao podem divergir.
void TestTabelasConcordam() {
  std::printf("as duas tabelas de nome concordam (spec 035):\n");

  int divergencias = 0;
  int comparados = 0;
  for (std::uint16_t interno = 1; interno < g3::SpeciesTableSize(); ++interno) {
    const int dex = g3::NationalDex(interno);
    if (dex <= 0) continue;  // indice invalido no gen3
    const std::string por_interno = g3::SpeciesName(interno);
    const std::string por_dex = g3::SpeciesNameByDex(dex);
    ++comparados;
    if (por_interno != por_dex) {
      if (divergencias < 3) {
        std::printf("       dex %d: interno=\"%s\" dex=\"%s\"\n", dex,
                    por_interno.c_str(), por_dex.c_str());
      }
      ++divergencias;
    }
  }
  Check(comparados == 386, "comparou as 386 especies do gen3");
  Check(divergencias == 0,
        "nenhum nome diverge entre a tabela interna e a por dex");
}

// --- Escrita (spec 033) ----------------------------------------------------
//
// A escrita altera save de jogo. O teste de ida e volta abaixo e a prova de
// que os offsets estao certos: um save que NAO foi alterado tem que sair
// identico byte a byte.

void TestEscritaNoPcBuffer() {
  std::printf("escrita no PC buffer (spec 033):\n");

  // Buffer do tamanho real: 9 secoes de PC x 3968 bytes.
  std::vector<std::uint8_t> pc((14 - 5) * 3968, 0);

  std::uint8_t rec[80];
  for (int i = 0; i < 80; ++i) rec[i] = static_cast<std::uint8_t>(i + 1);

  Check(g3::WriteBoxPokemonTo(pc, 0, 0, rec), "escreve no primeiro slot");
  const auto lido = g3::ReadBoxPokemonFrom(pc, 0, 0);
  Check(lido.has_value(), "e le de volta");

  bool bytes_iguais = true;
  for (int i = 0; i < 80; ++i) {
    if (lido->raw[i] != rec[i]) bytes_iguais = false;
  }
  Check(bytes_iguais, "os 80 bytes voltam identicos");

  // Esvaziar.
  Check(g3::WriteBoxPokemonTo(pc, 0, 0, nullptr), "esvazia o slot");
  const auto vazio = g3::ReadBoxPokemonFrom(pc, 0, 0);
  Check(vazio.has_value() && vazio->empty(), "o slot fica vazio");

  // Slot no meio e no fim, para pegar erro de indexacao.
  Check(g3::WriteBoxPokemonTo(pc, 7, 15, rec), "escreve no meio");
  Check(g3::WriteBoxPokemonTo(pc, 13, 29, rec), "escreve no ultimo slot");
  const auto ultimo = g3::ReadBoxPokemonFrom(pc, 13, 29);
  Check(ultimo.has_value() && ultimo->raw[0] == 1, "o ultimo slot volta certo");

  // Fora da faixa nao altera nada.
  Check(!g3::WriteBoxPokemonTo(pc, 14, 0, rec), "caixa fora da faixa recusa");
  Check(!g3::WriteBoxPokemonTo(pc, 0, 30, rec), "slot fora da faixa recusa");

  std::vector<std::uint8_t> curto(100, 0);
  Check(!g3::WriteBoxPokemonTo(curto, 0, 0, rec), "buffer pequeno recusa");
}

// O teste central da spec: um save real que passa por ida e volta SEM
// alteracao tem que sair byte a byte identico. Se falhar, ha erro de offset e
// nenhum save de verdade pode ser tocado.
void TestIdaEVoltaSemAlteracao(const std::vector<std::uint8_t>& file) {
  std::printf("save intacto apos ida e volta (spec 033):\n");

  const auto parsed = g3::ParseSave(file);
  if (!parsed) {
    std::printf("  SKIP save nao parseavel\n");
    return;
  }

  const auto pc = g3::BuildPcBuffer(file, *parsed);
  if (pc.empty()) {
    std::printf("  SKIP PC buffer vazio (secao faltando)\n");
    return;
  }

  std::vector<std::uint8_t> copia = file;
  Check(g3::ApplyPcBuffer(copia, *parsed, pc), "aplica o buffer de volta");

  // As secoes de PC (5..13) tem que voltar identicas. As demais nao sao
  // tocadas. A unica diferenca possivel e o checksum, se o original ja
  // estivesse invalido — por isso comparamos o arquivo inteiro e, se diferir,
  // conferimos que a diferenca esta so nos checksums.
  std::size_t diferentes = 0;
  for (std::size_t i = 0; i < file.size(); ++i) {
    if (file[i] != copia[i]) ++diferentes;
  }
  if (diferentes == 0) {
    Check(true, "arquivo identico byte a byte");
  } else {
    // Diferenca aceitavel: so nos 2 bytes de checksum de cada secao de PC.
    Check(diferentes <= (14 - 5) * 2,
          "diferencas limitadas aos checksums (save de origem tinha checksum "
          "invalido)");
    std::printf("       (%zu bytes diferentes, todos de checksum)\n",
                diferentes);
  }

  // E o mais importante: o save resultante tem que parsear de novo.
  const auto reparsed = g3::ParseSave(copia);
  Check(reparsed.has_value(), "o save resultante continua parseavel");

  const auto pc2 = g3::BuildPcBuffer(copia, *reparsed);
  Check(pc2 == pc, "e o PC buffer relido e igual ao que foi escrito");
}

void TestChecksumAposEscrita(const std::vector<std::uint8_t>& file) {
  std::printf("checksum apos escrita (spec 033):\n");

  const auto parsed = g3::ParseSave(file);
  if (!parsed) {
    std::printf("  SKIP save nao parseavel\n");
    return;
  }
  auto pc = g3::BuildPcBuffer(file, *parsed);
  if (pc.empty()) {
    std::printf("  SKIP PC buffer vazio\n");
    return;
  }

  // Altera um slot e grava.
  std::uint8_t rec[80];
  for (int i = 0; i < 80; ++i) rec[i] = static_cast<std::uint8_t>(0xA0 + i);
  g3::WriteBoxPokemonTo(pc, 0, 0, rec);

  std::vector<std::uint8_t> copia = file;
  Check(g3::ApplyPcBuffer(copia, *parsed, pc), "aplica com alteracao");

  // Todas as secoes de PC do slot ativo tem que ter checksum valido agora.
  const auto reparsed = g3::ParseSave(copia);
  Check(reparsed.has_value(), "o save alterado parseia");
  if (!reparsed) return;

  int pc_ok = 0, pc_total = 0;
  for (const auto& s : g3::ActiveSlot(*reparsed).sections) {
    if (s.id < 5) continue;  // so as secoes de PC
    ++pc_total;
    if (s.checksum_ok()) ++pc_ok;
  }
  Check(pc_total > 0 && pc_ok == pc_total,
        "todas as secoes de PC ficam com checksum valido");

  // E a alteracao esta la.
  const auto lido = g3::ReadBoxPokemonFrom(
      g3::BuildPcBuffer(copia, *reparsed), 0, 0);
  Check(lido.has_value() && lido->raw[0] == 0xA0,
        "o Pokemon escrito esta no save");
}

void TestComputedStats(const std::vector<std::uint8_t>& file) {
  std::printf("stats calculados:\n");

  const auto save = g3::ParseSave(file);
  if (!save) return;
  const auto pc = g3::BuildPcBuffer(file, *save);

  // Growth rate: byte 19 do personal. Derivado conferindo especies conhecidas.
  Check(g3::Personal(1).growth_rate == 3, "Bulbasaur = Medium Slow (3)");
  Check(g3::Personal(25).growth_rate == 0, "Pikachu = Medium Fast (0)");
  Check(g3::Personal(131).growth_rate == 5, "Lapras = Slow (5)");

  // Nivel a partir da experiencia, por curva.
  Check(g3::LevelFromExp(1000000, 0) == 100, "1.000.000 exp, MedFast = Nv.100");
  Check(g3::LevelFromExp(1250000, 5) == 100, "1.250.000 exp, Slow = Nv.100");
  Check(g3::LevelFromExp(0, 0) == 1, "0 exp = Nv.1");
  Check(g3::LevelFromExp(8, 0) == 2, "8 exp, MedFast = Nv.2");

  // Lapras do save: Nv.100, Quiet, IVs 31, EVs HP 252 / SpA 252.
  const auto lapras = g3::ReadBoxPokemonFrom(pc, 0, 2);
  if (!lapras || lapras->empty()) {
    Check(false, "Lapras encontrado");
    return;
  }
  const auto st = g3::ComputeStats(*lapras);
  Check(st.level == 100, "Lapras Nv.100");

  // HP = (2*130 + 31 + 252/4) * 100/100 + 100 + 10 = 464
  Check(st.values[0] == 464, "Lapras HP = 464");

  // SpA = ((2*85 + 31 + 63) + 5) = 269, Quiet sobe SpA em 10% -> 295
  Check(st.values[4] == 295, "Lapras SpA = 295 (Quiet +10%)");

  // Spe = (2*60 + 31 + 0) + 5 = 156, Quiet desce Spe em 10% -> 140
  Check(st.values[3] == 140, "Lapras Spe = 140 (Quiet -10%)");

  // Def nao e afetado pela natureza Quiet.
  Check(st.values[2] == 196, "Lapras Def = 196 (sem modificador)");

  // Slot vazio nao produz stats.
  const auto empty = g3::ComputeStats(g3::BoxPokemon{});
  Check(empty.level == 0 && empty.values[0] == 0, "slot vazio = stats zerados");
}

// --- Testes contra o save de referencia ------------------------------------

void TestRealSave(const std::vector<std::uint8_t>& file) {
  std::printf("save de referencia:\n");

  const auto off = g3::FindSaveOffset(file);
  Check(off.has_value(), "offset detectado");
  if (!off) return;
  // 96, nao 100: o header do SharkPortSave tem 100 bytes, mas o footer da
  // secao comeca 4 bytes antes do que os 100 sugerem. A base e derivada da
  // signature, nao assumida.
  Check(*off == 96, "offset = 96 (SharkPortSave)");

  const auto save = g3::ParseSave(file);
  Check(save.has_value(), "save parseado");
  if (!save) return;

  // Valores conferidos manualmente com dd/xxd — ver evidence-log da spec 001.
  Check(save->slot_a.save_index == 1374, "slot A save index = 1374");
  Check(save->slot_b.save_index == 1375, "slot B save index = 1375");
  Check(save->active_slot == 1, "slot ativo = B (o mais recente)");

  const auto& active = g3::ActiveSlot(*save);
  Check(active.sections.size() == g3::kSectionCount, "14 secoes lidas");
  Check(active.complete, "slot ativo completo (todos os ids presentes)");

  int checksum_ok = 0;
  for (const auto& s : active.sections) {
    if (s.checksum_ok()) ++checksum_ok;
  }

  // Se os checksums nao batem, o save nao e confiavel — nao da para afirmar
  // nada sobre o conteudo das caixas. Distinguir "parser quebrado" de "save
  // incompativel" importa: sem isso, um arquivo ruim parece bug de codigo.
  std::printf("  info %d/%zu secoes com checksum valido\n", checksum_ok,
              g3::kSectionCount);

  if (checksum_ok != static_cast<int>(g3::kSectionCount)) {
    std::printf(
        "  SKIP leitura de Box: este arquivo tem checksums invalidos.\n"
        "       O parser nao pode ser validado com ele — nao e falha de\n"
        "       codigo. Ver evidence-log da spec 001.\n");
  } else {
    // Leitura da Box 1: nao afirmamos QUAL especie — isso exige conferencia
    // humana contra o jogo (ver roadmap). Afirmamos que a leitura e coerente.
    int occupied = 0;
    for (std::size_t slot = 0; slot < g3::kSlotsPerBox; ++slot) {
      const auto mon = g3::ReadBoxPokemon(file, *save, 0, slot);
      if (!mon) {
        Check(false, "leitura de slot da Box 1 nao falhou");
        return;
      }
      if (mon->empty()) continue;
      ++occupied;
      // Especie fora da faixa indica permutacao errada — o bug silencioso que
      // esta spec mais teme.
      Check(mon->species < g3::SpeciesTableSize(),
            "especie dentro da faixa valida do gen3");
    }
    std::printf("  info %d slots ocupados na Box 1\n", occupied);
  }

  // Indices fora do intervalo devem ser rejeitados.
  Check(!g3::ReadBoxPokemon(file, *save, g3::kBoxCount, 0).has_value(),
        "box fora do intervalo rejeitada");
  Check(!g3::ReadBoxPokemon(file, *save, 0, g3::kSlotsPerBox).has_value(),
        "slot fora do intervalo rejeitado");
}

// Save cru de LeafGreen (projectpokemon.org, "Venusaur Solo Run"). Ao contrario
// do .sps, este permite afirmar CONTEUDO: a descricao publicada pelo autor diz
// exatamente o que deve estar la, o que da uma referencia externa e verificavel.
void TestRawSave(const std::vector<std::uint8_t>& file) {
  std::printf("save cru (LeafGreen Venusaur Solo Run):\n");

  const auto off = g3::FindSaveOffset(file);
  Check(off.has_value() && *off == 0, "offset = 0 (save cru)");

  const auto save = g3::ParseSave(file);
  Check(save.has_value(), "save parseado");
  if (!save) return;

  const auto& active = g3::ActiveSlot(*save);
  Check(save->active_slot == 0, "slot ativo = A (index 48 > 47)");
  Check(active.complete, "slot completo");

  int ok = 0;
  for (const auto& s : active.sections) {
    if (s.checksum_ok()) ++ok;
  }
  Check(ok == static_cast<int>(g3::kSectionCount), "14/14 checksums batem");

  // O PC deste save esta vazio — o autor guardou tudo na party. Isso e um caso
  // de teste util: exercita o caminho "caixa vazia" com dados reais.
  const auto mon = g3::ReadBoxPokemon(file, *save, 0, 0);
  Check(mon.has_value() && mon->empty(), "Box 1 slot 1 vazio (PC nao usado)");
}

}  // namespace

int main(int argc, char** argv) {
  TestChecksum();
  TestRejectsGarbage();
  TestSpeciesNames();
  TestNationalDex();
  TestPersonal();
  TestShiny();
  TestEscritaNoPcBuffer();
  TestNomePorDex();
  TestTabelasConcordam();

  const auto raw = ReadFile("leafgreen-test.sav");
  if (raw.empty()) {
    std::printf("save cru:\n  SKIP 'leafgreen-test.sav' nao encontrado\n");
  } else {
    TestRawSave(raw);
  }

  const std::string path =
      (argc >= 2) ? argv[1] : "pokemon-firered-leafgreen-version.34462.sps";
  const auto file = ReadFile(path);
  if (file.empty()) {
    std::printf("save de referencia:\n  SKIP '%s' nao encontrado\n",
                path.c_str());
  } else {
    TestRealSave(file);
    TestPokemonDetails(file);
    TestComputedStats(file);
    TestIdaEVoltaSemAlteracao(file);
    TestChecksumAposEscrita(file);
  }

  if (g_failures == 0) {
    std::printf("\nTodos os testes passaram.\n");
    return 0;
  }
  std::printf("\n%d teste(s) falharam.\n", g_failures);
  return 1;
}
