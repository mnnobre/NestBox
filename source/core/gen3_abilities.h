// Nomes das habilidades do gen3 (ids 0-76).
//
// O gen3 tem 77 habilidades, numeradas 0-76 — confirmado varrendo os bytes
// 22-23 de todas as 387 entradas de gen3_personal.h: os ids usados vao de 0 a
// 76, sem buracos.
//
// Os IDS vem do personal table ja embarcado; esta tabela so traduz id -> nome.
// Nomes em ingles, como aparecem no jogo (ver CREDITS.md — referencia PKHeX).

#pragma once

#include <cstdint>

namespace pokehome::gen3 {

inline constexpr int kAbilityCount = 77;

inline constexpr const char* kAbilityNames[kAbilityCount] = {
    "—",              // 0 — sem habilidade
    "Stench",         // 1
    "Drizzle",        // 2
    "Speed Boost",    // 3
    "Battle Armor",   // 4
    "Sturdy",         // 5
    "Damp",           // 6
    "Limber",         // 7
    "Sand Veil",      // 8
    "Static",         // 9
    "Volt Absorb",    // 10
    "Water Absorb",   // 11
    "Oblivious",      // 12
    "Cloud Nine",     // 13
    "Compound Eyes",  // 14
    "Insomnia",       // 15
    "Color Change",   // 16
    "Immunity",       // 17
    "Flash Fire",     // 18
    "Shield Dust",    // 19
    "Own Tempo",      // 20
    "Suction Cups",   // 21
    "Intimidate",     // 22
    "Shadow Tag",     // 23
    "Rough Skin",     // 24
    "Wonder Guard",   // 25
    "Levitate",       // 26
    "Effect Spore",   // 27
    "Synchronize",    // 28
    "Clear Body",     // 29
    "Natural Cure",   // 30
    "Lightning Rod",  // 31
    "Serene Grace",   // 32
    "Swift Swim",     // 33
    "Chlorophyll",    // 34
    "Illuminate",     // 35
    "Trace",          // 36
    "Huge Power",     // 37
    "Poison Point",   // 38
    "Inner Focus",    // 39
    "Magma Armor",    // 40
    "Water Veil",     // 41
    "Magnet Pull",    // 42
    "Soundproof",     // 43
    "Rain Dish",      // 44
    "Sand Stream",    // 45
    "Pressure",       // 46
    "Thick Fat",      // 47
    "Early Bird",     // 48
    "Flame Body",     // 49
    "Run Away",       // 50
    "Keen Eye",       // 51
    "Hyper Cutter",   // 52
    "Pickup",         // 53
    "Truant",         // 54
    "Hustle",         // 55
    "Cute Charm",     // 56
    "Plus",           // 57
    "Minus",          // 58
    "Forecast",       // 59
    "Sticky Hold",    // 60
    "Shed Skin",      // 61
    "Guts",           // 62
    "Marvel Scale",   // 63
    "Liquid Ooze",    // 64
    "Overgrow",       // 65
    "Blaze",          // 66
    "Torrent",        // 67
    "Swarm",          // 68
    "Rock Head",      // 69
    "Drought",        // 70
    "Arena Trap",     // 71
    "Vital Spirit",   // 72
    "White Smoke",    // 73
    "Pure Power",     // 74
    "Shell Armor",    // 75
    "Air Lock",       // 76
};

}  // namespace pokehome::gen3
