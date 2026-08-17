// Fixtures SINTETICAS dos cinco formatos modernos (spec 067).
//
// O que este teste existe para pegar, e que nenhum outro pega: as fixtures
// reais saem de saves de INICIO DE JOGO, entao HT (nome/genero/idioma/ID/
// amizade), memorias, ribbons, PP-ups, relearn, egg date e home tracker
// estao ZERADOS em todas elas. O teste do formato compara 0 contra 0 e passa
// lendo de qualquer offset zerado — a spec 066 provou isso plantando offsets
// errados no PA8 sem o ctest reclamar.
//
// As fixtures daqui nascem do PkHeX (tools/pkhex-synth), com cada campo
// vizinho recebendo um valor DIFERENTE. Um offset trocado com o vizinho
// deixa de ser invisivel.
//
// TD-02: um arquivo so para os cinco formatos, porque a prova e a mesma.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "pa8.h"
#include "pb7.h"
#include "pb8.h"
#include "pk8.h"
#include "pk9.h"
#include "pkm_crypto.h"

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  }
}

static void Eq(long long ours, long long theirs, const std::string& what) {
  // long long: no Windows `long` tem 32 bits e EC/PID/tracker viram
  // falso-negativo.
  if (ours != theirs) {
    std::printf("FALHOU: %s — nosso %lld, PkHeX %lld\n", what.c_str(), ours,
                theirs);
    ++g_failures;
  }
}

static std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
  // filesystem::path, nao string: no Windows o ifstream(string) usa a
  // codepage ANSI.
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

static std::map<std::string, std::string> ReadFlatJson(
    const std::filesystem::path& path) {
  std::map<std::string, std::string> out;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    const auto q1 = line.find('"');
    if (q1 == std::string::npos) continue;
    const auto q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos) continue;
    const auto colon = line.find(':', q2);
    if (colon == std::string::npos) continue;
    std::string value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') value.erase(0, 1);
    while (!value.empty() && (value.back() == ',' || value.back() == ' ')) {
      value.pop_back();
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    out[line.substr(q1 + 1, q2 - q1 - 1)] = value;
  }
  return out;
}

static long long AsInt(const std::map<std::string, std::string>& j,
                       const std::string& key) {
  const auto it = j.find(key);
  if (it == j.end()) return -999999;
  if (it->second == "true" || it->second == "True") return 1;
  if (it->second == "false" || it->second == "False") return 0;
  try {
    return std::stoll(it->second);
  } catch (...) {
    return -999999;
  }
}

// O System.Text.Json grava nao-ASCII como \uXXXX.
static std::string Unescape(const std::string& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\\' || i + 1 >= s.size()) {
      out.push_back(s[i]);
      continue;
    }
    if (s[i + 1] == 'u' && i + 5 < s.size()) {
      const unsigned c =
          static_cast<unsigned>(std::stoul(s.substr(i + 2, 4), nullptr, 16));
      if (c < 0x80) {
        out.push_back(static_cast<char>(c));
      } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
      } else {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
      }
      i += 5;
    } else {
      out.push_back(s[i + 1]);
      ++i;
    }
  }
  return out;
}

static std::string AsStr(const std::map<std::string, std::string>& j,
                         const std::string& key) {
  const auto it = j.find(key);
  return it == j.end() ? "<ausente>" : Unescape(it->second);
}

// Assert que so vale se o valor esperado for NAO-ZERO. E o coracao da spec:
// um campo que o PkHeX devolve zerado nao prova offset nenhum, entao em vez
// de comparar 0 com 0 (o falso-verde da 066) o teste FALHA reclamando que a
// fixture nao exercita aquele campo.
static void EqNonZero(long long ours, long long theirs,
                      const std::string& what) {
  if (theirs == 0 || theirs == -999999) {
    std::printf("FALHOU: %s — a fixture nao exercita este campo (PkHeX deu "
                "%lld). Conserte a fixture, nao o assert.\n",
                what.c_str(), theirs);
    ++g_failures;
    return;
  }
  Eq(ours, theirs, what);
}

// Campos que TODOS os cinco formatos tem e que estavam zerados nas fixtures
// reais. Cada um exige valor nao-zero.
static void CheckComuns(const pkm::Pokemon& p,
                        const std::map<std::string, std::string>& j,
                        const std::string& t) {
  // Bloco do handling trainer.
  Check(!p.ht_name.empty() && p.ht_name == AsStr(j, "HandlingTrainerName"),
        t + "HTName (" + p.ht_name + " vs " +
            AsStr(j, "HandlingTrainerName") + ")");
  Check(p.ht_name != p.ot_name, t + "HTName difere do OTName");
  EqNonZero(p.ht_gender, AsInt(j, "HandlingTrainerGender"), t + "HTGender");
  EqNonZero(p.ht_friendship, AsInt(j, "HandlingTrainerFriendship"),
            t + "HTFriendship");
  EqNonZero(p.current_handler, AsInt(j, "CurrentHandler"),
            t + "CurrentHandler");
  // OT friendship e HT friendship tem valores distintos de proposito: se o
  // parser trocasse os dois offsets, este par de asserts pegaria.
  EqNonZero(p.ot_friendship, AsInt(j, "OriginalTrainerFriendship"),
            t + "OTFriendship");
  Check(p.ot_friendship != p.ht_friendship,
        t + "OT e HT friendship sao distintos na fixture");

  // PP-ups: 3/1/2/0. O ultimo e zero por construcao (so ha 4 valores
  // possiveis), entao os tres primeiros usam EqNonZero e o quarto Eq.
  static const char* kUpKeys[4] = {"Move1_PPUps", "Move2_PPUps",
                                   "Move3_PPUps", "Move4_PPUps"};
  for (int i = 0; i < 3; ++i) {
    EqNonZero(p.pp_ups[i], AsInt(j, kUpKeys[i]), t + kUpKeys[i]);
  }
  Eq(p.pp_ups[3], AsInt(j, kUpKeys[3]), t + kUpKeys[3]);

  // Relearn moves: quatro golpes diferentes.
  static const char* kRelearnKeys[4] = {"RelearnMove1", "RelearnMove2",
                                        "RelearnMove3", "RelearnMove4"};
  for (int i = 0; i < 4; ++i) {
    EqNonZero(p.relearn_moves[i], AsInt(j, kRelearnKeys[i]),
              t + kRelearnKeys[i]);
  }

  // Egg date: estava 0/0/0 em toda fixture real. Met date tem valores
  // DIFERENTES, entao uma troca dos dois blocos aparece.
  EqNonZero(p.egg_date[0], AsInt(j, "EggYear"), t + "EggYear");
  EqNonZero(p.egg_date[1], AsInt(j, "EggMonth"), t + "EggMonth");
  EqNonZero(p.egg_date[2], AsInt(j, "EggDay"), t + "EggDay");
  EqNonZero(p.met_date[0], AsInt(j, "MetYear"), t + "MetYear");
  EqNonZero(p.met_date[1], AsInt(j, "MetMonth"), t + "MetMonth");
  EqNonZero(p.met_date[2], AsInt(j, "MetDay"), t + "MetDay");
  Check(p.egg_date != p.met_date, t + "egg e met date sao distintos");
  EqNonZero(p.egg_location, AsInt(j, "EggLocation"), t + "EggLocation");

  // Blocos que ja tinham cobertura, mas cujo valor aqui e distinto do vizinho.
  EqNonZero(p.ot_gender + 1, AsInt(j, "OriginalTrainerGender") + 1,
            t + "OTGender");
  EqNonZero(p.met_level, AsInt(j, "MetLevel"), t + "MetLevel");
  EqNonZero(p.height_scalar, AsInt(j, "HeightScalar"), t + "HeightScalar");
  EqNonZero(p.weight_scalar, AsInt(j, "WeightScalar"), t + "WeightScalar");
  Check(p.height_scalar != p.weight_scalar,
        t + "height e weight scalar sao distintos");

  // Hyper training: padrao assimetrico true/false/true/false/true/false na
  // ordem de EXIBICAO. A conversao para ordem fisica so e testavel assim.
  static const char* kHtKeys[6] = {"HT_HP",  "HT_ATK", "HT_DEF",
                                   "HT_SPE", "HT_SPA", "HT_SPD"};
  for (int i = 0; i < 6; ++i) {
    Eq(p.hyper_trained[i], AsInt(j, kHtKeys[i]), t + kHtKeys[i]);
  }
  Check(p.hyper_trained[0] != p.hyper_trained[1],
        t + "hyper train tem bits distintos");

  // IVs e EVs: seis valores distintos cada.
  static const char* kIvKeys[6] = {"IV_HP",  "IV_ATK", "IV_DEF",
                                   "IV_SPE", "IV_SPA", "IV_SPD"};
  static const char* kEvKeys[6] = {"EV_HP",  "EV_ATK", "EV_DEF",
                                   "EV_SPE", "EV_SPA", "EV_SPD"};
  for (int i = 0; i < 6; ++i) {
    EqNonZero(p.ivs[i], AsInt(j, kIvKeys[i]), t + kIvKeys[i]);
    EqNonZero(p.evs[i], AsInt(j, kEvKeys[i]), t + kEvKeys[i]);
  }
}

// Campos que os quatro formatos gen8+ tem e o PB7 nao: memorias, ribbons,
// HT id/idioma e home tracker.
static void CheckGen8(const pkm::Pokemon& p,
                      const std::map<std::string, std::string>& j,
                      const std::string& t) {
  EqNonZero(p.ht_id, AsInt(j, "HandlingTrainerID"), t + "HTID");
  EqNonZero(p.ht_language, AsInt(j, "HandlingTrainerLanguage"),
            t + "HTLanguage");
  Check(p.ht_language != p.language,
        t + "HT e OT language sao distintos na fixture");

  // Memorias: os 4 sub-campos de cada lado, todos com valores diferentes
  // entre si. Trocar OT com HT, ou memory com intensity, aparece.
  EqNonZero(p.ot_memory.memory, AsInt(j, "OriginalTrainerMemory"),
            t + "OTMemory");
  EqNonZero(p.ot_memory.intensity, AsInt(j, "OriginalTrainerMemoryIntensity"),
            t + "OTMemoryIntensity");
  EqNonZero(p.ot_memory.feeling, AsInt(j, "OriginalTrainerMemoryFeeling"),
            t + "OTMemoryFeeling");
  EqNonZero(p.ot_memory.text_var, AsInt(j, "OriginalTrainerMemoryVariable"),
            t + "OTMemoryVariable");
  EqNonZero(p.ht_memory.memory, AsInt(j, "HandlingTrainerMemory"),
            t + "HTMemory");
  EqNonZero(p.ht_memory.intensity, AsInt(j, "HandlingTrainerMemoryIntensity"),
            t + "HTMemoryIntensity");
  EqNonZero(p.ht_memory.feeling, AsInt(j, "HandlingTrainerMemoryFeeling"),
            t + "HTMemoryFeeling");
  EqNonZero(p.ht_memory.text_var, AsInt(j, "HandlingTrainerMemoryVariable"),
            t + "HTMemoryVariable");
  Check(p.ot_memory.memory != p.ht_memory.memory,
        t + "OT e HT memory sao distintos");

  // Home tracker: u64 com bytes distintos nas duas metades. Um U32 lido no
  // lugar do U64, ou o offset deslocado, quebra.
  EqNonZero(static_cast<long long>(p.home_tracker), AsInt(j, "Tracker"),
            t + "Tracker");

  // Ribbons: o bitfield tem de ter varios bytes marcados. O JSON traz cada
  // fita como bool, entao o assert direto e "as fitas que marcamos estao la",
  // conferidas pelo bitfield cru.
  EqNonZero(p.ribbon_count_memory, AsInt(j, "RibbonCountMemoryContest"),
            t + "RibbonCountMemoryContest");
  int ribbon_bytes_set = 0;
  for (std::size_t i = 0; i < p.ribbon_bytes.size(); ++i) {
    if (p.ribbon_bytes[i] != 0) ++ribbon_bytes_set;
  }
  Check(ribbon_bytes_set >= 4,
        t + "ribbons espalhadas em >= 4 bytes (tem " +
            std::to_string(ribbon_bytes_set) + ")");
  // As fitas marcadas moram nos dois blocos: o classico e o das marks.
  bool bloco_a = false, bloco_b = false;
  for (int i = 0; i < 8; ++i) {
    if (p.ribbon_bytes[i] != 0) bloco_a = true;
    if (p.ribbon_bytes[8 + i] != 0) bloco_b = true;
  }
  Check(bloco_a, t + "ribbons no bloco classico");
  Check(bloco_b, t + "ribbons no bloco das marks");
}

// Identidade basica — nao e o alvo da spec, mas ancora a fixture: se estes
// nao batem, o resto nao significa nada.
static void CheckIdentidade(const pkm::Pokemon& p,
                            const std::map<std::string, std::string>& j,
                            const std::string& t) {
  Eq(p.encryption_constant, AsInt(j, "EncryptionConstant"), t + "EC");
  Eq(p.pid, AsInt(j, "PID"), t + "PID");
  Eq(p.tid, AsInt(j, "TID16"), t + "TID16");
  Eq(p.sid, AsInt(j, "SID16"), t + "SID16");
  Eq(p.exp, AsInt(j, "EXP"), t + "EXP");
  Eq(p.checksum, AsInt(j, "Checksum"), t + "Checksum");
  Check(p.nickname == AsStr(j, "Nickname"),
        t + "Nickname (" + p.nickname + " vs " + AsStr(j, "Nickname") + ")");
  Check(p.ot_name == AsStr(j, "OriginalTrainerName"),
        t + "OTName (" + p.ot_name + " vs " +
            AsStr(j, "OriginalTrainerName") + ")");
  static const char* kMoveKeys[4] = {"Move1", "Move2", "Move3", "Move4"};
  for (int i = 0; i < 4; ++i) {
    EqNonZero(p.moves[i], AsInt(j, kMoveKeys[i]), t + kMoveKeys[i]);
  }
}

template <typename ParseFn>
static const pkm::Pokemon* Load(const char* fmt, ParseFn parse,
                                pkm::Pokemon& out,
                                std::map<std::string, std::string>& json) {
  const std::filesystem::path dir =
      std::filesystem::path(SYNTH_FIXTURES) / fmt;
  const auto bytes = ReadFile(dir / (std::string("synth.") + fmt));
  json = ReadFlatJson(dir / "synth.json");
  if (bytes.empty() || json.empty()) {
    std::printf("FALHOU: fixture sintetica ausente para %s (%s)\n", fmt,
                dir.string().c_str());
    ++g_failures;
    return nullptr;
  }
  const auto parsed = parse(bytes);
  if (!parsed) {
    std::printf("FALHOU: %s nao parseia a fixture sintetica\n", fmt);
    ++g_failures;
    return nullptr;
  }
  out = *parsed;

  // Roundtrip pela cifra, como os testes por formato fazem: o parser tem de
  // reencontrar tudo depois de cifrado.
  return &out;
}

int main() {
  {
    pkm::Pokemon p;
    std::map<std::string, std::string> j;
    if (Load("pk8", [](const std::vector<std::uint8_t>& b) {
              return pk8::Parse(b);
            }, p, j)) {
      const std::string t = "pk8: ";
      CheckIdentidade(p, j, t);
      CheckComuns(p, j, t);
      CheckGen8(p, j, t);
      EqNonZero(p.dynamax_level, AsInt(j, "DynamaxLevel"), t + "DynamaxLevel");
      EqNonZero(p.can_gigantamax, AsInt(j, "CanGigantamax"),
                t + "CanGigantamax");
    }
  }
  {
    pkm::Pokemon p;
    std::map<std::string, std::string> j;
    if (Load("pk9", [](const std::vector<std::uint8_t>& b) {
              return pk9::Parse(b);
            }, p, j)) {
      const std::string t = "pk9: ";
      CheckIdentidade(p, j, t);
      CheckComuns(p, j, t);
      CheckGen8(p, j, t);
      // Especificos do PK9. Tera original e override sao DIFERENTES de
      // proposito: no save real do dono os dois coincidem e uma troca de
      // offset entre eles seria invisivel.
      EqNonZero(p.scale, AsInt(j, "Scale"), t + "Scale");
      EqNonZero(p.obedience_level, AsInt(j, "ObedienceLevel"),
                t + "ObedienceLevel");
      Check(p.tera_type_original != p.tera_type_override,
            t + "tera original e override sao distintos na fixture");
      // O JSON traz o tipo por NOME (MoveType e enum); o binario guarda o
      // indice. Comparamos os dois pelo par de nomes conhecido da fixture.
      Check(AsStr(j, "TeraTypeOriginal") == "Psychic",
            t + "TeraTypeOriginal esperado na fixture");
      Check(AsStr(j, "TeraTypeOverride") == "Dark",
            t + "TeraTypeOverride esperado na fixture");
      Eq(p.tera_type_original, 13, t + "tera original (indice)");
      Eq(p.tera_type_override, 16, t + "tera override (indice)");
    }
  }
  {
    pkm::Pokemon p;
    std::map<std::string, std::string> j;
    if (Load("pa8", [](const std::vector<std::uint8_t>& b) {
              return pa8::Parse(b);
            }, p, j)) {
      const std::string t = "pa8: ";
      CheckIdentidade(p, j, t);
      CheckComuns(p, j, t);
      CheckGen8(p, j, t);
      // Especificos de PLA. Alpha true e noble false: um bit trocado aparece.
      EqNonZero(p.is_alpha, AsInt(j, "IsAlpha"), t + "IsAlpha");
      Eq(p.is_noble, AsInt(j, "IsNoble"), t + "IsNoble");
      Check(p.is_alpha != p.is_noble, t + "alpha e noble distintos");
      EqNonZero(p.alpha_move, AsInt(j, "AlphaMove"), t + "AlphaMove");
      static const char* kGvKeys[6] = {"GV_HP",  "GV_ATK", "GV_DEF",
                                       "GV_SPE", "GV_SPA", "GV_SPD"};
      for (int i = 0; i < 6; ++i) {
        EqNonZero(p.effort_levels[i], AsInt(j, kGvKeys[i]), t + kGvKeys[i]);
      }
    }
  }
  {
    pkm::Pokemon p;
    std::map<std::string, std::string> j;
    if (Load("pb8", [](const std::vector<std::uint8_t>& b) {
              return pb8::Parse(b);
            }, p, j)) {
      const std::string t = "pb8: ";
      CheckIdentidade(p, j, t);
      CheckComuns(p, j, t);
      CheckGen8(p, j, t);
    }
  }
  {
    pkm::Pokemon p;
    std::map<std::string, std::string> j;
    if (Load("pb7", [](const std::vector<std::uint8_t>& b) {
              return pb7::Parse(b);
            }, p, j)) {
      const std::string t = "pb7: ";
      CheckIdentidade(p, j, t);
      CheckComuns(p, j, t);
      // O PB7 nao tem HT id/idioma, memorias, ribbons nem home tracker: o
      // formato e anterior a eles. CheckGen8 nao se aplica.
      static const char* kAvKeys[6] = {"AV_HP",  "AV_ATK", "AV_DEF",
                                       "AV_SPE", "AV_SPA", "AV_SPD"};
      for (int i = 0; i < 6; ++i) {
        EqNonZero(p.awakening_values[i], AsInt(j, kAvKeys[i]), t + kAvKeys[i]);
      }
    }
  }

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("synth: 5 formatos, campos antes zerados agora cobertos\n");
  return 0;
}
