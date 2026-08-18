#include "modern_box_view.h"

#include <cstring>

#include "pkm_convert.h"
#include "species_facts.h"

namespace pokehome::view {

gen3::BoxPokemon ToBoxPokemon(const pkm::Pokemon& p) {
  gen3::BoxPokemon out;
  if (p.species == 0) return out;

  // National Dex, NUNCA `p.species` — ver o cabecalho.
  const std::uint16_t dex = pkm::NationalDex(p);
  if (dex == 0) return out;  // especie que nao mapeia: slot vazio, sem chute

  out.species = dex;
  out.national_dex = dex;
  out.species_name = gen3::SpeciesNameByDex(dex);
  out.nickname = p.nickname;

  // Shiny e nivel a fonte entrega prontos: o limiar do shiny e 16 no gen6+
  // (o `is_shiny()` do gen3 usa 8) e a curva de exp do gen3 para na dex 386.
  out.display_shiny = pkm::IsShiny(p);
  out.display_level = species::LevelFromExp(dex, p.exp);

  out.held_item = p.held_item;
  out.experience = p.exp;
  out.is_egg = p.is_egg;
  out.friendship = p.ot_friendship;
  out.personality = p.pid;
  out.ot_id = static_cast<std::uint32_t>(p.tid) |
              (static_cast<std::uint32_t>(p.sid) << 16);
  out.met_level = p.met_level;

  // Identidade da barra de status (spec 098): o formato moderno ja traz tudo.
  out.ot_name = p.ot_name;
  out.language = p.language;
  out.origin_game = p.origin_game;
  out.display_gender = p.gender;  // 0=M 1=F 2=sem sexo, mesma numeracao
  out.display_ball = p.ball;      // spec 099

  for (int i = 0; i < 4; ++i) {
    out.moves[i] = p.moves[i];
    out.pp[i] = p.pp[i];
  }
  for (int i = 0; i < 6; ++i) {
    out.ivs[i] = p.ivs[i];
    out.evs[i] = p.evs[i];
  }

  // `raw` fica ZERADO de proposito. Ele guarda os 80 bytes do slot GEN3
  // (spec 028) e um PKM moderno tem 260..408 — copiar o inicio produziria um
  // registro gen3 invalido que o NestBox gravaria como se fosse legitimo.
  // Consequencia assumida: depositar um Pokemon moderno no NestBox e uma
  // spec propria, com formato de slot proprio.
  //
  // Quem carrega o original e `modern` (spec 086): copia congelada, imutavel,
  // compartilhada por todas as copias que a MoveSession fizer deste registro.
  out.modern = std::make_shared<const pkm::Pokemon>(p);
  return out;
}

bool ApplyBoxChanges(savew::SaveData& sd,
                     const std::vector<BoxChange>& changes) {
  // Valida TUDO antes de tocar no primeiro slot — tudo-ou-nada.
  for (const BoxChange& c : changes) {
    if (c.box >= sd.box_count || c.slot >= sd.slots_per_box) return false;
    if (!c.mon.empty() && !c.mon.modern) return false;
  }
  for (const BoxChange& c : changes) {
    const bool ok = c.mon.empty() ? sd.Set(c.box, c.slot, pkm::Pokemon{})
                                  : sd.Set(c.box, c.slot, *c.mon.modern);
    if (!ok) return false;  // inalcancavel apos a validacao; cinto e suspensorio
  }
  return true;
}

}  // namespace pokehome::view
