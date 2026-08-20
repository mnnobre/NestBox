// MATRIZ DE ROTAS entre jogos (specs 143 e 145).
//
// A pergunta do dono: "o Arceus recebe Pokemon de todas as geracoes com 100%
// de certeza?". A resposta so vale com medicao — e o que existia era UMA rota
// (BDSP -> PLA) com UM Pokemon.
//
// Este programa pega o conteudo REAL de cada save de origem, converte para o
// formato do DESTINO pelo mesmo caminho da producao, e grava. O ciclo do
// jogo (ryujinx-nav/ciclo_lote.py) faz o resto.
//
// Nao usa Pokemon sinteticos de proposito: um mon gerado do zero nasce no
// destino e nao exercita conversao nenhuma. Aqui cada um vem mesmo de outro
// formato, com os campos que o jogo de origem gravou.
//
// O destino sai do PROPRIO save (spec 145): era fixo em PLA, o que so permitia
// medir ENTRADA no Arceus. Medir SAIDA e a mesma operacao com o destino trocado,
// e sair tem regras que entrar nao tem — um alpha do PLA mantem a flag no SV e
// perde no BDSP.
//
// Uso:
//   matriz_rotas <save de origem> <save de destino> [--limite N]
//
// Sai com 0 mesmo quando alguns Pokemon sao recusados: a recusa e RESULTADO,
// nao erro. O relatorio final diz quantos passaram e por que os outros nao.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "commit_plan.h"
#include "gen3_save.h"
#include "gen3_transfer.h"
#include "moveset_memory.h"
#include "pkm_convert.h"
#include "pkm_model.h"
#include "save_writer.h"
#include "transfer_rules.h"

namespace {

std::vector<std::uint8_t> Ler(const char* p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
}

const char* NomeFormato(pkm::Format f) {
  switch (f) {
    case pkm::Format::kPB7: return "PB7 (Let's Go)";
    case pkm::Format::kPK8: return "PK8 (Sword/Shield)";
    case pkm::Format::kPB8: return "PB8 (BDSP)";
    case pkm::Format::kPA8: return "PA8 (Legends Arceus)";
    case pkm::Format::kPK9: return "PK9 (Scarlet/Violet, Z-A)";
    default: return "?";
  }
}

// O que muda com o jogo de destino. Sai do SAVE, nunca de argumento: o
// formato sozinho nao basta como discriminador — Z-A e SV gravam ambos em
// kPK9 (a licao da spec 145).
struct Destino {
  pkm::Format formato;
  pokehome::moveset::Game jogo_ms;
  pokehome::compat::Game compat;
  const char* nome;
};

// O jogo de ORIGEM no vocabulario da memoria de moveset. O ConvertDown
// precisa dele para saber de qual engine o moveset atual veio.
pokehome::moveset::Game JogoMsDe(savew::Game g) {
  using MG = pokehome::moveset::Game;
  switch (g) {
    case savew::Game::kPLA:  return MG::kLegendsArceus;
    case savew::Game::kBDSP: return MG::kBdsp;
    case savew::Game::kZA:   return MG::kZA;
    case savew::Game::kSV:   return MG::kSV;
    case savew::Game::kSwSh: return MG::kSwSh;
    case savew::Game::kLGPE: return MG::kLgpe;
  }
  return MG::kSwSh;
}

bool DestinoDe(savew::Game g, Destino* out) {
  using MG = pokehome::moveset::Game;
  using CG = pokehome::compat::Game;
  switch (g) {
    case savew::Game::kPLA:
      *out = {pkm::Format::kPA8, MG::kLegendsArceus, CG::kLegendsArceus,
              "Legends: Arceus"};
      return true;
    case savew::Game::kBDSP:
      *out = {pkm::Format::kPB8, MG::kBdsp, CG::kBdsp,
              "Brilliant Diamond/Shining Pearl"};
      return true;
    case savew::Game::kZA:
      *out = {pkm::Format::kPK9, MG::kZA, CG::kLegendsZA, "Legends: Z-A"};
      return true;
    case savew::Game::kSV:
      *out = {pkm::Format::kPK9, MG::kSV, CG::kScarletViolet,
              "Scarlet/Violet"};
      return true;
    case savew::Game::kSwSh:
      *out = {pkm::Format::kPK8, MG::kSwSh, CG::kSwordShield, "Sword/Shield"};
      return true;
    case savew::Game::kLGPE:
      *out = {pkm::Format::kPB7, MG::kLgpe, CG::kLetsGo, "Let's Go"};
      return true;
  }
  return false;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "uso: matriz_rotas <save origem> <save PLA> [--limite N]\n");
    return 2;
  }
  int limite = 960;
  for (int i = 3; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--limite") == 0) limite = std::atoi(argv[i + 1]);

  const auto buf_src = Ler(argv[1]);
  const auto buf_dst = Ler(argv[2]);
  if (buf_src.empty() || buf_dst.empty()) {
    std::fprintf(stderr, "nao consegui ler os saves\n");
    return 1;
  }
  // GEN3 como DESTINO (spec 145). O `savew` nao conhece gen3 de proposito:
  // ele so serializa bytes, e a conversao para gen3 carrega REGRA (memoria de
  // moveset, learnset de destino, codigo de origem). Por isso o ramo vive
  // aqui, onde esse contexto existe — o mesmo criterio da spec 143, que poe a
  // rota no matriz_rotas e os bytes no savew.
  if (auto g3dst = pokehome::gen3::ParseSave(buf_dst)) {
    auto src3 = savew::Load(buf_src);
    if (!src3) {
      std::fprintf(stderr, "origem nao reconhecida\n");
      return 1;
    }
    auto pc = pokehome::gen3::BuildPcBuffer(buf_dst, *g3dst);
    if (pc.empty()) {
      std::fprintf(stderr, "gen3: PC buffer vazio no destino\n");
      return 1;
    }
    pokehome::moveset::Memory memoria;
    const pokehome::rules::SaveContext ctx3;
    std::size_t lidos3 = 0, conv3 = 0, grav3 = 0, destino_i3 = 0;
    std::map<std::string, int> recusas3;
    pkm::Format fmt3 = pkm::Format::kNone;

    // Comeca DEPOIS do que ja existe no destino, como o ramo moderno: rodar
    // varias origens sobre o mesmo save nao pode sobrescrever o acumulado.
    for (std::size_t b = 0; b < pokehome::gen3::kBoxCount; ++b)
      for (std::size_t s = 0; s < pokehome::gen3::kSlotsPerBox; ++s)
        if (auto m = pokehome::gen3::ReadBoxPokemonFrom(pc, b, s))
          if (m->species != 0)
            destino_i3 = b * pokehome::gen3::kSlotsPerBox + s + 1;

    for (std::size_t b = 0; b < src3->box_count && grav3 < (std::size_t)limite;
         ++b) {
      for (std::size_t s = 0;
           s < src3->slots_per_box && grav3 < (std::size_t)limite; ++s) {
        const auto& sl = src3->At(b, s);
        if (!sl.present || sl.mon.empty()) continue;
        ++lidos3;
        fmt3 = sl.mon.format;
        const auto v = pokehome::rules::CanTransfer(
            sl.mon, pokehome::compat::Game::kFireRed, ctx3);
        if (v.verdict == pokehome::rules::Verdict::kBlocked) {
          recusas3["regra: " + v.reason]++;
          continue;
        }
        std::uint8_t rec[80] = {};
        // 4 = FireRed na palavra de origins do gen3.
        if (!pokehome::g3x::ConvertDown(sl.mon, pokehome::learnset::Game::kFireRed,
                                        JogoMsDe(src3->game), &memoria, 4,
                                        rec)) {
          recusas3["ConvertDown recusou"]++;
          continue;
        }
        ++conv3;
        const std::size_t bb = destino_i3 / pokehome::gen3::kSlotsPerBox;
        const std::size_t ss = destino_i3 % pokehome::gen3::kSlotsPerBox;
        if (bb >= pokehome::gen3::kBoxCount) break;
        if (!pokehome::gen3::WriteBoxPokemonTo(pc, bb, ss, rec)) {
          recusas3["Write no destino falhou"]++;
          continue;
        }
        ++destino_i3;
        ++grav3;
      }
    }

    auto saida = buf_dst;
    if (!pokehome::gen3::ApplyPcBuffer(saida, *g3dst, pc)) {
      std::fprintf(stderr, "gen3: ApplyPcBuffer falhou\n");
      return 1;
    }
    std::ofstream o3(argv[2], std::ios::binary);
    o3.write(reinterpret_cast<const char*>(saida.data()),
             static_cast<std::streamsize>(saida.size()));
    if (!o3) {
      std::fprintf(stderr, "escrita incompleta\n");
      return 1;
    }
    std::printf("rota: %s -> FireRed (gen3)\n", NomeFormato(fmt3));
    std::printf("lidos=%zu convertidos=%zu gravados=%zu\n", lidos3, conv3,
                grav3);
    if (!recusas3.empty()) {
      std::printf("recusas:\n");
      for (const auto& [motivo, n] : recusas3)
        std::printf("  %4d x %s\n", n, motivo.c_str());
    }
    return 0;
  }

  // GEN3 como ORIGEM (spec 145) — a outra metade. Sobe cada registro de 80
  // bytes pelo `g3x::ConvertUp` e entrega ao MESMO caminho de entrada do ramo
  // moderno (AjustesDeEntrada + AplicaEntradaNoDestino), para nao existir um
  // segundo caminho de escrita: o bug do ovo da spec 143 nasceu assim.
  if (auto g3src = pokehome::gen3::ParseSave(buf_src)) {
    auto dst3 = savew::Load(buf_dst);
    if (!dst3) {
      std::fprintf(stderr, "destino nao reconhecido\n");
      return 1;
    }
    Destino d3;
    if (!DestinoDe(dst3->game, &d3)) {
      std::fprintf(stderr, "jogo de destino nao suportado\n");
      return 1;
    }
    const auto pc = pokehome::gen3::BuildPcBuffer(buf_src, *g3src);
    if (pc.empty()) {
      std::fprintf(stderr, "gen3: PC buffer vazio na origem\n");
      return 1;
    }
    pokehome::moveset::Memory memoria;
    const pokehome::rules::SaveContext ctx3;
    pokehome::commit::SaveInfo info3;
    info3.kind = pokehome::commit::SaveKind::kModerno;
    info3.formato = d3.formato;
    info3.jogo_ms = d3.jogo_ms;
    info3.trainer_name = dst3->trainer_name;

    std::size_t lidos3 = 0, conv3 = 0, grav3 = 0, destino_i3 = 0;
    std::map<std::string, int> recusas3;
    for (std::size_t b = 0; b < dst3->box_count; ++b)
      for (std::size_t s = 0; s < dst3->slots_per_box; ++s)
        if (const auto& sl = dst3->At(b, s); sl.present && !sl.mon.empty())
          destino_i3 = b * dst3->slots_per_box + s + 1;

    for (std::size_t b = 0;
         b < pokehome::gen3::kBoxCount && grav3 < (std::size_t)limite; ++b) {
      for (std::size_t s = 0;
           s < pokehome::gen3::kSlotsPerBox && grav3 < (std::size_t)limite;
           ++s) {
        // Os 80 bytes saem do proprio BoxPokemon (`raw`), nao de aritmetica
        // sobre `pc.data()`: o PC buffer tem 4 bytes de cabecalho antes dos
        // registros (kBoxDataOffset, o indice da caixa atual), e essa
        // constante nem e exposta no header. Calcular o offset aqui deu
        // `species=22317` e o apelido deslocado — o registro inteiro fora de
        // fase. `ReadBoxPokemonFrom` ja resolve isso.
        const auto info = pokehome::gen3::ReadBoxPokemonFrom(pc, b, s);
        if (!info || info->species == 0) continue;
        ++lidos3;

        auto sub = pokehome::g3x::ConvertUp(info->raw, d3.formato, d3.jogo_ms,
                                            &memoria);
        if (!sub) {
          recusas3["ConvertUp recusou"]++;
          continue;
        }
        // A regra do HOME so pode ser consultada DEPOIS de subir: ela fala em
        // National Dex, e o gen3 guarda indice interno.
        const auto v = pokehome::rules::CanTransfer(*sub, d3.compat, ctx3);
        if (v.verdict == pokehome::rules::Verdict::kBlocked) {
          recusas3["regra: " + v.reason]++;
          continue;
        }
        pokehome::commit::AplicaEntradaNoDestino(*sub, info3, &memoria);
        ++conv3;

        const std::size_t bb = destino_i3 / dst3->slots_per_box;
        const std::size_t ss = destino_i3 % dst3->slots_per_box;
        if (bb >= dst3->box_count) break;
        if (!dst3->Set(bb, ss, *sub)) {
          recusas3["Set no destino falhou"]++;
          continue;
        }
        ++destino_i3;
        ++grav3;
      }
    }

    const auto out3 = savew::Save(*dst3);
    std::ofstream o3(argv[2], std::ios::binary);
    o3.write(reinterpret_cast<const char*>(out3.data()),
             static_cast<std::streamsize>(out3.size()));
    if (!o3) {
      std::fprintf(stderr, "escrita incompleta\n");
      return 1;
    }
    std::printf("rota: FireRed (gen3) -> %s\n", d3.nome);
    std::printf("lidos=%zu convertidos=%zu gravados=%zu\n", lidos3, conv3,
                grav3);
    if (!recusas3.empty()) {
      std::printf("recusas:\n");
      for (const auto& [motivo, n] : recusas3)
        std::printf("  %4d x %s\n", n, motivo.c_str());
    }
    return 0;
  }

  auto src = savew::Load(buf_src);
  auto dst = savew::Load(buf_dst);
  if (!src || !dst) {
    std::fprintf(stderr, "save nao reconhecido\n");
    return 1;
  }
  Destino destino;
  if (!DestinoDe(dst->game, &destino)) {
    std::fprintf(stderr, "jogo de destino nao suportado\n");
    return 1;
  }

  std::size_t lidos = 0, convertidos = 0, gravados = 0;
  std::map<std::string, int> recusas;
  pkm::Format fmt_origem = pkm::Format::kPK9;

  // MESMO caminho de entrada da producao. A primeira versao disto chamava so
  // `pkm::Convert` + `AjustesDeEntrada`, pulando o reset de moveset/PP e o
  // tracker — e os 161 Pokemon viraram OVO no jogo, com exatamente os
  // defeitos que a spec 143 corrigiu. Era um segundo caminho de escrita: o
  // bug que esta spec existe para eliminar, recriado no proprio teste dela.
  // Contexto do save de destino: as regras precisam saber o que ja existe la
  // (Hyper Training tem piso de nivel, por exemplo).
  const pokehome::rules::SaveContext ctx;

  // O que a entrada precisa saber do save de destino.
  pokehome::commit::SaveInfo info;
  info.kind = pokehome::commit::SaveKind::kModerno;
  info.formato = destino.formato;
  info.jogo_ms = destino.jogo_ms;
  info.trainer_name = dst->trainer_name;
  pokehome::moveset::Memory memoria;

  // Comeca DEPOIS do que ja esta no destino. Sem isto, rodar a matriz para
  // varias origens sobre o mesmo save sobrescreve tudo a cada execucao — o
  // acumulado das 6 rotas saia com o total da ULTIMA.
  std::size_t destino_i = 0;
  for (std::size_t b = 0; b < dst->box_count; ++b)
    for (std::size_t s = 0; s < dst->slots_per_box; ++s) {
      const auto& sl = dst->At(b, s);
      if (sl.present && !sl.mon.empty()) destino_i = b * dst->slots_per_box + s + 1;
    }

  for (std::size_t b = 0; b < src->box_count && gravados < (std::size_t)limite; ++b) {
    for (std::size_t s = 0; s < src->slots_per_box && gravados < (std::size_t)limite; ++s) {
      const auto& sl = src->At(b, s);
      if (!sl.present || sl.mon.empty()) continue;
      ++lidos;
      fmt_origem = sl.mon.format;

      // 1. A regra do HOME diz que este Pokemon pode entrar no PLA?
      const auto v = pokehome::rules::CanTransfer(sl.mon, destino.compat, ctx);
      if (v.verdict == pokehome::rules::Verdict::kBlocked) {
        recusas["regra: " + v.reason]++;
        continue;
      }

      // 2. Conversao de formato.
      auto conv = pkm::Convert(sl.mon, destino.formato);
      if (!conv) {
        recusas["Convert recusou"]++;
        continue;
      }
      pkm::AjustesDeEntrada(*conv, sl.mon.format);
      pokehome::commit::AplicaEntradaNoDestino(*conv, info, &memoria);
      ++convertidos;

      // 3. Cabe no destino?
      const std::size_t bb = destino_i / dst->slots_per_box;
      const std::size_t ss = destino_i % dst->slots_per_box;
      if (bb >= dst->box_count) break;
      if (!dst->Set(bb, ss, *conv)) {
        recusas["Set no destino falhou"]++;
        continue;
      }
      ++destino_i;
      ++gravados;
    }
  }

  const auto out = savew::Save(*dst);
  std::ofstream o(argv[2], std::ios::binary);
  o.write(reinterpret_cast<const char*>(out.data()),
          static_cast<std::streamsize>(out.size()));
  if (!o) {
    std::fprintf(stderr, "escrita incompleta\n");
    return 1;
  }

  std::printf("rota: %s -> %s\n", NomeFormato(fmt_origem), destino.nome);
  std::printf("lidos=%zu convertidos=%zu gravados=%zu\n", lidos, convertidos,
              gravados);
  if (!recusas.empty()) {
    std::printf("recusas:\n");
    for (const auto& [motivo, n] : recusas)
      std::printf("  %4d x %s\n", n, motivo.c_str());
  }
  return 0;
}
