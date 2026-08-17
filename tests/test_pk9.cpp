// Teste do parser PK9 (spec 059) contra os JSONs do PkHeX — mesmo padrao do
// PK8, com os campos exclusivos do gen9: tera type, scale e obedience.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "gen3_save.h"
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
  if (ours != theirs) {
    std::printf("FALHOU: %s — nosso %lld, PkHeX %lld\n", what.c_str(), ours,
                theirs);
    ++g_failures;
  }
}

static std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
  // filesystem::path, nao string: no Windows o ifstream(string) usa a
  // codepage ANSI e nao acha nomes com caractere fora dela (a estrela do
  // shiny numa das fixtures).
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

// Ordem de tipos do PkHeX; Stellar e o 19 no gen9 (TD-01 da spec 059).
static const char* kTypeNames[] = {
    "Normal", "Fighting", "Flying",  "Poison", "Ground",  "Rock",
    "Bug",    "Ghost",    "Steel",   "Fire",   "Water",   "Grass",
    "Electric", "Psychic", "Ice",    "Dragon", "Dark",    "Fairy",
    "Stellar"};

static std::string TypeName(std::uint8_t t) {
  const int n = static_cast<int>(sizeof(kTypeNames) / sizeof(kTypeNames[0]));
  return t < n ? kTypeNames[t] : ("?" + std::to_string(t));
}

static void TestFixture(const std::filesystem::path& pkm) {
  // Nomes com caracteres especiais (a estrela do shiny): trabalhamos com
  // filesystem::path e so convertemos para exibir.
  const std::string base = pkm.stem().string();
  std::filesystem::path json = pkm;
  json.replace_extension(".json");
  const std::filesystem::path json_path =
      std::filesystem::path(PKHEX_JSON) / "pk9" / json.filename();
  const auto bytes = ReadFile(pkm);
  const auto j = ReadFlatJson(json_path);
  if (bytes.empty() || j.empty()) {
    std::printf("FALHOU: fixture/json ausente para %s\n", base.c_str());
    ++g_failures;
    return;
  }

  const auto parsed = pk9::Parse(bytes);
  Check(parsed.has_value(), base + ": parseia");
  if (!parsed) return;
  const pkm::Pokemon& p = *parsed;
  const std::string t = base + ": ";

  Eq(p.encryption_constant, AsInt(j, "EncryptionConstant"), t + "EC");
  Eq(p.checksum, AsInt(j, "Checksum"), t + "Checksum");
  // `species` guarda o indice INTERNO do gen9 (o que o binario tem); o JSON
  // traz o National Dex, que o PkHeX deriva por tabela. Sem essa tabela a
  // comparacao direta e invalida — so exigimos que o campo foi lido.
  Check(p.species != 0, t + "Species lido (interno " +
                            std::to_string(p.species) + ", dex PkHeX " +
                            std::to_string(AsInt(j, "Species")) + ")");
  Eq(p.form, AsInt(j, "Form"), t + "Form");
  Eq(p.held_item, AsInt(j, "HeldItem"), t + "HeldItem");
  Eq(p.tid, AsInt(j, "TID16"), t + "TID16");
  Eq(p.sid, AsInt(j, "SID16"), t + "SID16");
  Eq(p.exp, AsInt(j, "EXP"), t + "EXP");
  Eq(p.ability, AsInt(j, "Ability"), t + "Ability");
  Eq(p.ability_number, AsInt(j, "AbilityNumber"), t + "AbilityNumber");
  Eq(p.pid, AsInt(j, "PID"), t + "PID");
  // Genero: o PkHeX devolve 2 (sem genero) derivando da tabela da especie;
  // o binario so tem o bit. Comparamos apenas quando ha genero de verdade.
  if (AsInt(j, "Gender") != 2) {
    Eq(p.gender, AsInt(j, "Gender"), t + "Gender");
  }
  Eq(p.language, AsInt(j, "Language"), t + "Language");
  Eq(p.is_egg, AsInt(j, "IsEgg"), t + "IsEgg");
  Eq(p.is_nicknamed, AsInt(j, "IsNicknamed"), t + "IsNicknamed");
  Eq(p.fateful_encounter, AsInt(j, "FatefulEncounter"), t + "Fateful");
  Eq(p.markings, AsInt(j, "MarkingValue"), t + "MarkingValue");

  // Exclusivos do gen9.
  Check(TypeName(p.tera_type_original) == AsStr(j, "TeraTypeOriginal"),
        t + "TeraTypeOriginal (" + TypeName(p.tera_type_original) + " vs " +
            AsStr(j, "TeraTypeOriginal") + ")");
  Eq(p.tera_type_override, AsInt(j, "TeraTypeOverride"), t + "TeraOverride");
  Eq(p.scale, AsInt(j, "Scale"), t + "Scale");
  Eq(p.obedience_level, AsInt(j, "ObedienceLevel"), t + "ObedienceLevel");
  Eq(p.height_scalar, AsInt(j, "HeightScalar"), t + "HeightScalar");
  Eq(p.weight_scalar, AsInt(j, "WeightScalar"), t + "WeightScalar");

  Check(pokehome::gen3::NatureName(p.nature) == AsStr(j, "Nature"),
        t + "Nature");
  Check(pokehome::gen3::NatureName(p.stat_nature) == AsStr(j, "StatNature"),
        t + "StatNature");

  Check(p.nickname == AsStr(j, "Nickname"),
        t + "Nickname (" + p.nickname + " vs " + AsStr(j, "Nickname") + ")");
  Check(p.ot_name == AsStr(j, "OriginalTrainerName"), t + "OT");
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
  Eq(p.ot_gender, AsInt(j, "OriginalTrainerGender"), t + "OTGender");
  Eq(p.ot_friendship, AsInt(j, "OriginalTrainerFriendship"),
     t + "OTFriendship");
  Eq(p.current_handler, AsInt(j, "CurrentHandler"), t + "CurrentHandler");
  Eq(p.ht_friendship, AsInt(j, "HandlingTrainerFriendship"),
     t + "HTFriendship");
  Eq(p.ot_memory.memory, AsInt(j, "OriginalTrainerMemory"), t + "OTMemory");
  Eq(p.ot_memory.feeling, AsInt(j, "OriginalTrainerMemoryFeeling"),
     t + "OTMemoryFeeling");

  Eq(pkm::IsShiny(p), AsInt(j, "IsShiny"), t + "IsShiny");

  // Cifrar so o que chegou DECIFRADO. As fixtures `party-*` da spec 066 saem
  // do save real ja cifradas (`EncryptedPartyData`); cifra-las de novo daria
  // lixo — e o mesmo tropeco registrado no evidence-log da 062.
  std::vector<std::uint8_t> enc = bytes;
  if (pkc::IsDecrypted(enc.data(), pkc::kBlockPK8)) {
    pkc::Encrypt(enc.data(), enc.size(), pkc::kBlockPK8);
  }
  const auto again = pk9::Parse(enc);
  Check(again.has_value() && again->species == p.species &&
            again->nickname == p.nickname && again->pid == p.pid &&
            again->tera_type_original == p.tera_type_original,
        t + "parse do buffer cifrado bate");
}

int main() {
  // Varre o diretorio em vez de listar nomes: as fixtures tem caracteres
  // especiais (estrela do shiny) e listar a mao deixaria arquivos de fora.
  const std::filesystem::path dir(std::string(PKM_FIXTURES) + "pk9");
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".pk9") continue;
    TestFixture(entry.path());
    ++count;
  }
  Check(count >= 16, "achou as 16 fixtures (achou " +
                         std::to_string(count) + ")");

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("pk9: %d fixtures, tudo verde\n", count);
  return 0;
}
