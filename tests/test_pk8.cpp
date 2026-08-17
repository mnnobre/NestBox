// Teste do parser PK8 (spec 058) contra os JSONs do PkHeX (spec 056) —
// o criterio de integracao F06←F01/F02 da descoberta: cada campo do modelo
// preenchido pelo nosso parser tem de bater com o que o PkHeX le do MESMO
// arquivo.
//
// O JSON e flat ("Chave": valor por linha), entao o leitor aqui e um split
// de linhas — sem dependencia de parser JSON.
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gen3_save.h"
#include "pk8.h"
#include "pkm_crypto.h"

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  }
}

static std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

// "Chave": valor  ->  mapa string->string (aspas e virgula removidas).
static std::map<std::string, std::string> ReadFlatJson(
    const std::string& path) {
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
    // apara espacos, aspas e virgula final
    while (!value.empty() && (value.front() == ' ')) value.erase(0, 1);
    while (!value.empty() &&
           (value.back() == ',' || value.back() == ' ')) value.pop_back();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    out[line.substr(q1 + 1, q2 - q1 - 1)] = value;
  }
  return out;
}

// long long, nao long: no Windows long tem 32 bits e EC/PID (u32) estouram.
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
// "Éthernatos"). Desescapa para UTF-8 antes de comparar — mesmo
// tratamento que o test_pb7 ja fazia (spec 062).
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

static void Eq(long ours, long theirs, const std::string& what) {
  if (ours != theirs) {
    std::printf("FALHOU: %s — nosso %ld, PkHeX %ld\n", what.c_str(), ours,
                theirs);
    ++g_failures;
  }
}

static void TestFixture(const std::string& base) {
  const std::string pkm_path =
      std::string(PKM_FIXTURES) + "pk8/" + base + ".pk8";
  const std::string json_path =
      std::string(PKHEX_JSON) + "pk8/" + base + ".json";
  const auto bytes = ReadFile(pkm_path);
  const auto j = ReadFlatJson(json_path);
  if (bytes.empty() || j.empty()) {
    std::printf("FALHOU: fixture/json ausente para %s\n", base.c_str());
    ++g_failures;
    return;
  }

  const auto parsed = pk8::Parse(bytes);
  Check(parsed.has_value(), base + ": parseia");
  if (!parsed) return;
  const pkm::Pokemon& p = *parsed;

  const std::string t = base + ": ";
  Eq(p.encryption_constant, AsInt(j, "EncryptionConstant"), t + "EC");
  Eq(p.checksum, AsInt(j, "Checksum"), t + "Checksum");
  Eq(p.species, AsInt(j, "Species"), t + "Species");
  Eq(p.form, AsInt(j, "Form"), t + "Form");
  Eq(p.held_item, AsInt(j, "HeldItem"), t + "HeldItem");
  Eq(p.tid, AsInt(j, "TID16"), t + "TID16");
  Eq(p.sid, AsInt(j, "SID16"), t + "SID16");
  Eq(p.exp, AsInt(j, "EXP"), t + "EXP");
  Eq(p.ability, AsInt(j, "Ability"), t + "Ability");
  Eq(p.ability_number, AsInt(j, "AbilityNumber"), t + "AbilityNumber");
  Eq(p.pid, AsInt(j, "PID"), t + "PID");
  Eq(p.gender, AsInt(j, "Gender"), t + "Gender");
  Eq(p.language, AsInt(j, "Language"), t + "Language");
  Eq(p.is_egg, AsInt(j, "IsEgg"), t + "IsEgg");
  Eq(p.is_nicknamed, AsInt(j, "IsNicknamed"), t + "IsNicknamed");
  Eq(p.fateful_encounter, AsInt(j, "FatefulEncounter"), t + "Fateful");
  Eq(p.favorite, AsInt(j, "IsFavorite"), t + "IsFavorite");
  Eq(p.can_gigantamax, AsInt(j, "CanGigantamax"), t + "CanGigantamax");
  Eq(p.markings, AsInt(j, "MarkingValue"), t + "MarkingValue");
  Eq(p.dynamax_level, AsInt(j, "DynamaxLevel"), t + "DynamaxLevel");
  Eq(p.sociability, AsInt(j, "Sociability"), t + "Sociability");
  Eq(p.height_scalar, AsInt(j, "HeightScalar"), t + "HeightScalar");
  Eq(p.weight_scalar, AsInt(j, "WeightScalar"), t + "WeightScalar");
  Eq(p.fullness, AsInt(j, "Fullness"), t + "Fullness");
  Eq(p.enjoyment, AsInt(j, "Enjoyment"), t + "Enjoyment");
  Eq(p.pokerus, AsInt(j, "PokerusStrain") * 16 + AsInt(j, "PokerusDays"),
     t + "Pokerus");

  // Natureza comparada POR NOME (TD-02): o JSON traz o enum como string.
  Check(pokehome::gen3::NatureName(p.nature) == AsStr(j, "Nature"),
        t + "Nature (" + pokehome::gen3::NatureName(p.nature) + " vs " +
            AsStr(j, "Nature") + ")");
  Check(pokehome::gen3::NatureName(p.stat_nature) == AsStr(j, "StatNature"),
        t + "StatNature");

  // Strings.
  Check(p.nickname == AsStr(j, "Nickname"),
        t + "Nickname (" + p.nickname + " vs " + AsStr(j, "Nickname") + ")");
  Check(p.ot_name == AsStr(j, "OriginalTrainerName"), t + "OT");
  Check(p.ht_name == AsStr(j, "HandlingTrainerName"), t + "HT");

  // IVs/EVs (fisica: HP,Atk,Def,Spe,SpA,SpD — JSON usa nomes).
  static const char* kIvKeys[6] = {"IV_HP",  "IV_ATK", "IV_DEF",
                                   "IV_SPE", "IV_SPA", "IV_SPD"};
  static const char* kEvKeys[6] = {"EV_HP",  "EV_ATK", "EV_DEF",
                                   "EV_SPE", "EV_SPA", "EV_SPD"};
  for (int i = 0; i < 6; ++i) {
    Eq(p.ivs[i], AsInt(j, kIvKeys[i]), t + kIvKeys[i]);
    Eq(p.evs[i], AsInt(j, kEvKeys[i]), t + kEvKeys[i]);
  }

  // Hyper training: o JSON tem a flag crua (ordem de exibicao). Refaz o
  // mapa do TD-01 no sentido inverso para conferir bit a bit.
  const long long ht_flags = AsInt(j, "HyperTrainFlags");
  static constexpr int kHtBitForPhysical[6] = {0, 1, 2, 5, 3, 4};
  for (int i = 0; i < 6; ++i) {
    Eq(p.hyper_trained[i], (ht_flags >> kHtBitForPhysical[i]) & 1,
       t + "HyperTrain[" + std::to_string(i) + "]");
  }

  // Golpes.
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

  // Origem.
  Eq(p.met_level, AsInt(j, "MetLevel"), t + "MetLevel");
  Eq(p.met_location, AsInt(j, "MetLocation"), t + "MetLocation");
  Eq(p.egg_location, AsInt(j, "EggLocation"), t + "EggLocation");
  Eq(p.ball, AsInt(j, "Ball"), t + "Ball");
  Eq(p.met_date[0], AsInt(j, "MetYear"), t + "MetYear");
  Eq(p.met_date[1], AsInt(j, "MetMonth"), t + "MetMonth");
  Eq(p.met_date[2], AsInt(j, "MetDay"), t + "MetDay");
  Eq(p.ot_gender, AsInt(j, "OriginalTrainerGender"), t + "OTGender");
  Eq(p.ot_friendship, AsInt(j, "OriginalTrainerFriendship"),
     t + "OTFriendship");
  Eq(p.current_handler, AsInt(j, "CurrentHandler"), t + "CurrentHandler");
  Eq(p.ht_friendship, AsInt(j, "HandlingTrainerFriendship"),
     t + "HTFriendship");
  Eq(p.ot_memory.memory, AsInt(j, "OriginalTrainerMemory"), t + "OTMemory");
  Eq(p.ot_memory.intensity, AsInt(j, "OriginalTrainerMemoryIntensity"),
     t + "OTMemoryIntensity");
  Eq(p.ot_memory.feeling, AsInt(j, "OriginalTrainerMemoryFeeling"),
     t + "OTMemoryFeeling");

  // Shiny derivado (funcao, nao campo) contra o PkHeX.
  Eq(pkm::IsShiny(p), AsInt(j, "IsShiny"), t + "IsShiny");

  // Roundtrip de leitura: cifra com a NOSSA Encrypt e parseia de novo — o
  // caminho que o save real vai usar.
  std::vector<std::uint8_t> enc = bytes;
  // Cifrar so o que chegou DECIFRADO — as fixtures `party-*` (spec 066) ja
  // vem cifradas do save real.
  if (pkc::IsDecrypted(enc.data(), pkc::kBlockPK8)) {
    pkc::Encrypt(enc.data(), enc.size(), pkc::kBlockPK8);
  }
  const auto again = pk8::Parse(enc);
  Check(again.has_value() && again->species == p.species &&
            again->nickname == p.nickname && again->pid == p.pid,
        t + "parse do buffer cifrado bate");
}

int main() {
  TestFixture("bouffalant-shiny");
  TestFixture("cinderace-mint-nature");
  TestFixture("glastrier");
  TestFixture("mienshao");
  TestFixture("mr-mime-galar");
  TestFixture("rillaboom");
  TestFixture("toxtricity-garbage-bytes");

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("pk8: tudo verde\n");
  return 0;
}
