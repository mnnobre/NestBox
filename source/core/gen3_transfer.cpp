#include "gen3_transfer.h"

#include <algorithm>
#include <cctype>

#include "body_size.h"
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

// --- Ribbons gen3 -> bitfield moderno (L6, spec 138) ----------------------
//
// TODOS os offsets abaixo foram MEDIDOS pela sonda `tools/pkhex-138`, que
// liga cada propriedade do PKHeX.Core e faz o XOR do buffer para observar o
// bit que mudou. Nada aqui e citado de documentacao.
//
// O u32 gen3 (`FullRecord::ribbons`) tem dois formatos misturados:
//   bits  0..14  -> CINCO contadores de 3 bits (concurso: cool, beauty,
//                   cute, smart, tough), cada um 0..4
//   bits 15..31  -> bools individuais
//
// `ribbon_bytes` do nosso modelo cobre os offsets 52..59 (indices 0..7) e
// 64..71 (indices 8..15) do binario moderno. Todo alvo aqui cai em 52..59,
// entao indice = byte - 52.
void ApplyGen3Ribbons(std::uint32_t r, pkm::Pokemon& p) {
  // Contadores de concurso: 3 bits cada, a partir do bit 0.
  // Medido: Cool=4 + Beauty=3 + Cute=1 -> RibbonCountMemoryContest = 8.
  // A regra e a SOMA dos cinco contadores (nao a contagem de categorias).
  int contest = 0;
  for (int i = 0; i < 5; ++i) contest += static_cast<int>((r >> (i * 3)) & 0x7);

  // Bools individuais. `bit` e a posicao no u32 gen3; `byte`/`mask` sao o
  // destino medido no binario moderno.
  struct Map {
    std::uint8_t bit;   // no u32 gen3
    std::uint8_t byte;  // no binario moderno (52..59)
    std::uint8_t mask;
  };
  static constexpr Map kIndividuais[] = {
      {15, 52, 1u << 1},  // ChampionG3 (Hoenn)  — PK3 byte 77 bit 7
      {18, 54, 1u << 2},  // Artist              — PK3 byte 78 bit 2
      {19, 52, 1u << 7},  // Effort              — PK3 byte 78 bit 3
      {20, 56, 1u << 1},  // ChampionBattle      — PK3 byte 78 bit 4
      {21, 56, 1u << 2},  // ChampionRegional    — PK3 byte 78 bit 5
      {22, 56, 1u << 3},  // ChampionNational    — PK3 byte 78 bit 6
      {23, 54, 1u << 6},  // Country             — PK3 byte 78 bit 7
      {24, 54, 1u << 7},  // National            — PK3 byte 79 bit 0
      {25, 55, 1u << 0},  // Earth               — PK3 byte 79 bit 1
      {26, 55, 1u << 1},  // World               — PK3 byte 79 bit 2
  };
  for (const Map& m : kIndividuais) {
    if ((r >> m.bit) & 1) p.ribbon_bytes[m.byte - 52] |= m.mask;
  }

  // Torre: Winning (bit 16) e Victory (bit 17) NAO viram bit individual —
  // viram CONTAGEM no Battle Memory Ribbon. Medido: os dois ligados -> 2.
  // Regra diferente da do concurso, porque no gen3 o concurso ja era um
  // contador por categoria e a torre era um bool por conquista.
  int battle = 0;
  if ((r >> 16) & 1) ++battle;
  if ((r >> 17) & 1) ++battle;

  // Os DOIS contadores: byte 60 (contest) e byte 61 (battle). O segundo era
  // o TD-03 desta spec — quando ela rodou, o `pkm_model.h` ainda tinha um
  // campo so, e o valor ficava calculado e descartado. A spec 136 (paralela)
  // provou que o byte 61 era um campo independente que o app perdia na
  // conversao entre formatos, e criou `ribbon_count_battle`. Ligado aqui.
  if (contest > 0) {
    p.ribbon_count_memory = static_cast<std::uint8_t>(contest);
  }
  if (battle > 0) {
    p.ribbon_count_battle = static_cast<std::uint8_t>(battle);
  }
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

  // --- Ribbons do gen3 (L6, spec 138) ------------------------------------
  // Sem isto o Pokemon de GBA chega ao Switch "sem curriculo". O mapa e
  // MEDIDO contra o EntityConverter do PkHeX (`tools/pkhex-138`), nunca
  // chutado — a conversao direta PK3->PK9 e o caminho oficial por etapas
  // (PK3->PK4->PK5->PK6->PK7->PK8->PK9) dao o mesmo resultado.
  //
  // O gen5->gen6 (Poke Transporter) CONSOLIDA: os ribbons de concurso viram
  // UM Contest Memory Ribbon e os de torre UM Battle Memory Ribbon, cada um
  // com contador. Os demais seguem individuais.
  ApplyGen3Ribbons(full->ribbons, p);

  // Tera ao entrar no pk9: o mesmo `TeraOnEntry` do Convert entre formatos
  // modernos. Nao e o tipo primario e sim o tipo que NAO e Normal — medido em
  // todas as 733 do SV (spec 145); a regra `== Type1` da 069 vinha de uma
  // amostra de 10 sem nenhum Normal/X.
  if (destino == pkm::Format::kPK9) {
    p.tera_type_original = species::TeraOnEntry(dex);
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
  // PP corrente: a BASE do golpe NO CONTEXTO do formato (tabela do PKHeX,
  // spec 115) — o PLA reduz o PP de varios golpes, e acima da base o verify
  // acusa "PP above the amount allowed".
  for (int i = 0; i < 4; ++i) {
    p.pp[i] = p.moves[i] ? movepp::Modern(static_cast<std::uint8_t>(destino),
                                          p.moves[i])
                         : 0;
  }

  // HP ATUAL cheio (spec 119). Mora no NUCLEO do formato, nao na cauda de
  // party: sem isto o Pokemon aparece com 0 de vida no jogo — foi o que o
  // dono viu no Z-A. O HP maximo sai da mesma formula do save_writer.
  p.hp_current = species::MaxHp(dex, p.ivs[0], p.evs[0], level);
  p.status_condition = 0;

  // O nivel de obediencia (PK9) e as plus flags do Z-A saiam daqui na spec
  // 145: dependem do JOGO de destino, e esta funcao so conhece o FORMATO.
  // Agora vivem em `commit::AplicaEntradaNoDestino`, por onde toda entrada
  // passa — inclusive o gerador de lote, que nao percorre esta rota.

  // AV de HP no LGPE (spec 136). O LGPE da 1 AV de HP por nivel GANHO, entao
  // ele exige `AV_HP >= nivel_atual - met_level`. MEDIDO contra o PkHeX em
  // tools/pkhex-ribbon-av: o piso e exatamente o delta (met=5 cur=46 -> 41;
  // met=1 cur=100 -> 99; delta 0 -> piso 0).
  //
  // O `met_level` vem copiado do registro gen3 (linha ~84), entao um gen3
  // capturado no nivel 5 e criado ate 46 chegaria ILEGAL: o LegalityAnalysis
  // acusa "HP AV should be greater than 41" (medido na spec 129).
  //
  // Preenche o MINIMO que satisfaz a regra, e so quando o valor atual for
  // menor — o AV soma DIRETO no stat (spec 129), entao encher alem do minimo
  // inventaria HP e CP que o Pokemon nao teve, e rebaixar um AV maior que ja
  // veio seria perda de dado.
  if (destino == pkm::Format::kPB7 && level > p.met_level) {
    const std::uint8_t piso = static_cast<std::uint8_t>(level - p.met_level);
    if (p.awakening_values[0] < piso) p.awakening_values[0] = piso;
  }


  // Altura e peso ABSOLUTOS (spec 121): so PA8 e PB7 os guardam, e o
  // verificador do PkHeX recalcula e compara. Formula conferida contra o
  // oraculo ate a precisao do float:
  //   ratio = 0.8 + 0.4 * scalar / 255
  //   altura = base_altura * ratio_altura
  //   peso   = base_peso  * ratio_altura * ratio_peso
  if (destino == pkm::Format::kPA8 || destino == pkm::Format::kPB7) {
    const bool pla = destino == pkm::Format::kPA8;
    const body::Entry* tab = pla ? body::kLa : body::kGg;
    const std::size_t n =
        pla ? sizeof(body::kLa) / sizeof(body::kLa[0])
            : sizeof(body::kGg) / sizeof(body::kGg[0]);
    std::uint16_t base_h = 0, base_w = 0;
    if (body::Lookup(tab, n, dex, p.form, &base_h, &base_w) && base_h && base_w) {
      body::Absolutes(pla ? body::kRatioPla : body::kRatioLgpe, base_h, base_w,
                      p.height_scalar, p.weight_scalar, &p.height_absolute,
                      &p.weight_absolute);
    }
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
  // Habilidade: no gen3 NAO ha slot, ha um BIT — e o jogo deriva o slot de
  // `PID & 1`. Gravar o bit a partir do nosso `ability_number` (a TD-D3
  // original da spec 110) produz um registro que o verificador reprova com
  // "Ability does not match PID": 131 de 259 divergiam no lote medido, dos
  // quais 45 eram acusados (nos outros `ability1 == ability2` e o bit nao
  // importa).
  //
  // TD-D3 REVISTA (spec 149): quem manda e o PID — mas SO quando a especie
  // tem duas habilidades DISTINTAS. Ivysaur tem ability1 == ability2 == 65;
  // ligar o bit nele produz `ability_number = 2` numa especie de habilidade
  // unica, e o verificador troca de queixa para "Ability does not match
  // ability number" (86 registros, medido ao tentar so o PID).
  {
    const auto& pers = gen3::Personal(dex);
    const bool duas = pers.ability1 != pers.ability2;
    if (duas && (p.pid & 1u) != 0) r.iv32 |= 1u << 31;
  }

  // Origens: met_level | origem<<7 | bola<<11 | sexo do OT<<15.
  const std::uint8_t origem =
      (p.origin_game >= 1 && p.origin_game <= 5) ? p.origin_game
                                                 : origem_fallback;
  const std::uint8_t bola = (p.ball >= 1 && p.ball <= 12) ? p.ball : 4;
  // `met_location = 0` no gen3 significa OVO — e ovo nasce no nivel 0. Gravar
  // o nivel real junto produz o par contraditorio `(met=0, metlv=20)`, que o
  // verificador reprova. Eram 259 de 259 no lote medido; so o `met_level`
  // corrigido leva 134 deles a legalidade TOTAL (spec 149).
  //
  // Os dois campos andam juntos: enquanto `met_location` for 0 (TD-01 da spec
  // 109 — nao ha mapa de locais moderno -> gen3), o `met_level` tem de ser 0.
  const std::uint8_t met_level_g3 = 0;
  r.origins = static_cast<std::uint16_t>(
      (met_level_g3 & 0x7F) | (static_cast<std::uint16_t>(origem & 0x0F) << 7) |
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
