// Subida gen3 -> moderno (spec 109).
//
// O gen3 de origem e SINTETICO e construido pelo EncodeFullRecord da spec 108
// (roundtrip provado la). Os asserts conferem as regras do dono: identidade
// intacta, habilidade por slot, moveset do jogo alvo com o original
// memorizado, EV com clamp, item que nao viaja.
#include <cstdio>
#include <cstring>
#include <string>

#include "gen3_save.h"
#include "gen3_transfer.h"
#include "learnset.h"
#include "moveset_memory.h"
#include "pk9.h"
#include "pkm_convert.h"
#include "pkm_write_util.h"
#include "save_writer.h"
#include "species_facts.h"

namespace g3 = pokehome::gen3;
namespace g3x = pokehome::g3x;
namespace ms = pokehome::moveset;
namespace ls = pokehome::learnset;
namespace sp = pokehome::species;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  } else {
    std::printf("  ok: %s\n", what.c_str());
  }
}

// Um Pikachu de FireRed: especie gen3 25 (dex 25), shiny de proposito
// (xor(PID, OTID) < 8), moveset gen3 classico.
static g3::FullRecord Pikachu() {
  g3::FullRecord r;
  r.personality = 0x00010002;      // xor das metades: 0x0003
  r.ot_id = 0x00010001;            // tid=1 sid=1 -> xor total 3 < 8: shiny
  g3::EncodeGen3String("PIKACHU", r.nickname_raw, sizeof(r.nickname_raw));
  r.language = 2;                  // ENG
  r.flags = 0x02;
  g3::EncodeGen3String("ASH", r.ot_name_raw, sizeof(r.ot_name_raw));
  r.species = 25;
  r.experience = 100000;           // MediumFast -> nivel 46
  r.friendship = 180;
  r.pp_bonuses = 0b00000001;       // 1 PP-up no primeiro golpe
  r.moves[0] = 85;                 // Thunderbolt
  r.moves[1] = 86;                 // Thunder Wave
  r.moves[2] = 98;                 // Quick Attack
  r.moves[3] = 21;                 // Slam
  for (int i = 0; i < 4; ++i) r.pp[i] = 15;
  const std::uint8_t evs[6] = {255, 0, 0, 255, 0, 0};  // acima do teto moderno
  std::memcpy(r.evs, evs, 6);
  const std::uint32_t ivs[6] = {31, 30, 29, 28, 27, 26};
  for (int i = 0; i < 6; ++i) r.iv32 |= ivs[i] << (i * 5);
  // met_level 5, origem FireRed (4), Premier Ball (12), OT masculino.
  r.origins = static_cast<std::uint16_t>(5 | (4u << 7) | (12u << 11));
  return r;
}

int main() {
  std::printf("=== spec 109: subida gen3 -> moderno ===\n");

  const g3::FullRecord r = Pikachu();
  std::uint8_t raw[80];
  g3::EncodeFullRecord(r, raw);

  ms::Memory memory;

  // --- Para o Z-A (pk9) --------------------------------------------------
  auto up = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, &memory);
  Check(up.has_value(), "Pikachu converte para pk9");
  if (!up) return 1;

  Check(pkm::NationalDex(*up) == 25, "dex nacional 25 no destino");
  Check(up->pid == r.personality && up->encryption_constant == r.personality,
        "PID e EC preservados (TD-D1)");
  Check(up->tid == 1 && up->sid == 1, "TID/SID sao as metades do ot_id");
  Check(pkm::IsShiny(*up), "shiny gen3 continua shiny");
  Check(up->nature == r.personality % 25, "nature = PID %% 25");
  Check(up->nickname == "Pikachu" && !up->is_nicknamed,
        "nao-apelidado ganha a grafia moderna do nome (spec 115)");
  Check(up->ot_name == "ASH", "OT sobrevive");
  Check(up->ball == 12 && up->origin_game == 4 && up->met_level == 5,
        "bola/origem/met_level da palavra de origins");
  Check(up->held_item == 0, "item nao viaja (TD-D2)");
  Check(up->evs[0] == 252 && up->evs[3] == 252 && up->ivs[0] == 31,
        "EV com clamp 252; IVs intactos");
  Check(up->ability != 0 && up->ability_number == 1,
        "habilidade do slot 1 (bit 0)");
  Check(up->home_tracker != 0, "tracker atribuido");
  Check(up->tera_type_original == sp::Type1(25),
        "tera derivado do tipo primario no pk9");

  // Moveset: substituido pelo learnset do Z-A no nivel 46 — nao e o gen3.
  const std::uint8_t level = sp::LevelFromExp(25, r.experience);
  std::uint16_t esperado[4] = {0, 0, 0, 0};
  ls::MovesAtLevel(ls::Game::kZA, 25, 0, level, esperado);
  Check(esperado[0] != 0, "learnset do Z-A responde para Pikachu");
  Check(std::memcmp(up->moves.data(), esperado, sizeof(esperado)) == 0,
        "moveset substituido pelo do Z-A no nivel");

  // O moveset gen3 ORIGINAL ficou memorizado sob (tracker, kGen3).
  const ms::Snapshot* snap = memory.Recall(up->home_tracker, ms::Game::kGen3);
  Check(snap != nullptr, "moveset gen3 memorizado");
  if (snap) {
    Check((*snap).moves[0] == 85 && (*snap).moves[3] == 21,
          "a memoria guarda o moveset gen3 original");
    Check((*snap).pp_ups[0] == 1 && (*snap).pp_ups[1] == 0,
          "PP-ups do pp_bonuses gen3 na memoria");
  }

  // --- Restauracao G11: quem ja esteve no jogo volta com o moveset de la --
  {
    pkm::Pokemon custom = *up;
    custom.moves = {84, 85, 86, 87};  // moveset "editado no jogo"
    memory.Remember(custom, ms::Game::kZA);
    auto again = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, &memory);
    Check(again.has_value() && again->moves == custom.moves,
          "segunda subida restaura o moveset memorizado do Z-A (G11)");
  }

  // --- O pk9 gerado e serializavel e re-parseavel -------------------------
  {
    const std::vector<std::uint8_t> bytes = pk9::Write(*up);
    Check(!bytes.empty(), "Write(pk9) serializa o convertido");
    if (!bytes.empty()) {
      const auto back = pk9::Parse(bytes);
      Check(back.has_value(), "pk9::Parse reabre o convertido");
      if (back) {
        Check(pkm::NationalDex(*back) == 25 && back->pid == up->pid &&
                  back->nickname == "Pikachu" && back->ivs == up->ivs,
              "roundtrip do convertido preserva os campos");
      }
    }
  }

  // --- Recusas -------------------------------------------------------------
  {
    g3::FullRecord egg = Pikachu();
    egg.iv32 |= 1u << 30;
    std::uint8_t egg_raw[80];
    g3::EncodeFullRecord(egg, egg_raw);
    Check(!g3x::ConvertUp(egg_raw, pkm::Format::kPK9, ms::Game::kZA, nullptr)
               .has_value(),
          "ovo nao sobe (TD-D4)");

    g3::FullRecord bad = Pikachu();
    bad.flags |= 0x01;
    std::uint8_t bad_raw[80];
    g3::EncodeFullRecord(bad, bad_raw);
    Check(!g3x::ConvertUp(bad_raw, pkm::Format::kPK9, ms::Game::kZA, nullptr)
               .has_value(),
          "bad egg nao sobe");

    std::uint8_t zeros[80] = {};
    Check(!g3x::ConvertUp(zeros, pkm::Format::kPK9, ms::Game::kZA, nullptr)
               .has_value(),
          "slot vazio nao converte");
  }

  // =====================================================================
  // Descida (spec 110): o ciclo completo devolve o moveset gen3 original.
  // =====================================================================
  std::printf("=== spec 110: descida moderno -> gen3 ===\n");
  {
    std::uint8_t down[80];
    Check(g3x::ConvertDown(*up, ls::Game::kFireRed, ms::Game::kZA, &memory,
                           /*origem_fallback=*/4, down),
          "o Pikachu subido desce de volta para o FireRed");

    const auto full = g3::DecodeFullRecord(down);
    Check(full.has_value(), "o registro descido decodifica");
    if (full) {
      Check(full->personality == r.personality && full->ot_id == r.ot_id,
            "identidade intacta no ciclo completo");
      Check(full->species == 25, "indice gen3 da especie");
      // A promessa do dono: o moveset gen3 ORIGINAL restaurado da memoria.
      Check(full->moves[0] == 85 && full->moves[1] == 86 &&
                full->moves[2] == 98 && full->moves[3] == 21,
            "moveset gen3 original RESTAURADO da memoria");
      Check((full->pp_bonuses & 0x3) == 1, "PP-up original restaurado");
      Check(((full->origins >> 11) & 0xF) == 12 &&
                ((full->origins >> 7) & 0xF) == 4,
            "bola e jogo de origem sobrevivem ao ciclo");
      const g3::BoxPokemon mon = g3::ParseBoxPokemonRecord(down);
      Check(mon.nickname == "PIKACHU" && mon.ot_name == "ASH",
            "nomes sobrevivem ao ciclo");
      Check(mon.is_shiny(), "shiny continua shiny no gen3 (limiar 8)");
      Check(mon.ivs[0] == 31 && mon.evs[0] == 252,
            "IVs intactos; EVs com o clamp da subida");
    }

    // O moveset do Z-A ficou memorizado na descida — a proxima subida
    // restaura em vez de resetar.
    Check(memory.Recall(up->home_tracker, ms::Game::kZA) != nullptr,
          "moveset do Z-A memorizado ao sair");
  }

  // Sem memoria: moveset resetado pelo learnset gen3 do jogo alvo.
  {
    ms::Memory vazia;
    std::uint8_t down[80];
    Check(g3x::ConvertDown(*up, ls::Game::kFireRed, ms::Game::kZA, &vazia, 4,
                           down),
          "descida sem memoria funciona");
    const auto full = g3::DecodeFullRecord(down);
    std::uint16_t esperado[4] = {0, 0, 0, 0};
    const std::uint8_t level = sp::LevelFromExp(25, r.experience);
    ls::MovesAtLevel(ls::Game::kFireRed, 25, 0, level, esperado);
    Check(full && std::memcmp(full->moves, esperado, sizeof(esperado)) == 0,
          "sem memoria, moveset e o learnset do FireRed no nivel");
  }

  // Recusas da descida.
  {
    std::uint8_t down[80];
    pkm::Pokemon alto = *up;
    alto.species = pkm::SpeciesForFormat(922, pkm::Format::kPK9);  // Pawmo
    Check(!g3x::ConvertDown(alto, ls::Game::kFireRed, ms::Game::kZA, nullptr,
                            4, down),
          "especie sem indice gen3 (dex 922) nao desce");

    pkm::Pokemon forma = *up;
    forma.form = 1;
    Check(!g3x::ConvertDown(forma, ls::Game::kFireRed, ms::Game::kZA, nullptr,
                            4, down),
          "forma regional nao desce");

    pkm::Pokemon ovo = *up;
    ovo.is_egg = true;
    Check(!g3x::ConvertDown(ovo, ls::Game::kFireRed, ms::Game::kZA, nullptr,
                            4, down),
          "ovo nao desce");
  }

  if (g_failures) {
    std::printf("%d falha(s)\n", g_failures);
    return 1;
  }
  std::printf("gen3_transfer: tudo verde\n");
  return 0;
}
