// Conversao de formato entre jogos (spec 069, G06 da descoberta
// escrita-e-transferencia).
//
// Mover um Pokemon de BDSP (PB8) para Scarlet (PK9) nao e copiar bytes: os
// layouts sao diferentes, a lista de campos e diferente, e no PK9 ate o numero
// da especie e outra grandeza. Converter e REESCREVER num layout novo
// preservando o que a Pokemon HOME preserva (pesquisa-pokemon-home.md §7).
//
// Duas armadilhas que este header existe para resolver:
//
// 1. `pkm::Pokemon::species` NAO tem a mesma grandeza em todos os formatos. O
//    parser do PK9 guarda o INDICE INTERNO do gen9 (o Pawmo e 955 no binario e
//    922 na dex nacional); os outros quatro guardam National Dex. Copiar o
//    campo direto troca a especie em silencio — nenhum checksum acusa.
//    Confirmado contra o PkHeX na spec 069: `PK9.Species` devolve 922 enquanto
//    o u16 do offset 8 vale 955.
//
// 2. A escrita conservadora da spec 063 (`Write` parte de `p.raw`) NAO se
//    aplica aqui. Ver TD-01 na spec.
#pragma once

#include <cstdint>
#include <optional>

#include "pkm_model.h"

namespace pkm {

// National Dex do Pokemon, seja qual for o formato de origem. E o unico jeito
// certo de comparar especie entre dois Pokemon de formatos diferentes.
// Devolve 0 se o indice interno do PK9 nao mapeia para especie nenhuma.
std::uint16_t NationalDex(const Pokemon& p);

// O numero que o binario de `fmt` deve guardar para esta National Dex.
// Devolve 0 se a especie nao existe naquele formato — no PK9 isso significa
// "nao esta no gen9", e gravar 0 produziria um slot vazio silencioso.
std::uint16_t SpeciesForFormat(std::uint16_t national_dex, Format fmt);

// Converte para `destino`. Devolve nullopt se a especie nao e representavel no
// formato de destino (TD-03).
//
// Preservados sempre (§7): shiny (PID + TID/SID), OT/TID/SID, Poke Ball,
// jogo/data/local de origem, ribbons, marks, apelido, IVs, EVs, nature,
// ability, genero, exp, home_tracker.
//
// Zerados: campos que o formato destino nao tem (dynamax/gigantamax indo para
// BDSP ou PLA; tera type indo para SwSh; effort levels saindo do PLA).
//
// Derivados: tera type ao ENTRAR no PK9 vem do tipo primario da especie
// (§7, e confirmado contra o EntityConverter do PkHeX na spec 069).
//
// `Convert(p, p.format)` devolve copia identica, com o `raw` INTACTO — mover
// dentro do mesmo formato continua sob a regra conservadora da spec 063.
std::optional<Pokemon> Convert(const Pokemon& origem, Format destino);

// Ajustes de ENTRADA num formato diferente do de origem (spec 143).
//
// `Convert` reescreve o layout; estes sao os campos que o JOGO DE DESTINO
// espera diferentes de como a origem os guardava. Sao coisas que nenhum
// checksum acusa e que o PkHeX so revela pelo criterio DELTA (motivo que
// aparece no destino e nao existia na origem).
//
// Existe como funcao separada, e nao como parte do Convert, por um motivo
// concreto: ate a spec 143 esta logica vivia SO no `transfer::Commit`, e o
// deposito pela tela (`ApplyBoxChanges`, via `commit_plan`) a pulava inteira
// — um Pokemon do BDSP entrava no Legends: Arceus carregando o sentinela de
// egg location do BDSP. Um caminho de escrita que nao chame isto esta errado
// por construcao; `test_commit_plan` tranca essa porta.
//
// O que faz, e de onde cada regra veio (todas medidas, nenhuma deduzida):
//
//   PK9   obedience_level = met_level, scale = height_scalar  (spec 069/078)
//   PB8   egg_location 0 -> 0xFFFF ; saindo do PB8, 0xFFFF -> 0
//   PLA   effort_levels zerados (docs/pesquisa-effort-levels-pla.md)
//
// NAO mexe em moveset nem em handling trainer: o primeiro depende do learnset
// e da memoria (msv::Memory), o segundo do save de destino — nenhum dos dois
// esta ao alcance desta funcao, e ambos tem dono proprio no chamador.
//
// `origem_fmt` e o formato de ONDE o Pokemon veio. Se for igual ao formato de
// `mon`, a funcao nao faz nada: mover dentro do mesmo jogo continua sob a
// regra conservadora da spec 063.
void AjustesDeEntrada(Pokemon& mon, Format origem_fmt);

}  // namespace pkm
