// GERADO por tools/gen_game_moves.py - nao editar a mao.
//
// Ate que numero de golpe cada jogo conhece (spec 038). Fonte: as
// constantes MaxMoveID_* de PKHeX.Core/Legality/Legal.cs, lidas e
// reimplementadas - nada do PKHeX e linkado.
// Ver spec/memory/contexto-tecnico.md sobre licenca.
//
// Os IDs de golpe sao cumulativos por geracao: cada geracao acrescenta
// no fim da numeracao e nunca remove nem renumera. Por isso um teto por
// jogo responde "este golpe existe neste jogo?" com exatidao, sem
// precisar de uma matriz golpe x jogo (TD-01 da spec 038).
//
// A ordem e a MESMA do enum Game de game_species.h - os dois sao
// indexados pelo mesmo valor.

#pragma once

#include <cstddef>
#include <cstdint>

#include "game_species.h"

namespace pokehome::compat {

// O maior ID de golpe que qualquer jogo desta tabela conhece.
inline constexpr int kMaxMoveId = 920;

// Teto de ID de golpe por jogo, na ordem do enum Game.
inline constexpr std::uint16_t kGameMaxMove[] = {
    165,  // Red/Blue (MaxMoveID_1)
    165,  // Yellow (MaxMoveID_1)
    251,  // Gold/Silver (MaxMoveID_2)
    251,  // Crystal (MaxMoveID_2)
    354,  // Ruby/Sapphire (MaxMoveID_3)
    354,  // Emerald (MaxMoveID_3)
    354,  // FireRed (MaxMoveID_3)
    354,  // LeafGreen (MaxMoveID_3)
    467,  // Diamond/Pearl (MaxMoveID_4)
    467,  // Platinum (MaxMoveID_4)
    467,  // HeartGold/SoulSilver (MaxMoveID_4)
    559,  // Black/White (MaxMoveID_5)
    559,  // Black 2/White 2 (MaxMoveID_5)
    617,  // X/Y (MaxMoveID_6_XY)
    621,  // Omega Ruby/Alpha Sapphire (MaxMoveID_6_AO)
    719,  // Sun/Moon (MaxMoveID_7)
    728,  // Ultra Sun/Moon (MaxMoveID_7_USUM)
    742,  // Let's Go (MaxMoveID_7b)
    826,  // Sword/Shield (MaxMoveID_8)
    826,  // Brilliant Diamond/Shining Pearl (MaxMoveID_8b)
    850,  // Legends: Arceus (MaxMoveID_8a)
    919,  // Scarlet/Violet (MaxMoveID_9)
    920,  // Legends: Z-A (MaxMoveID_9a)
};

static_assert(sizeof(kGameMaxMove) / sizeof(kGameMaxMove[0]) ==
                  static_cast<std::size_t>(Game::kCount),
              "a tabela de golpes tem de cobrir os mesmos jogos que a"
              " de especies");

// O golpe existe nos dados deste jogo? Nao pergunta se ESTA especie
// pode aprende-lo - isso e legalidade, e esta fora do escopo do
// indicador da secao 2 da pesquisa.
//
// Golpe 0 e slot vazio: existe em todo jogo, para nunca gerar aviso.
inline bool HasMove(Game game, int move) {
  if (move < 0 || move > kMaxMoveId) return false;
  if (move == 0) return true;
  const std::size_t g = static_cast<std::size_t>(game);
  if (g >= static_cast<std::size_t>(Game::kCount)) return false;
  return move <= static_cast<int>(kGameMaxMove[g]);
}

inline int MaxMoveId(Game game) {
  const std::size_t g = static_cast<std::size_t>(game);
  if (g >= static_cast<std::size_t>(Game::kCount)) return 0;
  return static_cast<int>(kGameMaxMove[g]);
}

// O primeiro golpe do conjunto que o jogo de destino nao conhece, ou 0
// se todos cabem. E o que a tela precisa: o aviso amarelo mostra QUAL
// golpe motivou o aviso.
//
// Repare que isto AVISA, nao bloqueia: quem recusa o movimento e
// HasSpecies (marca vermelha). Ver secao 2 da pesquisa.
inline int MissingMoveIn(Game game, const std::uint16_t* moves,
                         std::size_t count) {
  if (moves == nullptr) return 0;
  if (game == Game::kCount) return 0;
  for (std::size_t i = 0; i < count; ++i) {
    const int move = static_cast<int>(moves[i]);
    if (move != 0 && !HasMove(game, move)) return move;
  }
  return 0;
}

}  // namespace pokehome::compat
