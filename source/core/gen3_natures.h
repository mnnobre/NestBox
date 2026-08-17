// Nomes de natureza. GERADO — nao editar a mao.
// Fonte: PKHeX text resources (ver CREDITS.md). A natureza deriva de
// personality % 25.

#pragma once

namespace pokehome::gen3 {

inline constexpr const char* kNatureNames[] = {
    "Hardy",
    "Lonely",
    "Brave",
    "Adamant",
    "Naughty",
    "Bold",
    "Docile",
    "Relaxed",
    "Impish",
    "Lax",
    "Timid",
    "Hasty",
    "Serious",
    "Jolly",
    "Naive",
    "Modest",
    "Mild",
    "Quiet",
    "Bashful",
    "Rash",
    "Calm",
    "Gentle",
    "Sassy",
    "Careful",
    "Quirky",
};

inline constexpr int kNatureCount = sizeof(kNatureNames) / sizeof(kNatureNames[0]);

}  // namespace pokehome::gen3
