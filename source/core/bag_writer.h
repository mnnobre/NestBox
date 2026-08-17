// Bag do save (spec 072, G08 da descoberta escrita-e-transferencia).
//
// A REGRA, de docs/pesquisa-pokemon-home.md §7:
//
//     "o HOME nao armazena itens — ao depositar, o item volta automaticamente
//      para a Bag do jogo de ORIGEM. Nunca viaja, nunca se perde"
//
// O dono descreveu isso errado ("gera no inventario do destino"). A escrita
// acontece no save de ORIGEM, no momento do deposito. O destino nunca recebe
// item nenhum.
//
// --- Formatos suportados --------------------------------------------------
//
// SwSh e LGPE, e SO eles. Os dois guardam o slot como um u32 AUTO-DESCRITIVO:
//
//     bits  0..14  indice do item
//     bits 15..29  quantidade
//
// Descoberto sondando o PkHeX (tools/pkhex-bag, comando `probe`): altera a
// contagem de um item pela API dele, reserializa e difere contra a linha de
// base. Os bytes que mudam SAO o campo — offset e codificacao saem juntos, sem
// derivar por analogia (a licao da spec 066).
//
// SV e BDSP entraram na spec 074, com a OUTRA mecanica: tabela DENSA de
// stride 16 indexada pelo proprio id do item, sem id gravado no slot.
//
//     SV    SCBlock 0x21C9BD44, slot = id*16
//           +0x0 u32 Pouch  +0x4 u32 Count  +0x8 u32 Flags  +0xC padding
//     BDSP  save flat, slot = 0x563C + id*16
//           +0x0 u32 Count  +0x4 u32 IsNew(0=novo)  +0x8 u32 IsFavorite
//           +0xC u16 SortOrder (0 = NAO ADQUIRIDO — o item some da bag)
//
// PLA fica de FORA de proposito, e nao por dificuldade: a bag dele e a mais
// simples das tres (lista esparsa de stride 4 no bloco 0x9FE2790A, mecanica
// igual a do SwSh). O motivo e que **Pokemon de Legends Arceus nao seguram
// item**, e a unica origem de escrita de bag no produto e o WithdrawHeldItem.
// Implementar seria codigo inalcancavel. Ver spec 074, TD do escopo.
#pragma once

#include <cstdint>
#include <vector>

#include "save_writer.h"

namespace bagw {

// Limite de quantidade por slot. Fonte: `InventoryPouch.MaxCount` do PkHeX
// 25.12.21, que reporta 999 para toda pouch normal dos dois jogos (e 1 para
// KeyItems/TMHMs, que nao sao alvo desta regra — Pokemon nao segura key item).
constexpr std::uint16_t kMaxCount = 999;

// Um item na bag, do jeito que o slot o descreve.
struct Item {
  std::uint16_t id = 0;
  std::uint16_t count = 0;
};

// O jogo tem bag legivel por este modulo?
bool Supported(savew::Game g);

// Le a bag inteira (todas as pouches, itens com id != 0).
std::vector<Item> ReadBag(const savew::SaveData& sd);

// Quantos de `item_id` o jogador tem. 0 se nao tem.
std::uint16_t CountOf(const savew::SaveData& sd, std::uint16_t item_id);

// Soma `qtd` ao item na bag: incrementa o slot existente, ou ocupa um slot
// vazio se o item e novo. Satura em kMaxCount — o excedente e PERDIDO, que e
// o comportamento do jogo (a bag nao guarda mais que o teto).
//
// false se o jogo nao e suportado, se o item_id e 0, ou se a bag esta cheia e
// o item e novo. Marca a regiao como suja para o Save() reescrever.
bool AddItemToBag(savew::SaveData& sd, std::uint16_t item_id,
                  std::uint16_t qtd);

// A OPERACAO DO DEPOSITO. Tira o item segurado do Pokemon em (box, slot) e o
// devolve a bag DESTE save — o de ORIGEM.
//
// false se o slot esta vazio, se o Pokemon nao segura nada (nada a fazer, e
// nao e erro do chamador — confira antes), ou se a bag recusou.
bool WithdrawHeldItem(savew::SaveData& sd, std::size_t box_i,
                      std::size_t slot_i);

}  // namespace bagw
