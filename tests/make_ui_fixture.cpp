// Gera o save gen3 sintetico que o roteiro do controle remoto usa (spec 134).
//
// Por que sintetico, e nao o save do dono: o roteiro MOVE e GRAVA. Apontar o
// ctest para um save real seria escrever num arquivo protegido pela baseline
// (ver o guardrail do CLAUDE.md) e, pior, deixaria o teste dependente de o
// dono ter aquele save com aquele Pokemon naquele slot. A fixture nasce aqui,
// com conteudo conhecido, e o roteiro pode afirmar o nome e o nivel.
//
// Uso: make_ui_fixture <saida.sav> [base.sav]
//
// SEM `base.sav` o molde e construido do zero aqui: 14 secoes por slot, com
// id, signature e checksum como o formato exige. E o caminho normal, e o que
// permite ao ctest rodar num clone limpo — nenhum save entra no repositorio
// (o `.gitignore` barra `*.sav`, e essa regra existe por um bom motivo).
//
// COM `base.sav`, um save gen3 valido serve de molde e so as caixas sao
// reescritas. Serve para a fixture ficar o mais parecida possivel com um save
// de verdade. O molde e lido, NUNCA escrito.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "gen3_save.h"

namespace g3 = pokehome::gen3;

namespace {

std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

void Put16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFF);
  p[1] = static_cast<std::uint8_t>(v >> 8);
}

void Put32(std::uint8_t* p, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}

// Um save gen3 vazio, porem VALIDO: 2 slots de 14 secoes, cada uma com id,
// signature e checksum coerentes. E o minimo que o ParseSave aceita, e e o
// que evita a fixture — e portanto o ctest — depender de um save real.
std::vector<std::uint8_t> BuildEmptySave() {
  // 131072 e nao kSaveSize (114688): o formato usa 114688, mas um cartucho de
  // GBA tem 128K e e ESSE o tamanho que a varredura exige (`IsGbaSave` em
  // switch_sim.h). Um save do tamanho exato do formato e descartado antes de
  // chegar ao parser, e a fixture nunca apareceria na lista.
  std::vector<std::uint8_t> file(131072, 0);
  for (std::size_t slot = 0; slot < 2; ++slot) {
    for (std::size_t sec = 0; sec < g3::kSectionCount; ++sec) {
      std::uint8_t* p =
          file.data() + slot * g3::kSlotSize + sec * g3::kSectionSize;
      Put16(p + g3::kOffSectionId, static_cast<std::uint16_t>(sec));
      // Secao 0, offset 0xAC: o campo que o DetectGame le. 1 = FireRed/
      // LeafGreen — o AppId sob o qual o teste instala a fixture no
      // simulador. Deixado em 0 o save vira Ruby/Sapphire e a varredura o
      // descarta como incompativel com o jogo daquela pasta.
      if (sec == 0) Put32(p + 0xAC, 1u);
      Put32(p + g3::kOffSignature, g3::kSignature);
      // O slot A precisa ganhar do B para ser o ativo: indice maior vence.
      Put32(p + g3::kOffSaveIndex, slot == 0 ? 1u : 0u);
      // Checksum por ultimo: ele cobre os bytes uteis da secao.
      Put16(p + g3::kOffChecksum, g3::ComputeChecksum(p, g3::kSectionData));
    }
  }
  return file;
}

// Um Pokemon plantado, com tudo que o parser da tela le. Os valores sao
// arbitrarios mas FIXOS: o roteiro afirma species e level, entao mudar algo
// aqui e quebrar o teste de proposito.
g3::FullRecord MakeMon(std::uint16_t species_internal, std::uint32_t exp,
                       const std::string& nickname) {
  g3::FullRecord r;
  // O personality decide a ordem das substruturas e a natureza; um valor fixo
  // mantem a fixture reproduzivel byte a byte.
  r.personality = 0x1234ABCDu;
  r.ot_id = 0x00010001u;
  r.language = 2;  // ingles
  // 0x02 = has_species. Sem este bit o slot conta como vazio.
  r.flags = 0x02;
  g3::EncodeGen3String(nickname, r.nickname_raw, sizeof(r.nickname_raw));
  g3::EncodeGen3String("NESTBOX", r.ot_name_raw, sizeof(r.ot_name_raw));

  r.species = species_internal;
  r.experience = exp;
  r.friendship = 70;
  r.moves[0] = 33;  // Tackle
  r.pp[0] = 35;
  r.met_location = 1;
  // met_level 5 | origem FireRed (4) << 7 | pokeball (4) << 11
  r.origins = 5 | (4u << 7) | (4u << 11);
  // IVs medianos: 15 em cada um dos 6 campos de 5 bits.
  r.iv32 = 15u | (15u << 5) | (15u << 10) | (15u << 15) | (15u << 20) |
           (15u << 25);
  return r;
}

}  // namespace

// Save de BDSP VAZIO e sintetico (spec 140), para o roteiro ter um DESTINO
// MODERNO onde as regras de transferencia tenham o que dizer.
//
// Por que BDSP, e nao SwSh/SV/PLA: os tres de SCBlock exigem a estrutura
// cifrada inteira, com hash que so o PkHeX (ou um save real) produz. O BDSP e
// Unity com OFFSET FIXO, e o `savew::Load` o reconhece pelo TAMANHO EXATO
// (979108) — um arquivo zerado desse tamanho ja abre como save valido de 40
// caixas vazias. Medido pela sonda da spec 140, nao suposto.
//
// Por que sintetico e nao o save real do dono: ele e protegido pela baseline
// (`scripts/saves-baseline.py`) e o roteiro ESCREVE. Ver o guardrail do
// CLAUDE.md.
int WriteBdspFixture(const char* out_path) {
  // 979108 = kBdspSize. Zerado: 40x30 slots vazios, e o `Save()` do writer
  // recalcula o MD5 quando o app gravar.
  std::vector<std::uint8_t> file(979108, 0);
  std::ofstream out(out_path, std::ios::binary);
  if (!out) {
    std::fprintf(stderr, "nao foi possivel escrever %s\n", out_path);
    return 1;
  }
  out.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
  if (!out) {
    std::fprintf(stderr, "escrita incompleta em %s\n", out_path);
    return 1;
  }
  std::printf("fixture BDSP gerada: %s (%zu bytes, 40 caixas vazias)\n",
              out_path, file.size());
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "uso: make_ui_fixture <saida.sav> [base.sav]\n"
                 "     make_ui_fixture --bdsp <SaveData.bin>\n");
    return 2;
  }
  // Modo BDSP (spec 140): o destino moderno do roteiro do portao unico.
  if (std::strcmp(argv[1], "--bdsp") == 0) {
    if (argc < 3) {
      std::fprintf(stderr, "uso: make_ui_fixture --bdsp <SaveData.bin>\n");
      return 2;
    }
    return WriteBdspFixture(argv[2]);
  }
  const char* out_path = argv[1];
  const char* base_path = (argc >= 3) ? argv[2] : nullptr;

  std::vector<std::uint8_t> file;
  if (base_path) {
    file = ReadFile(base_path);
    // >= e nao ==: um .sav de emulador costuma ter 128K, com padding depois
    // dos 114688 bytes que o formato usa. O parser le so o inicio, e o padding
    // e preservado na saida.
    if (file.size() < g3::kSaveSize) {
      std::fprintf(stderr,
                   "base invalida: %s tem %zu bytes, esperado ao menos %zu\n",
                   base_path, file.size(), g3::kSaveSize);
      return 1;
    }
  } else {
    file = BuildEmptySave();
  }

  const auto save = g3::ParseSave(file);
  if (!save) {
    std::fprintf(stderr, "base invalida: parser recusou %s\n",
                 base_path ? base_path : "(save construido do zero)");
    return 1;
  }

  std::vector<std::uint8_t> pc = g3::BuildPcBuffer(file, *save);
  if (pc.empty()) {
    std::fprintf(stderr, "base invalida: PC buffer vazio\n");
    return 1;
  }

  // Zera TODAS as caixas: a fixture nao herda o conteudo do molde, para o
  // roteiro afirmar contagem sem depender do save de origem.
  for (std::size_t b = 0; b < g3::kBoxCount; ++b) {
    for (std::size_t s = 0; s < g3::kSlotsPerBox; ++s) {
      g3::WriteBoxPokemonTo(pc, b, s, nullptr);
    }
  }

  // Caixa 0, slot 0: Bulbasaur nivel 5. E o que o roteiro move.
  //
  // 1 = indice INTERNO de Bulbasaur no gen3. 135 de exp = exatamente o nivel
  // 5 na curva MEDIUM_SLOW da especie (6/5 n^3 - 15 n^2 + 100 n - 140: o
  // nivel 5 comeca em 135, o 6 em 179). O roteiro afirma `slot_level 5`,
  // entao baixar este numero quebra o teste de proposito.
  std::uint8_t rec[80] = {};
  g3::EncodeFullRecord(MakeMon(1, 135, "BULBASAUR"), rec);
  if (!g3::WriteBoxPokemonTo(pc, 0, 0, rec)) {
    std::fprintf(stderr, "falha ao escrever o slot 0\n");
    return 1;
  }

  // Caixa 0, slot 1: Charmander nivel 5, para o roteiro do modo TROCAR
  // (spec 133) ter dois Pokemon distintos e poder afirmar que trocaram de
  // lugar — com um so, "trocou" e "nao fez nada" ficam indistinguiveis.
  // 4 = indice INTERNO de Charmander no gen3; mesma curva MEDIUM_SLOW.
  std::uint8_t rec2[80] = {};
  g3::EncodeFullRecord(MakeMon(4, 135, "CHARMANDER"), rec2);
  if (!g3::WriteBoxPokemonTo(pc, 0, 1, rec2)) {
    std::fprintf(stderr, "falha ao escrever o slot 1\n");
    return 1;
  }

  // Caixa 0, slot 2: Squirtle REPROVADO pelo verificador, com os EVs
  // estourados (6 x 252 = 1512, contra o teto de 510). Existe para o roteiro
  // do TD-01 da spec 133 provar que ARRASTAR nao fura o bloqueio que o A
  // respeita — sem um Pokemon ilegal, essa trava nao teria como ser exercida.
  g3::FullRecord bad = MakeMon(7, 135, "SQUIRTLE");
  for (std::uint8_t& ev : bad.evs) ev = 252;
  std::uint8_t rec3[80] = {};
  g3::EncodeFullRecord(bad, rec3);
  if (!g3::WriteBoxPokemonTo(pc, 0, 2, rec3)) {
    std::fprintf(stderr, "falha ao escrever o slot 2\n");
    return 1;
  }

  // Caixa 0, slot 4: NINCADA — o caso da spec 140.
  //
  // Ele existe para provar, NA TELA, que as regras de transferencia do core
  // chegaram ao gesto. E o unico Pokemon da fixture em que os dois portoes
  // DISCORDAM sobre um destino BDSP (medido pela sonda da spec 140):
  //
  //   portao de FORMATO (ConvertUp -> PB8)  : PASSA
  //   regra (rules::CanTransfer -> kBdsp)   : BLOQUEIA
  //     "Nincada so entra no BDSP se tiver vindo de la"
  //
  // Ate a spec 140 a tela so rodava o primeiro, entao ele entrava. Bulbasaur
  // (slot 0) e o CONTRA-CASO no mesmo destino: passa nos dois: uma regra que
  // bloqueia tudo nao prova nada.
  //
  // 301 = indice INTERNO de Nincada no gen3 (dex 290), medido pela sonda —
  // nao chutado. O `origins` do MakeMon marca origem FireRed (4), que e
  // justamente o "nao veio do BDSP" que a regra exige.
  g3::FullRecord nincada = MakeMon(301, 135, "NINCADA");
  std::uint8_t rec5[80] = {};
  g3::EncodeFullRecord(nincada, rec5);
  if (!g3::WriteBoxPokemonTo(pc, 0, 4, rec5)) {
    std::fprintf(stderr, "falha ao escrever o slot 4\n");
    return 1;
  }

  // Caixa 0, slot 3: Pidgey com um golpe que o FireRed NAO conhece.
  //
  // Existe para a spec 131 provar o aviso EM REPOUSO — o rodape tem de dizer
  // o motivo com o cursor parado, sem o jogador precisar tentar soltar. O
  // golpe 851 (Armor Cannon, gen9) esta muito acima da tabela gen3, entao
  // `MissingMoveIn` o reprova contra qualquer jogo de GBA.
  g3::FullRecord odd = MakeMon(16, 135, "PIDGEY");
  odd.moves[1] = 851;
  odd.pp[1] = 5;
  std::uint8_t rec4[80] = {};
  g3::EncodeFullRecord(odd, rec4);
  if (!g3::WriteBoxPokemonTo(pc, 0, 3, rec4)) {
    std::fprintf(stderr, "falha ao escrever o slot 3\n");
    return 1;
  }

  if (!g3::ApplyPcBuffer(file, *save, pc)) {
    std::fprintf(stderr, "falha ao aplicar o PC buffer\n");
    return 1;
  }

  std::ofstream out(out_path, std::ios::binary);
  if (!out) {
    std::fprintf(stderr, "nao foi possivel escrever %s\n", out_path);
    return 1;
  }
  out.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
  if (!out) {
    std::fprintf(stderr, "escrita incompleta em %s\n", out_path);
    return 1;
  }

  std::printf("fixture gerada: %s (%zu bytes, molde: %s)\n", out_path,
              file.size(), base_path ? base_path : "sintetico");
  return 0;
}
