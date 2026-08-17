// Gerador de PKM convertidos para a validacao EXTERNA (spec 069, G06).
//
// Nao e um teste: e o produtor dos arquivos que o PkHeX vai julgar. A regra do
// dono e que "passou no nosso teste" nao fecha criterio — o veredito externo
// fecha. Este binario le uma fixture, converte para o formato pedido e grava o
// binario do destino, para que `tools/pkhex-pkm dump` leia os mesmos campos.
//
// Uso: convert_dump <fixture> <pb7|pk8|pb8|pa8|pk9> <saida>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pa8.h"
#include "pb7.h"
#include "pb8.h"
#include "pk8.h"
#include "pk9.h"
#include "pkm_convert.h"

int main(int argc, char** argv) {
  if (argc != 4) {
    std::printf("uso: convert_dump <fixture> <pb7|pk8|pb8|pa8|pk9> <saida>\n");
    return 2;
  }

  const std::filesystem::path in = argv[1];
  const std::string alvo = argv[2];
  const std::filesystem::path out = argv[3];

  // filesystem::path, nao string (codepage ANSI no Windows).
  std::ifstream f(in, std::ios::binary);
  if (!f) {
    std::printf("nao abriu: %s\n", in.string().c_str());
    return 1;
  }
  const std::vector<std::uint8_t> bytes(
      (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

  const std::string ext = in.extension().string();
  std::optional<pkm::Pokemon> p;
  if (ext == ".pb7") p = pb7::Parse(bytes);
  else if (ext == ".pk8") p = pk8::Parse(bytes);
  else if (ext == ".pb8") p = pb8::Parse(bytes);
  else if (ext == ".pa8") p = pa8::Parse(bytes);
  else if (ext == ".pk9") p = pk9::Parse(bytes);
  if (!p) {
    std::printf("nao parseou: %s\n", in.string().c_str());
    return 1;
  }

  pkm::Format fmt = pkm::Format::kNone;
  if (alvo == "pb7") fmt = pkm::Format::kPB7;
  else if (alvo == "pk8") fmt = pkm::Format::kPK8;
  else if (alvo == "pb8") fmt = pkm::Format::kPB8;
  else if (alvo == "pa8") fmt = pkm::Format::kPA8;
  else if (alvo == "pk9") fmt = pkm::Format::kPK9;

  const auto conv = pkm::Convert(*p, fmt);
  if (!conv) {
    std::printf("Convert recusou (especie/forma/habilidade nao cabe)\n");
    return 3;
  }

  std::vector<std::uint8_t> bin;
  switch (fmt) {
    case pkm::Format::kPB7: bin = pb7::Write(*conv); break;
    case pkm::Format::kPK8: bin = pk8::Write(*conv); break;
    case pkm::Format::kPB8: bin = pb8::Write(*conv); break;
    case pkm::Format::kPA8: bin = pa8::Write(*conv); break;
    case pkm::Format::kPK9: bin = pk9::Write(*conv); break;
    default: return 2;
  }

  std::ofstream o(out, std::ios::binary);
  o.write(reinterpret_cast<const char*>(bin.data()),
          static_cast<std::streamsize>(bin.size()));
  std::printf("ok: %s -> %s (%zu bytes), species no binario = %u\n",
              in.filename().string().c_str(), alvo.c_str(), bin.size(),
              conv->species);
  return 0;
}
