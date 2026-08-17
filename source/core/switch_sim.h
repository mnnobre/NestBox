// Regras de descoberta de save data, compartilhadas entre o Switch e o
// simulador de PC (spec 039).
//
// No console o app monta o save data por ApplicationId e procura dentro; no PC
// a "montagem" e um diretorio. O que decide o que serve — tamanho de arquivo,
// nome, id do titulo — e o mesmo nos dois, e mora aqui.
//
// Sem isto, o caminho do PC seria uma segunda implementacao das mesmas regras,
// que divergiria da primeira sem ninguem notar (TD-02 da spec 039).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pokehome::sim {

// Save de GBA: o gen3 tem exatamente 128 KB. O tamanho e o filtro barato antes
// de tentar parsear.
inline constexpr std::size_t kGbaSaveBytes = 131072;

// Piso de tamanho para um save de jogo Switch. So o tamanho decide aqui; quem
// valida de verdade e o parser de cada formato.
//
// Era 0x100000 (1 MB), calibrado so para SwishCrypto — e isso EXCLUIA saves
// reais: o SaveData.bin do BDSP tem 979.108 bytes e o savedata.bin do Let's Go
// tem exatamente 1.048.576, que nao passa de um teste `>` contra 1 MB
// (spec 042).
//
// 512 KB fica abaixo do menor save observado (979.108) com folga, e bem acima
// de qualquer arquivo auxiliar: o main2 do Arceus tem 8 bytes e o poke_trade
// do SwSh tem 789.
inline constexpr std::size_t kMainMinBytes = 512 * 1024;

// Converte o nome de um diretorio em ApplicationId.
//
// No simulador, o nome da pasta E o id em hex — e assim que o app exercita a
// mesma busca em kSwitchTitles que faria no console.
//
// Devolve false para nome que nao seja hex de 16 digitos: pasta alheia dentro
// da raiz nao pode virar um jogo.
inline bool ParseAppId(const std::string& dir_name, std::uint64_t* out) {
  if (dir_name.size() != 16) return false;
  std::uint64_t id = 0;
  for (char c : dir_name) {
    id <<= 4;
    if (c >= '0' && c <= '9') {
      id |= static_cast<std::uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      id |= static_cast<std::uint64_t>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      id |= static_cast<std::uint64_t>(c - 'A' + 10);
    } else {
      return false;
    }
  }
  if (out) *out = id;
  return true;
}

// Nome de diretorio para um ApplicationId: hex maiusculo de 16 digitos, o
// mesmo formato que o titledb usa.
inline std::string AppIdToDir(std::uint64_t app_id) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kHex[app_id & 0xF];
    app_id >>= 4;
  }
  return out;
}

// Extensoes que o app trata como save de arquivo.
inline bool HasSaveExtension(const std::string& name) {
  const char* kExts[] = {".sav", ".srm", ".sps", ".dsv"};
  for (const char* ext : kExts) {
    const std::size_t n = std::string(ext).size();
    if (name.size() <= n) continue;
    if (name.compare(name.size() - n, n, ext) == 0) return true;
  }
  return false;
}

// Este arquivo, com este tamanho, serve como save de GBA?
//
// Tamanho exato, nao "pelo menos": um arquivo maior nao e um save gen3 com
// lixo no fim, e um menor nao tem as duas metades que o formato exige.
inline bool IsGbaSave(const std::string& name, std::size_t size) {
  return HasSaveExtension(name) && size == kGbaSaveBytes;
}

// Nomes de save de jogo Switch conhecidos.
//
// Nao e tudo "main": esse e o nome do SwishCrypto (SwSh, Arceus, SV, Z-A).
// BDSP e Unity e usa SaveData.bin; Let's Go usa savedata.bin. Sao engines
// diferentes, nao variacoes do mesmo formato (spec 042).
inline constexpr const char* kSaveNames[] = {
    "main",          // SwishCrypto
    "SaveData.bin",  // Brilliant Diamond / Shining Pearl
    "savedata.bin",  // Let's Go
};

// Este arquivo serve como save de jogo Switch?
//
// Lista de nomes conhecidos, e nao "qualquer arquivo grande": o arquivo
// "backup" tem EXATAMENTE o mesmo tamanho do "main", e o BDSP traz um
// "Backup.bin" do tamanho do "SaveData.bin". Aceitar por tamanho abriria o
// backup em vez do save (TD-01 da spec 042).
inline bool IsMainSave(const std::string& name, std::size_t size) {
  if (size <= kMainMinBytes) return false;
  for (const char* known : kSaveNames) {
    if (name == known) return true;
  }
  return false;
}

}  // namespace pokehome::sim
