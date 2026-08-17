// GERADO por tools/pkhex-tables - nao editar a mao (spec 079).
//
// Quais especies podem carregar a flag Gigantamax. O verificador de legalidade
// usa isto para o motivo mais frequente depois de contest stats: 707 Pokemon
// dos saves reais tem a flag Gmax ligada em especie que nao Gmaxa
// (docs/pesquisa-verificador-legalidade.md secao 5).
//
// A flag mora no binario do PK8/PB8 e a especie e a National Dex.
//
// Fonte: PKHeX.Core 25.12.21, Gigantamax.CanToggle, varrido nas 1025 especies.
// Nada do PkHeX e linkado; ver spec/memory/contexto-tecnico.md sobre licenca.

#pragma once

#include <cstdint>

namespace pokehome::gmax {

// 31 especies com Gigantamax:
//   3 Venusaur
//   6 Charizard
//   9 Blastoise
//   12 Butterfree
//   25 Pikachu
//   52 Meowth
//   68 Machamp
//   94 Gengar
//   99 Kingler
//   131 Lapras
//   133 Eevee
//   143 Snorlax
//   569 Garbodor
//   812 Rillaboom
//   815 Cinderace
//   818 Inteleon
//   823 Corviknight
//   826 Orbeetle
//   834 Drednaw
//   839 Coalossal
//   841 Flapple
//   842 Appletun
//   844 Sandaconda
//   849 Toxtricity
//   851 Centiskorch
//   858 Hatterene
//   861 Grimmsnarl
//   869 Alcremie
//   879 Copperajah
//   884 Duraludon
//   892 Urshifu
inline constexpr std::uint16_t kGmaxSpecies[31] = {
    3,6,9,12,25,52,68,94,99,131,133,143,569,812,815,818,
    823,826,834,839,841,842,844,849,851,858,861,869,879,884,892,
};

// Destas, as que SO Gmaxam na forma 0 — medido pela sobrecarga
// CanToggle(especie, forma): para as demais toda forma aceita.
//   25 Pikachu (a forma regional nao Gmaxa)
//   52 Meowth (a forma regional nao Gmaxa)
inline constexpr std::uint16_t kGmaxFormZeroOnly[2] = {
    25,52,
};

// Esta (especie, forma) pode ter a flag Gigantamax ligada?
inline bool CanGigantamax(int dex, int form) {
  bool found = false;
  for (std::uint16_t d : kGmaxSpecies) {
    if (d == dex) { found = true; break; }
  }
  if (!found) return false;
  for (std::uint16_t d : kGmaxFormZeroOnly) {
    if (d == dex) return form == 0;
  }
  return true;
}

}  // namespace pokehome::gmax