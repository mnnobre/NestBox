// GERADO por tools/pkhex-tables - nao editar a mao (spec 126).
// Charset da gen4: cada u16 do pk4 e um INDICE nesta tabela, nao UTF-16.
// Extraido do StringConverter4 do PKHeX.Core, caractere a caractere.
#pragma once

#include <cstdint>

namespace pokehome::gen4 {

struct CharMap { std::uint16_t code; char ch; };
inline constexpr CharMap kChars[77] = {
    {0x012B, 'A'},
    {0x012C, 'B'},
    {0x012D, 'C'},
    {0x012E, 'D'},
    {0x012F, 'E'},
    {0x0130, 'F'},
    {0x0131, 'G'},
    {0x0132, 'H'},
    {0x0133, 'I'},
    {0x0134, 'J'},
    {0x0135, 'K'},
    {0x0136, 'L'},
    {0x0137, 'M'},
    {0x0138, 'N'},
    {0x0139, 'O'},
    {0x013A, 'P'},
    {0x013B, 'Q'},
    {0x013C, 'R'},
    {0x013D, 'S'},
    {0x013E, 'T'},
    {0x013F, 'U'},
    {0x0140, 'V'},
    {0x0141, 'W'},
    {0x0142, 'X'},
    {0x0143, 'Y'},
    {0x0144, 'Z'},
    {0x0145, 'a'},
    {0x0146, 'b'},
    {0x0147, 'c'},
    {0x0148, 'd'},
    {0x0149, 'e'},
    {0x014A, 'f'},
    {0x014B, 'g'},
    {0x014C, 'h'},
    {0x014D, 'i'},
    {0x014E, 'j'},
    {0x014F, 'k'},
    {0x0150, 'l'},
    {0x0151, 'm'},
    {0x0152, 'n'},
    {0x0153, 'o'},
    {0x0154, 'p'},
    {0x0155, 'q'},
    {0x0156, 'r'},
    {0x0157, 's'},
    {0x0158, 't'},
    {0x0159, 'u'},
    {0x015A, 'v'},
    {0x015B, 'w'},
    {0x015C, 'x'},
    {0x015D, 'y'},
    {0x015E, 'z'},
    {0x0121, '0'},
    {0x0122, '1'},
    {0x0123, '2'},
    {0x0124, '3'},
    {0x0125, '4'},
    {0x0126, '5'},
    {0x0127, '6'},
    {0x0128, '7'},
    {0x0129, '8'},
    {0x012A, '9'},
    {0x01DE, ' '},
    {0x01AE, '.'},
    {0x01AD, ','},
    {0x01B3, '\''},
    {0x01BE, '-'},
    {0x01AB, '!'},
    {0x01AC, '?'},
    {0x01B1, '/'},
    {0x01B9, '('},
    {0x01BA, ')'},
    {0x01C4, ':'},
    {0x01C5, ';'},
    {0x01C2, '&'},
    {0x01BD, '+'},
    {0x01C3, '~'},
};

inline constexpr std::uint16_t kTerminator = 0xFFFF;

inline char DecodeChar(std::uint16_t code) {
  for (const CharMap& m : kChars) {
    if (m.code == code) return m.ch;
  }
  return '?';
}

inline std::uint16_t EncodeChar(char c) {
  for (const CharMap& m : kChars) {
    if (m.ch == c) return m.code;
  }
  return 0x01DE;  // espaco
}

}  // namespace pokehome::gen4