// Nomes de especie por indice INTERNO do gen3 (nao National Dex).
//
// Os indices 1-251 coincidem com a National Dex; 252-276 sao invalidos (usados
// internamente para formas de Unown); 277-411 mapeiam para 252-386 deslocados.
//
// GERADO — nao editar a mao. Fonte: PKHeX text resources (ver CREDITS.md),
// mapeamento de indice conferido contra a tabela gen3.

#pragma once

namespace pokehome::gen3 {

inline constexpr const char* kSpeciesNames[] = {
    "???",  // 0
    "Bulbasaur",  // 1
    "Ivysaur",  // 2
    "Venusaur",  // 3
    "Charmander",  // 4
    "Charmeleon",  // 5
    "Charizard",  // 6
    "Squirtle",  // 7
    "Wartortle",  // 8
    "Blastoise",  // 9
    "Caterpie",  // 10
    "Metapod",  // 11
    "Butterfree",  // 12
    "Weedle",  // 13
    "Kakuna",  // 14
    "Beedrill",  // 15
    "Pidgey",  // 16
    "Pidgeotto",  // 17
    "Pidgeot",  // 18
    "Rattata",  // 19
    "Raticate",  // 20
    "Spearow",  // 21
    "Fearow",  // 22
    "Ekans",  // 23
    "Arbok",  // 24
    "Pikachu",  // 25
    "Raichu",  // 26
    "Sandshrew",  // 27
    "Sandslash",  // 28
    "Nidoran♀",  // 29
    "Nidorina",  // 30
    "Nidoqueen",  // 31
    "Nidoran♂",  // 32
    "Nidorino",  // 33
    "Nidoking",  // 34
    "Clefairy",  // 35
    "Clefable",  // 36
    "Vulpix",  // 37
    "Ninetales",  // 38
    "Jigglypuff",  // 39
    "Wigglytuff",  // 40
    "Zubat",  // 41
    "Golbat",  // 42
    "Oddish",  // 43
    "Gloom",  // 44
    "Vileplume",  // 45
    "Paras",  // 46
    "Parasect",  // 47
    "Venonat",  // 48
    "Venomoth",  // 49
    "Diglett",  // 50
    "Dugtrio",  // 51
    "Meowth",  // 52
    "Persian",  // 53
    "Psyduck",  // 54
    "Golduck",  // 55
    "Mankey",  // 56
    "Primeape",  // 57
    "Growlithe",  // 58
    "Arcanine",  // 59
    "Poliwag",  // 60
    "Poliwhirl",  // 61
    "Poliwrath",  // 62
    "Abra",  // 63
    "Kadabra",  // 64
    "Alakazam",  // 65
    "Machop",  // 66
    "Machoke",  // 67
    "Machamp",  // 68
    "Bellsprout",  // 69
    "Weepinbell",  // 70
    "Victreebel",  // 71
    "Tentacool",  // 72
    "Tentacruel",  // 73
    "Geodude",  // 74
    "Graveler",  // 75
    "Golem",  // 76
    "Ponyta",  // 77
    "Rapidash",  // 78
    "Slowpoke",  // 79
    "Slowbro",  // 80
    "Magnemite",  // 81
    "Magneton",  // 82
    "Farfetch’d",  // 83
    "Doduo",  // 84
    "Dodrio",  // 85
    "Seel",  // 86
    "Dewgong",  // 87
    "Grimer",  // 88
    "Muk",  // 89
    "Shellder",  // 90
    "Cloyster",  // 91
    "Gastly",  // 92
    "Haunter",  // 93
    "Gengar",  // 94
    "Onix",  // 95
    "Drowzee",  // 96
    "Hypno",  // 97
    "Krabby",  // 98
    "Kingler",  // 99
    "Voltorb",  // 100
    "Electrode",  // 101
    "Exeggcute",  // 102
    "Exeggutor",  // 103
    "Cubone",  // 104
    "Marowak",  // 105
    "Hitmonlee",  // 106
    "Hitmonchan",  // 107
    "Lickitung",  // 108
    "Koffing",  // 109
    "Weezing",  // 110
    "Rhyhorn",  // 111
    "Rhydon",  // 112
    "Chansey",  // 113
    "Tangela",  // 114
    "Kangaskhan",  // 115
    "Horsea",  // 116
    "Seadra",  // 117
    "Goldeen",  // 118
    "Seaking",  // 119
    "Staryu",  // 120
    "Starmie",  // 121
    "Mr. Mime",  // 122
    "Scyther",  // 123
    "Jynx",  // 124
    "Electabuzz",  // 125
    "Magmar",  // 126
    "Pinsir",  // 127
    "Tauros",  // 128
    "Magikarp",  // 129
    "Gyarados",  // 130
    "Lapras",  // 131
    "Ditto",  // 132
    "Eevee",  // 133
    "Vaporeon",  // 134
    "Jolteon",  // 135
    "Flareon",  // 136
    "Porygon",  // 137
    "Omanyte",  // 138
    "Omastar",  // 139
    "Kabuto",  // 140
    "Kabutops",  // 141
    "Aerodactyl",  // 142
    "Snorlax",  // 143
    "Articuno",  // 144
    "Zapdos",  // 145
    "Moltres",  // 146
    "Dratini",  // 147
    "Dragonair",  // 148
    "Dragonite",  // 149
    "Mewtwo",  // 150
    "Mew",  // 151
    "Chikorita",  // 152
    "Bayleef",  // 153
    "Meganium",  // 154
    "Cyndaquil",  // 155
    "Quilava",  // 156
    "Typhlosion",  // 157
    "Totodile",  // 158
    "Croconaw",  // 159
    "Feraligatr",  // 160
    "Sentret",  // 161
    "Furret",  // 162
    "Hoothoot",  // 163
    "Noctowl",  // 164
    "Ledyba",  // 165
    "Ledian",  // 166
    "Spinarak",  // 167
    "Ariados",  // 168
    "Crobat",  // 169
    "Chinchou",  // 170
    "Lanturn",  // 171
    "Pichu",  // 172
    "Cleffa",  // 173
    "Igglybuff",  // 174
    "Togepi",  // 175
    "Togetic",  // 176
    "Natu",  // 177
    "Xatu",  // 178
    "Mareep",  // 179
    "Flaaffy",  // 180
    "Ampharos",  // 181
    "Bellossom",  // 182
    "Marill",  // 183
    "Azumarill",  // 184
    "Sudowoodo",  // 185
    "Politoed",  // 186
    "Hoppip",  // 187
    "Skiploom",  // 188
    "Jumpluff",  // 189
    "Aipom",  // 190
    "Sunkern",  // 191
    "Sunflora",  // 192
    "Yanma",  // 193
    "Wooper",  // 194
    "Quagsire",  // 195
    "Espeon",  // 196
    "Umbreon",  // 197
    "Murkrow",  // 198
    "Slowking",  // 199
    "Misdreavus",  // 200
    "Unown",  // 201
    "Wobbuffet",  // 202
    "Girafarig",  // 203
    "Pineco",  // 204
    "Forretress",  // 205
    "Dunsparce",  // 206
    "Gligar",  // 207
    "Steelix",  // 208
    "Snubbull",  // 209
    "Granbull",  // 210
    "Qwilfish",  // 211
    "Scizor",  // 212
    "Shuckle",  // 213
    "Heracross",  // 214
    "Sneasel",  // 215
    "Teddiursa",  // 216
    "Ursaring",  // 217
    "Slugma",  // 218
    "Magcargo",  // 219
    "Swinub",  // 220
    "Piloswine",  // 221
    "Corsola",  // 222
    "Remoraid",  // 223
    "Octillery",  // 224
    "Delibird",  // 225
    "Mantine",  // 226
    "Skarmory",  // 227
    "Houndour",  // 228
    "Houndoom",  // 229
    "Kingdra",  // 230
    "Phanpy",  // 231
    "Donphan",  // 232
    "Porygon2",  // 233
    "Stantler",  // 234
    "Smeargle",  // 235
    "Tyrogue",  // 236
    "Hitmontop",  // 237
    "Smoochum",  // 238
    "Elekid",  // 239
    "Magby",  // 240
    "Miltank",  // 241
    "Blissey",  // 242
    "Raikou",  // 243
    "Entei",  // 244
    "Suicune",  // 245
    "Larvitar",  // 246
    "Pupitar",  // 247
    "Tyranitar",  // 248
    "Lugia",  // 249
    "Ho-Oh",  // 250
    "Celebi",  // 251
    "???",  // 252
    "???",  // 253
    "???",  // 254
    "???",  // 255
    "???",  // 256
    "???",  // 257
    "???",  // 258
    "???",  // 259
    "???",  // 260
    "???",  // 261
    "???",  // 262
    "???",  // 263
    "???",  // 264
    "???",  // 265
    "???",  // 266
    "???",  // 267
    "???",  // 268
    "???",  // 269
    "???",  // 270
    "???",  // 271
    "???",  // 272
    "???",  // 273
    "???",  // 274
    "???",  // 275
    "???",  // 276
    "Treecko",  // 277
    "Grovyle",  // 278
    "Sceptile",  // 279
    "Torchic",  // 280
    "Combusken",  // 281
    "Blaziken",  // 282
    "Mudkip",  // 283
    "Marshtomp",  // 284
    "Swampert",  // 285
    "Poochyena",  // 286
    "Mightyena",  // 287
    "Zigzagoon",  // 288
    "Linoone",  // 289
    "Wurmple",  // 290
    "Silcoon",  // 291
    "Beautifly",  // 292
    "Cascoon",  // 293
    "Dustox",  // 294
    "Lotad",  // 295
    "Lombre",  // 296
    "Ludicolo",  // 297
    "Seedot",  // 298
    "Nuzleaf",  // 299
    "Shiftry",  // 300
    "Nincada",  // 301
    "Ninjask",  // 302
    "Shedinja",  // 303
    "Taillow",  // 304
    "Swellow",  // 305
    "Shroomish",  // 306
    "Breloom",  // 307
    "Spinda",  // 308
    "Wingull",  // 309
    "Pelipper",  // 310
    "Surskit",  // 311
    "Masquerain",  // 312
    "Wailmer",  // 313
    "Wailord",  // 314
    "Skitty",  // 315
    "Delcatty",  // 316
    "Kecleon",  // 317
    "Baltoy",  // 318
    "Claydol",  // 319
    "Nosepass",  // 320
    "Torkoal",  // 321
    "Sableye",  // 322
    "Barboach",  // 323
    "Whiscash",  // 324
    "Luvdisc",  // 325
    "Corphish",  // 326
    "Crawdaunt",  // 327
    "Feebas",  // 328
    "Milotic",  // 329
    "Carvanha",  // 330
    "Sharpedo",  // 331
    "Trapinch",  // 332
    "Vibrava",  // 333
    "Flygon",  // 334
    "Makuhita",  // 335
    "Hariyama",  // 336
    "Electrike",  // 337
    "Manectric",  // 338
    "Numel",  // 339
    "Camerupt",  // 340
    "Spheal",  // 341
    "Sealeo",  // 342
    "Walrein",  // 343
    "Cacnea",  // 344
    "Cacturne",  // 345
    "Snorunt",  // 346
    "Glalie",  // 347
    "Lunatone",  // 348
    "Solrock",  // 349
    "Azurill",  // 350
    "Spoink",  // 351
    "Grumpig",  // 352
    "Plusle",  // 353
    "Minun",  // 354
    "Mawile",  // 355
    "Meditite",  // 356
    "Medicham",  // 357
    "Swablu",  // 358
    "Altaria",  // 359
    "Wynaut",  // 360
    "Duskull",  // 361
    "Dusclops",  // 362
    "Roselia",  // 363
    "Slakoth",  // 364
    "Vigoroth",  // 365
    "Slaking",  // 366
    "Gulpin",  // 367
    "Swalot",  // 368
    "Tropius",  // 369
    "Whismur",  // 370
    "Loudred",  // 371
    "Exploud",  // 372
    "Clamperl",  // 373
    "Huntail",  // 374
    "Gorebyss",  // 375
    "Absol",  // 376
    "Shuppet",  // 377
    "Banette",  // 378
    "Seviper",  // 379
    "Zangoose",  // 380
    "Relicanth",  // 381
    "Aron",  // 382
    "Lairon",  // 383
    "Aggron",  // 384
    "Castform",  // 385
    "Volbeat",  // 386
    "Illumise",  // 387
    "Lileep",  // 388
    "Cradily",  // 389
    "Anorith",  // 390
    "Armaldo",  // 391
    "Ralts",  // 392
    "Kirlia",  // 393
    "Gardevoir",  // 394
    "Bagon",  // 395
    "Shelgon",  // 396
    "Salamence",  // 397
    "Beldum",  // 398
    "Metang",  // 399
    "Metagross",  // 400
    "Regirock",  // 401
    "Regice",  // 402
    "Registeel",  // 403
    "Kyogre",  // 404
    "Groudon",  // 405
    "Rayquaza",  // 406
    "Latias",  // 407
    "Latios",  // 408
    "Jirachi",  // 409
    "Deoxys",  // 410
    "Chimecho",  // 411
};

inline constexpr int kSpeciesCount = sizeof(kSpeciesNames) / sizeof(kSpeciesNames[0]);

// National Dex correspondente a cada indice INTERNO do gen3. 0 = invalido.
// Os arquivos de sprite do PokeAPI sao nomeados por National Dex, entao esta
// tabela e a ponte entre o que o save diz e o arquivo em disco.
inline constexpr int kNationalDex[] = {
    0,  // interno 0
    1,  // interno 1
    2,  // interno 2
    3,  // interno 3
    4,  // interno 4
    5,  // interno 5
    6,  // interno 6
    7,  // interno 7
    8,  // interno 8
    9,  // interno 9
    10,  // interno 10
    11,  // interno 11
    12,  // interno 12
    13,  // interno 13
    14,  // interno 14
    15,  // interno 15
    16,  // interno 16
    17,  // interno 17
    18,  // interno 18
    19,  // interno 19
    20,  // interno 20
    21,  // interno 21
    22,  // interno 22
    23,  // interno 23
    24,  // interno 24
    25,  // interno 25
    26,  // interno 26
    27,  // interno 27
    28,  // interno 28
    29,  // interno 29
    30,  // interno 30
    31,  // interno 31
    32,  // interno 32
    33,  // interno 33
    34,  // interno 34
    35,  // interno 35
    36,  // interno 36
    37,  // interno 37
    38,  // interno 38
    39,  // interno 39
    40,  // interno 40
    41,  // interno 41
    42,  // interno 42
    43,  // interno 43
    44,  // interno 44
    45,  // interno 45
    46,  // interno 46
    47,  // interno 47
    48,  // interno 48
    49,  // interno 49
    50,  // interno 50
    51,  // interno 51
    52,  // interno 52
    53,  // interno 53
    54,  // interno 54
    55,  // interno 55
    56,  // interno 56
    57,  // interno 57
    58,  // interno 58
    59,  // interno 59
    60,  // interno 60
    61,  // interno 61
    62,  // interno 62
    63,  // interno 63
    64,  // interno 64
    65,  // interno 65
    66,  // interno 66
    67,  // interno 67
    68,  // interno 68
    69,  // interno 69
    70,  // interno 70
    71,  // interno 71
    72,  // interno 72
    73,  // interno 73
    74,  // interno 74
    75,  // interno 75
    76,  // interno 76
    77,  // interno 77
    78,  // interno 78
    79,  // interno 79
    80,  // interno 80
    81,  // interno 81
    82,  // interno 82
    83,  // interno 83
    84,  // interno 84
    85,  // interno 85
    86,  // interno 86
    87,  // interno 87
    88,  // interno 88
    89,  // interno 89
    90,  // interno 90
    91,  // interno 91
    92,  // interno 92
    93,  // interno 93
    94,  // interno 94
    95,  // interno 95
    96,  // interno 96
    97,  // interno 97
    98,  // interno 98
    99,  // interno 99
    100,  // interno 100
    101,  // interno 101
    102,  // interno 102
    103,  // interno 103
    104,  // interno 104
    105,  // interno 105
    106,  // interno 106
    107,  // interno 107
    108,  // interno 108
    109,  // interno 109
    110,  // interno 110
    111,  // interno 111
    112,  // interno 112
    113,  // interno 113
    114,  // interno 114
    115,  // interno 115
    116,  // interno 116
    117,  // interno 117
    118,  // interno 118
    119,  // interno 119
    120,  // interno 120
    121,  // interno 121
    122,  // interno 122
    123,  // interno 123
    124,  // interno 124
    125,  // interno 125
    126,  // interno 126
    127,  // interno 127
    128,  // interno 128
    129,  // interno 129
    130,  // interno 130
    131,  // interno 131
    132,  // interno 132
    133,  // interno 133
    134,  // interno 134
    135,  // interno 135
    136,  // interno 136
    137,  // interno 137
    138,  // interno 138
    139,  // interno 139
    140,  // interno 140
    141,  // interno 141
    142,  // interno 142
    143,  // interno 143
    144,  // interno 144
    145,  // interno 145
    146,  // interno 146
    147,  // interno 147
    148,  // interno 148
    149,  // interno 149
    150,  // interno 150
    151,  // interno 151
    152,  // interno 152
    153,  // interno 153
    154,  // interno 154
    155,  // interno 155
    156,  // interno 156
    157,  // interno 157
    158,  // interno 158
    159,  // interno 159
    160,  // interno 160
    161,  // interno 161
    162,  // interno 162
    163,  // interno 163
    164,  // interno 164
    165,  // interno 165
    166,  // interno 166
    167,  // interno 167
    168,  // interno 168
    169,  // interno 169
    170,  // interno 170
    171,  // interno 171
    172,  // interno 172
    173,  // interno 173
    174,  // interno 174
    175,  // interno 175
    176,  // interno 176
    177,  // interno 177
    178,  // interno 178
    179,  // interno 179
    180,  // interno 180
    181,  // interno 181
    182,  // interno 182
    183,  // interno 183
    184,  // interno 184
    185,  // interno 185
    186,  // interno 186
    187,  // interno 187
    188,  // interno 188
    189,  // interno 189
    190,  // interno 190
    191,  // interno 191
    192,  // interno 192
    193,  // interno 193
    194,  // interno 194
    195,  // interno 195
    196,  // interno 196
    197,  // interno 197
    198,  // interno 198
    199,  // interno 199
    200,  // interno 200
    201,  // interno 201
    202,  // interno 202
    203,  // interno 203
    204,  // interno 204
    205,  // interno 205
    206,  // interno 206
    207,  // interno 207
    208,  // interno 208
    209,  // interno 209
    210,  // interno 210
    211,  // interno 211
    212,  // interno 212
    213,  // interno 213
    214,  // interno 214
    215,  // interno 215
    216,  // interno 216
    217,  // interno 217
    218,  // interno 218
    219,  // interno 219
    220,  // interno 220
    221,  // interno 221
    222,  // interno 222
    223,  // interno 223
    224,  // interno 224
    225,  // interno 225
    226,  // interno 226
    227,  // interno 227
    228,  // interno 228
    229,  // interno 229
    230,  // interno 230
    231,  // interno 231
    232,  // interno 232
    233,  // interno 233
    234,  // interno 234
    235,  // interno 235
    236,  // interno 236
    237,  // interno 237
    238,  // interno 238
    239,  // interno 239
    240,  // interno 240
    241,  // interno 241
    242,  // interno 242
    243,  // interno 243
    244,  // interno 244
    245,  // interno 245
    246,  // interno 246
    247,  // interno 247
    248,  // interno 248
    249,  // interno 249
    250,  // interno 250
    251,  // interno 251
    0,  // interno 252
    0,  // interno 253
    0,  // interno 254
    0,  // interno 255
    0,  // interno 256
    0,  // interno 257
    0,  // interno 258
    0,  // interno 259
    0,  // interno 260
    0,  // interno 261
    0,  // interno 262
    0,  // interno 263
    0,  // interno 264
    0,  // interno 265
    0,  // interno 266
    0,  // interno 267
    0,  // interno 268
    0,  // interno 269
    0,  // interno 270
    0,  // interno 271
    0,  // interno 272
    0,  // interno 273
    0,  // interno 274
    0,  // interno 275
    0,  // interno 276
    252,  // interno 277
    253,  // interno 278
    254,  // interno 279
    255,  // interno 280
    256,  // interno 281
    257,  // interno 282
    258,  // interno 283
    259,  // interno 284
    260,  // interno 285
    261,  // interno 286
    262,  // interno 287
    263,  // interno 288
    264,  // interno 289
    265,  // interno 290
    266,  // interno 291
    267,  // interno 292
    268,  // interno 293
    269,  // interno 294
    270,  // interno 295
    271,  // interno 296
    272,  // interno 297
    273,  // interno 298
    274,  // interno 299
    275,  // interno 300
    290,  // interno 301
    291,  // interno 302
    292,  // interno 303
    276,  // interno 304
    277,  // interno 305
    285,  // interno 306
    286,  // interno 307
    327,  // interno 308
    278,  // interno 309
    279,  // interno 310
    283,  // interno 311
    284,  // interno 312
    320,  // interno 313
    321,  // interno 314
    300,  // interno 315
    301,  // interno 316
    352,  // interno 317
    343,  // interno 318
    344,  // interno 319
    299,  // interno 320
    324,  // interno 321
    302,  // interno 322
    339,  // interno 323
    340,  // interno 324
    370,  // interno 325
    341,  // interno 326
    342,  // interno 327
    349,  // interno 328
    350,  // interno 329
    318,  // interno 330
    319,  // interno 331
    328,  // interno 332
    329,  // interno 333
    330,  // interno 334
    296,  // interno 335
    297,  // interno 336
    309,  // interno 337
    310,  // interno 338
    322,  // interno 339
    323,  // interno 340
    363,  // interno 341
    364,  // interno 342
    365,  // interno 343
    331,  // interno 344
    332,  // interno 345
    361,  // interno 346
    362,  // interno 347
    337,  // interno 348
    338,  // interno 349
    298,  // interno 350
    325,  // interno 351
    326,  // interno 352
    311,  // interno 353
    312,  // interno 354
    303,  // interno 355
    307,  // interno 356
    308,  // interno 357
    333,  // interno 358
    334,  // interno 359
    360,  // interno 360
    355,  // interno 361
    356,  // interno 362
    315,  // interno 363
    287,  // interno 364
    288,  // interno 365
    289,  // interno 366
    316,  // interno 367
    317,  // interno 368
    357,  // interno 369
    293,  // interno 370
    294,  // interno 371
    295,  // interno 372
    366,  // interno 373
    367,  // interno 374
    368,  // interno 375
    359,  // interno 376
    353,  // interno 377
    354,  // interno 378
    336,  // interno 379
    335,  // interno 380
    369,  // interno 381
    304,  // interno 382
    305,  // interno 383
    306,  // interno 384
    351,  // interno 385
    313,  // interno 386
    314,  // interno 387
    345,  // interno 388
    346,  // interno 389
    347,  // interno 390
    348,  // interno 391
    280,  // interno 392
    281,  // interno 393
    282,  // interno 394
    371,  // interno 395
    372,  // interno 396
    373,  // interno 397
    374,  // interno 398
    375,  // interno 399
    376,  // interno 400
    377,  // interno 401
    378,  // interno 402
    379,  // interno 403
    382,  // interno 404
    383,  // interno 405
    384,  // interno 406
    380,  // interno 407
    381,  // interno 408
    385,  // interno 409
    386,  // interno 410
    358,  // interno 411
};

}  // namespace pokehome::gen3
