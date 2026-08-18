# Code/Research Credits

## PKHeX

This application would not have been possible without the research done at
<https://projectpokemon.org/>, the work done by the
[PKHeX](https://github.com/kwsch/PKHeX) developers, and the sprites archived by
<https://pokemondb.net/> and <https://www.bulbagarden.net/>.

The PKHeX code was used as a **reference** for this project's implementation of:

* Reading/writing PKM formats
* Reading/writing save file formats
* Encryption/decryption of PKM and save data
* Reading/writing binary resource files

The following resources are sourced directly from the PKHeX codebase:

* Species/form "personal data" (stats, typing, gender ratio, etc)
* Move learnsets
* Text resources (names of Pokémon, items, moves, locations, ribbons, etc)

## OpenHome

[OpenHome](https://github.com/andrewbenington/OpenHome) by Andrew Benington was
used as an architectural reference — particularly its approach to lossless
storage of cross-generation data and its save-conversion strategies.

**No OpenHome code is used in this project.** OpenHome and its `pkm_rs` crate
are licensed GPL-3.0+; this project reimplements save parsing independently and
does not link against them.

## PKVault

[PKVault](https://github.com/Chnapy/PKVault) by Chnapy was used as a reference
for UX decisions around centralized storage and Pokédex presentation.

## Libraries

* [borealis](https://github.com/xfangfang/borealis) — UI framework
* [libnx](https://github.com/switchbrew/libnx) / [devkitPro](https://devkitpro.org/) — Switch homebrew toolchain

## Sprites and game assets

All Pokémon sprites, names, and related game assets are © The Pokémon Company,
Nintendo, Game Freak, and Creatures Inc. They are used here for
non-commercial, personal use only. This project is not affiliated with or
endorsed by any of them.

## Disclaimer

This tool is intended for personal use with your own legally dumped games and
saves. The developers do not endorse piracy.

**Always back up your save files.** While care is taken to avoid corruption,
save data loss is always a possibility when third-party tools touch game saves.

## PokeAPI

Pokémon sprites are sourced from [PokeAPI/sprites](https://github.com/PokeAPI/sprites)
(generation III, Emerald). The same source used by PKVault.

Poké Ball item sprites (`romfs/ui/balls`) come from the same repository.
The base stat, ability name and type tables generated into
`source/core/gen9_base_stats.h` and `source/core/ability_names.h` are built
from the CSV data of [PokeAPI/pokeapi](https://github.com/PokeAPI/pokeapi)
by `tools/gen_modern_tables.py`.

## Type icons

Type icons (`romfs/ui/types`) are from
[partywhale/pokemon-type-icons](https://github.com/partywhale/pokemon-type-icons),
rasterized from the original SVGs.
