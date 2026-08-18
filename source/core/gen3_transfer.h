// Transferencia entre geracoes — a SUBIDA (spec 109, F03 da descoberta
// transferencia-entre-geracoes).
//
// Converte um registro gen3 de 80 bytes num pkm::Pokemon do formato alvo,
// pelas regras decididas pelo dono (2026-08-17):
//
//   - a especie precisa existir no jogo de destino (portao G05 — quem confere
//     e a UI, via compat::HasSpecies; aqui so a representabilidade do FORMATO
//     e conferida);
//   - habilidade por SLOT: o bit gen3 escolhe ability1/ability2 da especie;
//   - moveset SUBSTITUIDO pelo do jogo alvo (restaurado da memoria se o
//     Pokemon ja esteve la — G11); o moveset gen3 original e MEMORIZADO sob
//     (tracker, kGen3) para a volta;
//   - PID/TID/SID intactos (shiny e identidade preservados — TD-D1);
//   - item NAO viaja (TD-D2, regra da spec 072);
//   - ovo e bad egg nao sobem (TD-D4).
#pragma once

#include <cstdint>
#include <optional>

#include "gen3_save.h"
#include "moveset_memory.h"
#include "pkm_model.h"

namespace pokehome::g3x {

// `raw` sao os 80 bytes do slot gen3. `dest_ms` e o jogo alvo no vocabulario
// da memoria (kSwSh/kSV/kZA/kLgpe/kLegendsArceus/kBdsp). `memory` e o banco
// de movesets do NestBox: e ATUALIZADO (moveset gen3 memorizado) e CONSULTADO
// (restauracao no destino). Pode ser nullptr num contexto sem banco — ai o
// moveset e sempre resetado por nivel e nada e memorizado.
//
// nullopt: slot vazio, ovo, bad egg, especie fora do formato de destino.
std::optional<pkm::Pokemon> ConvertUp(const std::uint8_t raw[80],
                                      pkm::Format destino,
                                      moveset::Game dest_ms,
                                      moveset::Memory* memory);

// A DESCIDA (spec 110): pkm moderno -> registro gen3 de 80 bytes em `out`.
//
// `dest_learnset` e o jogo gen3 ALVO (kFireRed/kEmerald/...) para o reset por
// nivel quando nao ha moveset gen3 memorizado. `src_ms` e o jogo de ONDE o
// Pokemon esta saindo — o moveset moderno atual e memorizado la para a
// proxima ida. `origem_fallback` (1..5) entra na palavra de origins quando o
// jogo de origem do Pokemon nao e gen3.
//
// false: vazio, ovo, especie sem indice gen3, forma nao representavel
// (so Unown e Deoxys tem forma no gen3 — a do Unown rederiva do PID).
bool ConvertDown(const pkm::Pokemon& p, learnset::Game dest_learnset,
                 moveset::Game src_ms, moveset::Memory* memory,
                 std::uint8_t origem_fallback, std::uint8_t out[80]);

}  // namespace pokehome::g3x
