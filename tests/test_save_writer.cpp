// Testes do save writer (spec 068, G02+G03).
//
// O teste central e o G03, o PORTAO DE SEGURANCA do epico de escrita:
//
//     Save(Load(x)) == x   byte a byte, sem alterar nada
//
// Se regravar sem mudar nada ja muda bytes, qualquer transferencia futura
// estaria corrompendo dado em silencio.
//
// GUARDRAIL: os saves do simulador sao do dono e sao SOMENTE LEITURA. Todo
// arquivo aqui e aberto para leitura; a unica escrita acontece dentro de um
// SaveSandbox (spec 064), num diretorio temporario que se apaga sozinho.
//
// A PARTY FICA FORA (decisao do dono): so as caixas sao lidas e escritas. O
// save de PLA tem 0 nas caixas e 1 na party — para nos e um save VAZIO, e a
// contagem 0 abaixo e o comportamento CORRETO.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "save_sandbox.h"
#include "save_writer.h"

namespace fs = std::filesystem;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  } else {
    std::printf("  ok: %s\n", what.c_str());
  }
}

static std::vector<std::uint8_t> ReadFile(const std::string& path) {
  // std::filesystem::path: ifstream com std::string nao abre nome com
  // caractere especial no Windows.
  std::ifstream f(fs::path(path), std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

static bool WriteFile(const std::string& path,
                      const std::vector<std::uint8_t>& data) {
  std::ofstream f(fs::path(path), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(data.data()),
          static_cast<std::streamsize>(data.size()));
  return bool(f);
}

// Onde os dois arquivos divergem — um numero solto nao ajuda a depurar.
static std::string FirstDiff(const std::vector<std::uint8_t>& a,
                             const std::vector<std::uint8_t>& b) {
  if (a.size() != b.size())
    return "tamanhos diferentes: " + std::to_string(a.size()) + " vs " +
           std::to_string(b.size());
  std::size_t n = 0, first = 0;
  bool got = false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) {
      if (!got) { first = i; got = true; }
      ++n;
    }
  }
  if (!got) return "identicos";
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%zu bytes diferentes, o primeiro em 0x%zX",
                n, first);
  return buf;
}

struct Caso {
  const char* nome;
  const char* rel;
  savew::Game jogo;
  std::size_t esperado;  // contagem do tools/pkhex-verify (descoberta)
};

// ---------------------------------------------------------------------------
// G03: o portao. Roundtrip byte-identico sobre a COPIA do save real.
// ---------------------------------------------------------------------------
static void TestRoundtrip(const Caso& c) {
  const std::string original = std::string(SIM_SAVES) + c.rel;
  std::printf("[%s]\n", c.nome);

  auto sb = sandbox::SaveSandbox::Create(original);
  if (!sb) {
    std::printf("FALHOU: %s: sandbox nao criado (save ausente?)\n", c.nome);
    ++g_failures;
    return;
  }

  const std::vector<std::uint8_t> file = ReadFile(sb->path());
  if (file.empty()) {
    std::printf("FALHOU: %s: copia vazia\n", c.nome);
    ++g_failures;
    return;
  }

  // Reconhecimento sem dica: Load(file) tem de achar o jogo sozinho.
  auto autodetect = savew::Load(file);
  Check(autodetect && autodetect->game == c.jogo,
        std::string(c.nome) + ": Load reconhece o jogo sem dica");

  auto sd = savew::Load(file, c.jogo);
  if (!sd) {
    std::printf("FALHOU: %s: Load falhou\n", c.nome);
    ++g_failures;
    return;
  }

  // Contagem contra o oraculo externo (tools/pkhex-verify).
  Check(sd->Count() == c.esperado,
        std::string(c.nome) + ": " + std::to_string(sd->Count()) +
            " Pokemon nas caixas (PkHeX: " + std::to_string(c.esperado) + ")");

  // O PORTAO: regravar sem alterar nada devolve o arquivo identico.
  const std::vector<std::uint8_t> again = savew::Save(*sd);
  Check(again == file, std::string(c.nome) +
                           ": G03 roundtrip byte-identico — " +
                           FirstDiff(file, again));

  // E o arquivo regravado ainda abre e conta o mesmo.
  auto reload = savew::Load(again, c.jogo);
  Check(reload && reload->Count() == c.esperado,
        std::string(c.nome) + ": o arquivo regravado reabre com a mesma contagem");
}

// ---------------------------------------------------------------------------
// Escrita real: altera UM Pokemon e prova que so ele mudou.
// O veredito externo do PkHeX roda no script da spec; aqui provamos a parte
// que o C++ consegue provar sozinho.
// ---------------------------------------------------------------------------
static void TestEscritaReal(const Caso& c) {
  const std::string original = std::string(SIM_SAVES) + c.rel;
  auto sb = sandbox::SaveSandbox::Create(original);
  if (!sb) return;
  const std::vector<std::uint8_t> file = ReadFile(sb->path());
  auto sd = savew::Load(file, c.jogo);
  if (!sd) return;

  // Acha o primeiro slot ocupado. Se o save esta vazio (PLA), nao ha o que
  // alterar — e isso e correto, nao falha.
  std::size_t bi = 0, si = 0;
  bool achou = false;
  for (std::size_t b = 0; b < sd->box_count && !achou; ++b)
    for (std::size_t s = 0; s < sd->slots_per_box && !achou; ++s)
      if (sd->At(b, s).present) { bi = b; si = s; achou = true; }
  if (!achou) {
    std::printf("  N/A: %s: caixas vazias, nada a alterar (esperado no PLA)\n",
                c.nome);
    return;
  }

  pkm::Pokemon mon = sd->At(bi, si).mon;
  const std::uint16_t item_antigo = mon.held_item;
  const std::uint16_t item_novo = std::uint16_t(item_antigo == 1 ? 2 : 1);
  mon.held_item = item_novo;
  Check(sd->Set(bi, si, mon), std::string(c.nome) + ": Set no slot ocupado");

  const std::vector<std::uint8_t> escrito = savew::Save(*sd);
  Check(escrito != file, std::string(c.nome) + ": o arquivo mudou de fato");
  Check(escrito.size() == file.size(),
        std::string(c.nome) + ": o tamanho do arquivo nao mudou");

  // A escrita vai para a COPIA — nunca para o original.
  Check(WriteFile(sb->path(), escrito),
        std::string(c.nome) + ": gravado na copia do sandbox");

  auto depois = savew::Load(ReadFile(sb->path()), c.jogo);
  if (!depois) {
    std::printf("FALHOU: %s: o save alterado nao reabre\n", c.nome);
    ++g_failures;
    return;
  }
  Check(depois->Count() == c.esperado,
        std::string(c.nome) + ": contagem inalterada apos a escrita (" +
            std::to_string(depois->Count()) + ")");
  Check(depois->At(bi, si).mon.held_item == item_novo,
        std::string(c.nome) + ": o campo alterado tem o valor novo");

  // NENHUM outro Pokemon mudou: PID e especie de todos os demais batem.
  std::size_t divergentes = 0;
  for (std::size_t b = 0; b < sd->box_count; ++b) {
    for (std::size_t s = 0; s < sd->slots_per_box; ++s) {
      if (b == bi && s == si) continue;
      const auto& a = sd->At(b, s);
      const auto& d = depois->At(b, s);
      if (a.present != d.present) { ++divergentes; continue; }
      if (!a.present) continue;
      if (a.mon.pid != d.mon.pid || a.mon.species != d.mon.species ||
          a.mon.held_item != d.mon.held_item || a.mon.nickname != d.mon.nickname)
        ++divergentes;
    }
  }
  Check(divergentes == 0,
        std::string(c.nome) + ": nenhum outro Pokemon mudou (" +
            std::to_string(divergentes) + " divergentes)");
}

// ---------------------------------------------------------------------------
// Violacao plantada: o recalculo de integridade e o que sustenta a escrita.
//
// A prova precisa atacar a INTEGRIDADE, e nao um byte qualquer. Cada familia
// guarda a integridade num lugar diferente, entao o teste ataca o lugar certo
// de cada uma:
//
//  - SwSh/SV/PLA: o SHA256 dos 32 bytes finais (SwishCrypto). Nosso proprio
//    Load recusa o arquivo — a checagem e local.
//  - BDSP (MD5 em 0xe9818) e LGPE (dois CRC16): a integridade fica FORA da
//    regiao das caixas, entao o nosso leitor continua lendo os Pokemon
//    normalmente. Quem acusa e o PkHeX. Aqui provamos o que da para provar em
//    C++ — que o valor gravado pelo Save() e o recalculado, e que mexer nele
//    produz um arquivo cujo campo de integridade NAO bate mais.
// ---------------------------------------------------------------------------
static void TestIntegridadePlantada(const Caso& c) {
  const std::string original = std::string(SIM_SAVES) + c.rel;
  auto sb = sandbox::SaveSandbox::Create(original);
  if (!sb) return;
  const std::vector<std::uint8_t> file = ReadFile(sb->path());
  auto sd = savew::Load(file, c.jogo);
  if (!sd) return;

  std::vector<std::uint8_t> gravado = savew::Save(*sd);
  if (gravado.empty()) return;

  if (c.jogo == savew::Game::kSwSh || c.jogo == savew::Game::kSV ||
      c.jogo == savew::Game::kPLA || c.jogo == savew::Game::kZA) {
    // O hash de integridade sao os 32 bytes finais do arquivo.
    std::vector<std::uint8_t> quebrado = gravado;
    quebrado[quebrado.size() - 1] ^= 0xFF;
    Check(!savew::Load(quebrado, c.jogo).has_value(),
          std::string(c.nome) +
              ": hash SHA256 adulterado faz o Load RECUSAR o arquivo");
    return;
  }

  if (c.jogo == savew::Game::kBDSP) {
    // O Save() gravou o MD5 correto? Recalcula do jeito que o formato manda.
    constexpr std::size_t kHash = 0xe9818;
    std::vector<std::uint8_t> zerado = gravado;
    std::fill(zerado.begin() + kHash, zerado.begin() + kHash + 16, 0);
    std::uint8_t esperado[16];
    savew::Md5(zerado.data(), zerado.size(), esperado);
    Check(std::equal(esperado, esperado + 16, gravado.begin() + kHash),
          "BDSP: o MD5 gravado pelo Save() e o recalculado do arquivo");

    // Violacao: um byte de caixa mexido SEM recalcular o MD5 deixa o hash
    // gravado divergente do conteudo — que e exatamente o que o PkHeX acusa.
    std::vector<std::uint8_t> sem_recalculo = gravado;
    sem_recalculo[0x14ef4 + 100] ^= 0xFF;
    std::vector<std::uint8_t> z2 = sem_recalculo;
    std::fill(z2.begin() + kHash, z2.begin() + kHash + 16, 0);
    std::uint8_t agora[16];
    savew::Md5(z2.data(), z2.size(), agora);
    Check(!std::equal(agora, agora + 16, sem_recalculo.begin() + kHash),
          "BDSP: byte de caixa mexido sem recalcular DIVERGE do MD5 gravado");
    return;
  }

  // LGPE: os dois CRC16.
  constexpr std::size_t kBox = 0x5c00, kPcSize = 0x3f7a0, kPcChk = 0xb8662;
  const std::uint16_t gravado_crc = std::uint16_t(gravado[kPcChk] |
                                                  (gravado[kPcChk + 1] << 8));
  Check(gravado_crc == savew::Crc16Arc(gravado.data() + kBox, kPcSize),
        "LGPE: o CRC16 gravado pelo Save() e o recalculado da regiao PC");

  std::vector<std::uint8_t> sem_recalculo = gravado;
  sem_recalculo[kBox + 100] ^= 0xFF;
  Check(gravado_crc != savew::Crc16Arc(sem_recalculo.data() + kBox, kPcSize),
        "LGPE: byte de caixa mexido sem recalcular DIVERGE do CRC16 gravado");
}

// ---------------------------------------------------------------------------
// Os 21 blocos do LGPE (spec 073).
//
// Tres provas, nesta ordem:
//  1. Os 21 CRCs recalculados do save REAL batem com os gravados nele. E o
//     oraculo externo: os valores vieram do jogo, nao do nosso codigo.
//  2. O Save() grava os 21 corretos.
//  3. Mexer num bloco que ANTES nao era recalculado (bloco 2, MyStatus — os
//     dados do treinador) faz o CRC daquele bloco ser ATUALIZADO. Este e o
//     teste que prova que a mudanca serve para alguma coisa: antes da 073 ele
//     ficaria vermelho.
// ---------------------------------------------------------------------------
static std::uint16_t LeU16(const std::vector<std::uint8_t>& f, std::size_t at) {
  return std::uint16_t(f[at] | (f[at + 1] << 8));
}

static void TestBlocosLgpe(const Caso& c) {
  const std::string original = std::string(SIM_SAVES) + c.rel;
  auto sb = sandbox::SaveSandbox::Create(original);
  if (!sb) {
    std::printf("FALHOU: LGPE: sandbox nao criado (save ausente?)\n");
    ++g_failures;
    return;
  }
  const std::vector<std::uint8_t> file = ReadFile(sb->path());
  auto sd = savew::Load(file, c.jogo);
  if (!sd) {
    std::printf("FALHOU: LGPE: Load falhou\n");
    ++g_failures;
    return;
  }

  Check(savew::kLgpeBlockCount == 21, "LGPE: a tabela tem os 21 blocos");

  // 1. O SAVE REAL: os 21 CRCs gravados pelo JOGO batem com os recalculados.
  //    (docs/pesquisa-blocos-lgpe.md conferiu 21/21 por script; aqui vira gate.)
  std::size_t batem = 0;
  for (std::size_t id = 0; id < savew::kLgpeBlockCount; ++id) {
    const savew::LgpeBlock& b = savew::kLgpeBlocks[id];
    const std::size_t chk = savew::LgpeChecksumOffset(id);
    if (b.offset + b.size > file.size() || chk + 2 > file.size()) continue;
    const std::uint16_t gravado = LeU16(file, chk);
    const std::uint16_t calc = savew::Crc16Arc(file.data() + b.offset, b.size);
    if (gravado == calc) {
      ++batem;
    } else {
      std::printf("    bloco %2zu @0x%06zX len=0x%05zX chk@0x%zX grav=0x%04X"
                  " calc=0x%04X DIVERGE\n",
                  id, b.offset, b.size, chk, gravado, calc);
    }
  }
  Check(batem == savew::kLgpeBlockCount,
        "LGPE: os 21 CRCs do save REAL batem com os recalculados (" +
            std::to_string(batem) + "/21)");

  // 2. O Save() grava os 21 corretos.
  const std::vector<std::uint8_t> gravado = savew::Save(*sd);
  if (gravado.size() != file.size()) {
    std::printf("FALHOU: LGPE: Save() devolveu tamanho errado\n");
    ++g_failures;
    return;
  }
  std::size_t ok_save = 0;
  for (std::size_t id = 0; id < savew::kLgpeBlockCount; ++id) {
    const savew::LgpeBlock& b = savew::kLgpeBlocks[id];
    if (LeU16(gravado, savew::LgpeChecksumOffset(id)) ==
        savew::Crc16Arc(gravado.data() + b.offset, b.size))
      ++ok_save;
  }
  Check(ok_save == savew::kLgpeBlockCount,
        "LGPE: o Save() grava os 21 CRCs corretos (" +
            std::to_string(ok_save) + "/21)");

  // 3. A PROVA DA SPEC 073: um bloco que ANTES nao era recalculado.
  //    O bloco 2 (MyStatus, dados do treinador) nunca foi tocado pelo Save()
  //    antigo, que so conhecia bag(0), header(8) e PC(9). Alteramos um byte
  //    dele no arquivo de ENTRADA e exigimos que o Save() atualize o CRC.
  constexpr std::size_t kMyStatus = 2;
  const savew::LgpeBlock& ms = savew::kLgpeBlocks[kMyStatus];
  const std::size_t ms_chk = savew::LgpeChecksumOffset(kMyStatus);

  savew::SaveData mexido = *sd;
  // Um byte no meio do bloco do treinador. NAO passa por Set() — e exatamente
  // o caso "uma spec futura escreveu aqui" que a 073 existe para cobrir.
  mexido.file[ms.offset + 0x40] ^= 0xFF;

  const std::uint16_t crc_antigo = LeU16(file, ms_chk);
  const std::uint16_t crc_esperado =
      savew::Crc16Arc(mexido.file.data() + ms.offset, ms.size);
  Check(crc_esperado != crc_antigo,
        "LGPE: o byte plantado no bloco 2 de fato muda o CRC dele (" +
            std::to_string(crc_antigo) + " -> " +
            std::to_string(crc_esperado) + ")");

  const std::vector<std::uint8_t> depois = savew::Save(mexido);
  if (depois.size() != file.size()) {
    std::printf("FALHOU: LGPE: Save() do save mexido devolveu tamanho errado\n");
    ++g_failures;
    return;
  }
  const std::uint16_t crc_gravado = LeU16(depois, ms_chk);
  Check(crc_gravado == crc_esperado,
        "LGPE: o Save() ATUALIZOU o CRC do bloco 2 (treinador), que antes da "
        "spec 073 nao era recalculado");
  Check(crc_gravado != crc_antigo,
        "LGPE: e o CRC do bloco 2 nao ficou com o valor antigo");

  // Nenhum outro bloco foi arrastado junto: so o 2 mudou de checksum.
  std::size_t outros_mudaram = 0;
  for (std::size_t id = 0; id < savew::kLgpeBlockCount; ++id) {
    if (id == kMyStatus) continue;
    const std::size_t chk = savew::LgpeChecksumOffset(id);
    if (LeU16(depois, chk) != LeU16(file, chk)) ++outros_mudaram;
  }
  Check(outros_mudaram == 0,
        "LGPE: nenhum outro CRC mudou (" + std::to_string(outros_mudaram) + ")");
}

// ---------------------------------------------------------------------------
// Spec 080 — LEGALIDADE ABSOLUTA (so o Z-A permite).
//
// Os outros cinco saves sao de terceiros e tem Pokemon editados: neles a linha
// de base e 0% de legalidade, e o unico criterio possivel e o DELTA. O save de
// Z-A e do DONO e e 96/96 legal pelo PkHeX, entao aqui da para exigir o
// criterio forte: depois de ESCREVER, os 96 continuam legais.
//
// O C++ nao julga legalidade — quem julga e o PkHeX. Este teste faz a escrita
// e DEIXA o arquivo alterado num caminho conhecido, para o tools/pkhex-verify
// dar o veredito no evidence-log. O sandbox se apaga sozinho no destrutor,
// entao a copia para o juiz vai para fora dele, de proposito.
// ---------------------------------------------------------------------------
static void TestEscritaParaOJuiz(const Caso& c, const std::string& destino) {
  const std::string original = std::string(SIM_SAVES) + c.rel;
  auto sb = sandbox::SaveSandbox::Create(original);
  if (!sb) {
    std::printf("FALHOU: %s: sandbox nao criado\n", c.nome);
    ++g_failures;
    return;
  }
  const std::vector<std::uint8_t> file = ReadFile(sb->path());
  auto sd = savew::Load(file, c.jogo);
  if (!sd) {
    std::printf("FALHOU: %s: Load falhou\n", c.nome);
    ++g_failures;
    return;
  }

  // Primeiro slot ocupado. Alteramos o HELD ITEM para uma Poke Ball (item 4),
  // que e um item legal de se segurar — trocar por um item invalido derrubaria
  // a legalidade por motivo NOSSO, e o teste deixaria de medir o writer.
  std::size_t bi = 0, si = 0;
  bool achou = false;
  for (std::size_t b = 0; b < sd->box_count && !achou; ++b)
    for (std::size_t s = 0; s < sd->slots_per_box && !achou; ++s)
      if (sd->At(b, s).present) { bi = b; si = s; achou = true; }
  if (!achou) {
    std::printf("FALHOU: %s: nenhum slot ocupado\n", c.nome);
    ++g_failures;
    return;
  }

  pkm::Pokemon mon = sd->At(bi, si).mon;
  const std::uint16_t antes = mon.held_item;
  mon.held_item = 4;  // Poke Ball
  Check(sd->Set(bi, si, mon), std::string(c.nome) + ": Set (held_item -> 4)");

  const std::vector<std::uint8_t> escrito = savew::Save(*sd);
  Check(!escrito.empty() && escrito.size() == file.size(),
        std::string(c.nome) + ": Save() devolveu arquivo do mesmo tamanho");
  Check(WriteFile(destino, escrito),
        std::string(c.nome) + ": arquivo alterado gravado em " + destino);
  std::printf("    [juiz] box=%zu slot=%zu held_item %u -> 4; rode:\n"
              "    cd tools/pkhex-verify && dotnet run -- \"%s\"\n",
              bi + 1, si + 1, unsigned(antes), destino.c_str());
}

// Vetores conhecidos: se MD5/CRC16 estiverem errados, o save de BDSP e o de
// LGPE sairiam com integridade invalida e so o PkHeX acusaria.
static void TestVetores() {
  std::uint8_t d[16];
  savew::Md5(reinterpret_cast<const std::uint8_t*>("abc"), 3, d);
  static const std::uint8_t kMd5Abc[16] = {0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2,
                                           0x4f, 0xb0, 0xd6, 0x96, 0x3f, 0x7d,
                                           0x28, 0xe1, 0x7f, 0x72};
  bool ok = true;
  for (int i = 0; i < 16; ++i) ok = ok && d[i] == kMd5Abc[i];
  Check(ok, "MD5(\"abc\") bate com o vetor conhecido");

  // CRC-16/ARC("123456789") == 0xBB3D (vetor de catalogo).
  Check(savew::Crc16Arc(
            reinterpret_cast<const std::uint8_t*>("123456789"), 9) == 0xBB3D,
        "CRC-16/ARC(\"123456789\") == 0xBB3D");
}

int main() {
  // Contagens do tools/pkhex-verify, medidas antes de qualquer escrita
  // (descoberta escrita-e-transferencia, "Linha de base de legalidade").
  const Caso casos[] = {
      {"Shield", "01008DB008C2C000/Amaral/main", savew::Game::kSwSh, 194},
      {"Scarlet", "0100A3D008C5C000/Amaral/main", savew::Game::kSV, 440},
      {"Legends Arceus", "01001F5010DFA000/Amaral/main", savew::Game::kPLA, 0},
      {"BDSP", "0100000011D90000/Amaral/SaveData.bin", savew::Game::kBDSP, 255},
      {"LGPE", "0100187003A36000/Amaral/savedata.bin", savew::Game::kLGPE, 92},
      // Spec 080. O UNICO save legitimo do projeto: 96 Pokemon, 96/96 LEGAIS
      // pelo PkHeX, OT=Amaral (o proprio dono). E por isso o save de
      // referencia do criterio de legalidade ABSOLUTA — nos outros cinco, com
      // Pokemon editados de terceiros, so da para exigir o criterio DELTA.
      {"Legends Z-A", "0100F43008C44000/Amaral/main", savew::Game::kZA, 96},
  };

  TestVetores();

  std::printf("\n=== G03: roundtrip byte-identico ===\n");
  for (const Caso& c : casos) TestRoundtrip(c);

  std::printf("\n=== Escrita real (um Pokemon) ===\n");
  for (const Caso& c : casos) TestEscritaReal(c);

  std::printf("\n=== Violacao plantada: integridade ===\n");
  for (const Caso& c : casos) TestIntegridadePlantada(c);

  std::printf("\n=== LGPE: os 21 blocos (spec 073) ===\n");
  for (const Caso& c : casos)
    if (c.jogo == savew::Game::kLGPE) TestBlocosLgpe(c);

  // Spec 080: deixa o Z-A alterado para o juiz externo (PkHeX) julgar a
  // LEGALIDADE ABSOLUTA. O caminho e fixo para o evidence-log poder cita-lo.
  std::printf("\n=== Z-A: escrita para o juiz externo (spec 080) ===\n");
  {
    const std::string destino =
        (fs::temp_directory_path() / "nestbox-za-escrito.bin").string();
    for (const Caso& c : casos)
      if (c.jogo == savew::Game::kZA) TestEscritaParaOJuiz(c, destino);
  }

  if (g_failures) {
    std::printf("\n%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("\nsave_writer: tudo verde\n");
  return 0;
}
