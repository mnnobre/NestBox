// Matriz de validacao pre-PROD (spec 114).
//
// Tres personas sobre o CORE das trocas: usuario normal (N), usuario menos
// convencional (U) e QA tentando quebrar (Q). Dados reais (saves limpos e do
// simulador, SOMENTE LEITURA — tudo em memoria) e sinteticos extremos.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gen3_save.h"
#include "gen3_transfer.h"
#include "learnset.h"
#include "legality.h"
#include "modern_box_view.h"
#include "moveset_memory.h"
#include "nestbox_ab.h"
#include "nestbox_file.h"
#include "pk9.h"
#include "pkm_convert.h"
#include "pkm_crypto.h"
#include "save_writer.h"
#include "species_facts.h"
#include "swish_crypto.h"

namespace fs = std::filesystem;
namespace g3 = pokehome::gen3;
namespace g3x = pokehome::g3x;
namespace ms = pokehome::moveset;
namespace ls = pokehome::learnset;
namespace nb = pokehome::nest;
namespace sp = pokehome::species;
namespace vw = pokehome::view;

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
  std::ifstream f(fs::path(path), std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
}

// Registro gen3 sintetico parametrizavel.
struct Gen3Opts {
  std::uint16_t species = 25;
  std::uint32_t pid = 0x00010002;
  std::uint32_t otid = 0x00010001;
  std::uint32_t exp = 100000;
  std::uint8_t evs[6] = {252, 100, 50, 100, 4, 4};
  std::uint8_t language = 2;
  const char* nick = "PIKACHU";
  bool egg = false;
};

static void Faz(const Gen3Opts& o, std::uint8_t out[80]) {
  g3::FullRecord r;
  r.personality = o.pid;
  r.ot_id = o.otid;
  g3::EncodeGen3String(o.nick, r.nickname_raw, sizeof(r.nickname_raw));
  r.language = o.language;
  r.flags = 0x02;
  g3::EncodeGen3String("QA", r.ot_name_raw, sizeof(r.ot_name_raw));
  r.species = o.species;
  r.experience = o.exp;
  r.moves[0] = 33;
  r.pp[0] = 35;
  std::memcpy(r.evs, o.evs, 6);
  const std::uint32_t ivs[6] = {31, 30, 29, 28, 27, 26};
  for (int i = 0; i < 6; ++i) r.iv32 |= ivs[i] << (i * 5);
  if (o.egg) r.iv32 |= 1u << 30;
  r.origins = static_cast<std::uint16_t>(5 | (4u << 7) | (4u << 11));
  g3::EncodeFullRecord(r, out);
}

// ---------------------------------------------------------------------------
// N — usuario normal
// ---------------------------------------------------------------------------

// N1: gen3 -> NestBox -> gen3, cru e byte-identico atraves do arquivo.
static void TestN1() {
  std::printf("\n=== N1: gen3 -> NestBox -> gen3 (roundtrip cru) ===\n");
  std::uint8_t raw[80];
  Faz(Gen3Opts{}, raw);

  nb::NestData banco = nb::MakeEmpty(3, 30);
  nb::SlotWrite(banco.At(1, 7), nb::kGen3, raw, sizeof(raw));
  const auto bytes = nb::ab::Wrap(nb::Encode(banco), 1);
  const nb::ab::Slot slot = nb::ab::Unwrap(bytes);
  Check(slot.valid, "banco embrulhado e valido");
  const nb::NestData volta = nb::Decode(slot.payload);
  Check(volta.valid() && std::memcmp(nb::SlotPayload(volta.At(1, 7)), raw,
                                     80) == 0,
        "80 bytes identicos apos arquivo completo");
}

// N2: gen3 -> cada formato moderno; contratos de campo.
static void TestN2() {
  std::printf("\n=== N2: gen3 -> cada jogo de Switch ===\n");
  std::uint8_t raw[80];
  Faz(Gen3Opts{}, raw);

  struct Alvo { const char* nome; pkm::Format fmt; ms::Game msg; };
  const Alvo alvos[] = {
      {"SwSh", pkm::Format::kPK8, ms::Game::kSwSh},
      {"SV", pkm::Format::kPK9, ms::Game::kSV},
      {"Z-A", pkm::Format::kPK9, ms::Game::kZA},
      {"BDSP", pkm::Format::kPB8, ms::Game::kBdsp},
      {"PLA", pkm::Format::kPA8, ms::Game::kLegendsArceus},
      {"LGPE", pkm::Format::kPB7, ms::Game::kLgpe},
  };
  for (const auto& a : alvos) {
    ms::Memory mem;
    auto up = g3x::ConvertUp(raw, a.fmt, a.msg, &mem);
    const std::string t = std::string("gen3 -> ") + a.nome + ": ";
    Check(up.has_value(), t + "converte");
    if (!up) continue;
    Check(pkm::NationalDex(*up) == 25, t + "dex 25");
    Check(up->pid == 0x00010002 && pkm::IsShiny(*up), t + "PID/shiny");
    Check(up->ivs[0] == 31 && up->evs[0] == 252, t + "IV/EV");
    Check(up->held_item == 0, t + "sem item");
    Check(up->moves[0] != 0, t + "moveset do alvo preenchido");
    Check(!legality::CheckLegality(*up).suspect, t + "passa na legalidade");
    Check(mem.Recall(up->home_tracker, ms::Game::kGen3) != nullptr,
          t + "gen3 memorizado");
  }
}

// N4: moderno -> gen3 -> moderno com restauracao.
static void TestN4() {
  std::printf("\n=== N4: ciclo com restauracao de moveset ===\n");
  std::uint8_t raw[80];
  Faz(Gen3Opts{}, raw);
  ms::Memory mem;
  auto up = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, &mem);
  if (!up) { Check(false, "subida"); return; }
  const auto za_moves = up->moves;

  std::uint8_t down[80];
  Check(g3x::ConvertDown(*up, ls::Game::kFireRed, ms::Game::kZA, &mem, 4,
                         down),
        "descida");
  const auto full = g3::DecodeFullRecord(down);
  Check(full && full->moves[0] == 33, "moveset gen3 original restaurado");

  auto up2 = g3x::ConvertUp(down, pkm::Format::kPK9, ms::Game::kZA, &mem);
  Check(up2.has_value() && up2->moves == za_moves,
        "segunda subida restaura o moveset do Z-A");
}

// ---------------------------------------------------------------------------
// U — usuario menos convencional
// ---------------------------------------------------------------------------

static void TestU1() {
  std::printf("\n=== U1: extremos ===\n");
  {
    Gen3Opts o;
    o.exp = 0;  // nivel 1
    std::uint8_t raw[80];
    Faz(o, raw);
    auto up = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, nullptr);
    Check(up.has_value(), "nivel 1 converte");
    Check(up && up->moves[0] != 0, "nivel 1 ganha moveset (learnset nivel 1)");
  }
  {
    Gen3Opts o;
    o.exp = 1000000;  // nivel 100 (MediumFast)
    std::uint8_t raw[80];
    Faz(o, raw);
    auto up = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, nullptr);
    Check(up.has_value() && sp::LevelFromExp(25, up->exp) == 100,
          "nivel 100 converte com exp intacta");
  }
  {
    Gen3Opts o;
    o.evs[0] = 0; o.evs[1] = 0; o.evs[2] = 0;
    o.evs[3] = 0; o.evs[4] = 0; o.evs[5] = 0;
    std::uint8_t raw[80];
    Faz(o, raw);
    auto up = g3x::ConvertUp(raw, pkm::Format::kPK8, ms::Game::kSwSh, nullptr);
    Check(up.has_value(), "EV zero converte");
  }
  for (std::uint8_t lang : {1, 2, 3, 4, 5, 7}) {
    Gen3Opts o;
    o.language = lang;
    std::uint8_t raw[80];
    Faz(o, raw);
    auto up = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, nullptr);
    Check(up.has_value() && up->language == lang,
          "idioma " + std::to_string(lang) + " sobrevive");
  }
}

static void TestU2() {
  std::printf("\n=== U2: Unown, Deoxys, Mew ===\n");
  {
    Gen3Opts o;
    o.species = g3::InternalFromDex(201);  // Unown
    o.nick = "UNOWN";
    std::uint8_t raw[80];
    Faz(o, raw);
    auto up = g3x::ConvertUp(raw, pkm::Format::kPK8, ms::Game::kSwSh, nullptr);
    Check(up.has_value(), "Unown converte");
    Check(up && up->form < 28, "letra do Unown na faixa (PID)");
    // A letra rederiva do MESMO PID na descida — sem perda.
    if (up) {
      std::uint8_t down[80];
      Check(g3x::ConvertDown(*up, ls::Game::kEmerald, ms::Game::kSwSh,
                             nullptr, 3, down),
            "Unown desce (forma vem do PID)");
    }
  }
  for (int dex : {151, 386}) {  // Mew, Deoxys
    Gen3Opts o;
    o.species = g3::InternalFromDex(dex);
    o.nick = "LENDA";
    std::uint8_t raw[80];
    Faz(o, raw);
    auto up = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, nullptr);
    // Presenca no JOGO e portao da UI (HasSpecies); aqui so o formato.
    Check(up.has_value(), "dex " + std::to_string(dex) + " converte no formato");
  }
}

static void TestU3() {
  std::printf("\n=== U3: cadeia gen3 -> ZA -> gen3 -> SwSh -> gen3 ===\n");
  std::uint8_t raw[80];
  Faz(Gen3Opts{}, raw);
  ms::Memory mem;

  auto za = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, &mem);
  if (!za) { Check(false, "gen3 -> ZA"); return; }
  std::uint8_t g1[80];
  if (!g3x::ConvertDown(*za, ls::Game::kFireRed, ms::Game::kZA, &mem, 4, g1)) {
    Check(false, "ZA -> gen3");
    return;
  }
  auto sw = g3x::ConvertUp(g1, pkm::Format::kPK8, ms::Game::kSwSh, &mem);
  if (!sw) { Check(false, "gen3 -> SwSh"); return; }
  Check(sw->moves != za->moves || sw->moves[0] != 0,
        "SwSh resetou pelo learnset proprio (primeira visita)");
  std::uint8_t g2[80];
  Check(g3x::ConvertDown(*sw, ls::Game::kFireRed, ms::Game::kSwSh, &mem, 4,
                         g2),
        "SwSh -> gen3");
  const auto f2 = g3::DecodeFullRecord(g2);
  Check(f2 && f2->moves[0] == 33,
        "moveset gen3 ORIGINAL sobrevive a cadeia inteira");
  Check(mem.Recall(za->home_tracker, ms::Game::kZA) != nullptr &&
            mem.Recall(za->home_tracker, ms::Game::kSwSh) != nullptr &&
            mem.Recall(za->home_tracker, ms::Game::kGen3) != nullptr,
        "memoria guarda os tres jogos da cadeia");
}

static void TestU4() {
  std::printf("\n=== U4: moderno <-> moderno cruzado ===\n");
  auto sd = savew::Load(ReadFile(std::string(CLEAN_SAVES) + "swsh/main"),
                        savew::Game::kSwSh);
  if (!sd) { Check(false, "swsh limpo abriu"); return; }
  const pkm::Pokemon* p = nullptr;
  for (const auto& s : sd->box) {
    if (s.present && s.mon.species != 0) { p = &s.mon; break; }
  }
  if (!p) { Check(false, "swsh tem mon"); return; }

  auto conv = pkm::Convert(*p, pkm::Format::kPK9);
  Check(conv.has_value(), "SwSh -> pk9 converte");
  if (conv) {
    Check(pkm::NationalDex(*conv) == pkm::NationalDex(*p),
          "mesma dex nacional apos Convert");
    Check(conv->pid == p->pid, "PID intacto no Convert");
  }
}

static void TestU5() {
  std::printf("\n=== U5: memoria de moveset com jogos novos no arquivo ===\n");
  nb::NestData banco = nb::MakeEmpty(2, 30);
  pkm::Pokemon p;
  p.format = pkm::Format::kPK9;
  p.species = 25;
  p.home_tracker = 0x8000000000001234ULL;
  p.moves = {85, 86, 98, 21};
  banco.movesets.Remember(p, ms::Game::kGen3);
  banco.movesets.Remember(p, ms::Game::kZA);
  banco.movesets.Remember(p, ms::Game::kSwSh);

  const nb::NestData volta = nb::Decode(nb::Encode(banco));
  Check(volta.valid(), "banco com memoria decodifica");
  Check(volta.movesets.Recall(p.home_tracker, ms::Game::kGen3) != nullptr &&
            volta.movesets.Recall(p.home_tracker, ms::Game::kZA) != nullptr &&
            volta.movesets.Recall(p.home_tracker, ms::Game::kSwSh) != nullptr,
        "entradas kGen3/kZA/kSwSh sobrevivem ao arquivo (secao v4)");
}

// ---------------------------------------------------------------------------
// Q — QA quebrando
// ---------------------------------------------------------------------------

static void TestQ1() {
  std::printf("\n=== Q1: recusas ===\n");
  {
    Gen3Opts o;
    o.egg = true;
    std::uint8_t raw[80];
    Faz(o, raw);
    Check(!g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, nullptr)
               .has_value(),
          "ovo nao sobe");
  }
  {
    std::uint8_t zeros[80] = {};
    Check(!g3x::ConvertUp(zeros, pkm::Format::kPK9, ms::Game::kZA, nullptr)
               .has_value(),
          "vazio nao sobe");
  }
}

static void TestQ2() {
  std::printf("\n=== Q2: banco adulterado ===\n");
  nb::NestData banco = nb::MakeEmpty(2, 30);
  std::uint8_t raw[80];
  Faz(Gen3Opts{}, raw);
  nb::SlotWrite(banco.At(0, 0), nb::kGen3, raw, sizeof(raw));
  const auto payload = nb::Encode(banco);
  const auto wrapped = nb::ab::Wrap(payload, 7);

  {
    auto trunc = wrapped;
    trunc.resize(trunc.size() / 2);
    Check(!nb::ab::Unwrap(trunc).valid, "arquivo truncado e recusado");
  }
  {
    auto flip = wrapped;
    flip[flip.size() / 2] ^= 0xFF;
    Check(!nb::ab::Unwrap(flip).valid, "CRC invertido e recusado");
  }
  {
    auto vfut = payload;
    vfut[4] = 0x63;  // versao 99
    Check(nb::Decode(vfut).boxes == 0, "versao futura e recusada");
  }
  {
    auto sbad = payload;
    sbad[10] = 0x11;  // slot_bytes mentiroso
    Check(nb::Decode(sbad).boxes == 0, "slot_bytes errado e recusado");
  }
  {
    // Tamanho de payload mentiroso dentro do slot: clamp, nunca leitura fora.
    auto d = nb::Decode(payload);
    std::uint8_t* s = d.At(0, 0);
    s[2] = 0xFF;
    s[3] = 0xFF;
    Check(nb::SlotSize(s) == nb::kSlotPayload,
          "tamanho mentiroso e saturado no payload");
  }
}

static void TestQ3() {
  std::printf("\n=== Q3: registro gen3 adulterado ===\n");
  std::uint8_t raw[80];
  Faz(Gen3Opts{}, raw);
  {
    auto bad = std::vector<std::uint8_t>(raw, raw + 80);
    bad[0x1C] ^= 0xFF;  // checksum do registro quebrado
    Check(legality::CheckLegalityGen3(bad.data()).suspect,
          "checksum quebrado e acusado pelo verificador");
  }
  {
    Gen3Opts o;
    o.evs[0] = 255; o.evs[1] = 255; o.evs[2] = 255;
    o.evs[3] = 255; o.evs[4] = 255; o.evs[5] = 255;
    std::uint8_t evraw[80];
    Faz(o, evraw);
    Check(legality::CheckLegalityGen3(evraw).suspect, "EV 1530 e acusado");
  }
}

static void TestQ4() {
  std::printf("\n=== Q4: 80 bytes de lixo nao derrubam nada ===\n");
  std::uint32_t x = 0xC0FFEE01;
  int converteu = 0, recusou = 0, acusou = 0;
  for (int caso = 0; caso < 200; ++caso) {
    std::uint8_t raw[80];
    for (auto& b : raw) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;  // xorshift deterministico
      b = static_cast<std::uint8_t>(x);
    }
    const auto up =
        g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, nullptr);
    if (!up) {
      ++recusou;
    } else {
      ++converteu;
      if (legality::CheckLegality(*up).suspect ||
          legality::CheckLegalityGen3(raw).suspect) {
        ++acusou;
      }
    }
  }
  std::printf("    200 lixos: %d recusados, %d converteram (%d acusados)\n",
              recusou, converteu, acusou);
  Check(recusou + converteu == 200, "nenhum crash em 200 registros de lixo");
  Check(converteu == acusou,
        "todo lixo que converteu foi acusado pela legalidade");
}

// N7 (spec 125): a NestBox "relembra" os golpes originais na chegada, e a
// ida de mesmo formato reaplica os do jogo.
static void TestN7() {
  std::printf("\n=== N7: relembrar na NestBox (spec 125) ===\n");
  std::uint8_t raw[80];
  Faz(Gen3Opts{}, raw);
  ms::Memory mem;

  // Sobe para o Z-A: moveset vira o do Z-A.
  auto up = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, &mem);
  if (!up) { Check(false, "subida"); return; }
  const auto za_moves = up->moves;
  // COPIA o snapshot: o ponteiro do Recall aponta para dentro do vetor da
  // memoria e o Remember de logo abaixo pode realoca-lo (dangling que so
  // aparecia sob o ctest).
  const auto* orig_ptr = mem.Recall(up->home_tracker, ms::Game::kGen3);
  Check(orig_ptr != nullptr, "original memorizado na subida");
  if (!orig_ptr) return;
  const ms::Snapshot orig = *orig_ptr;

  // Chega a NestBox: relembra os ORIGINAIS sem voltar ao jogo.
  pkm::Pokemon banco = *up;
  Check(ms::RestoreOnBank(banco, mem, ms::Game::kZA),
        "RestoreOnBank restaurou na chegada");
  Check(banco.moves == orig.moves,
        "registro no banco tem os golpes ORIGINAIS");
  Check(mem.Recall(up->home_tracker, ms::Game::kZA) != nullptr,
        "moveset do Z-A ficou memorizado ao sair de la");

  // Mover dentro da box nao re-memoriza nem muda nada.
  pkm::Pokemon dentro = banco;
  Check(!ms::RestoreOnBank(dentro, mem, ms::Game::kZA),
        "mover dentro da box e no-op");

  // Ida de MESMO formato de volta ao Z-A: ApplyOnEntry restaura os do Z-A.
  pkm::Pokemon volta = banco;
  const std::uint8_t lvl = sp::LevelFromExp(25, volta.exp);
  Check(mem.ApplyOnEntry(volta, ms::Game::kZA, lvl),
        "entrada no Z-A restaura da memoria");
  Check(volta.moves == za_moves, "golpes do Z-A de volta na ida");

  // E o registro reserializa com os golpes restaurados.
  const auto bytes = pokehome::view::WriteModern(banco);
  Check(!bytes.empty(), "WriteModern serializa o restaurado");
  const auto re = pk9::Parse(bytes);
  Check(re.has_value() && re->moves == orig.moves,
        "payload regravado carrega os golpes originais");
}

int main() {
  std::printf("=== spec 114: matriz de validacao pre-PROD ===\n");
  TestN1();
  TestN2();
  TestN4();
  TestN7();
  TestU1();
  TestU2();
  TestU3();
  TestU4();
  TestU5();
  TestQ1();
  TestQ2();
  TestQ3();
  TestQ4();

  if (g_failures) {
    std::printf("\n%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("\nqa_matrix: tudo verde\n");
  return 0;
}
