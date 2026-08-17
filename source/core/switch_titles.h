// Gerado de blawar/titledb (US.en.json).
// ApplicationId e identidade estavel do jogo — o nome do NACP nao e:
// no console do dono, 223 de 225 saves vieram sem nome nenhum.
//
// Os marcados com [console] foram conferidos no Switch do dono.
#pragma once

#include <cstdint>

namespace nestbox {

struct SwitchTitle {
  std::uint64_t app_id;
  const char* name;
};

inline constexpr int kSwitchTitleCount = 28;

inline constexpr SwitchTitle kSwitchTitles[kSwitchTitleCount] = {
    {0x0100000011D90000, "PokemonTM Brilliant Diamond"},
    {0x010003F003A34000, "PokemonTM: Let's Go, Pikachu!"},
    {0x010008C01E742000, "PokemonTM Friends"},
    {0x010009F014D98000, "Pokemon TV"},
    {0x010015F008C54000, "PokemonTM HOME"},
    {0x0100187003A36000, "PokemonTM: Let's Go, Eevee!"},  // [console]
    {0x010018E011D92000, "PokemonTM Shining Pearl"},
    {0x01001F5010DFA000, "PokemonTM Legends: Arceus"},  // [console]
    {0x01002B5023434000, "(Spanish) Pokemon LeafGreen Version"},
    {0x010034D02340E000, "(English) Pokemon LeafGreen Version"},
    {0x01003D200BAA2000, "Pokemon Mystery DungeonTM: Rescue Team DX"},
    {0x01004B3023412000, "(French) Pokemon FireRed Version"},
    {0x0100554023408000, "(English) Pokemon FireRed Version"},  // [console]
    {0x01005B7008C52000, "PokemonTM Champions"},
    {0x01005D100807A000, "PokemonTM Quest"},
    {0x010072400E04A000, "Pokemon Cafe ReMix"},
    {0x010087C02342E000, "(French) Pokemon LeafGreen Version"},
    {0x01008DB008C2C000, "PokemonTM Shield"},
    {0x01008F6008C5E000, "PokemonTM Violet"},
    {0x0100930019B14000, "Tempoknight"},
    {0x0100939011ED4000, "Pokemon UNITE"},
    {0x0100A3D008C5C000, "PokemonTM Scarlet"},
    {0x0100ABF008968000, "PokemonTM Sword"},
    {0x0100B3F000BE2000, "Pokken TournamentTM DX"},
    {0x0100C37020CAE000, "Pokettohiro"},
    {0x0100EB702342C000, "(Spanish) Pokemon FireRed Version"},
    {0x0100F43008C44000, "PokemonTM Legends: Z-A"},  // [console]
    {0x0100F4300BF2C000, "New Pokemon SnapTM"},
};

}  // namespace nestbox
