// Teste do parser PB8 (spec 061) contra os JSONs do PkHeX — mesmo padrao do
// PK8/PK9. As fixtures sao Pokemon REAIS extraidos do save de BDSP do dono
// (build/switch-sim/saves/0100000011D90000, aberto somente para leitura).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "gen3_save.h"
#include "pb8.h"
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
  // filesystem::path, nao string: as fixtures vieram de um save FRANCES e os
  // nomes tem acento (Meganium, Crefollet). ifstream(string) no Windows usa a
  // codepage ANSI e nao acha esses arquivos.
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

// O JsonSerializer do .NET escapa nao-ASCII como \uXXXX, e as fixtures vieram
// de um save FRANCES: sem desescapar, "Méganium" do binario nunca bateria com
// "Méganium" do JSON. Devolve UTF-8, que e como o parser entrega.
static std::string Unescape(const std::string& in) {
  std::string out;
  for (std::size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '\\' || i + 1 >= in.size()) {
      out.push_back(in[i]);
      continue;
    }
    const char esc = in[i + 1];
    if (esc == 'u' && i + 5 < in.size()) {
      const unsigned c =
          std::stoul(in.substr(i + 2, 4), nullptr, 16);
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
      // \\ \" \/ e os demais: o proprio caractere seguinte.
      out.push_back(esc);
      ++i;
    }
  }
  return out;
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
      value = Unescape(value.substr(1, value.size() - 2));
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
    // stoll, nao stol: `long` tem 32 bits no Windows e EC/PID estouram.
    return std::stoll(it->second);
  } catch (...) {
    return -999999;
  }
}

static std::string AsStr(const std::map<std::string, std::string>& j,
                         const std::string& key) {
  const auto it = j.find(key);
  return it == j.end() ? "<ausente>" : it->second;
}

static void TestFixture(const std::filesystem::path& pkm) {
  const std::string base = pkm.stem().string();
  std::filesystem::path json = pkm;
  json.replace_extension(".json");
  const std::filesystem::path json_path =
      std::filesystem::path(PKHEX_JSON) / "pb8" / json.filename();
  const auto bytes = ReadFile(pkm);
  const auto j = ReadFlatJson(json_path);
  if (bytes.empty() || j.empty()) {
    std::printf("FALHOU: fixture/json ausente para %s\n", base.c_str());
    ++g_failures;
    return;
  }

  const auto parsed = pb8::Parse(bytes);
  Check(parsed.has_value(), base + ": parseia");
  if (!parsed) return;
  const pkm::Pokemon& p = *parsed;
  const std::string t = base + ": ";

  Eq(p.encryption_constant, AsInt(j, "EncryptionConstant"), t + "EC");
  Eq(p.checksum, AsInt(j, "Checksum"), t + "Checksum");
  // Ao contrario do PK9, o PB8 guarda o National Dex direto no binario —
  // conferido, e por isso comparamos de verdade (TD-02 da spec 061).
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
  Eq(p.height_scalar, AsInt(j, "HeightScalar"), t + "HeightScalar");
  Eq(p.weight_scalar, AsInt(j, "WeightScalar"), t + "WeightScalar");
  Eq(p.sociability, AsInt(j, "Sociability"), t + "Sociability");
  Eq(p.fullness, AsInt(j, "Fullness"), t + "Fullness");
  Eq(p.enjoyment, AsInt(j, "Enjoyment"), t + "Enjoyment");
  Eq(p.form_argument, AsInt(j, "FormArgument"), t + "FormArgument");
  Eq(p.affixed_ribbon, AsInt(j, "AffixedRibbon"), t + "AffixedRibbon");
  Eq(p.home_tracker, AsInt(j, "Tracker"), t + "Tracker");

  // Contest do gen4 — o PB8 os usa de verdade (o PK8 herda mas nao usa).
  Eq(p.contest_stats[0], AsInt(j, "ContestCool"), t + "ContestCool");
  Eq(p.contest_stats[1], AsInt(j, "ContestBeauty"), t + "ContestBeauty");
  Eq(p.contest_stats[2], AsInt(j, "ContestCute"), t + "ContestCute");
  Eq(p.contest_stats[3], AsInt(j, "ContestSmart"), t + "ContestSmart");
  Eq(p.contest_stats[4], AsInt(j, "ContestTough"), t + "ContestTough");
  Eq(p.contest_sheen, AsInt(j, "ContestSheen"), t + "ContestSheen");

  Check(pokehome::gen3::NatureName(p.nature) == AsStr(j, "Nature"),
        t + "Nature (" + pokehome::gen3::NatureName(p.nature) + " vs " +
            AsStr(j, "Nature") + ")");
  Check(pokehome::gen3::NatureName(p.stat_nature) == AsStr(j, "StatNature"),
        t + "StatNature");

  Check(p.nickname == AsStr(j, "Nickname"),
        t + "Nickname (" + p.nickname + " vs " + AsStr(j, "Nickname") + ")");
  Check(p.ot_name == AsStr(j, "OriginalTrainerName"),
        t + "OT (" + p.ot_name + " vs " + AsStr(j, "OriginalTrainerName") +
            ")");
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
  Eq(p.ht_memory.memory, AsInt(j, "HandlingTrainerMemory"), t + "HTMemory");
  Eq(p.ht_memory.feeling, AsInt(j, "HandlingTrainerMemoryFeeling"),
     t + "HTMemoryFeeling");

  Eq(pkm::IsShiny(p), AsInt(j, "IsShiny"), t + "IsShiny");

  // O save real guarda cifrado: o caminho que vamos usar de verdade.
  std::vector<std::uint8_t> enc = bytes;
  // Cifrar so o que chegou DECIFRADO — as fixtures `party-*` (spec 066) ja
  // vem cifradas do save real.
  if (pkc::IsDecrypted(enc.data(), pkc::kBlockPK8)) {
    pkc::Encrypt(enc.data(), enc.size(), pkc::kBlockPK8);
  }
  const auto again = pb8::Parse(enc);
  Check(again.has_value() && again->species == p.species &&
            again->nickname == p.nickname && again->pid == p.pid &&
            again->ot_name == p.ot_name,
        t + "parse do buffer cifrado bate");
}

int main() {
  // Varre o diretorio em vez de listar nomes: os nomes tem acento (save
  // frances) e listar a mao deixaria arquivos de fora.
  const std::filesystem::path dir(std::string(PKM_FIXTURES) + "pb8");
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".pb8") continue;
    TestFixture(entry.path());
    ++count;
  }
  Check(count >= 19,
        "achou as 19 fixtures (achou " + std::to_string(count) + ")");

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("pb8: %d fixtures, tudo verde\n", count);
  return 0;
}
