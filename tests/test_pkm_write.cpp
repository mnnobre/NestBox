// Teste da escrita de PKM (spec 063, G01 da descoberta escrita-e-transferencia).
//
// O criterio duro e o ROUNDTRIP BYTE-IDENTICO: Parse(bytes) -> Write() tem de
// reproduzir exatamente os bytes que entraram. Se isso vale nas 141 fixtures,
// a escrita conservadora (TD-01: partir do raw) esta provada — nenhum byte que
// o modelo nao cobre foi perdido.
//
// Sobre ele empilham: roundtrip semantico, mutacao isolada e checksum.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "pa8.h"
#include "pb7.h"
#include "pb8.h"
#include "pk8.h"
#include "pk9.h"
#include "pkm_crypto.h"

namespace fs = std::filesystem;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
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

// Primeiro offset em que dois buffers divergem (-1 se iguais). Serve para o
// diagnostico dizer ONDE a escrita errou, nao so que errou.
static long long FirstDiff(const std::vector<std::uint8_t>& a,
                           const std::vector<std::uint8_t>& b) {
  if (a.size() != b.size()) return -2;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return static_cast<long long>(i);
  }
  return -1;
}

// Comparacao campo a campo do modelo. Nao compara `raw` (e o buffer inteiro,
// coberto pelo teste byte-identico) nem `checksum` (recalculado por TD-03).
// Devolve o nome do primeiro campo divergente, ou "" se tudo bate.
static std::string DiffModel(const pkm::Pokemon& a, const pkm::Pokemon& b) {
#define CMP(field)                     \
  if (!(a.field == b.field)) return #field;
  CMP(format)
  CMP(encryption_constant)
  CMP(sanity)
  CMP(species)
  CMP(form)
  CMP(gender)
  CMP(nature)
  CMP(stat_nature)
  CMP(ability)
  CMP(ability_number)
  CMP(is_egg)
  CMP(is_nicknamed)
  CMP(nickname)
  CMP(language)
  CMP(pid)
  CMP(fateful_encounter)
  CMP(ot_name)
  CMP(tid)
  CMP(sid)
  CMP(ot_gender)
  CMP(ot_friendship)
  CMP(ht_name)
  CMP(ht_gender)
  CMP(ht_language)
  CMP(ht_id)
  CMP(ht_friendship)
  CMP(current_handler)
  CMP(ot_memory.memory)
  CMP(ot_memory.intensity)
  CMP(ot_memory.feeling)
  CMP(ot_memory.text_var)
  CMP(ht_memory.memory)
  CMP(ht_memory.intensity)
  CMP(ht_memory.feeling)
  CMP(ht_memory.text_var)
  CMP(exp)
  CMP(held_item)
  CMP(markings)
  CMP(pokerus)
  CMP(favorite)
  CMP(fullness)
  CMP(enjoyment)
  CMP(status_condition)
  CMP(palma)
  CMP(form_argument)
  CMP(move_record_flags)
  CMP(ivs)
  CMP(evs)
  CMP(hyper_trained)
  CMP(contest_stats)
  CMP(contest_sheen)
  CMP(moves)
  CMP(pp)
  CMP(pp_ups)
  CMP(relearn_moves)
  CMP(origin_game)
  CMP(met_location)
  CMP(met_level)
  CMP(egg_location)
  CMP(met_date)
  CMP(egg_date)
  CMP(ball)
  CMP(home_tracker)
  CMP(ribbon_bytes)
  CMP(ribbon_count_memory)
  CMP(affixed_ribbon)
  CMP(height_scalar)
  CMP(weight_scalar)
  CMP(scale)
  CMP(dynamax_level)
  CMP(can_gigantamax)
  CMP(sociability)
  CMP(is_alpha)
  CMP(is_noble)
  CMP(effort_levels)
  CMP(alpha_move)
  CMP(tera_type_original)
  CMP(tera_type_override)
  CMP(obedience_level)
  CMP(awakening_values)
  CMP(received_flags)
#undef CMP
  return "";
}

// Um formato = seu Parse, seu Write, seu bloco de cifra e sua extensao. Isso
// evita cinco copias do corpo do teste.
struct FormatUnder {
  const char* ext;              // ".pk8"
  const char* dir;              // "pk8"
  std::size_t block;            // bloco da cifra, para IsDecrypted/Checksum
  std::optional<pkm::Pokemon> (*parse)(const std::vector<std::uint8_t>&);
  std::vector<std::uint8_t> (*write)(const pkm::Pokemon&);
};

static void TestFixture(const FormatUnder& f,
                        const std::filesystem::path& path) {
  const std::string t = std::string(f.dir) + "/" + path.stem().string() + ": ";
  const auto bytes = ReadFile(path);
  if (bytes.empty()) {
    Check(false, t + "fixture lida");
    return;
  }

  const auto parsed = f.parse(bytes);
  if (!parsed) {
    Check(false, t + "parseia");
    return;
  }
  const pkm::Pokemon& p = *parsed;

  // (1) Roundtrip byte-identico. O Parse guarda em `raw` o buffer DECIFRADO;
  // as fixtures podem estar cifradas (o pa8 vem decifrado, outras nao), entao
  // o alvo da comparacao e sempre `p.raw`, nunca o arquivo cru.
  const auto written = f.write(p);
  const long long diff = FirstDiff(p.raw, written);
  Check(diff == -1, t + "roundtrip byte-identico (1a divergencia no offset " +
                        std::to_string(diff) + ")");

  // (2) Roundtrip semantico: reparsear o que escrevemos devolve o mesmo modelo.
  const auto reparsed = f.parse(written);
  if (!reparsed) {
    Check(false, t + "o buffer escrito volta a parsear");
    return;
  }
  const std::string field = DiffModel(p, *reparsed);
  Check(field.empty(), t + "roundtrip semantico (campo divergente: " + field +
                           ")");

  // (4) O buffer escrito e um buffer decifrado valido — o checksum de 0x06
  // bate com a soma dos blocos.
  Check(pkc::IsDecrypted(written.data(), f.block),
        t + "o buffer escrito passa em IsDecrypted");

  // (3) Mutacao: mexer num campo muda AQUELE campo e nada mais.
  {
    pkm::Pokemon m = p;
    m.nickname = "Zz";
    m.is_nicknamed = true;
    m.ivs[0] = static_cast<std::uint8_t>(p.ivs[0] == 31 ? 7 : 31);
    m.held_item = static_cast<std::uint16_t>(p.held_item == 55 ? 56 : 55);

    const auto mut = f.write(m);
    Check(mut.size() == written.size(), t + "mutacao preserva o tamanho");
    const auto back = f.parse(mut);
    if (!back) {
      Check(false, t + "o buffer mutado parseia");
      return;
    }
    Check(back->nickname == "Zz", t + "mutacao: nickname chegou");
    Check(back->ivs[0] == m.ivs[0], t + "mutacao: IV[0] chegou");
    Check(back->held_item == m.held_item, t + "mutacao: held_item chegou");

    // E o resto? Compara o modelo mutado relido contra o mutado esperado: se
    // a escrita tivesse vazado para outro campo, apareceria aqui.
    const std::string leak = DiffModel(m, *back);
    Check(leak.empty(), t + "mutacao nao vazou (campo divergente: " + leak +
                            ")");

    // Prova mais forte, no nivel do byte: so os offsets dos tres campos
    // mudaram. Contamos os bytes diferentes — um numero grande denunciaria
    // uma escrita que reconstroi o buffer em vez de sobrescrever.
    std::size_t changed = 0;
    for (std::size_t i = 0; i < mut.size() && i < written.size(); ++i) {
      if (mut[i] != written[i]) ++changed;
    }
    // Apelido (26 bytes) + IV/nicknamed (4) + item (2) + checksum (2) = 34 no
    // pior caso. O limite generoso ainda pega uma reconstrucao do zero.
    Check(changed <= 40, t + "mutacao mexeu em poucos bytes (" +
                             std::to_string(changed) + ")");
  }
}

int main() {
  const FormatUnder formats[] = {
      {".pk8", "pk8", pkc::kBlockPK8, &pk8::Parse, &pk8::Write},
      {".pk9", "pk9", pkc::kBlockPK8, &pk9::Parse, &pk9::Write},
      {".pb8", "pb8", pkc::kBlockPK8, &pb8::Parse, &pb8::Write},
      {".pa8", "pa8", pkc::kBlockPA8, &pa8::Parse, &pa8::Write},
      {".pb7", "pb7", pkc::kBlockPB7, &pb7::Parse, &pb7::Write},
  };

  int total = 0;
  for (const auto& f : formats) {
    const std::filesystem::path dir(std::string(PKM_FIXTURES) + f.dir);
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().extension() != f.ext) continue;
      TestFixture(f, entry.path());
      ++count;
    }
    std::printf("%s: %d fixtures\n", f.dir, count);
    Check(count > 0, std::string("achou fixtures de ") + f.dir);
    total += count;
  }

  // A descoberta fala em 141 fixtures (o numero de JSONs do PkHeX em
  // tests/fixtures/pkhex). Os binarios sao 160: a spec 066 acrescentou as
  // fixtures `party-*`, extraidas dos saves reais e sem JSON de oraculo.
  // Este teste nao precisa do JSON — o oraculo dele e o proprio buffer de
  // entrada — entao cobre as 160, party inclusive.
  Check(total == 160,
        "as 160 fixtures binarias (achou " + std::to_string(total) + ")");

  // Pokemon sem `raw` (TD-02): o caminho existe e produz buffer valido, mas
  // NAO e um Pokemon de jogo — so os campos do modelo sobre zeros.
  //
  // VEREDITO DO PkHeX, spec 076 (`tools/pkhex-pkm legality`): **ILEGAL**.
  // O buffer tem checksum coerente e o PkHeX o LE sem erro — Species 25,
  // Nickname "Pika", ChecksumValid true — mas `LegalityAnalysis.Valid` e
  // false, com 10 violacoes:
  //
  //   Invalid Move 1: Empty Move.
  //   Invalid: Unable to match an encounter from origin game.
  //   Invalid: Encryption Constant matches PID.
  //   Invalid: Nickname does not match species name.
  //   Invalid: Language ID should be <= , not .
  //   Invalid: OT Name too short.
  //   Invalid: Ability is not valid for species/form.
  //   Invalid: Current handler cannot be the OT.
  //   Invalid: Invalid Affixed Ribbon/Marking: Kalos Champion
  //   Invalid: Pokemon HOME Transfer Tracker is missing.
  //
  // CONSEQUENCIA, e e o ponto desta secao: este caminho serve para REESCREVER
  // um Pokemon que veio de um save (onde `raw` traz os bytes originais e so os
  // campos modelados sao sobrescritos). Ele NAO serve para CRIAR Pokemon do
  // nada — o resultado e um Pokemon que o PkHeX recusa e que provavelmente o
  // jogo tambem recusaria. Uma spec futura de "criar Pokemon" precisa
  // preencher encontro, OT, idioma, EC/PID, habilidade e tracker; nada disso
  // sai de zeros.
  {
    pkm::Pokemon novo;
    novo.species = 25;
    novo.nickname = "Pika";
    const auto b = pk8::Write(novo);
    Check(b.size() == pk8::kStoredSize, "raw vazio: tamanho stored");
    Check(pkc::IsDecrypted(b.data(), pkc::kBlockPK8),
          "raw vazio: checksum coerente");
    const auto back = pk8::Parse(b);
    Check(back.has_value() && back->species == 25 && back->nickname == "Pika",
          "raw vazio: os campos do modelo chegaram");

    // spec 076: grava o buffer para o PkHeX julgar.
    //
    // Ate aqui o teste so provava que o buffer PARSEIA DE VOLTA — e o nosso
    // parser e o nosso escritor, o que faz dele oraculo de si mesmo (a licao
    // da spec 069). Se o PkHeX aceita este Pokemon e outra pergunta, e e a
    // que importa para um "criar Pokemon" futuro.
    //
    // O arquivo vai para o diretorio de build; o veredito e colado no
    // evidence-log da spec 076 por:
    //   cd tools/pkhex-pkm && dotnet run -- legality <caminho>/raw-vazio.pk8
    std::ofstream out(fs::path("raw-vazio.pk8"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()),
              static_cast<std::streamsize>(b.size()));
    Check(bool(out), "raw vazio: buffer gravado como raw-vazio.pk8 para o PkHeX");
  }

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("pkm_write: %d fixtures, roundtrip byte-identico, tudo verde\n",
              total);
  return 0;
}
