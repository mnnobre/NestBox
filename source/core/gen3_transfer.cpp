#include "gen3_transfer.h"

#include <algorithm>
#include <cctype>

#include "move_pp.h"
#include "pkm_convert.h"
#include "species_facts.h"

namespace pokehome::g3x {
namespace {

// Letra do Unown a partir do PID — formula do gen3: dois bits de cada byte.
std::uint8_t UnownForm(std::uint32_t pid) {
  const std::uint32_t v = ((pid >> 24) & 0x3) << 6 | ((pid >> 16) & 0x3) << 4 |
                          ((pid >> 8) & 0x3) << 2 | (pid & 0x3);
  return static_cast<std::uint8_t>(v % 28);
}

std::string UpperAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

std::optional<pkm::Pokemon> ConvertUp(const std::uint8_t raw[80],
                                      pkm::Format destino,
                                      moveset::Game dest_ms,
                                      moveset::Memory* memory) {
  const auto full = gen3::DecodeFullRecord(raw);
  if (!full) return std::nullopt;

  // Bad egg (bit 0 das flags) e ovo (bit 30 do iv32) nao sobem (TD-D4).
  if (full->flags & 0x01) return std::nullopt;
  if ((full->iv32 >> 30) & 1) return std::nullopt;

  const int dex_i = gen3::NationalDex(full->species);
  if (dex_i <= 0) return std::nullopt;
  const auto dex = static_cast<std::uint16_t>(dex_i);

  const std::uint16_t alvo = pkm::SpeciesForFormat(dex, destino);
  if (alvo == 0 || destino == pkm::Format::kNone) return std::nullopt;

  // O parser de tela resolve o que ja sabe (nomes, sexo) — reaproveitado em
  // vez de reimplementado.
  const gen3::BoxPokemon view = gen3::ParseBoxPokemonRecord(raw);

  pkm::Pokemon p;
  p.format = destino;
  p.species = alvo;
  p.form = dex == 201 ? UnownForm(full->personality) : 0;  // TD-02

  // --- Identidade (TD-D1: PID/TID/SID intactos) -------------------------
  p.encryption_constant = full->personality;
  p.pid = full->personality;
  p.tid = static_cast<std::uint16_t>(full->ot_id & 0xFFFF);
  p.sid = static_cast<std::uint16_t>(full->ot_id >> 16);
  p.nature = static_cast<std::uint8_t>(full->personality % 25);
  p.stat_nature = p.nature;
  p.gender = gen3::Gender(view);
  p.language = full->language;
  // No gen3 o nome default e o da especie em CAIXA ALTA. Nos formatos
  // modernos, um nao-apelidado carrega o nome da especie na grafia MODERNA
  // ("Pikachu") — e o que o PkHeX exige e o que a transferencia oficial faz
  // (spec 115, guiado pelo pkhex-verify).
  p.is_nicknamed =
      UpperAscii(view.nickname) != UpperAscii(gen3::SpeciesName(full->species));
  p.nickname = p.is_nicknamed ? view.nickname : gen3::SpeciesNameByDex(dex);
  p.ot_name = view.ot_name;
  p.ot_gender = static_cast<std::uint8_t>((full->origins >> 15) & 1);
  p.ot_friendship = full->friendship;

  // --- Habilidade por slot (regra do dono) ------------------------------
  const auto& personal = gen3::Personal(dex);
  p.ability = personal.ability(full->iv32 >> 31 ? 1 : 0);
  p.ability_number = (full->iv32 >> 31) & 1 ? 2 : 1;

  // --- Origem ------------------------------------------------------------
  p.origin_game = static_cast<std::uint8_t>((full->origins >> 7) & 0x0F);
  p.ball = static_cast<std::uint8_t>((full->origins >> 11) & 0x0F);
  if (p.ball == 0) p.ball = 4;  // Poke Ball: origem desconhecida nao fica sem bola
  p.met_level = static_cast<std::uint8_t>(full->origins & 0x7F);
  // 30001 = "Poke Transfer", o local que o PkHeX exige para origem antiga em
  // formato moderno (spec 115; a spec 109 gravava 0 e o verify acusava).
  p.met_location = 30001;
  // Data de chegada: constante valida (TD-02 da spec 115) — o BDSP recusa
  // data zerada. {ano-2000, mes, dia}.
  p.met_date = {24, 1, 1};
  // "Sem egg location" no PB8 e 0xFFFF; nos demais formatos e 0.
  p.egg_location = destino == pkm::Format::kPB8 ? 0xFFFF : 0;
  // 0 no campo de ribbon afixada significa "Kalos Champion"; nenhuma e 0xFF.
  p.affixed_ribbon = 0xFF;
  p.exp = full->experience;
  p.held_item = 0;  // TD-D2: item nao viaja

  // --- Stats -------------------------------------------------------------
  for (int i = 0; i < 6; ++i) {
    p.ivs[i] = static_cast<std::uint8_t>((full->iv32 >> (i * 5)) & 0x1F);
    // Clamp 252: o gen3 aceita ate 255 por stat, os modernos nao.
    p.evs[i] = std::min<std::uint8_t>(full->evs[i], 252);
    p.contest_stats[i] = full->contest[i];
  }
  p.pokerus = full->pokerus;
  // Marcacoes: 4 bits gen3 viram os 4 primeiros pares de bits modernos
  // (valor 1 = marcado).
  for (int i = 0; i < 4; ++i) {
    if (full->markings & (1u << i)) p.markings |= 1u << (2 * i);
  }

  // Tera ao entrar no pk9: derivado do tipo primario, como o Convert entre
  // formatos modernos ja faz (conferido contra o PkHeX na spec 069).
  if (destino == pkm::Format::kPK9) {
    const std::uint8_t t1 = species::Type1(dex);
    p.tera_type_original = t1 == 0xFF ? 0 : t1;
    p.tera_type_override = p.tera_type_original;
  }

  // --- Tracker + moveset (G10/G11) ---------------------------------------
  // Deterministico: o mesmo registro gen3 gera o mesmo tracker em conversoes
  // repetidas — e o que faz a memoria de moveset reencontra-lo na volta.
  moveset::AssignTracker(p);

  const std::uint8_t level = species::LevelFromExp(dex, full->experience);
  if (memory) {
    // Memoriza o moveset gen3 ORIGINAL sob a familia kGen3, para a descida
    // restaurar (regra do dono: "quando volta pra box restauramos").
    pkm::Pokemon original = p;
    for (int i = 0; i < 4; ++i) {
      original.moves[i] = full->moves[i];
      original.pp_ups[i] =
          static_cast<std::uint8_t>((full->pp_bonuses >> (i * 2)) & 0x3);
    }
    memory->Remember(original, moveset::Game::kGen3);
    // Entrando no destino: restaura se ja esteve la; senao reset por nivel.
    memory->ApplyOnEntry(p, dest_ms, level);
  } else {
    moveset::ResetMovesByLevel(p, dest_ms, level);
  }
  // PP corrente: a BASE do golpe (tabela do PKHeX, spec 115) — acima disso o
  // verify acusa "PP above the amount allowed".
  for (int i = 0; i < 4; ++i) {
    p.pp[i] = p.moves[i] ? movepp::Modern(p.moves[i]) : 0;
  }

  return p;
}

bool ConvertDown(const pkm::Pokemon& p, learnset::Game dest_learnset,
                 moveset::Game src_ms, moveset::Memory* memory,
                 std::uint8_t origem_fallback, std::uint8_t out[80]) {
  if (p.empty() || p.is_egg) return false;

  const std::uint16_t dex = pkm::NationalDex(p);
  const std::uint16_t interno = gen3::InternalFromDex(dex);
  if (interno == 0) return false;

  // Forma: o gen3 so representa a do Unown (rederiva do PID) e a do Deoxys
  // (que e do jogo). Qualquer outra forma != 0 viraria o Pokemon base em
  // silencio — recusa.
  if (p.form != 0 && dex != 201 && dex != 386) return false;

  gen3::FullRecord r;
  r.personality = p.pid;
  r.ot_id = static_cast<std::uint32_t>(p.tid) |
            (static_cast<std::uint32_t>(p.sid) << 16);
  // Nao-apelidado volta ao default gen3: nome da especie em CAIXA ALTA
  // (spec 115 — a subida poe a grafia moderna, a descida desfaz).
  const std::string nick =
      p.is_nicknamed ? p.nickname : UpperAscii(gen3::SpeciesName(interno));
  gen3::EncodeGen3String(nick, r.nickname_raw, sizeof(r.nickname_raw));
  r.language = (p.language >= 1 && p.language <= 7) ? p.language : 2;
  r.flags = 0x02;  // tem especie
  gen3::EncodeGen3String(p.ot_name, r.ot_name_raw, sizeof(r.ot_name_raw));
  // Marcacoes: par de bits moderno != 0 vira o bit gen3.
  for (int i = 0; i < 4; ++i) {
    if ((p.markings >> (2 * i)) & 0x3) r.markings |= 1u << i;
  }

  r.species = interno;
  r.held_item = 0;  // TD-D2: item nao viaja
  r.experience = p.exp;
  r.friendship = p.ot_friendship;

  for (int i = 0; i < 6; ++i) r.evs[i] = p.evs[i];
  for (int i = 0; i < 5; ++i) r.contest[i] = p.contest_stats[i];
  r.contest[5] = p.contest_sheen;
  r.pokerus = p.pokerus;

  // IV32: 6x5 bits + egg (nao — ovo ja foi recusado) + bit de habilidade.
  for (int i = 0; i < 6; ++i) {
    r.iv32 |= static_cast<std::uint32_t>(p.ivs[i] & 0x1F) << (i * 5);
  }
  // Slot -> bit: 2 vira bit 1; 1 e hidden (4) caem no bit 0 (TD-D3).
  if (p.ability_number == 2) r.iv32 |= 1u << 31;

  // Origens: met_level | origem<<7 | bola<<11 | sexo do OT<<15.
  const std::uint8_t origem =
      (p.origin_game >= 1 && p.origin_game <= 5) ? p.origin_game
                                                 : origem_fallback;
  const std::uint8_t bola = (p.ball >= 1 && p.ball <= 12) ? p.ball : 4;
  r.origins = static_cast<std::uint16_t>(
      (p.met_level & 0x7F) | (static_cast<std::uint16_t>(origem & 0x0F) << 7) |
      (static_cast<std::uint16_t>(bola & 0x0F) << 11) |
      (static_cast<std::uint16_t>(p.ot_gender & 1) << 15));
  r.met_location = 0;  // TD-01 da spec 109, mesmo criterio

  // --- Moveset (a promessa do dono: "quando volta, restauramos") ----------
  const std::uint8_t level = species::LevelFromExp(dex, p.exp);
  const std::uint64_t tracker =
      p.home_tracker ? p.home_tracker : moveset::DeriveTracker(p);
  const moveset::Snapshot* snap =
      memory ? memory->Recall(tracker, moveset::Game::kGen3) : nullptr;
  if (memory) {
    // O moveset moderno atual fica memorizado no jogo de ORIGEM, para a
    // proxima subida restaurar (G11 na outra direcao).
    pkm::Pokemon com_tracker = p;
    com_tracker.home_tracker = tracker;
    memory->Remember(com_tracker, src_ms);
  }
  if (snap) {
    for (int i = 0; i < 4; ++i) {
      r.moves[i] = snap->moves[i];
      r.pp_bonuses |= static_cast<std::uint8_t>((snap->pp_ups[i] & 0x3)
                                                << (i * 2));
    }
  } else {
    std::uint16_t mv[4] = {0, 0, 0, 0};
    learnset::MovesAtLevel(dest_learnset, dex, 0, level, mv);
    if (mv[0] == 0) return false;  // especie sem learnset no alvo: sem golpe
    for (int i = 0; i < 4; ++i) r.moves[i] = mv[i];
  }
  // PP corrente: a base gen3 do golpe (tabela do PKHeX, spec 115).
  for (int i = 0; i < 4; ++i) {
    r.pp[i] = r.moves[i] ? movepp::Gen3(r.moves[i]) : 0;
  }

  gen3::EncodeFullRecord(r, out);
  return true;
}

}  // namespace pokehome::g3x
