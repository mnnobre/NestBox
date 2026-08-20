// Teste do parser PB7 (spec 062) contra os JSONs do PkHeX. Mesmo padrao do
// PK9, com os campos exclusivos do LGPE: AVs e a forma nos bits do byte 29.
//
// As fixtures saem do save REAL de Let's Go Eevee do dono (spec 062) — nao
// ha .pb7 no OpenHome. Nomes tem acento (o save e frances), por isso toda
// leitura passa por std::filesystem::path.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "gen3_save.h"
#include "gen3_transfer.h"
#include "pb7.h"
#include "pkm_crypto.h"
#include "save_writer.h"
#include "species_facts.h"

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

static std::string AsStr(const std::map<std::string, std::string>& j,
                         const std::string& key) {
  const auto it = j.find(key);
  return it == j.end() ? "<ausente>" : it->second;
}

// O JSON guarda o nome escapado em \uXXXX (acentos do save frances). Traduz
// para UTF-8 para comparar com o que o parser devolve.
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

static void TestFixture(const std::filesystem::path& pkm) {
  const std::string base = pkm.stem().string();
  std::filesystem::path json = pkm;
  json.replace_extension(".json");
  const std::filesystem::path json_path =
      std::filesystem::path(PKHEX_JSON) / "pb7" / json.filename();
  const auto bytes = ReadFile(pkm);
  const auto j = ReadFlatJson(json_path);
  if (bytes.empty() || j.empty()) {
    std::printf("FALHOU: fixture/json ausente para %s\n", base.c_str());
    ++g_failures;
    return;
  }

  const auto parsed = pb7::Parse(bytes);
  Check(parsed.has_value(), base + ": parseia");
  if (!parsed) return;
  const pkm::Pokemon& p = *parsed;
  const std::string t = base + ": ";

  Eq(p.encryption_constant, AsInt(j, "EncryptionConstant"), t + "EC");
  Eq(p.checksum, AsInt(j, "Checksum"), t + "Checksum");
  Eq(p.sanity, AsInt(j, "Sanity"), t + "Sanity");
  // TD-03: no LGPE o binario ja guarda o National Dex (a dex e so a de
  // Kanto + Meltan/Melmetal), entao a comparacao direta vale — ao
  // contrario do PK9, onde o campo e indice interno.
  Eq(p.species, AsInt(j, "Species"), t + "Species");
  Eq(p.form, AsInt(j, "Form"), t + "Form");
  Eq(p.held_item, AsInt(j, "HeldItem"), t + "HeldItem");
  Eq(p.tid, AsInt(j, "TID16"), t + "TID16");
  Eq(p.sid, AsInt(j, "SID16"), t + "SID16");
  Eq(p.exp, AsInt(j, "EXP"), t + "EXP");
  Eq(p.ability, AsInt(j, "Ability"), t + "Ability");
  Eq(p.ability_number, AsInt(j, "AbilityNumber"), t + "AbilityNumber");
  Eq(p.pid, AsInt(j, "PID"), t + "PID");
  Eq(p.favorite, AsInt(j, "IsFavorite"), t + "IsFavorite");
  // Genero: o PkHeX devolve 2 (sem genero) derivando da tabela da especie;
  // o binario so tem os bits. Comparamos quando ha genero de verdade.
  if (AsInt(j, "Gender") != 2) {
    Eq(p.gender, AsInt(j, "Gender"), t + "Gender");
  }
  Eq(p.language, AsInt(j, "Language"), t + "Language");
  Eq(p.is_egg, AsInt(j, "IsEgg"), t + "IsEgg");
  Eq(p.is_nicknamed, AsInt(j, "IsNicknamed"), t + "IsNicknamed");
  Eq(p.fateful_encounter, AsInt(j, "FatefulEncounter"), t + "Fateful");
  Eq(p.markings, AsInt(j, "MarkingValue"), t + "MarkingValue");
  Eq(p.form_argument, AsInt(j, "FormArgument"), t + "FormArgument");
  Eq(p.height_scalar, AsInt(j, "HeightScalar"), t + "HeightScalar");
  Eq(p.weight_scalar, AsInt(j, "WeightScalar"), t + "WeightScalar");
  Eq(p.status_condition, AsInt(j, "Status_Condition"), t + "StatusCondition");
  Eq(p.fullness, AsInt(j, "Fullness"), t + "Fullness");
  Eq(p.enjoyment, AsInt(j, "Enjoyment"), t + "Enjoyment");

  Check(pokehome::gen3::NatureName(p.nature) == AsStr(j, "Nature"),
        t + "Nature (" + pokehome::gen3::NatureName(p.nature) + " vs " +
            AsStr(j, "Nature") + ")");

  Check(p.nickname == Unescape(AsStr(j, "Nickname")),
        t + "Nickname (" + p.nickname + " vs " +
            Unescape(AsStr(j, "Nickname")) + ")");
  Check(p.ot_name == Unescape(AsStr(j, "OriginalTrainerName")),
        t + "OT (" + p.ot_name + " vs " +
            Unescape(AsStr(j, "OriginalTrainerName")) + ")");
  Check(p.ht_name == Unescape(AsStr(j, "HandlingTrainerName")),
        t + "HT (" + p.ht_name + " vs " +
            Unescape(AsStr(j, "HandlingTrainerName")) + ")");

  static const char* kIvKeys[6] = {"IV_HP",  "IV_ATK", "IV_DEF",
                                   "IV_SPE", "IV_SPA", "IV_SPD"};
  static const char* kEvKeys[6] = {"EV_HP",  "EV_ATK", "EV_DEF",
                                   "EV_SPE", "EV_SPA", "EV_SPD"};
  // Exclusivo do LGPE: awakening values, na mesma ordem fisica.
  static const char* kAvKeys[6] = {"AV_HP",  "AV_ATK", "AV_DEF",
                                   "AV_SPE", "AV_SPA", "AV_SPD"};
  for (int i = 0; i < 6; ++i) {
    Eq(p.ivs[i], AsInt(j, kIvKeys[i]), t + kIvKeys[i]);
    Eq(p.evs[i], AsInt(j, kEvKeys[i]), t + kEvKeys[i]);
    Eq(p.awakening_values[i], AsInt(j, kAvKeys[i]), t + kAvKeys[i]);
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
  Eq(p.ht_gender, AsInt(j, "HandlingTrainerGender"), t + "HTGender");
  Eq(p.ht_friendship, AsInt(j, "HandlingTrainerFriendship"),
     t + "HTFriendship");
  Eq(p.pokerus, (AsInt(j, "PokerusStrain") << 4) | AsInt(j, "PokerusDays"),
     t + "Pokerus");

  Eq(pkm::IsShiny(p), AsInt(j, "IsShiny"), t + "IsShiny");

  // Roundtrip da cifra de bloco 0x38 — este teste e o primeiro a exercitar
  // esse caminho (spec 054 so teve fixtures 0x50 e 0x58).
  //
  // As fixtures vem CIFRADAS do PkHeX (EncryptedBoxData), entao o buffer
  // decifrado e `p.raw`: cifra-lo tem de reproduzir o arquivo byte a byte.
  std::vector<std::uint8_t> enc = p.raw;
  pkc::Encrypt(enc.data(), enc.size(), pkc::kBlockPB7);
  Check(enc == bytes, t + "Encrypt(raw) reproduz o arquivo original");

  const auto again = pb7::Parse(enc);
  Check(again.has_value() && again->species == p.species &&
            again->nickname == p.nickname && again->pid == p.pid &&
            again->awakening_values == p.awakening_values,
        t + "parse do buffer cifrado bate");
}


// --- Cauda de party do LGPE (spec 129) ------------------------------------
//
// Fecha o TD da spec 113: no LGPE box e party tem o MESMO tamanho (260), o
// `FillPartyTail16` do save_writer nunca dispara, e o convertido chegava com
// nivel 0, HP 0, stats 0 e CP 0.
//
// Os numeros esperados sao do PkHeX, medidos com `tools/pkhex-pb7cp` (ver o
// evidence-log da spec 129). Este teste os congela: se a formula mudar, ele
// fica vermelho.

static std::uint16_t R16(const std::vector<std::uint8_t>& b, std::size_t o) {
  return static_cast<std::uint16_t>(b[o] | b[o + 1] << 8);
}

// Monta um modelo CONVERTIDO (raw vazio) no nivel pedido.
static pkm::Pokemon Convertido(std::uint16_t dex, std::uint8_t level,
                               std::uint8_t nature, std::uint8_t iv,
                               std::uint8_t av) {
  pkm::Pokemon p;
  p.format = pkm::Format::kPB7;
  p.species = dex;
  p.nature = nature;
  p.stat_nature = nature;
  for (int i = 0; i < 6; ++i) {
    p.ivs[i] = iv;
    p.awakening_values[i] = av;
    p.evs[i] = 252;  // LGPE ignora EV: se entrar na conta, o teste acusa
  }
  // Exp do nivel exato pela curva da especie.
  for (std::uint32_t e = 0;; ++e) {
    if (pokehome::species::LevelFromExp(dex, e) == level) { p.exp = e; break; }
    if (e > 2000000) break;
  }
  p.hp_current = 0;              // o estado "morto" que a spec conserta
  p.status_condition = 0x88;     // e um status sujo, que tem de ser zerado
  return p;
}

static void TestCaudaParty() {
  const std::string t = "[129 cauda LGPE] ";

  struct Caso {
    std::uint16_t dex;
    std::uint8_t level, nature, iv, av;
    int hp, atk, def, spe, spa, spd, cp;
  };
  // Oraculo PkHeX (tools/pkhex-pb7cp). Ordem fisica: HP,Atk,Def,Spe,SpA,SpD.
  const Caso casos[] = {
      // Pikachu lvl 50, IV 31, sem AV, natureza Hardy (0 = neutra)
      {25, 50, 0, 31, 0, 110, 75, 60, 110, 70, 70, 1485},
      // Pikachu lvl 50 com AV 100 nos seis: AV soma direto no stat
      {25, 50, 0, 31, 100, 210, 175, 160, 210, 170, 170, 3885},
      // Blastoise lvl 46, IV 20, Adamant (3): +Atk / -SpA
      {9, 46, 3, 20, 0, 137, 99, 106, 85, 82, 110, 1708},
      // Mewtwo lvl 70, IV 31, Timid (10): +Spe / -Atk
      {150, 70, 10, 31, 0, 250, 162, 152, 228, 242, 152, 4981},
      // Magikarp lvl 5, IV 0 — o piso da tabela
      {129, 5, 0, 0, 0, 17, 6, 10, 13, 6, 7, 17},
      // Bulbasaur lvl 12, IV 31
      {1, 12, 0, 31, 0, 36, 20, 20, 19, 24, 24, 102},
      // Eevee lvl 33, IV 31
      {133, 33, 0, 31, 0, 89, 51, 48, 51, 44, 58, 675},
  };

  for (const Caso& c : casos) {
    const pkm::Pokemon p = Convertido(c.dex, c.level, c.nature, c.iv, c.av);
    std::vector<std::uint8_t> buf = pb7::Write(p);
    Check(buf.size() == pb7::kStoredSize, t + "tamanho do registro");
    // O Write devolve DECIFRADO, entao os offsets sao diretos.
    const std::string q = t + "dex " + std::to_string(c.dex) + " ";

    Check(buf[0xEC] == c.level, q + "nivel em 0xEC");
    Check(R16(buf, 0xF2) == c.hp, q + "HP maximo em 0xF2");
    Check(R16(buf, 0xF0) == c.hp, q + "HP atual CHEIO em 0xF0");
    Check(R16(buf, 0xF4) == c.atk, q + "Atk em 0xF4");
    Check(R16(buf, 0xF6) == c.def, q + "Def em 0xF6");
    Check(R16(buf, 0xF8) == c.spe, q + "Spe em 0xF8");
    Check(R16(buf, 0xFA) == c.spa, q + "SpA em 0xFA");
    Check(R16(buf, 0xFC) == c.spd, q + "SpD em 0xFC");
    Check(R16(buf, 0xFE) == c.cp, q + "CP em 0xFE");
    // Status zerado: o convertido nao chega dormindo.
    Check(buf[0xE8] == 0 && buf[0xE9] == 0 && buf[0xEA] == 0 &&
              buf[0xEB] == 0,
          q + "status zerado em 0xE8");
    // O registro tem de continuar legivel pelo proprio parser.
    const auto again = pb7::Parse(buf);
    Check(again.has_value(), q + "reparse do convertido");
    if (again) {
      Check(again->hp_current == c.hp, q + "hp_current no reparse");
      Check(again->status_condition == 0, q + "status no reparse");
    }
  }

  // Teto de 10000 no CP: Mewtwo nivel 100 com AV 200 nos seis estoura.
  {
    const pkm::Pokemon p = Convertido(150, 100, 0, 31, 200);
    const std::vector<std::uint8_t> buf = pb7::Write(p);
    Check(R16(buf, 0xFE) == 10000, t + "CP limitado a 10000");
  }

  // A TRAVA: registro NATIVO (raw preenchido) NAO pode ser tocado. Se o
  // preenchimento vazar para ele, os stats do save real seriam reescritos.
  {
    pkm::Pokemon nativo = Convertido(25, 50, 0, 31, 0);
    std::vector<std::uint8_t> base = pb7::Write(nativo);  // convertido
    const auto lido = pb7::Parse(base);
    Check(lido.has_value(), t + "le o registro para virar nativo");
    if (lido) {
      // Agora com raw: mexe o nivel na cauda a mao e confirma que o Write
      // preserva — e o comportamento do nativo.
      pkm::Pokemon com_raw = *lido;
      com_raw.raw[0xEC] = 99;
      com_raw.raw[0xFE] = 0x77;
      const std::vector<std::uint8_t> out = pb7::Write(com_raw);
      Check(out[0xEC] == 99, t + "NATIVO: nivel do raw preservado");
      Check(out[0xFE] == 0x77, t + "NATIVO: CP do raw preservado");
    }
  }
}


// --- Deposito de verdade num save limpo de LGPE (spec 129) ----------------
//
// O teste acima exercita o `pb7::Write` isolado. Este passa pelo caminho
// REAL: Load do save limpo -> poe o convertido num slot -> Save -> Load de
// novo -> confere o que o registro carrega. E o que prova que a cauda
// sobrevive a cifra e ao checksum do save.
//
// SOMENTE MEMORIA: o arquivo de tests/saves-limpos NUNCA e reescrito.
static void TestDepositoSaveLimpo() {
  const std::string t = "[129 deposito LGPE] ";
  const std::filesystem::path sav_path =
      std::filesystem::path(std::string(CLEAN_SAVES)) / "lgpe" /
      "savedata.bin";
  std::ifstream in(sav_path, std::ios::binary);
  if (!in) {
    Check(false, t + "abriu o save limpo de LGPE");
    return;
  }
  const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
  auto sd = savew::Load(file);
  Check(sd.has_value(), t + "Load reconheceu o save");
  if (!sd) return;
  Check(sd->game == savew::Game::kLGPE, t + "e um save de LGPE");

  // Um slot VAZIO, para nao mexer em nenhum Pokemon do save limpo.
  std::size_t livre = sd->box.size();
  for (std::size_t i = 0; i < sd->box.size(); ++i) {
    if (!sd->box[i].present) { livre = i; break; }
  }
  Check(livre < sd->box.size(), t + "achou um slot vazio");
  if (livre >= sd->box.size()) return;

  // Blastoise nivel 46 convertido (o Pokemon do bug da spec 113), com o
  // estado "morto" que a spec 129 tem de consertar.
  const pkm::Pokemon novo = Convertido(9, 46, 3, 20, 0);
  Check(novo.raw.empty(), t + "o modelo e CONVERTIDO (raw vazio)");
  // `Set` e o UNICO caminho que marca o slot como sujo — mexer em `box` na
  // mao nao chega ao arquivo, porque o Save() so reescreve os sujos.
  const std::size_t bi = livre / sd->slots_per_box;
  const std::size_t si = livre % sd->slots_per_box;
  Check(sd->Set(bi, si, novo), t + "Set no slot vazio");

  const std::vector<std::uint8_t> saida = savew::Save(*sd);
  Check(saida.size() == file.size(), t + "o save mantem o tamanho");

  auto relido = savew::Load(saida);
  Check(relido.has_value(), t + "Load do save gravado");
  if (!relido) return;
  Check(relido->box[livre].present, t + "o slot tem Pokemon");
  const pkm::Pokemon& m = relido->box[livre].mon;
  Check(m.species == 9, t + "especie sobreviveu");
  // Os numeros sao o oraculo PkHeX (mesmo caso da tabela acima).
  Check(m.hp_current == 137, t + "HP atual CHEIO depois do ciclo");
  Check(m.status_condition == 0, t + "status zerado depois do ciclo");
  Check(m.raw.size() == pb7::kStoredSize, t + "o raw relido tem 260 bytes");
  if (m.raw.size() == pb7::kStoredSize) {
    Check(m.raw[0xEC] == 46, t + "nivel 46 na cauda");
    Check(R16(m.raw, 0xF2) == 137, t + "HP maximo na cauda");
    Check(R16(m.raw, 0xF4) == 99, t + "Atk na cauda");
    Check(R16(m.raw, 0xFE) == 1708, t + "CP na cauda");
  }

  // O RESTO do save nao pode ter mudado: so o slot escolhido difere.
  std::size_t mexidos = 0;
  for (std::size_t i = 0; i < sd->box.size(); ++i) {
    if (i == livre) continue;
    const auto& a = relido->box[i];
    if (a.present != (i < sd->box.size() && sd->box[i].present)) ++mexidos;
  }
  Check(mexidos == 0, t + "nenhum outro slot mudou de estado");

  // Grava a COPIA para o veredito externo do PkHeX (tools/pkhex-verify e
  // tools/pkhex-hp). Vai para o diretorio de build, nunca sobre a origem.
  std::ofstream out("lgpe-deposito-129.bin", std::ios::binary);
  out.write(reinterpret_cast<const char*>(saida.data()),
            static_cast<std::streamsize>(saida.size()));
  out.close();
  std::printf("  (copia para o PkHeX: lgpe-deposito-129.bin, slot %zu)\n",
              livre);
}

// Spec 136: deposito de um gen3 SUBIDO DE VERDADE pelo ConvertUp, para o
// veredito externo do PkHeX sobre a regra de AV do LGPE.
//
// O TestDepositoSaveLimpo acima usa `Convertido()`, um modelo sintetico — ele
// nao passa pelo gen3_transfer, que e onde a regra de AV mora. Este aqui sobe
// um Pikachu gen3 com met_level 5 e nivel 46: exatamente o caso que a spec
// 129 mediu como ILEGAL ("HP AV should be greater than 41").
//
// SOMENTE MEMORIA: o save de tests/saves-limpos NUNCA e reescrito.
static void TestDepositoGen3SubidoAv() {
  const std::string t = "[136 AV gen3->LGPE] ";
  const std::filesystem::path sav_path =
      std::filesystem::path(std::string(CLEAN_SAVES)) / "lgpe" /
      "savedata.bin";
  std::ifstream in(sav_path, std::ios::binary);
  if (!in) {
    Check(false, t + "abriu o save limpo de LGPE");
    return;
  }
  const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
  auto sd = savew::Load(file);
  if (!sd) {
    Check(false, t + "Load reconheceu o save");
    return;
  }

  // Pikachu gen3: experiencia 100000 -> nivel 46; origins com met_level 5.
  pokehome::gen3::FullRecord r;
  r.personality = 0x00010002;
  r.ot_id = 0x00010001;
  pokehome::gen3::EncodeGen3String("PIKACHU", r.nickname_raw,
                                   sizeof(r.nickname_raw));
  r.language = 2;
  r.flags = 0x02;
  pokehome::gen3::EncodeGen3String("ASH", r.ot_name_raw, sizeof(r.ot_name_raw));
  r.species = 25;
  r.experience = 100000;
  r.friendship = 180;
  r.moves[0] = 85;
  r.moves[1] = 86;
  r.moves[2] = 98;
  r.moves[3] = 21;
  for (int i = 0; i < 4; ++i) r.pp[i] = 15;
  const std::uint32_t ivs[6] = {31, 30, 29, 28, 27, 26};
  for (int i = 0; i < 6; ++i) r.iv32 |= ivs[i] << (i * 5);
  r.origins = static_cast<std::uint16_t>(5 | (4u << 7) | (12u << 11));

  std::uint8_t raw[80];
  pokehome::gen3::EncodeFullRecord(r, raw);
  auto up = pokehome::g3x::ConvertUp(raw, pkm::Format::kPB7,
                                     pokehome::moveset::Game::kLgpe, nullptr);
  Check(up.has_value(), t + "o gen3 sobe para pb7");
  if (!up) return;
  Check(up->met_level == 5, t + "met_level 5 veio do gen3");
  Check(up->awakening_values[0] >= 41, t + "AV_HP preenchido ate o piso 41");

  std::size_t livre = sd->box.size();
  for (std::size_t i = 0; i < sd->box.size(); ++i) {
    if (!sd->box[i].present) { livre = i; break; }
  }
  if (livre >= sd->box.size()) {
    Check(false, t + "achou um slot vazio");
    return;
  }
  Check(sd->Set(livre / sd->slots_per_box, livre % sd->slots_per_box, *up),
        t + "Set no slot vazio");

  const std::vector<std::uint8_t> saida = savew::Save(*sd);
  auto relido = savew::Load(saida);
  Check(relido.has_value() && relido->box[livre].present,
        t + "o slot tem Pokemon depois do ciclo");
  if (relido && relido->box[livre].present) {
    Check(relido->box[livre].mon.awakening_values[0] >= 41,
          t + "o AV sobreviveu a cifra e ao checksum do save");
  }

  // NOTA (medido): este Pikachu sintetico tem OUTRAS invalidades (moveset,
  // correlacao de PID, peso, CP) que fazem o PkHeX casar outro encounter, e
  // com isso o check de AV nem chega a rodar no save. Entao o veredito
  // EXTERNO da regra de AV nao sai daqui — sai da sonda `tools/pkhex-ribbon-av`
  // secao (C), num modelo limpo, onde o PkHeX responde:
  //   AV_HP= 0 -> "Invalid: HP AV should be greater than 41."
  //   AV_HP=41 -> SEM motivo de AV
  // O que este deposito prova e o outro elo: que o AV preenchido SOBREVIVE a
  // cifra e ao checksum do save real.
  std::ofstream out("lgpe-av-136.bin", std::ios::binary);
  out.write(reinterpret_cast<const char*>(saida.data()),
            static_cast<std::streamsize>(saida.size()));
  out.close();
  std::printf("  (copia para o PkHeX: lgpe-av-136.bin, slot %zu)\n", livre);
}

int main() {
  // Varredura de diretorio: os nomes vem do save frances do dono e tem
  // acento — listar a mao deixaria arquivos de fora.
  const std::filesystem::path dir(std::string(PKM_FIXTURES) + "pb7");
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".pb7") continue;
    TestFixture(entry.path());
    ++count;
  }
  Check(count >= 90, "achou as fixtures do save real (achou " +
                         std::to_string(count) + ")");

  TestCaudaParty();
  TestDepositoSaveLimpo();
  TestDepositoGen3SubidoAv();

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("pb7: %d fixtures, tudo verde\n", count);
  return 0;
}
