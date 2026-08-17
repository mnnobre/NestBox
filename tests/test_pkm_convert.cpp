// Teste da conversao de formato (spec 069, G06 da descoberta
// escrita-e-transferencia).
//
// O criterio: para cada uma das 20 direcoes (5 formatos x 4 destinos), os
// campos [HOME] do modelo (pesquisa §7) tem de sobreviver — e a ESPECIE tem de
// sobreviver com a conversao de indice CERTA.
//
// A armadilha que este teste existe para pegar: `pkm::Pokemon::species` guarda
// indice INTERNO do gen9 no PK9 e National Dex nos outros quatro. Comparar o
// campo cru entre formatos diferentes daria falso-verde para quatro das cinco
// origens e mascararia a troca de especie exatamente onde ela acontece.
// Por isso toda comparacao passa por pkm::NationalDex.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "gen9_species_id.h"
#include "pa8.h"
#include "pb7.h"
#include "pb8.h"
#include "pk8.h"
#include "pk9.h"
#include "pkm_convert.h"
#include "species_facts.h"

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  }
}

static std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
  // filesystem::path, nao string: no Windows o ifstream(string) usa codepage
  // ANSI e nao acha os nomes com a estrela do shiny.
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

struct FormatUnder {
  pkm::Format fmt;
  const char* ext;
  const char* dir;
  std::optional<pkm::Pokemon> (*parse)(const std::vector<std::uint8_t>&);
  std::vector<std::uint8_t> (*write)(const pkm::Pokemon&);
};

static const FormatUnder kFormats[] = {
    {pkm::Format::kPB7, ".pb7", "pb7", pb7::Parse, pb7::Write},
    {pkm::Format::kPK8, ".pk8", "pk8", pk8::Parse, pk8::Write},
    {pkm::Format::kPB8, ".pb8", "pb8", pb8::Parse, pb8::Write},
    {pkm::Format::kPA8, ".pa8", "pa8", pa8::Parse, pa8::Write},
    {pkm::Format::kPK9, ".pk9", "pk9", pk9::Parse, pk9::Write},
};

// Os campos [HOME] do modelo. `species` fica FORA desta macro de proposito:
// e a unica grandeza que muda de significado entre formatos e por isso e
// comparada a parte, sempre via NationalDex.
#define HOME_FIELDS(X) \
  X(pid) X(tid) X(sid) X(ot_name) X(ball) X(nature) X(nickname) \
  X(ivs) X(evs) X(gender) X(ability) X(exp) X(form) X(language) \
  X(met_location) X(met_date) X(origin_game) X(is_egg)

// National Dex calculado pelo TESTE, independente de pkm::NationalDex.
//
// Nao e duplicacao ociosa: usar a funcao do produto nos DOIS lados da
// comparacao faz o erro se cancelar. A violacao plantada da spec 069 provou
// isso — com `NationalDex` quebrada, origem e destino devolviam o mesmo numero
// errado e as 20 direcoes ficavam VERDES. O oraculo tem de ser externo a coisa
// testada.
static std::uint16_t DexOracle(const pkm::Pokemon& p) {
  if (p.format == pkm::Format::kPK9) {
    return pokehome::gen9::kInternalToNational[p.species];
  }
  return p.species;
}

// Compara os campos [HOME] que o DESTINO representa. Devolve o nome do
// primeiro divergente, ou "" se tudo bate.
static std::string DiffHome(const pkm::Pokemon& a, const pkm::Pokemon& b) {
  if (DexOracle(a) != DexOracle(b)) return "species(NationalDex)";
#define CMP(f) if (!(a.f == b.f)) return #f;
  HOME_FIELDS(CMP)
#undef CMP
  return "";
}

// ---------------------------------------------------------------------------

static void TestOneDirection(const FormatUnder& src, const FormatUnder& dst,
                             const std::filesystem::path& path) {
  const std::string t = std::string(src.dir) + "->" + dst.dir + "/" +
                        path.stem().string() + ": ";
  const auto bytes = ReadFile(path);
  if (bytes.empty()) return;
  const auto parsed = src.parse(bytes);
  if (!parsed) return;
  const pkm::Pokemon& origem = *parsed;

  const auto conv = pkm::Convert(origem, dst.fmt);
  if (!conv) {
    // Recusa legitima: o destino nao consegue ENDERECAR o dado (TD-03) —
    // especie fora da tabela, forma que nao cabe em 5 bits, ou habilidade
    // acima de 255 indo para o PB7 (u8). Nao e falha do teste, e a resposta
    // certa. Mas o teste exige um motivo: se todos passarem, o Convert
    // desistiu sem razao e isso e bug.
    const std::uint16_t dex = DexOracle(origem);
    const bool especie = pkm::SpeciesForFormat(dex, dst.fmt) == 0;
    const bool forma = dst.fmt == pkm::Format::kPB7 && origem.form > 0x1F;
    const bool habilidade =
        dst.fmt == pkm::Format::kPB7 && origem.ability > 0xFF;
    Check(especie || forma || habilidade,
          t + "recusa tem motivo (dex " + std::to_string(dex) + ", forma " +
              std::to_string(origem.form) + ", ability " +
              std::to_string(origem.ability) + ")");
    return;
  }

  Check(conv->format == dst.fmt, t + "formato do resultado e o pedido");

  // TD-01: o raw da origem tem de ser DESCARTADO. Se sobrevivesse, o Write do
  // destino escreveria campos novos por cima de bytes de outro layout.
  Check(conv->raw.empty(), t + "raw da origem descartado (TD-01)");

  // Os campos [HOME] sobreviveram ao modelo convertido.
  const std::string d1 = DiffHome(origem, *conv);
  Check(d1.empty(), t + "campos [HOME] no modelo convertido (divergente: " +
                        d1 + ")");

  // A prova que interessa: passar pelo BINARIO do destino. Um Convert que so
  // mexe no modelo mas grava num offset errado passaria no check acima.
  const auto written = dst.write(*conv);
  const auto reparsed = dst.parse(written);
  if (!reparsed) {
    Check(false, t + "o binario do destino volta a parsear");
    return;
  }
  const std::string d2 = DiffHome(origem, *reparsed);
  Check(d2.empty(),
        t + "campos [HOME] sobrevivem ao binario do destino (divergente: " +
            d2 + ")");

  // Shiny e derivado de PID+TID+SID. Se qualquer um dos tres se perder, um
  // shiny deixa de ser shiny — o dado mais visivel de todos (§7).
  Check(pkm::IsShiny(origem) == pkm::IsShiny(*reparsed),
        t + "shiny preservado");

  // home_tracker: preservado sempre que o destino o representa. O PB7 e
  // anterior ao HOME e nao tem o campo.
  if (dst.fmt != pkm::Format::kPB7) {
    Check(reparsed->home_tracker == origem.home_tracker,
          t + "home_tracker preservado");
  } else {
    Check(reparsed->home_tracker == 0,
          t + "home_tracker zerado no PB7 (formato nao tem o campo)");
  }

  // --- Campos exclusivos zeram no destino que nao os tem ----------------
  if (dst.fmt != pkm::Format::kPK8) {
    Check(reparsed->dynamax_level == 0 && !reparsed->can_gigantamax,
          t + "dynamax/gigantamax zerados (so o PK8 os tem)");
  }
  if (dst.fmt != pkm::Format::kPA8) {
    Check(!reparsed->is_alpha && !reparsed->is_noble &&
              reparsed->alpha_move == 0 &&
              reparsed->effort_levels == std::array<std::uint8_t, 6>{},
          t + "alpha/noble/effort levels zerados (so o PA8 os tem)");
  }
  if (dst.fmt != pkm::Format::kPK9) {
    Check(reparsed->tera_type_original == 0 &&
              reparsed->tera_type_override == 0 &&
              reparsed->obedience_level == 0,
          t + "tera type e obedience zerados (so o PK9 os tem)");
  }
  if (dst.fmt != pkm::Format::kPB7) {
    Check(reparsed->awakening_values == std::array<std::uint8_t, 6>{},
          t + "awakening values zerados (so o PB7 os tem)");
  }

  // --- Campo DERIVADO: tera type ao entrar no PK9 (§7) -------------------
  // "ao entrar em Scarlet/Violet, recebe Tera Type derivado do tipo primario".
  // Confirmado contra o EntityConverter do PkHeX em 10/10 especies (spec 069).
  if (dst.fmt == pkm::Format::kPK9) {
    const std::uint8_t esperado =
        pokehome::species::Type1(DexOracle(origem));
    Check(esperado != 0xFF && reparsed->tera_type_original == esperado,
          t + "tera type derivado do tipo primario (esperado " +
              std::to_string(esperado) + ", veio " +
              std::to_string(reparsed->tera_type_original) + ")");
  }
}

// Ida e volta A->B->A. O que NAO sobrevive esta documentado no evidence-log:
// os campos [HOME] tem de voltar; o `raw` nao volta (TD-01) e os campos
// exclusivos de A perdidos em B nao ressuscitam.
static void TestRoundTrip(const FormatUnder& a, const FormatUnder& b,
                          const std::filesystem::path& path) {
  const std::string t = std::string(a.dir) + "->" + b.dir + "->" + a.dir + "/" +
                        path.stem().string() + ": ";
  const auto bytes = ReadFile(path);
  if (bytes.empty()) return;
  const auto parsed = a.parse(bytes);
  if (!parsed) return;

  const auto mid = pkm::Convert(*parsed, b.fmt);
  if (!mid) return;  // recusa ja coberta em TestOneDirection
  const auto midBin = b.parse(b.write(*mid));
  if (!midBin) {
    Check(false, t + "etapa intermediaria parseia");
    return;
  }

  const auto back = pkm::Convert(*midBin, a.fmt);
  if (!back) {
    Check(false, t + "volta e possivel");
    return;
  }
  const auto backBin = a.parse(a.write(*back));
  if (!backBin) {
    Check(false, t + "volta parseia");
    return;
  }

  // Os campos [HOME] voltam. Excecao conhecida e documentada: o PB7 tem
  // apelido de 12 chars e nomes de treinador de 11, contra 13 dos gen8+ —
  // um nome longo e TRUNCADO na ida e nao volta a crescer.
  const bool via_pb7 = b.fmt == pkm::Format::kPB7;
  pkm::Pokemon esperado = *parsed;
  if (via_pb7) {
    // Compara contra o que CABIA, nao contra o original: exigir o nome inteiro
    // de volta seria exigir informacao que o formato intermediario nao tem.
    esperado.nickname = backBin->nickname;
    esperado.ot_name = backBin->ot_name;
  }
  const std::string d = DiffHome(esperado, *backBin);
  Check(d.empty(), t + "campos [HOME] sobrevivem a ida e volta (divergente: " +
                       d + ")");
  Check(pkm::IsShiny(*parsed) == pkm::IsShiny(*backBin),
        t + "shiny sobrevive a ida e volta");
}

// ---------------------------------------------------------------------------

int main() {
  // --- Prova direta da armadilha de species ------------------------------
  // O Pawmo e 955 no binario do PK9 e 922 na dex nacional (spec 065, e
  // confirmado contra o PkHeX na 069). Se estas asserções cairem, tudo o que
  // vem depois esta comparando a grandeza errada.
  {
    pkm::Pokemon fake;
    fake.format = pkm::Format::kPK9;
    fake.species = 955;
    Check(pkm::NationalDex(fake) == 922,
          "NationalDex traduz o indice interno do PK9 (955 -> 922)");
    fake.format = pkm::Format::kPB8;
    fake.species = 922;
    Check(pkm::NationalDex(fake) == 922,
          "NationalDex passa direto fora do PK9");
    Check(pkm::SpeciesForFormat(922, pkm::Format::kPK9) == 955,
          "SpeciesForFormat volta ao indice interno (922 -> 955)");
    Check(pkm::SpeciesForFormat(922, pkm::Format::kPK8) == 922,
          "SpeciesForFormat passa direto fora do PK9");
  }

  // Converter para o proprio formato devolve copia identica, raw INCLUSIVE:
  // mover dentro do mesmo jogo continua sob a regra conservadora da spec 063.
  {
    pkm::Pokemon p;
    p.format = pkm::Format::kPK8;
    p.species = 25;
    p.raw.assign(328, 0xAB);
    const auto same = pkm::Convert(p, pkm::Format::kPK8);
    Check(same.has_value() && same->raw == p.raw,
          "Convert para o proprio formato preserva o raw (regra da spec 063)");
  }

  const std::filesystem::path fixtures = PKM_FIXTURES;
  const std::filesystem::path synth = SYNTH_FIXTURES;

  // As sinteticas (spec 067) sao as que EXERCITAM os campos: as fixtures reais
  // saem de saves de inicio de jogo com ribbons, memorias e tracker zerados.
  // Quantas fixtures do PK9 tem indice interno DIFERENTE da National Dex. Se
  // for zero, o teste inteiro esta cego para a armadilha de species — ver o
  // comentario da amostragem abaixo.
  int pk9_discriminantes = 0;

  int pairs = 0;
  for (const auto& src : kFormats) {
    std::vector<std::filesystem::path> files;

    const auto synth_file =
        synth / src.dir / (std::string("synth") + src.ext);
    if (std::filesystem::exists(synth_file)) files.push_back(synth_file);

    // Amostragem das fixtures reais. O corte NAO pode ser "as 6 primeiras":
    // a violacao plantada da spec 069 provou que, com esse corte, quebrar a
    // conversao de species do PK9 deixava as 20 direcoes VERDES. As 6
    // primeiras do diretorio (Growlithe, Tauros, Munchlax...) sao todas
    // especies em que o indice interno do gen9 COINCIDE com a National Dex,
    // entao a traducao errada devolvia o mesmo numero.
    //
    // Regra: entra tudo em que interno != nacional, e so depois se completa a
    // amostra. E o mesmo principio da spec 067 — a fixture tem de EXERCITAR o
    // campo, senao o teste compara um valor consigo mesmo.
    const auto dir = fixtures / src.dir;
    if (std::filesystem::exists(dir)) {
      std::vector<std::filesystem::path> resto;
      for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() != src.ext) continue;
        const auto b = ReadFile(e.path());
        const auto pk = src.parse(b);
        const std::uint16_t dex = pk ? DexOracle(*pk) : 0;
        const bool discrimina =
            dex != 0 && pokehome::gen9::ToInternal(dex) != dex;
        if (discrimina && src.fmt == pkm::Format::kPK9) ++pk9_discriminantes;
        (discrimina ? files : resto).push_back(e.path());
      }
      for (const auto& r : resto) {
        if (files.size() >= 8) break;
        files.push_back(r);
      }
    }

    if (files.empty()) {
      Check(false, std::string("fixtures de ") + src.dir + " encontradas");
      continue;
    }

    for (const auto& dst : kFormats) {
      if (dst.fmt == src.fmt) continue;
      ++pairs;
      for (const auto& f : files) {
        TestOneDirection(src, dst, f);
        TestRoundTrip(src, dst, f);
      }
    }
  }

  Check(pairs == 20, "as 20 direcoes de conversao foram exercitadas (foram " +
                         std::to_string(pairs) + ")");

  // A trava contra o falso-verde: sem fixture em que interno != nacional, as
  // 20 direcoes passam MESMO com a conversao de species quebrada. Provado na
  // violacao plantada da spec 069.
  Check(pk9_discriminantes >= 3,
        "ha fixtures de PK9 que DISCRIMINAM a conversao de species (foram " +
            std::to_string(pk9_discriminantes) + ", minimo 3)");

  std::printf("%s: %d checagens, %d falhas\n",
              g_failures ? "VERMELHO" : "VERDE", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
