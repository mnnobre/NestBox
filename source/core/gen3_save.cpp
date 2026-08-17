#include "gen3_save.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <numeric>

#include "gen3_abilities.h"
// Tabela de nomes por dex nacional (1..1025). Mora em namespace za por razoes
// historicas — foi criada para o parser do Legends Z-A —, mas o dado e a dex
// nacional, que serve a todo o app (spec 035).
#include "gen9_species.h"
#include "gen3_moves.h"
#include "gen3_natures.h"
#include "gen3_personal.h"
#include "gen3_species.h"

namespace pokehome::gen3 {
namespace {

std::uint16_t ReadU16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t ReadU32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

// O PC buffer comeca na secao 5 e se estende pelas secoes seguintes. Cada secao
// contribui com kSectionData bytes uteis; o buffer e a concatenacao deles na
// ordem logica dos ids.
constexpr std::uint16_t kFirstPcSection = 5;

// Dentro do PC buffer: 4 bytes de "caixa atual", depois 420 registros.
constexpr std::size_t kBoxDataOffset = 0x0004;

// As 24 permutacoes de 4 blocos, em ordem lexicografica — a mesma ordem que o
// gen3 usa para indexar por (personality % 24). Gerada em vez de transcrita:
// uma tabela de 96 numeros digitada a mao e superficie de erro sem ganho.
std::array<std::array<int, 4>, 24> BuildBlockOrders() {
  std::array<std::array<int, 4>, 24> orders{};
  std::array<int, 4> perm = {0, 1, 2, 3};
  for (auto& order : orders) {
    order = perm;
    std::next_permutation(perm.begin(), perm.end());
  }
  return orders;
}

const std::array<std::array<int, 4>, 24>& BlockOrders() {
  static const auto kOrders = BuildBlockOrders();
  return kOrders;
}

// Tabela de caracteres do gen3 (ocidental). Cobre o intervalo usado por
// nicknames; entradas desconhecidas viram '?'. Terminador e 0xFF.
char DecodeChar(std::uint8_t c) {
  if (c == 0x00) return ' ';
  if (c >= 0xA1 && c <= 0xAA) return static_cast<char>('0' + (c - 0xA1));
  if (c >= 0xBB && c <= 0xD4) return static_cast<char>('A' + (c - 0xBB));
  if (c >= 0xD5 && c <= 0xEE) return static_cast<char>('a' + (c - 0xD5));
  if (c == 0xAE) return '-';
  if (c == 0xAD) return '.';
  if (c == 0xBA) return '/';
  return '?';
}

std::string DecodeString(const std::uint8_t* p, std::size_t max_len) {
  std::string out;
  for (std::size_t i = 0; i < max_len; ++i) {
    if (p[i] == 0xFF) break;
    out.push_back(DecodeChar(p[i]));
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

// Le um slot e devolve os dados, ou nullopt se os indices forem invalidos.
std::optional<Slot> ReadSlot(const std::vector<std::uint8_t>& file,
                             std::size_t slot_base) {
  if (slot_base + kSlotSize > file.size()) return std::nullopt;

  Slot slot;
  slot.sections.reserve(kSectionCount);

  std::array<bool, kSectionCount> seen{};
  bool all_valid = true;

  for (std::size_t i = 0; i < kSectionCount; ++i) {
    const std::size_t off = slot_base + i * kSectionSize;
    const std::uint8_t* sec = file.data() + off;

    if (ReadU32(sec + kOffSignature) != kSignature) {
      all_valid = false;
      continue;
    }

    Section s;
    s.id = ReadU16(sec + kOffSectionId);
    s.stored_checksum = ReadU16(sec + kOffChecksum);
    s.computed_checksum = ComputeChecksum(sec, kSectionData);
    s.save_index = ReadU32(sec + kOffSaveIndex);
    s.file_offset = off;
    slot.sections.push_back(s);

    if (s.id < kSectionCount) {
      if (seen[s.id]) all_valid = false;  // id duplicado
      seen[s.id] = true;
    } else {
      all_valid = false;
    }

    slot.save_index = s.save_index;
  }

  slot.complete =
      all_valid && std::all_of(seen.begin(), seen.end(), [](bool v) { return v; });
  return slot;
}

// Remonta o PC buffer concatenando as secoes de id >= 5 na ordem logica.
std::vector<std::uint8_t> BuildPcBufferFromSlot(
    const std::vector<std::uint8_t>& file, const Slot& slot) {
  std::vector<std::uint8_t> pc;
  pc.reserve((kSectionCount - kFirstPcSection) * kSectionData);

  for (std::uint16_t want = kFirstPcSection; want < kSectionCount; ++want) {
    const auto it = std::find_if(
        slot.sections.begin(), slot.sections.end(),
        [want](const Section& s) { return s.id == want; });
    if (it == slot.sections.end()) return {};  // secao faltando

    const std::uint8_t* src = file.data() + it->file_offset;
    pc.insert(pc.end(), src, src + kSectionData);
  }
  return pc;
}

// Descriptografa os 48 bytes de substrutura e devolve os 4 blocos ja na ordem
// canonica (Growth, Attacks, EVs, Misc).
std::array<std::uint8_t, 48> DecryptSubstructures(const std::uint8_t* record,
                                                  std::uint32_t personality,
                                                  std::uint32_t ot_id) {
  const std::uint32_t key = personality ^ ot_id;

  std::array<std::uint8_t, 48> decrypted{};
  for (std::size_t i = 0; i < 48; i += 4) {
    const std::uint32_t word = ReadU32(record + 32 + i) ^ key;
    decrypted[i + 0] = static_cast<std::uint8_t>(word & 0xFF);
    decrypted[i + 1] = static_cast<std::uint8_t>((word >> 8) & 0xFF);
    decrypted[i + 2] = static_cast<std::uint8_t>((word >> 16) & 0xFF);
    decrypted[i + 3] = static_cast<std::uint8_t>((word >> 24) & 0xFF);
  }

  // A ordem fisica dos blocos depende de personality % 24. order[i] diz qual
  // bloco canonico esta na posicao fisica i.
  const auto& order = BlockOrders()[personality % 24];
  std::array<std::uint8_t, 48> canonical{};
  for (std::size_t phys = 0; phys < 4; ++phys) {
    const std::size_t logical = static_cast<std::size_t>(order[phys]);
    std::memcpy(canonical.data() + logical * 12, decrypted.data() + phys * 12, 12);
  }
  return canonical;
}

}  // namespace

std::uint16_t ComputeChecksum(const std::uint8_t* data, std::size_t size) {
  std::uint32_t sum = 0;
  for (std::size_t i = 0; i + 3 < size; i += 4) sum += ReadU32(data + i);
  return static_cast<std::uint16_t>((sum & 0xFFFF) + (sum >> 16));
}

std::optional<std::size_t> FindSaveOffset(const std::vector<std::uint8_t>& file) {
  // Procura a signature da primeira secao e deriva a base a partir dela, em vez
  // de assumir offsets conhecidos. Cobre .sav cru (base 0) e containers como
  // SharkPortSave, cujo header desloca tudo.
  //
  // Exige duas signatures consecutivas espacadas por kSectionSize: um valor
  // isolado pode aparecer por acaso dentro dos dados.
  if (file.size() < kSaveSize) return std::nullopt;

  const std::size_t limit = file.size() - kSaveSize;
  for (std::size_t base = 0; base <= limit; ++base) {
    if (ReadU32(file.data() + base + kOffSignature) != kSignature) continue;
    if (base + kSectionSize + kOffSignature + 4 > file.size()) continue;
    if (ReadU32(file.data() + base + kSectionSize + kOffSignature) != kSignature) {
      continue;
    }
    return base;
  }
  return std::nullopt;
}

std::optional<SaveFile> ParseSave(const std::vector<std::uint8_t>& file) {
  const auto base = FindSaveOffset(file);
  if (!base) return std::nullopt;

  SaveFile save;
  save.base_offset = *base;

  auto a = ReadSlot(file, *base);
  auto b = ReadSlot(file, *base + kSlotSize);
  if (!a || !b) return std::nullopt;

  save.slot_a = std::move(*a);
  save.slot_b = std::move(*b);

  // Slot ativo: o de maior save index entre os completos. Um slot incompleto
  // perde independentemente do indice — e o caso de um save interrompido.
  if (save.slot_a.complete && save.slot_b.complete) {
    save.active_slot = (save.slot_b.save_index > save.slot_a.save_index) ? 1 : 0;
  } else if (save.slot_b.complete) {
    save.active_slot = 1;
  } else if (save.slot_a.complete) {
    save.active_slot = 0;
  } else {
    return std::nullopt;  // nenhum slot utilizavel
  }

  return save;
}

const Slot& ActiveSlot(const SaveFile& save) {
  return save.active_slot == 1 ? save.slot_b : save.slot_a;
}

std::vector<std::uint8_t> BuildPcBuffer(const std::vector<std::uint8_t>& file,
                                        const SaveFile& save) {
  return BuildPcBufferFromSlot(file, ActiveSlot(save));
}

// --- Escrita (spec 033) ----------------------------------------------------

bool WriteBoxPokemonTo(std::vector<std::uint8_t>& pc, std::size_t box,
                       std::size_t slot, const std::uint8_t* rec) {
  if (box >= kBoxCount || slot >= kSlotsPerBox) return false;

  // Exige um PC buffer INTEIRO, nao so espaco para este slot. Escrever num
  // buffer pequeno demais para ser um PC buffer significa que o chamador
  // passou outra coisa — e escrever ali corromperia o que quer que seja.
  const std::size_t expected =
      (kSectionCount - kFirstPcSection) * kSectionData;
  if (pc.size() != expected) return false;

  const std::size_t index = box * kSlotsPerBox + slot;
  const std::size_t off = kBoxDataOffset + index * kBoxPokemonSize;
  if (off + kBoxPokemonSize > pc.size()) return false;

  if (rec) {
    std::memcpy(pc.data() + off, rec, kBoxPokemonSize);
  } else {
    // Slot vazio e 80 bytes zerados — e como ParseBoxPokemonRecord detecta
    // vazio (personality e ot_id em zero).
    std::memset(pc.data() + off, 0, kBoxPokemonSize);
  }
  return true;
}

bool ApplyPcBuffer(std::vector<std::uint8_t>& file, const SaveFile& save,
                   const std::vector<std::uint8_t>& pc) {
  const Slot& slot = ActiveSlot(save);

  const std::size_t expected =
      (kSectionCount - kFirstPcSection) * kSectionData;
  if (pc.size() != expected) return false;

  // Localiza todas as secoes ANTES de escrever qualquer byte: se faltar uma,
  // o arquivo nao pode ficar meio escrito.
  std::vector<std::size_t> offsets;
  offsets.reserve(kSectionCount - kFirstPcSection);
  for (std::uint16_t want = kFirstPcSection; want < kSectionCount; ++want) {
    const auto it = std::find_if(
        slot.sections.begin(), slot.sections.end(),
        [want](const Section& s) { return s.id == want; });
    if (it == slot.sections.end()) return false;
    if (it->file_offset + kSectionSize > file.size()) return false;
    offsets.push_back(it->file_offset);
  }

  for (std::size_t i = 0; i < offsets.size(); ++i) {
    std::uint8_t* dst = file.data() + offsets[i];
    std::memcpy(dst, pc.data() + i * kSectionData, kSectionData);

    // Checksum so das secoes tocadas. Recalcular todas mascararia um checksum
    // ja invalido no arquivo de origem (TD-02 da spec 033).
    const std::uint16_t sum = ComputeChecksum(dst, kSectionData);
    dst[kOffChecksum] = static_cast<std::uint8_t>(sum & 0xFF);
    dst[kOffChecksum + 1] = static_cast<std::uint8_t>(sum >> 8);
  }
  return true;
}

std::optional<BoxPokemon> ReadBoxPokemon(const std::vector<std::uint8_t>& file,
                                         const SaveFile& save, std::size_t box,
                                         std::size_t slot) {
  const auto pc = BuildPcBuffer(file, save);
  return ReadBoxPokemonFrom(pc, box, slot);
}

std::optional<BoxPokemon> ReadBoxPokemonFrom(
    const std::vector<std::uint8_t>& pc, std::size_t box, std::size_t slot) {
  if (box >= kBoxCount || slot >= kSlotsPerBox) return std::nullopt;
  if (pc.empty()) return std::nullopt;

  const std::size_t index = box * kSlotsPerBox + slot;
  const std::size_t off = kBoxDataOffset + index * kBoxPokemonSize;
  if (off + kBoxPokemonSize > pc.size()) return std::nullopt;

  return ParseBoxPokemonRecord(pc.data() + off);
}

// A decodificacao de um registro de 80 bytes, isolada do save que o contem.
// Um caminho so: o save e o NestBox passam por aqui (spec 028).
BoxPokemon ParseBoxPokemonRecord(const std::uint8_t* rec) {
  BoxPokemon mon;
  mon.personality = ReadU32(rec + 0x00);
  mon.ot_id = ReadU32(rec + 0x04);

  // Slot vazio: personality zerada. Checar antes de descriptografar evita
  // divisao por zero conceitual no shuffle e leitura de lixo.
  if (mon.personality == 0 && mon.ot_id == 0) {
    mon.species = 0;
    return mon;  // raw fica zerado, que e o certo para slot vazio
  }

  // Guarda os bytes crus ANTES de decodificar. E o que permite ao NestBox
  // armazenar sem perder os campos que este parser descarta (spec 028).
  std::memcpy(mon.raw, rec, kBoxPokemonSize);

  mon.nickname = DecodeString(rec + 0x08, 10);
  // Identidade da barra de status (spec 098): idioma em 0x12, OT em 0x14.
  mon.language = rec[0x12];
  mon.ot_name = DecodeString(rec + 0x14, 7);

  const auto blocks = DecryptSubstructures(rec, mon.personality, mon.ot_id);
  const std::uint8_t* g = blocks.data();        // Growth  (0x20)
  const std::uint8_t* a = blocks.data() + 12;   // Attacks (0x2C)
  const std::uint8_t* e = blocks.data() + 24;   // EVs     (0x38)
  const std::uint8_t* m = blocks.data() + 36;   // Misc    (0x44)

  mon.species = ReadU16(g);
  mon.held_item = ReadU16(g + 2);
  mon.experience = ReadU32(g + 4);
  mon.friendship = g[9];

  for (int i = 0; i < 4; ++i) {
    mon.moves[i] = ReadU16(a + i * 2);
    mon.pp[i] = a[8 + i];
  }

  for (int i = 0; i < 6; ++i) mon.evs[i] = e[i];

  // Origem em 0x46 (offset 2 do bloco Misc): nivel nos bits 0-6.
  const std::uint16_t origins = ReadU16(m + 2);
  mon.met_level = static_cast<std::uint8_t>(origins & 0x7F);
  // Jogo de origem nos bits 7-10 (1=Sapphire 2=Ruby 3=Emerald 4=FR 5=LG),
  // a mesma numeracao que os formatos modernos usam para o gen3 (spec 098).
  mon.origin_game = static_cast<std::uint8_t>((origins >> 7) & 0x0F);

  // IV32 em 0x48 (offset 4): 6 campos de 5 bits, depois ovo e habilidade.
  const std::uint32_t iv32 = ReadU32(m + 4);
  for (int i = 0; i < 6; ++i) {
    mon.ivs[i] = static_cast<std::uint8_t>((iv32 >> (i * 5)) & 0x1F);
  }
  mon.is_egg = ((iv32 >> 30) & 1) != 0;
  mon.ability_bit = static_cast<std::uint8_t>((iv32 >> 31) & 1);

  return mon;
}

std::string NatureName(std::uint8_t nature) {
  if (nature >= kNatureCount) return "???";
  return kNatureNames[nature];
}

std::string MoveName(std::uint16_t move) {
  if (move == 0 || move >= kMoveCount) return "";
  return kMoveNames[move];
}

std::string AbilityName(std::uint8_t id) {
  if (id >= kAbilityCount) return "";
  return kAbilityNames[id];
}

std::string SpeciesNameByDex(int dex) {
  if (dex <= 0 || dex >= za::kGen9SpeciesCount) return "";
  return za::kGen9Species[dex];
}

int MaxKnownDex() { return za::kGen9SpeciesCount - 1; }

PersonalInfo Personal(int national_dex) {
  PersonalInfo info;
  if (national_dex <= 0 || national_dex >= kPersonalCount) return info;

  const std::uint8_t* e = kPersonalData + national_dex * kPersonalEntrySize;
  for (int i = 0; i < 6; ++i) info.base_stats[i] = e[i];
  info.type1 = e[6];
  info.type2 = e[7];
  info.growth_rate = e[19];
  // Offsets 22-23 confirmados contra especies conhecidas: Bulbasaur 65/65
  // (Overgrow), Charmander 66/66 (Blaze), Squirtle 67/67 (Torrent),
  // Pikachu 9/9 (Static). Ver spec 023.
  info.ability1 = e[22];
  info.ability2 = e[23];
  info.gender_ratio = e[16];
  return info;
}

std::uint8_t Gender(const BoxPokemon& mon) {
  // Fonte moderna ja resolveu (ToBoxPokemon, spec 098).
  if (mon.display_gender != 0xFF) return mon.display_gender;
  const std::uint8_t ratio =
      Personal(NationalDex(mon.species)).gender_ratio;
  if (ratio == 255) return 2;
  if (ratio == 254) return 1;
  if (ratio == 0) return 0;
  return (mon.personality & 0xFF) < ratio ? 1 : 0;
}

namespace {

// Experiencia necessaria para atingir um nivel, por curva de crescimento.
// Formulas do jogo; o PKHeX usa tabelas pre-calculadas, aqui calculamos —
// 100 niveis nao justificam 600 constantes.
std::uint32_t ExpForLevel(int n, std::uint8_t growth) {
  if (n <= 1) return 0;
  const double x = n;
  switch (growth) {
    case 0:  // Medium Fast
      return static_cast<std::uint32_t>(x * x * x);
    case 1: {  // Erratic
      if (n < 50) return static_cast<std::uint32_t>(x * x * x * (100 - x) / 50);
      if (n < 68) return static_cast<std::uint32_t>(x * x * x * (150 - x) / 100);
      if (n < 98)
        return static_cast<std::uint32_t>(x * x * x *
                                          ((1911 - 10 * x) / 3) / 500);
      return static_cast<std::uint32_t>(x * x * x * (160 - x) / 100);
    }
    case 2: {  // Fluctuating
      if (n < 15)
        return static_cast<std::uint32_t>(x * x * x * ((x + 1) / 3 + 24) / 50);
      if (n < 36) return static_cast<std::uint32_t>(x * x * x * (x + 14) / 50);
      return static_cast<std::uint32_t>(x * x * x * (x / 2 + 32) / 50);
    }
    case 3:  // Medium Slow
      return static_cast<std::uint32_t>(6.0 * x * x * x / 5 - 15 * x * x +
                                        100 * x - 140);
    case 4:  // Fast
      return static_cast<std::uint32_t>(4.0 * x * x * x / 5);
    case 5:  // Slow
      return static_cast<std::uint32_t>(5.0 * x * x * x / 4);
    default:
      return static_cast<std::uint32_t>(x * x * x);
  }
}

}  // namespace

std::uint8_t LevelFromExp(std::uint32_t exp, std::uint8_t growth_rate) {
  // Busca linear do topo: devolve o maior nivel cuja exigencia cabe na
  // experiencia atual.
  for (int level = 100; level > 1; --level) {
    if (exp >= ExpForLevel(level, growth_rate)) {
      return static_cast<std::uint8_t>(level);
    }
  }
  return 1;
}

BattleStats ComputeStats(const BoxPokemon& mon) {
  BattleStats out;
  if (mon.empty()) return out;

  // Fonte moderna ja trouxe o nivel pronto (spec 082): ela vence, e sai daqui.
  //
  // A checagem NAO pode ser "a especie saiu da tabela do gen3", que foi a
  // primeira tentativa: `mon.species` de uma fonte moderna e NATIONAL DEX, e
  // `NationalDex()` o interpreta como INDICE INTERNO do gen3. O Turtwig (387)
  // reinterpretado assim cai numa especie gen3 com base stats validos, entao
  // o caminho de calculo rodava e devolvia o nivel de outro Pokemon — 6 em vez
  // de 5. Os stats de batalha continuam zerados de proposito: para calcula-los
  // faltaria a tabela de base stats do gen8/gen9, que este projeto nao tem.
  if (mon.display_level != 0) {
    out.level = mon.display_level;
    return out;
  }

  const int dex = NationalDex(mon.species);
  const PersonalInfo info = Personal(dex);
  if (info.base_stats[0] == 0) return out;

  const int level = LevelFromExp(mon.experience, info.growth_rate);
  out.level = static_cast<std::uint8_t>(level);

  // HP tem formula propria; os demais compartilham a mesma e recebem o
  // modificador de natureza.
  out.values[0] = static_cast<std::uint16_t>(
      (2 * info.base_stats[0] + mon.ivs[0] + mon.evs[0] / 4) * level / 100 +
      level + 10);

  // Natureza: cada uma aumenta um stat em 10% e reduz outro. O indice 0-4
  // dentro do grupo diz qual sobe; o resto da divisao, qual desce. Naturezas
  // neutras (n % 6 == 0) nao alteram nada.
  const int nature = mon.nature();
  const int up = nature / 5;    // 0=Atk 1=Def 2=Spe 3=SpA 4=SpD
  const int down = nature % 5;

  for (int i = 1; i < 6; ++i) {
    int value = (2 * info.base_stats[i] + mon.ivs[i] + mon.evs[i] / 4) * level /
                    100 + 5;

    // O indice do stat no save (1=Atk 2=Def 3=Spe 4=SpA 5=SpD) e o da
    // natureza (0=Atk 1=Def 2=Spe 3=SpA 4=SpD) diferem em 1.
    const int nature_index = i - 1;
    if (up != down) {
      if (nature_index == up) value = value * 110 / 100;
      if (nature_index == down) value = value * 90 / 100;
    }
    out.values[i] = static_cast<std::uint16_t>(value);
  }
  return out;
}

std::string TypeName(std::uint8_t type) {
  // Numeracao do gen3, derivada do proprio personal_fr conferindo especies
  // conhecidas (Rattata=0, Mankey=1, Pidgey=0/2, ...). Difere da moderna:
  // Dark e 16 aqui, e Fairy nao existe.
  static const char* kNames[] = {
      "Normal", "Lutador", "Voador",  "Venenoso", "Terrestre", "Pedra",
      "Inseto", "Fantasma", "Metal",  "Fogo",     "Agua",      "Planta",
      "Eletrico", "Psiquico", "Gelo", "Dragao",   "Sombrio",
  };
  constexpr int kCount = sizeof(kNames) / sizeof(kNames[0]);
  if (type >= kCount) return "???";
  return kNames[type];
}

std::optional<TrainerInfo> ReadTrainerInfo(
    const std::vector<std::uint8_t>& file, const SaveFile& save) {
  const Slot& slot = ActiveSlot(save);
  for (const Section& s : slot.sections) {
    if (s.id != 0) continue;
    if (s.file_offset + 0x12 > file.size()) return std::nullopt;
    const std::uint8_t* d = file.data() + s.file_offset;

    TrainerInfo info;
    info.name = DecodeString(d + 0x00, 7);
    // Trainer ID em 0x0A: u32, metade baixa e o numero visivel no jogo.
    info.public_id =
        static_cast<std::uint16_t>(d[0x0A] | (d[0x0B] << 8));
    // Play time em 0x0E: u16 horas + u8 minutos.
    info.hours = static_cast<std::uint16_t>(d[0x0E] | (d[0x0F] << 8));
    info.minutes = d[0x10];
    return info;
  }
  return std::nullopt;
}

std::string SpeciesName(std::uint16_t species) {
  if (species >= kSpeciesCount) return "???";
  return kSpeciesNames[species];
}

std::uint16_t SpeciesTableSize() {
  return static_cast<std::uint16_t>(kSpeciesCount);
}

int NationalDex(std::uint16_t species) {
  if (species >= kSpeciesCount) return 0;
  return kNationalDex[species];
}

}  // namespace pokehome::gen3
