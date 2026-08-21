// Encontros do Let's Go — GERADO por tools/pkhex-alpha --lgpe-tab.
// NAO EDITAR A MAO. Spec 150.
//
// Um Pokemon que CHEGA no Let's Go precisa casar met_location e met_level
// com um encontro que existe no jogo: o PB7 nao tem local de transferencia
// como o 30001 do HOME. Com met=0 sao 151/151 ilegais.
//
// Varios encontros por especie, ORDENADOS POR NIVEL: quem chega escolhe o
// de maior nivel que ainda cabe (met_level > nivel atual reprova com
// "Current level is below met level").
//
// As duas versoes entram na mesma lista: o LGPE e par, e o verificador
// aceita encontro de qualquer uma das duas.
#pragma once

#include <cstdint>

namespace pokehome::lgpe {

struct Encontro {
  std::uint16_t met;
  std::uint8_t  nivel;
};

inline constexpr Encontro kEncontros[] = {
    {  39,   3},  // Bulbasaur
    {  31,  12},  // Bulbasaur
    {  39,   3},  // Ivysaur
    {  31,  12},  // Ivysaur
    {  39,   3},  // Venusaur
    {  31,  12},  // Venusaur
    {   5,   3},  // Charmander
    {   6,   7},  // Charmander
    {  26,  14},  // Charmander
    {  41,  18},  // Charmander
    {   5,   3},  // Charmeleon
    {   6,   7},  // Charmeleon
    {  26,  14},  // Charmeleon
    {  41,  18},  // Charmeleon
    {   3,   3},  // Charizard
    {   4,   3},  // Charizard
    {   5,   3},  // Charizard
    {   6,   3},  // Charizard
    {   9,   3},  // Charizard
    {  10,   3},  // Charizard
    {  11,   3},  // Charizard
    {  12,   3},  // Charizard
    {  13,   3},  // Charizard
    {  14,   3},  // Charizard
    {  15,   3},  // Charizard
    {  16,   3},  // Charizard
    {  17,   3},  // Charizard
    {  18,   3},  // Charizard
    {  19,   3},  // Charizard
    {  20,   3},  // Charizard
    {  21,   3},  // Charizard
    {  22,   3},  // Charizard
    {  23,   3},  // Charizard
    {  24,   3},  // Charizard
    {  25,   3},  // Charizard
    {  26,   3},  // Charizard
    {  27,   3},  // Charizard
    {   6,   7},  // Charizard
    {  26,  14},  // Charizard
    {  41,  18},  // Charizard
    {  26,   7},  // Squirtle
    {  27,   9},  // Squirtle
    {  33,  16},  // Squirtle
    {  44,  39},  // Squirtle
    {  26,   7},  // Wartortle
    {  27,   9},  // Wartortle
    {  33,  16},  // Wartortle
    {  44,  39},  // Wartortle
    {  26,   7},  // Blastoise
    {  27,   9},  // Blastoise
    {  33,  16},  // Blastoise
    {  44,  39},  // Blastoise
    {   4,   3},  // Caterpie
    {  39,   3},  // Caterpie
    {   4,   3},  // Metapod
    {  39,   3},  // Metapod
    {   4,   3},  // Butterfree
    {  39,   3},  // Butterfree
    {   4,   3},  // Weedle
    {  39,   3},  // Weedle
    {   4,   3},  // Kakuna
    {  39,   3},  // Kakuna
    {   4,   3},  // Beedrill
    {  39,   3},  // Beedrill
    {   3,   3},  // Pidgey
    {   4,   3},  // Pidgey
    {   7,   3},  // Pidgey
    {   8,   3},  // Pidgey
    {   9,   3},  // Pidgey
    {  10,   3},  // Pidgey
    {  13,   3},  // Pidgey
    {  14,   3},  // Pidgey
    {  15,   3},  // Pidgey
    {  16,   3},  // Pidgey
    {  17,   3},  // Pidgey
    {  18,   3},  // Pidgey
    {  19,   3},  // Pidgey
    {  20,   3},  // Pidgey
    {  21,   3},  // Pidgey
    {  22,   3},  // Pidgey
    {  23,   3},  // Pidgey
    {  26,   3},  // Pidgey
    {  27,   3},  // Pidgey
    {  39,   3},  // Pidgey
    {  26,   7},  // Pidgey
    {  27,   9},  // Pidgey
    {   7,  11},  // Pidgey
    {   8,  11},  // Pidgey
    {  13,  13},  // Pidgey
    {   9,  22},  // Pidgey
    {  10,  22},  // Pidgey
    {  14,  31},  // Pidgey
    {  18,  31},  // Pidgey
    {  15,  33},  // Pidgey
    {  16,  33},  // Pidgey
    {  17,  33},  // Pidgey
    {  19,  33},  // Pidgey
    {  20,  33},  // Pidgey
    {  23,  37},  // Pidgey
    {   3,   3},  // Pidgeotto
    {   4,   3},  // Pidgeotto
    {   7,   3},  // Pidgeotto
    {   8,   3},  // Pidgeotto
    {   9,   3},  // Pidgeotto
    {  10,   3},  // Pidgeotto
    {  13,   3},  // Pidgeotto
    {  14,   3},  // Pidgeotto
    {  15,   3},  // Pidgeotto
    {  16,   3},  // Pidgeotto
    {  17,   3},  // Pidgeotto
    {  18,   3},  // Pidgeotto
    {  19,   3},  // Pidgeotto
    {  20,   3},  // Pidgeotto
    {  21,   3},  // Pidgeotto
    {  22,   3},  // Pidgeotto
    {  23,   3},  // Pidgeotto
    {  26,   3},  // Pidgeotto
    {  27,   3},  // Pidgeotto
    {  39,   3},  // Pidgeotto
    {  26,   7},  // Pidgeotto
    {  27,   9},  // Pidgeotto
    {   7,  11},  // Pidgeotto
    {   8,  11},  // Pidgeotto
    {  13,  13},  // Pidgeotto
    {   9,  22},  // Pidgeotto
    {  10,  22},  // Pidgeotto
    {  14,  31},  // Pidgeotto
    {  18,  31},  // Pidgeotto
    {  15,  33},  // Pidgeotto
    {  16,  33},  // Pidgeotto
    {  17,  33},  // Pidgeotto
    {  19,  33},  // Pidgeotto
    {  20,  33},  // Pidgeotto
    {  23,  37},  // Pidgeotto
    {   3,   3},  // Pidgeot
    {   4,   3},  // Pidgeot
    {   7,   3},  // Pidgeot
    {   8,   3},  // Pidgeot
    {   9,   3},  // Pidgeot
    {  10,   3},  // Pidgeot
    {  13,   3},  // Pidgeot
    {  14,   3},  // Pidgeot
    {  15,   3},  // Pidgeot
    {  16,   3},  // Pidgeot
    {  17,   3},  // Pidgeot
    {  18,   3},  // Pidgeot
    {  19,   3},  // Pidgeot
    {  20,   3},  // Pidgeot
    {  21,   3},  // Pidgeot
    {  22,   3},  // Pidgeot
    {  23,   3},  // Pidgeot
    {  26,   3},  // Pidgeot
    {  27,   3},  // Pidgeot
    {  39,   3},  // Pidgeot
    {  26,   7},  // Pidgeot
    {  27,   9},  // Pidgeot
    {   7,  11},  // Pidgeot
    {   8,  11},  // Pidgeot
    {  13,  13},  // Pidgeot
    {   9,  22},  // Pidgeot
    {  10,  22},  // Pidgeot
    {  14,  31},  // Pidgeot
    {  18,  31},  // Pidgeot
    {  15,  33},  // Pidgeot
    {  16,  33},  // Pidgeot
    {  17,  33},  // Pidgeot
    {  19,  33},  // Pidgeot
    {  20,  33},  // Pidgeot
    {  23,  37},  // Pidgeot
    {   3,   3},  // Rattata
    {   4,   3},  // Rattata
    {   5,   3},  // Rattata
    {  24,   3},  // Rattata
    {   6,   7},  // Rattata
    {   7,  11},  // Rattata
    {   8,  11},  // Rattata
    {  13,  13},  // Rattata
    {  11,  17},  // Rattata
    {  12,  18},  // Rattata
    {   9,  22},  // Rattata
    {  10,  22},  // Rattata
    {  18,  31},  // Rattata
    {  19,  33},  // Rattata
    {  20,  33},  // Rattata
    {  23,  37},  // Rattata
    {  51,  39},  // Rattata
    {   3,   3},  // Raticate
    {   4,   3},  // Raticate
    {   5,   3},  // Raticate
    {  24,   3},  // Raticate
    {   6,   7},  // Raticate
    {   7,  11},  // Raticate
    {   8,  11},  // Raticate
    {  13,  13},  // Raticate
    {  11,  17},  // Raticate
    {  12,  18},  // Raticate
    {   9,  22},  // Raticate
    {  10,  22},  // Raticate
    {  18,  31},  // Raticate
    {  19,  33},  // Raticate
    {  20,  33},  // Raticate
    {  23,  37},  // Raticate
    {  51,  39},  // Raticate
    {   5,   3},  // Spearow
    {   6,   3},  // Spearow
    {  11,   3},  // Spearow
    {  12,   3},  // Spearow
    {  24,   3},  // Spearow
    {  25,   3},  // Spearow
    {   6,   7},  // Spearow
    {  11,  17},  // Spearow
    {  12,  18},  // Spearow
    {  25,  41},  // Spearow
    {   5,   3},  // Fearow
    {   6,   3},  // Fearow
    {  11,   3},  // Fearow
    {  12,   3},  // Fearow
    {  24,   3},  // Fearow
    {  25,   3},  // Fearow
    {   6,   7},  // Fearow
    {  11,  17},  // Fearow
    {  12,  18},  // Fearow
    {  25,  41},  // Fearow
    {   5,   3},  // Ekans
    {   6,   7},  // Ekans
    {   5,   3},  // Arbok
    {   6,   7},  // Arbok
    {  39,   3},  // Pikachu
    {  28,   5},  // Pikachu
    {  39,   3},  // Raichu
    {  28,   5},  // Raichu
    {   5,   3},  // Sandshrew
    {   6,   7},  // Sandshrew
    {   5,   3},  // Sandslash
    {   6,   7},  // Sandslash
    {  24,   3},  // NidoranF
    {  11,  17},  // NidoranF
    {  12,  18},  // NidoranF
    {  25,  41},  // NidoranF
    {  24,   3},  // Nidorina
    {  11,  17},  // Nidorina
    {  12,  18},  // Nidorina
    {  25,  41},  // Nidorina
    {  24,   3},  // Nidoqueen
    {  11,  17},  // Nidoqueen
    {  12,  18},  // Nidoqueen
    {  25,  41},  // Nidoqueen
    {  24,   3},  // NidoranM
    {  11,  17},  // NidoranM
    {  12,  18},  // NidoranM
    {  25,  41},  // NidoranM
    {  24,   3},  // Nidorino
    {  11,  17},  // Nidorino
    {  12,  18},  // Nidorino
    {  25,  41},  // Nidorino
    {  24,   3},  // Nidoking
    {  11,  17},  // Nidoking
    {  12,  18},  // Nidoking
    {  25,  41},  // Nidoking
    {  40,   5},  // Clefairy
    {  40,   5},  // Clefable
    {   7,  11},  // Vulpix
    {   8,  11},  // Vulpix
    {   9,  22},  // Vulpix
    {  10,  22},  // Vulpix
    {   7,  11},  // Ninetales
    {   8,  11},  // Ninetales
    {   9,  22},  // Ninetales
    {  10,  22},  // Ninetales
    {   7,  11},  // Jigglypuff
    {   8,  11},  // Jigglypuff
    {   9,  22},  // Jigglypuff
    {  10,  22},  // Jigglypuff
    {   7,  11},  // Wigglytuff
    {   8,  11},  // Wigglytuff
    {   9,  22},  // Wigglytuff
    {  10,  22},  // Wigglytuff
    {  40,   5},  // Zubat
    {  43,  13},  // Zubat
    {  41,  18},  // Zubat
    {  47,  27},  // Zubat
    {  44,  39},  // Zubat
    {  45,  41},  // Zubat
    {  46,  51},  // Zubat
    {  40,   5},  // Golbat
    {  43,  13},  // Golbat
    {  41,  18},  // Golbat
    {  47,  27},  // Golbat
    {  44,  39},  // Golbat
    {  45,  41},  // Golbat
    {  46,  51},  // Golbat
    {   3,   3},  // Oddish
    {   4,   3},  // Oddish
    {  39,   3},  // Oddish
    {  26,   7},  // Oddish
    {  27,   9},  // Oddish
    {  14,  31},  // Oddish
    {  15,  33},  // Oddish
    {  16,  33},  // Oddish
    {  17,  33},  // Oddish
    {  23,  37},  // Oddish
    {   3,   3},  // Gloom
    {   4,   3},  // Gloom
    {  39,   3},  // Gloom
    {  26,   7},  // Gloom
    {  27,   9},  // Gloom
    {  14,  31},  // Gloom
    {  15,  33},  // Gloom
    {  16,  33},  // Gloom
    {  17,  33},  // Gloom
    {  23,  37},  // Gloom
    {   3,   3},  // Vileplume
    {   4,   3},  // Vileplume
    {  39,   3},  // Vileplume
    {  26,   7},  // Vileplume
    {  27,   9},  // Vileplume
    {  14,  31},  // Vileplume
    {  15,  33},  // Vileplume
    {  16,  33},  // Vileplume
    {  17,  33},  // Vileplume
    {  23,  37},  // Vileplume
    {  40,   5},  // Paras
    {  40,   5},  // Parasect
    {  26,   7},  // Venonat
    {  27,   9},  // Venonat
    {  16,  33},  // Venonat
    {  17,  33},  // Venonat
    {  26,   7},  // Venomoth
    {  27,   9},  // Venomoth
    {  16,  33},  // Venomoth
    {  17,  33},  // Venomoth
    {  43,  13},  // Diglett
    {  43,  13},  // Dugtrio
    {  26,   7},  // Meowth
    {  27,   9},  // Meowth
    {  26,   7},  // Persian
    {  27,   9},  // Persian
    {  33,  16},  // Persian
    {   6,   7},  // Psyduck
    {  26,   7},  // Psyduck
    {  27,   9},  // Psyduck
    {   8,  11},  // Psyduck
    {  19,  33},  // Psyduck
    {  46,  51},  // Psyduck
    {   6,   7},  // Golduck
    {  26,   7},  // Golduck
    {  27,   9},  // Golduck
    {   8,  11},  // Golduck
    {  19,  33},  // Golduck
    {  46,  51},  // Golduck
    {   5,   3},  // Mankey
    {   6,   7},  // Mankey
    {   5,   3},  // Primeape
    {   6,   7},  // Primeape
    {   7,  11},  // Growlithe
    {   8,  11},  // Growlithe
    {   9,  22},  // Growlithe
    {  10,  22},  // Growlithe
    {   7,  11},  // Arcanine
    {   8,  11},  // Arcanine
    {  33,  16},  // Arcanine
    {   9,  22},  // Arcanine
    {  10,  22},  // Arcanine
    {  24,   3},  // Poliwag
    {  27,   9},  // Poliwag
    {  25,  41},  // Poliwag
    {  46,  51},  // Poliwag
    {  24,   3},  // Poliwhirl
    {  27,   9},  // Poliwhirl
    {  25,  41},  // Poliwhirl
    {  46,  51},  // Poliwhirl
    {  24,   3},  // Poliwrath
    {  27,   9},  // Poliwrath
    {  25,  41},  // Poliwrath
    {  46,  51},  // Poliwrath
    {   7,  11},  // Abra
    {   8,  11},  // Abra
    {   9,  22},  // Abra
    {  10,  22},  // Abra
    {   7,  11},  // Kadabra
    {   8,  11},  // Kadabra
    {   9,  22},  // Kadabra
    {  10,  22},  // Kadabra
    {   7,  11},  // Alakazam
    {   8,  11},  // Alakazam
    {   9,  22},  // Alakazam
    {  10,  22},  // Alakazam
    {  41,  18},  // Machop
    {  45,  41},  // Machop
    {  41,  18},  // Machoke
    {  45,  41},  // Machoke
    {  41,  18},  // Machamp
    {  45,  41},  // Machamp
    {   3,   3},  // Bellsprout
    {   4,   3},  // Bellsprout
    {  39,   3},  // Bellsprout
    {  26,   7},  // Bellsprout
    {  27,   9},  // Bellsprout
    {  14,  31},  // Bellsprout
    {  15,  33},  // Bellsprout
    {  16,  33},  // Bellsprout
    {  17,  33},  // Bellsprout
    {  23,  37},  // Bellsprout
    {   3,   3},  // Weepinbell
    {   4,   3},  // Weepinbell
    {  39,   3},  // Weepinbell
    {  26,   7},  // Weepinbell
    {  27,   9},  // Weepinbell
    {  14,  31},  // Weepinbell
    {  15,  33},  // Weepinbell
    {  16,  33},  // Weepinbell
    {  17,  33},  // Weepinbell
    {  23,  37},  // Weepinbell
    {   3,   3},  // Victreebel
    {   4,   3},  // Victreebel
    {  39,   3},  // Victreebel
    {  26,   7},  // Victreebel
    {  27,   9},  // Victreebel
    {  14,  31},  // Victreebel
    {  15,  33},  // Victreebel
    {  16,  33},  // Victreebel
    {  17,  33},  // Victreebel
    {  23,  37},  // Victreebel
    {   6,   7},  // Tentacool
    {  26,   7},  // Tentacool
    {  13,  13},  // Tentacool
    {  12,  18},  // Tentacool
    {  14,  31},  // Tentacool
    {  15,  33},  // Tentacool
    {  20,  33},  // Tentacool
    {  21,  37},  // Tentacool
    {  22,  37},  // Tentacool
    {  23,  37},  // Tentacool
    {  22,  39},  // Tentacool
    {  44,  39},  // Tentacool
    {   6,   7},  // Tentacruel
    {  26,   7},  // Tentacruel
    {  13,  13},  // Tentacruel
    {  12,  18},  // Tentacruel
    {  14,  31},  // Tentacruel
    {  15,  33},  // Tentacruel
    {  20,  33},  // Tentacruel
    {  21,  37},  // Tentacruel
    {  22,  37},  // Tentacruel
    {  23,  37},  // Tentacruel
    {  22,  39},  // Tentacruel
    {  44,  39},  // Tentacruel
    {  40,   5},  // Geodude
    {  41,  18},  // Geodude
    {  45,  41},  // Geodude
    {  46,  51},  // Geodude
    {  40,   5},  // Graveler
    {  41,  18},  // Graveler
    {  45,  41},  // Graveler
    {  46,  51},  // Graveler
    {  40,   5},  // Golem
    {  41,  18},  // Golem
    {  45,  41},  // Golem
    {  46,  51},  // Golem
    {  19,  33},  // Ponyta
    {  19,  33},  // Rapidash
    {  44,  39},  // Slowpoke
    {  44,  39},  // Slowbro
    {  42,  37},  // Magnemite
    {  42,  37},  // Magneton
    {  14,  31},  // Farfetchd
    {  15,  33},  // Farfetchd
    {  18,  31},  // Doduo
    {  19,  33},  // Doduo
    {  20,  33},  // Doduo
    {  18,  31},  // Dodrio
    {  19,  33},  // Dodrio
    {  20,  33},  // Dodrio
    {  44,  39},  // Seel
    {  44,  39},  // Dewgong
    {  42,  37},  // Grimer
    {  51,  39},  // Grimer
    {  42,  37},  // Muk
    {  51,  39},  // Muk
    {  44,  39},  // Shellder
    {  44,  39},  // Cloyster
    {  47,  27},  // Gastly
    {  47,  27},  // Haunter
    {  47,  27},  // Gengar
    {  40,   5},  // Onix
    {  41,  18},  // Onix
    {  45,  41},  // Onix
    {  13,  13},  // Drowzee
    {  13,  13},  // Hypno
    {  12,  18},  // Krabby
    {  14,  31},  // Krabby
    {  15,  33},  // Krabby
    {  12,  18},  // Kingler
    {  14,  31},  // Kingler
    {  15,  33},  // Kingler
    {  42,  37},  // Voltorb
    {  42,  37},  // Electrode
    {  25,  41},  // Exeggcute
    {  25,  41},  // Exeggutor
    {  41,  18},  // Cubone
    {  47,  27},  // Cubone
    {  41,  18},  // Marowak
    {  47,  27},  // Marowak
    {  38,  30},  // Hitmonlee
    {  45,  41},  // Hitmonlee
    {  38,  30},  // Hitmonchan
    {  45,  41},  // Hitmonchan
    {  46,  51},  // Lickitung
    {  42,  37},  // Koffing
    {  51,  39},  // Koffing
    {  42,  37},  // Weezing
    {  51,  39},  // Weezing
    {  41,  18},  // Rhyhorn
    {  45,  41},  // Rhyhorn
    {  46,  51},  // Rhyhorn
    {  41,  18},  // Rhydon
    {  45,  41},  // Rhydon
    {  46,  51},  // Rhydon
    {  13,   3},  // Chansey
    {  17,   3},  // Chansey
    {  40,   5},  // Chansey
    {   7,  11},  // Chansey
    {   8,  11},  // Chansey
    {  43,  13},  // Chansey
    {  11,  17},  // Chansey
    {  12,  18},  // Chansey
    {  10,  22},  // Chansey
    {  47,  27},  // Chansey
    {  14,  31},  // Chansey
    {  18,  31},  // Chansey
    {  15,  33},  // Chansey
    {  16,  33},  // Chansey
    {  19,  33},  // Chansey
    {  20,  33},  // Chansey
    {  23,  37},  // Chansey
    {  42,  37},  // Chansey
    {  51,  39},  // Chansey
    {  25,  41},  // Chansey
    {  45,  41},  // Chansey
    {  46,  51},  // Chansey
    {  23,  37},  // Tangela
    {  41,  18},  // Kangaskhan
    {  13,  13},  // Horsea
    {  14,  31},  // Horsea
    {  15,  33},  // Horsea
    {  13,  13},  // Seadra
    {  14,  31},  // Seadra
    {  15,  33},  // Seadra
    {   8,  11},  // Goldeen
    {   8,  11},  // Seaking
    {  20,  33},  // Staryu
    {  21,  37},  // Staryu
    {  23,  37},  // Staryu
    {  20,  33},  // Starmie
    {  21,  37},  // Starmie
    {  23,  37},  // Starmie
    {  13,  13},  // MrMime
    {  16,  33},  // Scyther
    {  17,  33},  // Scyther
    {  44,  39},  // Jynx
    {  42,  37},  // Electabuzz
    {  51,  39},  // Magmar
    {  16,  33},  // Pinsir
    {  17,  33},  // Pinsir
    {  16,  33},  // Tauros
    {  17,  33},  // Tauros
    {  24,   3},  // Magikarp
    {   6,   5},  // Magikarp
    {   6,   7},  // Magikarp
    {  26,   7},  // Magikarp
    {  27,   9},  // Magikarp
    {   8,  11},  // Magikarp
    {  13,  13},  // Magikarp
    {  12,  18},  // Magikarp
    {  14,  31},  // Magikarp
    {  15,  33},  // Magikarp
    {  20,  33},  // Magikarp
    {  21,  37},  // Magikarp
    {  22,  37},  // Magikarp
    {  23,  37},  // Magikarp
    {  22,  39},  // Magikarp
    {  44,  39},  // Magikarp
    {  25,  41},  // Magikarp
    {  46,  51},  // Magikarp
    {  24,   3},  // Gyarados
    {   6,   5},  // Gyarados
    {   6,   7},  // Gyarados
    {  26,   7},  // Gyarados
    {  27,   9},  // Gyarados
    {   8,  11},  // Gyarados
    {  13,  13},  // Gyarados
    {  12,  18},  // Gyarados
    {  14,  31},  // Gyarados
    {  15,  33},  // Gyarados
    {  20,  33},  // Gyarados
    {  21,  37},  // Gyarados
    {  22,  37},  // Gyarados
    {  23,  37},  // Gyarados
    {  22,  39},  // Gyarados
    {  44,  39},  // Gyarados
    {  25,  41},  // Gyarados
    {  46,  51},  // Gyarados
    {  52,  34},  // Lapras
    {  21,  37},  // Lapras
    {  22,  37},  // Lapras
    {  51,  39},  // Ditto
    {  46,  51},  // Ditto
    {  28,   5},  // Eevee
    {  19,  33},  // Eevee
    {  28,   5},  // Vaporeon
    {  19,  33},  // Vaporeon
    {  28,   5},  // Jolteon
    {  19,  33},  // Jolteon
    {  28,   5},  // Flareon
    {  19,  33},  // Flareon
    {   9,  22},  // Porygon
    {  38,  34},  // Porygon
    {  36,  44},  // Omanyte
    {  36,  44},  // Omastar
    {  36,  44},  // Kabuto
    {  36,  44},  // Kabutops
    {  36,  44},  // Aerodactyl
    {  14,  34},  // Snorlax
    {  18,  34},  // Snorlax
    {  46,  51},  // Snorlax
    {   3,   3},  // Articuno
    {   4,   3},  // Articuno
    {   5,   3},  // Articuno
    {   6,   3},  // Articuno
    {   9,   3},  // Articuno
    {  10,   3},  // Articuno
    {  11,   3},  // Articuno
    {  12,   3},  // Articuno
    {  13,   3},  // Articuno
    {  14,   3},  // Articuno
    {  15,   3},  // Articuno
    {  16,   3},  // Articuno
    {  17,   3},  // Articuno
    {  18,   3},  // Articuno
    {  19,   3},  // Articuno
    {  20,   3},  // Articuno
    {  21,   3},  // Articuno
    {  22,   3},  // Articuno
    {  23,   3},  // Articuno
    {  24,   3},  // Articuno
    {  25,   3},  // Articuno
    {  26,   3},  // Articuno
    {  27,   3},  // Articuno
    {  44,  50},  // Articuno
    {   3,   3},  // Zapdos
    {   4,   3},  // Zapdos
    {   5,   3},  // Zapdos
    {   6,   3},  // Zapdos
    {   9,   3},  // Zapdos
    {  10,   3},  // Zapdos
    {  11,   3},  // Zapdos
    {  12,   3},  // Zapdos
    {  13,   3},  // Zapdos
    {  14,   3},  // Zapdos
    {  15,   3},  // Zapdos
    {  16,   3},  // Zapdos
    {  17,   3},  // Zapdos
    {  18,   3},  // Zapdos
    {  19,   3},  // Zapdos
    {  20,   3},  // Zapdos
    {  21,   3},  // Zapdos
    {  22,   3},  // Zapdos
    {  23,   3},  // Zapdos
    {  24,   3},  // Zapdos
    {  25,   3},  // Zapdos
    {  26,   3},  // Zapdos
    {  27,   3},  // Zapdos
    {  42,  50},  // Zapdos
    {   3,   3},  // Moltres
    {   4,   3},  // Moltres
    {   5,   3},  // Moltres
    {   6,   3},  // Moltres
    {   9,   3},  // Moltres
    {  10,   3},  // Moltres
    {  11,   3},  // Moltres
    {  12,   3},  // Moltres
    {  13,   3},  // Moltres
    {  14,   3},  // Moltres
    {  15,   3},  // Moltres
    {  16,   3},  // Moltres
    {  17,   3},  // Moltres
    {  18,   3},  // Moltres
    {  19,   3},  // Moltres
    {  20,   3},  // Moltres
    {  21,   3},  // Moltres
    {  22,   3},  // Moltres
    {  23,   3},  // Moltres
    {  24,   3},  // Moltres
    {  25,   3},  // Moltres
    {  26,   3},  // Moltres
    {  27,   3},  // Moltres
    {  45,  50},  // Moltres
    {  12,  18},  // Dratini
    {  12,  18},  // Dragonair
    {   3,   3},  // Dragonite
    {   4,   3},  // Dragonite
    {   5,   3},  // Dragonite
    {   6,   3},  // Dragonite
    {   9,   3},  // Dragonite
    {  10,   3},  // Dragonite
    {  11,   3},  // Dragonite
    {  12,   3},  // Dragonite
    {  13,   3},  // Dragonite
    {  14,   3},  // Dragonite
    {  15,   3},  // Dragonite
    {  16,   3},  // Dragonite
    {  17,   3},  // Dragonite
    {  18,   3},  // Dragonite
    {  19,   3},  // Dragonite
    {  20,   3},  // Dragonite
    {  21,   3},  // Dragonite
    {  22,   3},  // Dragonite
    {  23,   3},  // Dragonite
    {  24,   3},  // Dragonite
    {  25,   3},  // Dragonite
    {  26,   3},  // Dragonite
    {  27,   3},  // Dragonite
    {  12,  18},  // Dragonite
    {  46,  70},  // Mewtwo
};

struct Faixa {
  std::uint16_t dex;
  std::uint16_t inicio;
  std::uint8_t  n;
};

inline constexpr Faixa kFaixas[] = {
    {   1,     0,   2},
    {   2,     2,   2},
    {   3,     4,   2},
    {   4,     6,   4},
    {   5,    10,   4},
    {   6,    14,  26},
    {   7,    40,   4},
    {   8,    44,   4},
    {   9,    48,   4},
    {  10,    52,   2},
    {  11,    54,   2},
    {  12,    56,   2},
    {  13,    58,   2},
    {  14,    60,   2},
    {  15,    62,   2},
    {  16,    64,  35},
    {  17,    99,  35},
    {  18,   134,  35},
    {  19,   169,  17},
    {  20,   186,  17},
    {  21,   203,  10},
    {  22,   213,  10},
    {  23,   223,   2},
    {  24,   225,   2},
    {  25,   227,   2},
    {  26,   229,   2},
    {  27,   231,   2},
    {  28,   233,   2},
    {  29,   235,   4},
    {  30,   239,   4},
    {  31,   243,   4},
    {  32,   247,   4},
    {  33,   251,   4},
    {  34,   255,   4},
    {  35,   259,   1},
    {  36,   260,   1},
    {  37,   261,   4},
    {  38,   265,   4},
    {  39,   269,   4},
    {  40,   273,   4},
    {  41,   277,   7},
    {  42,   284,   7},
    {  43,   291,  10},
    {  44,   301,  10},
    {  45,   311,  10},
    {  46,   321,   1},
    {  47,   322,   1},
    {  48,   323,   4},
    {  49,   327,   4},
    {  50,   331,   1},
    {  51,   332,   1},
    {  52,   333,   2},
    {  53,   335,   3},
    {  54,   338,   6},
    {  55,   344,   6},
    {  56,   350,   2},
    {  57,   352,   2},
    {  58,   354,   4},
    {  59,   358,   5},
    {  60,   363,   4},
    {  61,   367,   4},
    {  62,   371,   4},
    {  63,   375,   4},
    {  64,   379,   4},
    {  65,   383,   4},
    {  66,   387,   2},
    {  67,   389,   2},
    {  68,   391,   2},
    {  69,   393,  10},
    {  70,   403,  10},
    {  71,   413,  10},
    {  72,   423,  12},
    {  73,   435,  12},
    {  74,   447,   4},
    {  75,   451,   4},
    {  76,   455,   4},
    {  77,   459,   1},
    {  78,   460,   1},
    {  79,   461,   1},
    {  80,   462,   1},
    {  81,   463,   1},
    {  82,   464,   1},
    {  83,   465,   2},
    {  84,   467,   3},
    {  85,   470,   3},
    {  86,   473,   1},
    {  87,   474,   1},
    {  88,   475,   2},
    {  89,   477,   2},
    {  90,   479,   1},
    {  91,   480,   1},
    {  92,   481,   1},
    {  93,   482,   1},
    {  94,   483,   1},
    {  95,   484,   3},
    {  96,   487,   1},
    {  97,   488,   1},
    {  98,   489,   3},
    {  99,   492,   3},
    { 100,   495,   1},
    { 101,   496,   1},
    { 102,   497,   1},
    { 103,   498,   1},
    { 104,   499,   2},
    { 105,   501,   2},
    { 106,   503,   2},
    { 107,   505,   2},
    { 108,   507,   1},
    { 109,   508,   2},
    { 110,   510,   2},
    { 111,   512,   3},
    { 112,   515,   3},
    { 113,   518,  22},
    { 114,   540,   1},
    { 115,   541,   1},
    { 116,   542,   3},
    { 117,   545,   3},
    { 118,   548,   1},
    { 119,   549,   1},
    { 120,   550,   3},
    { 121,   553,   3},
    { 122,   556,   1},
    { 123,   557,   2},
    { 124,   559,   1},
    { 125,   560,   1},
    { 126,   561,   1},
    { 127,   562,   2},
    { 128,   564,   2},
    { 129,   566,  18},
    { 130,   584,  18},
    { 131,   602,   3},
    { 132,   605,   2},
    { 133,   607,   2},
    { 134,   609,   2},
    { 135,   611,   2},
    { 136,   613,   2},
    { 137,   615,   2},
    { 138,   617,   1},
    { 139,   618,   1},
    { 140,   619,   1},
    { 141,   620,   1},
    { 142,   621,   1},
    { 143,   622,   3},
    { 144,   625,  24},
    { 145,   649,  24},
    { 146,   673,  24},
    { 147,   697,   1},
    { 148,   698,   1},
    { 149,   699,  24},
    { 150,   723,   1},
};

// O encontro de MAIOR nivel que ainda cabe num Pokemon deste nivel.
// nullptr = a especie nao aparece no LGPE, ou todo encontro dela exige
// nivel maior que o atual (um Raticate nivel 5 nao cabe no encontro lv 9).
inline const Encontro* Acha(std::uint16_t dex, std::uint8_t nivel_atual) {
  for (const auto& f : kFaixas) {
    if (f.dex != dex) continue;
    const Encontro* melhor = nullptr;
    for (std::uint8_t i = 0; i < f.n; ++i) {
      const Encontro& e = kEncontros[f.inicio + i];
      if (e.nivel <= nivel_atual) melhor = &e;  // ordenados por nivel
    }
    return melhor;
  }
  return nullptr;
}

}  // namespace pokehome::lgpe
