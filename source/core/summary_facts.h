// Fatos exibidos na tela de summary (spec 103): characteristic derivada dos
// IVs e o memo de encontro. Header-only: so tabelas e formatacao.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace pokehome::summary {

// Characteristic: determinada pelo MAIOR IV; empate resolvido percorrendo os
// stats a partir de (ec % 6), na ordem do save (HP, Atk, Def, Spe, SpA, SpD).
// A frase e stat*5 + (maior IV % 5). Algoritmo do jogo, conferido no PKHeX
// (EntityCharacteristic). Gens 3-5 nao tem EC — usa-se o PID no lugar.
inline int CharacteristicIndex(const std::uint8_t ivs[6], std::uint32_t ec) {
  int max = 0;
  for (int i = 0; i < 6; ++i)
    if (ivs[i] > max) max = ivs[i];
  const int start = static_cast<int>(ec % 6);
  for (int i = 0; i < 6; ++i) {
    const int stat = (start + i) % 6;
    if (ivs[stat] == max) return stat * 5 + max % 5;
  }
  return 0;  // inalcancavel: algum stat sempre iguala o maximo
}

// As 30 frases, na ordem stat*5 + iv%5 (stats na ordem do save).
inline const char* CharacteristicText(int index) {
  static const char* kTexts[30] = {
      // HP
      "Loves to eat", "Takes plenty of siestas", "Nods off a lot",
      "Scatters things often", "Likes to relax",
      // Attack
      "Proud of its power", "Likes to thrash about", "A little quick tempered",
      "Likes to fight", "Quick tempered",
      // Defense
      "Sturdy body", "Capable of taking hits", "Highly persistent",
      "Good endurance", "Good perseverance",
      // Speed
      "Likes to run", "Alert to sounds", "Impetuous and silly",
      "Somewhat of a clown", "Quick to flee",
      // Sp. Atk
      "Highly curious", "Mischievous", "Thoroughly cunning",
      "Often lost in thought", "Very finicky",
      // Sp. Def
      "Strong willed", "Somewhat vain", "Strongly defiant",
      "Hates to lose", "Somewhat stubborn"};
  return (index >= 0 && index < 30) ? kTexts[index] : "";
}

// Nome completo do jogo de origem para o memo, pelos codigos GameVersion
// unificados (os mesmos de GameSigla na UI). "" = desconhecido.
inline const char* OriginGameName(std::uint8_t game) {
  switch (game) {
    case 1: return "Pokemon Sapphire";     case 2: return "Pokemon Ruby";
    case 3: return "Pokemon Emerald";      case 4: return "Pokemon FireRed";
    case 5: return "Pokemon LeafGreen";    case 7: return "Pokemon HeartGold";
    case 8: return "Pokemon SoulSilver";   case 10: return "Pokemon Diamond";
    case 11: return "Pokemon Pearl";       case 12: return "Pokemon Platinum";
    case 15: return "Pokemon Colosseum/XD";
    case 20: return "Pokemon White";       case 21: return "Pokemon Black";
    case 22: return "Pokemon White 2";     case 23: return "Pokemon Black 2";
    case 24: return "Pokemon X";           case 25: return "Pokemon Y";
    case 26: return "Pokemon Alpha Sapphire";
    case 27: return "Pokemon Omega Ruby";
    case 30: return "Pokemon Sun";         case 31: return "Pokemon Moon";
    case 32: return "Pokemon Ultra Sun";   case 33: return "Pokemon Ultra Moon";
    case 34: return "Pokemon GO";
    case 42: return "Pokemon: Let's Go, Pikachu!";
    case 43: return "Pokemon: Let's Go, Eevee!";
    case 44: return "Pokemon Sword";       case 45: return "Pokemon Shield";
    case 47: return "Pokemon Legends: Arceus";
    case 48: return "Pokemon Brilliant Diamond";
    case 49: return "Pokemon Shining Pearl";
    case 50: return "Pokemon Scarlet";     case 51: return "Pokemon Violet";
    case 52: return "Pokemon Legends: Z-A";  // conferido no save do dono
    default: return "";
  }
}

// Memo do summary, como o HOME exibe (referencia da spec 103):
//   fateful:  "Seems to have had a fateful encounter in {lugar} on {M/D/YYYY}."
//   com data: "Seems to have met in {lugar} on {M/D/YYYY}."
//   sem data: "Seems to have met in {lugar}."  (gen3 nao grava data)
//
// met_date no formato dos PKM modernos: {ano-2000, mes, dia}; {0,0,0} = sem
// data. Origem desconhecida vira "Pokemon HOME" — cobre os presentes gerados
// no proprio HOME, cujo codigo de versao nao mapeamos.
inline std::string BuildMemo(bool fateful, std::uint8_t origin_game,
                             const std::uint8_t met_date[3]) {
  const char* place = OriginGameName(origin_game);
  if (*place == '\0') place = "Pokemon HOME";
  std::string out = fateful ? "Seems to have had a fateful encounter in "
                            : "Seems to have met in ";
  out += place;
  const bool has_date = met_date[0] || met_date[1] || met_date[2];
  if (has_date) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), " on %d/%d/%d", met_date[1], met_date[2],
                  2000 + met_date[0]);
    out += buf;
  }
  out += ".";
  return out;
}

// --- Judge de IVs (o avaliador do jogo) ------------------------------------
//
// O avaliador dá um rótulo POR STAT — é assim no jogo, e é onde o número
// serve: ao lado do stat correspondente, não como nota única do bicho.
//
// As faixas são do jogo, não do PkHeX — ele não avalia, só verifica
// legalidade. Valem de gen6 em diante; o avaliador de gen3/4/5 usa outra
// escala, e um registro daquelas gerações vai receber rótulo pela escala
// nova. Pendência conhecida, não medida.

// Rótulo de um IV. Curto de propósito: isto é desenhado seis vezes em volta
// do hexágono, a ~163px do centro, e uma frase longa invade o rótulo do stat
// vizinho. A frase inteira do avaliador ("não dá pra ser pior") não cabe.
inline const char* JudgeIvText(std::uint8_t iv) {
  if (iv == 31) return "Perfeito";
  if (iv >= 30) return "Fantástico";
  if (iv >= 26) return "Muito bom";
  if (iv >= 16) return "Bom";
  if (iv >= 1) return "Regular";
  return "Ruim";
}

}  // namespace pokehome::summary
