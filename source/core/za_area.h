// GERADO por tools/pkhex-za2 - nao editar a mao (spec 145).
//
// As especies do Legends Z-A que sao NATIVAS DO HYPERSPACE (a area do DLC),
// e nao de Lumiose.
//
// O jogo chama essa area de "next dimension" na tela — foi por esse nome que
// o dono a reconheceu ao ver os ovos. `IsHyperspaceNative` e como o PkHeX a
// chama na tabela.
//
// Por que existe: o jogo desenha OVO no lugar delas se o save nao tiver o
// progresso que libera a area. Medido em 2026-08-19 contra o save real do
// dono, com o DLC instalado e habilitado no emulador (`dlc.json`,
// is_enabled=true): das 364 do lote, as 232 de Lumiose desenham e estas 132
// viram ovo. A previsao bateu 30/30 na Box 5, incluindo os casos dificeis
// (Absol desenha entre ovos; a linha de Bagon->Metagross inteira desenha).
//
// NAO e defeito do registro: o PkHeX aprova os 132, o HP vem cheio, as plus
// flags estao certas e o save continua integro depois de salvar no jogo.
// Por isso o lote as marca como NAO-TESTAVEIS na tela, em vez de falha.
//
// Fonte: PKHeX.Core 25.12.21, PersonalInfo9ZA.IsLumioseNative.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pokehome::za_area {

// 132 especies nativas do Hyperspace.
inline constexpr std::uint16_t kHyperspace[132] = {
    39,40,41,42,52,53,56,57,83,104,105,122,
    137,169,174,211,233,252,253,254,255,256,257,258,
    259,260,316,317,325,326,335,336,349,350,352,358,
    380,381,382,383,384,396,397,398,433,439,474,479,
    485,491,509,510,517,518,538,539,562,563,590,591,
    615,622,623,638,639,640,647,648,649,720,721,739,
    740,767,768,769,770,778,801,802,807,808,809,821,
    822,823,827,828,848,849,852,853,863,865,866,867,
    876,877,900,904,926,927,931,932,933,934,935,936,
    937,942,943,944,945,951,952,957,958,959,967,969,
    970,971,972,973,977,978,979,996,997,998,999,1000,
};

// A especie so aparece na tela se a area do DLC estiver liberada no save?
inline bool SoNoHyperspace(std::uint16_t dex) {
  for (const std::uint16_t d : kHyperspace)
    if (d == dex) return true;
  return false;
}

}  // namespace pokehome::za_area
