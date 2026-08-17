// Teste do parser PA8 (spec 060) contra os JSONs do PkHeX — mesmo padrao do
// PK9, com os campos exclusivos de PLA: alpha, noble, effort levels (GV_*) e
// alpha move.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "gen3_save.h"
#include "pa8.h"
#include "pkm_crypto.h"

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  }
}

static void Eq(long long ours, long long theirs, const std::string& what) {
  // long long: no Windows `long` tem 32 bits e EC/PID viram falso-negativo.
  if (ours != theirs) {
    std::printf("FALHOU: %s — nosso %lld, PkHeX %lld\n", what.c_str(), ours,
                theirs);
    ++g_failures;
  }
}

static std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
  // filesystem::path, nao string: no Windows o ifstream(string) usa a
  // codepage ANSI e nao acha nomes com caractere fora dela.
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

// O System.Text.Json grava os nao-ASCII como \uXXXX (Ethernatos vira
// Desescapa para UTF-8 antes de comparar — mesmo tratamento que o
// test_pb7 ja fazia (spec 062).
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

static void TestFixture(const std::filesystem::path& pkm) {
  const std::string base = pkm.stem().string();
  std::filesystem::path json = pkm;
  json.replace_extension(".json");
  const std::filesystem::path json_path =
      std::filesystem::path(PKHEX_JSON) / "pa8" / json.filename();
  const auto bytes = ReadFile(pkm);
  const auto j = ReadFlatJson(json_path);
  if (bytes.empty() || j.empty()) {
    std::printf("FALHOU: fixture/json ausente para %s\n", base.c_str());
    ++g_failures;
    return;
  }

  const auto parsed = pa8::Parse(bytes);
  Check(parsed.has_value(), base + ": parseia");
  if (!parsed) return;
  const pkm::Pokemon& p = *parsed;
  const std::string t = base + ": ";

  // Os dois tamanhos do formato precisam aparecer no conjunto de fixtures.
  Check(bytes.size() == pa8::kStoredSize || bytes.size() == pa8::kPartySize,
        t + "tamanho 360 ou 376 (tem " + std::to_string(bytes.size()) + ")");

  Eq(p.encryption_constant, AsInt(j, "EncryptionConstant"), t + "EC");
  Eq(p.checksum, AsInt(j, "Checksum"), t + "Checksum");
  Eq(p.sanity, AsInt(j, "Sanity"), t + "Sanity");
  // Ao contrario do PK9, o PA8 guarda o National Dex direto (TD-01): a
  // comparacao contra o JSON vale sem tabela de conversao.
  Eq(p.species, AsInt(j, "Species"), t + "Species");
  Eq(p.form, AsInt(j, "Form"), t + "Form");
  Eq(p.held_item, AsInt(j, "HeldItem"), t + "HeldItem");
  Eq(p.tid, AsInt(j, "TID16"), t + "TID16");
  Eq(p.sid, AsInt(j, "SID16"), t + "SID16");
  Eq(p.exp, AsInt(j, "EXP"), t + "EXP");
  Eq(p.ability, AsInt(j, "Ability"), t + "Ability");
  Eq(p.ability_number, AsInt(j, "AbilityNumber"), t + "AbilityNumber");
  Eq(p.pid, AsInt(j, "PID"), t + "PID");
  // Genero: o PkHeX devolve 2 (sem genero) derivando da tabela da especie; o
  // binario so tem o bit. Comparamos apenas quando ha genero de verdade.
  if (AsInt(j, "Gender") != 2) {
    Eq(p.gender, AsInt(j, "Gender"), t + "Gender");
  }
  Eq(p.language, AsInt(j, "Language"), t + "Language");
  Eq(p.is_egg, AsInt(j, "IsEgg"), t + "IsEgg");
  Eq(p.is_nicknamed, AsInt(j, "IsNicknamed"), t + "IsNicknamed");
  Eq(p.fateful_encounter, AsInt(j, "FatefulEncounter"), t + "Fateful");
  Eq(p.markings, AsInt(j, "MarkingValue"), t + "MarkingValue");
  Eq(p.status_condition, AsInt(j, "Status_Condition"), t + "StatusCondition");
  Eq(p.height_scalar, AsInt(j, "HeightScalar"), t + "HeightScalar");
  Eq(p.weight_scalar, AsInt(j, "WeightScalar"), t + "WeightScalar");
  Eq(p.scale, AsInt(j, "Scale"), t + "Scale");
  Eq(p.favorite, AsInt(j, "IsFavorite"), t + "IsFavorite");
  Eq(p.form_argument, AsInt(j, "FormArgument"), t + "FormArgument");
  Eq(p.home_tracker, AsInt(j, "Tracker"), t + "Tracker");

  // Exclusivos de PLA.
  Eq(p.is_alpha, AsInt(j, "IsAlpha"), t + "IsAlpha");
  Eq(p.is_noble, AsInt(j, "IsNoble"), t + "IsNoble");
  Eq(p.alpha_move, AsInt(j, "AlphaMove"), t + "AlphaMove");
  static const char* kGvKeys[6] = {"GV_HP",  "GV_ATK", "GV_DEF",
                                   "GV_SPE", "GV_SPA", "GV_SPD"};
  for (int i = 0; i < 6; ++i) {
    Eq(p.effort_levels[i], AsInt(j, kGvKeys[i]), t + kGvKeys[i]);
  }

  // Campos que o PA8 nao tem (TD-03): tem de sair zerados.
  Eq(p.tera_type_original, 0, t + "TeraTypeOriginal zerado");
  Eq(p.obedience_level, 0, t + "ObedienceLevel zerado");
  Eq(p.dynamax_level, AsInt(j, "DynamaxLevel"), t + "DynamaxLevel");
  Eq(p.can_gigantamax, AsInt(j, "CanGigantamax"), t + "CanGigantamax");
  Eq(p.sociability, AsInt(j, "Sociability"), t + "Sociability");
  Check(p.move_record_flags.empty(), t + "sem TM flags");

  Check(pokehome::gen3::NatureName(p.nature) == AsStr(j, "Nature"),
        t + "Nature (" + pokehome::gen3::NatureName(p.nature) + " vs " +
            AsStr(j, "Nature") + ")");
  Check(pokehome::gen3::NatureName(p.stat_nature) == AsStr(j, "StatNature"),
        t + "StatNature");

  Check(p.nickname == AsStr(j, "Nickname"),
        t + "Nickname (" + p.nickname + " vs " + AsStr(j, "Nickname") + ")");
  Check(p.ot_name == AsStr(j, "OriginalTrainerName"),
        t + "OT (" + p.ot_name + " vs " + AsStr(j, "OriginalTrainerName") + ")");
  Check(p.ht_name == AsStr(j, "HandlingTrainerName"), t + "HT");

  static const char* kIvKeys[6] = {"IV_HP",  "IV_ATK", "IV_DEF",
                                   "IV_SPE", "IV_SPA", "IV_SPD"};
  static const char* kEvKeys[6] = {"EV_HP",  "EV_ATK", "EV_DEF",
                                   "EV_SPE", "EV_SPA", "EV_SPD"};
  for (int i = 0; i < 6; ++i) {
    Eq(p.ivs[i], AsInt(j, kIvKeys[i]), t + kIvKeys[i]);
    Eq(p.evs[i], AsInt(j, kEvKeys[i]), t + kEvKeys[i]);
  }

  const long long ht_flags = AsInt(j, "HyperTrainFlags");
  static constexpr int kHtBitForPhysical[6] = {0, 1, 2, 5, 3, 4};
  for (int i = 0; i < 6; ++i) {
    Eq(p.hyper_trained[i], (ht_flags >> kHtBitForPhysical[i]) & 1,
       t + "HyperTrain[" + std::to_string(i) + "]");
  }

  static const char* kMoveKeys[4] = {"Move1", "Move2", "Move3", "Move4"};
  static const char* kPpKeys[4] = {"Move1_PP", "Move2_PP", "Move3_PP",
                                   "Move4_PP"};
  static const char* kUpKeys[4] = {"Move1_PPUps", "Move2_PPUps",
                                   "Move3_PPUps", "Move4_PPUps"};
  static const char* kRelearnKeys[4] = {"RelearnMove1", "RelearnMove2",
                                        "RelearnMove3", "RelearnMove4"};
  for (int i = 0; i < 4; ++i) {
    Eq(p.moves[i], AsInt(j, kMoveKeys[i]), t + kMoveKeys[i]);
    Eq(p.pp[i], AsInt(j, kPpKeys[i]), t + kPpKeys[i]);
    Eq(p.pp_ups[i], AsInt(j, kUpKeys[i]), t + kUpKeys[i]);
    Eq(p.relearn_moves[i], AsInt(j, kRelearnKeys[i]), t + kRelearnKeys[i]);
  }

  Eq(p.met_level, AsInt(j, "MetLevel"), t + "MetLevel");
  Eq(p.met_location, AsInt(j, "MetLocation"), t + "MetLocation");
  Eq(p.egg_location, AsInt(j, "EggLocation"), t + "EggLocation");
  Eq(p.ball, AsInt(j, "Ball"), t + "Ball");
  Eq(p.met_date[0], AsInt(j, "MetYear"), t + "MetYear");
  Eq(p.met_date[1], AsInt(j, "MetMonth"), t + "MetMonth");
  Eq(p.met_date[2], AsInt(j, "MetDay"), t + "MetDay");
  Eq(p.egg_date[0], AsInt(j, "EggYear"), t + "EggYear");
  Eq(p.egg_date[1], AsInt(j, "EggMonth"), t + "EggMonth");
  Eq(p.egg_date[2], AsInt(j, "EggDay"), t + "EggDay");
  Eq(p.ot_gender, AsInt(j, "OriginalTrainerGender"), t + "OTGender");
  Eq(p.ot_friendship, AsInt(j, "OriginalTrainerFriendship"),
     t + "OTFriendship");
  Eq(p.current_handler, AsInt(j, "CurrentHandler"), t + "CurrentHandler");
  Eq(p.ht_friendship, AsInt(j, "HandlingTrainerFriendship"),
     t + "HTFriendship");
  Eq(p.ht_gender, AsInt(j, "HandlingTrainerGender"), t + "HTGender");
  Eq(p.ht_language, AsInt(j, "HandlingTrainerLanguage"), t + "HTLanguage");
  Eq(p.ht_id, AsInt(j, "HandlingTrainerID"), t + "HTID");
  Eq(p.ot_memory.memory, AsInt(j, "OriginalTrainerMemory"), t + "OTMemory");
  Eq(p.ot_memory.feeling, AsInt(j, "OriginalTrainerMemoryFeeling"),
     t + "OTMemoryFeeling");
  Eq(p.ot_memory.intensity, AsInt(j, "OriginalTrainerMemoryIntensity"),
     t + "OTMemoryIntensity");
  Eq(p.ht_memory.memory, AsInt(j, "HandlingTrainerMemory"), t + "HTMemory");
  Eq(p.ht_memory.feeling, AsInt(j, "HandlingTrainerMemoryFeeling"),
     t + "HTMemoryFeeling");
  Eq(p.pokerus, AsInt(j, "PokerusState"), t + "PokerusState");
  Eq(p.contest_sheen, AsInt(j, "ContestSheen"), t + "ContestSheen");

  Eq(pkm::IsShiny(p), AsInt(j, "IsShiny"), t + "IsShiny");

  // Roundtrip: cifrado pela NOSSA Encrypt, o parser tem de reencontrar tudo —
  // e o caminho que o save real exercita.
  // Cifrar so o que chegou DECIFRADO. As fixtures `party-*` da spec 066 saem
  // do save real ja cifradas; cifra-las de novo daria lixo.
  std::vector<std::uint8_t> enc = bytes;
  if (pkc::IsDecrypted(enc.data(), pkc::kBlockPA8)) {
    pkc::Encrypt(enc.data(), enc.size(), pkc::kBlockPA8);
    Check(enc != bytes, t + "Encrypt de fato alterou os bytes");
  }
  const auto again = pa8::Parse(enc);
  Check(again.has_value() && again->species == p.species &&
            again->nickname == p.nickname && again->pid == p.pid &&
            again->is_alpha == p.is_alpha &&
            again->alpha_move == p.alpha_move &&
            again->effort_levels == p.effort_levels,
        t + "parse do buffer cifrado bate");
}

int main() {
  // Varre o diretorio em vez de listar nomes a mao.
  const std::filesystem::path dir(std::string(PKM_FIXTURES) + "pa8");
  int count = 0;
  bool saw_360 = false, saw_376 = false;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".pa8") continue;
    const auto sz = std::filesystem::file_size(entry.path());
    if (sz == pa8::kStoredSize) saw_360 = true;
    if (sz == pa8::kPartySize) saw_376 = true;
    TestFixture(entry.path());
    ++count;
  }
  Check(count >= 7,
        "achou as 7 fixtures (achou " + std::to_string(count) + ")");
  // O formato tem dois tamanhos; ambos precisam estar cobertos.
  Check(saw_360, "cobriu o tamanho stored (360)");
  Check(saw_376, "cobriu o tamanho party (376)");

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("pa8: %d fixtures, tudo verde\n", count);
  return 0;
}
