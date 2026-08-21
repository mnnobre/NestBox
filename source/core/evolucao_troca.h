// GERADO por tools/pkhex-alpha --gera-troca - nao editar a mao (spec 146).
//
// As especies que evoluem por TROCA. O NestBox move Pokemon entre jogos, que
// e a operacao que o jogo reconhece como troca — mas o jogo nunca dispara o
// EVENTO de evolucao ao carregar a caixa. Sem esta tabela, um Haunter
// transferido fica elegivel para sempre e nunca evolui.
//
// Fonte: PKHeX.Core, EvolutionTree.Forward, uniao de 12 arvores (Gen1..SV).
// Nada do PkHeX e linkado; ver spec/memory/contexto-tecnico.md sobre licenca.
//
// A tabela e a UNIAO das arvores porque a evolucao existe em jogos
// diferentes: Machoke -> Machamp vale no SwSh e nao no SV. Saber se um jogo
// aceita o RESULTADO e outra pergunta, respondida por HasSpecies().
//
// DEC-1: o item segurado nao e requisito — ele nunca viaja na transferencia.
// TradeShelmetKarrablast fica de fora: exige troca casada, que nao modelamos.

#pragma once

#include <cstddef>
#include <cstdint>

namespace pokehome::evo {

struct TrocaEvo {
  std::uint16_t base;   // dex de quem entra
  std::uint16_t alvo;   // dex de quem sai
};

// 22 pares, ordenados por dex da base (busca binaria).
inline constexpr TrocaEvo kTrocaEvo[22] = {
    {  61,  186},  // Poliwhirl -> Politoed  [Gen2 Gen3 Gen4 Gen5 Gen6 Gen7 SwSh BDSP SV]
    {  64,   65},  // Kadabra -> Alakazam  [Gen1 Gen2 Gen3 Gen4 Gen5 Gen6 Gen7 LGPE SwSh PLA BDSP]
    {  67,   68},  // Machoke -> Machamp  [Gen1 Gen2 Gen3 Gen4 Gen5 Gen6 Gen7 LGPE SwSh PLA BDSP]
    {  75,   76},  // Graveler -> Golem  [Gen1 Gen2 Gen3 Gen4 Gen5 Gen6 Gen7 LGPE PLA BDSP SV]
    {  79,  199},  // Slowpoke -> Slowking  [Gen2 Gen3 Gen4 Gen5 Gen6 Gen7 SwSh BDSP SV]
    {  93,   94},  // Haunter -> Gengar  [Gen1 Gen2 Gen3 Gen4 Gen5 Gen6 Gen7 LGPE SwSh PLA BDSP SV]
    {  95,  208},  // Onix -> Steelix  [Gen2 Gen3 Gen4 Gen5 Gen6 Gen7 SwSh BDSP]
    { 112,  464},  // Rhydon -> Rhyperior  [Gen4 Gen5 Gen6 Gen7 SwSh BDSP SV]
    { 117,  230},  // Seadra -> Kingdra  [Gen2 Gen3 Gen4 Gen5 Gen6 Gen7 SwSh BDSP SV]
    { 123,  212},  // Scyther -> Scizor  [Gen2 Gen3 Gen4 Gen5 Gen6 Gen7 SwSh BDSP SV]
    { 125,  466},  // Electabuzz -> Electivire  [Gen4 Gen5 Gen6 Gen7 SwSh BDSP SV]
    { 126,  467},  // Magmar -> Magmortar  [Gen4 Gen5 Gen6 Gen7 SwSh BDSP SV]
    { 137,  233},  // Porygon -> Porygon2  [Gen2 Gen3 Gen4 Gen5 Gen6 Gen7 SwSh BDSP SV]
    { 233,  474},  // Porygon2 -> PorygonZ  [Gen4 Gen5 Gen6 Gen7 LGPE SwSh BDSP SV]
    { 349,  350},  // Feebas -> Milotic  [Gen5 Gen6 Gen7 LGPE SwSh SV]
    { 356,  477},  // Dusclops -> Dusknoir  [Gen4 Gen5 Gen6 Gen7 LGPE SwSh BDSP SV]
    { 525,  526},  // Boldore -> Gigalith  [Gen5 Gen6 Gen7 LGPE SwSh]
    { 533,  534},  // Gurdurr -> Conkeldurr  [Gen5 Gen6 Gen7 LGPE SwSh SV]
    { 682,  683},  // Spritzee -> Aromatisse  [Gen6 Gen7 LGPE SwSh]
    { 684,  685},  // Swirlix -> Slurpuff  [Gen6 Gen7 LGPE SwSh]
    { 708,  709},  // Phantump -> Trevenant  [Gen6 Gen7 LGPE SwSh SV]
    { 710,  711},  // Pumpkaboo -> Gourgeist  [Gen6 Gen7 LGPE SwSh]
};

// O dex do evoluido, ou 0 se esta especie nao evolui por troca.
inline int AlvoDaTroca(int dex) {
  std::size_t lo = 0, hi = sizeof(kTrocaEvo) / sizeof(kTrocaEvo[0]);
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (kTrocaEvo[mid].base < dex) lo = mid + 1;
    else hi = mid;
  }
  const std::size_t n = sizeof(kTrocaEvo) / sizeof(kTrocaEvo[0]);
  if (lo < n && kTrocaEvo[lo].base == dex) return kTrocaEvo[lo].alvo;
  return 0;
}

inline bool EvoluiPorTroca(int dex) { return AlvoDaTroca(dex) != 0; }

}  // namespace pokehome::evo
