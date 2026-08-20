// Snapshot das caixas de um save, em JSON, para o DIFF entre fases (spec 143).
//
// O ciclo do teste de lote e save -> jogo -> save. A terceira fase e a que
// nenhum teste tinha: o jogo carrega, NORMALIZA o que nao gosta, salva, e o
// dado some sem erro nenhum. Comparar o snapshot de antes com o de depois e
// o que torna essa normalizacao visivel.
//
// Uso:  dump_box <save> [saida.json]     (sem saida = stdout)
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gen3_save.h"
#include "pkm_convert.h"
#include "pkm_model.h"
#include "save_writer.h"

namespace {

// Escapa o que o JSON exige. Os nomes vem do save do usuario: assumir que
// sao ASCII limpo produziria JSON invalido no primeiro apelido com aspas.
std::string J(const std::string& s) {
  std::string o;
  for (const char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else {
          o += c;
        }
    }
  }
  return o;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "uso: dump_box <save> [saida.json]\n");
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "nao abriu %s\n", argv[1]);
    return 1;
  }
  std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
  in.close();

  // GEN3 primeiro: `savew::Load` so conhece os formatos modernos, e um save
  // de FireRed cairia em "nao reconhecido" — foi o que aconteceu quando o
  // dono montou o mGBA para validar o G3 (spec 145).
  //
  // O JSON sai com as MESMAS chaves do ramo moderno, para o diff do
  // ciclo_lote nao precisar saber de qual geracao veio. O que o gen3 nao tem
  // (tracker, handler, scale, forma) sai zerado.
  if (auto g3 = pokehome::gen3::ParseSave(buf)) {
    // BuildPcBuffer remonta as secoes do slot ativo UMA vez. `ReadBoxPokemon`
    // faria isso a cada slot — 420 remontagens de ~35 KB, que o proprio
    // header desaconselha.
    const auto pc = pokehome::gen3::BuildPcBuffer(buf, *g3);
    if (pc.empty()) {
      std::fprintf(stderr, "gen3: PC buffer vazio (secao faltando) em %s\n",
                   argv[1]);
      return 1;
    }
    std::string out3 = "{\n  \"arquivo\": \"" + J(argv[1]) + "\",\n";
    out3 += "  \"caixas\": " + std::to_string(pokehome::gen3::kBoxCount) + ",\n";
    out3 += "  \"slots_por_caixa\": " +
            std::to_string(pokehome::gen3::kSlotsPerBox) + ",\n";
    out3 += "  \"pokemon\": [\n";
    bool primeiro3 = true;
    for (std::size_t b = 0; b < pokehome::gen3::kBoxCount; ++b) {
      for (std::size_t s = 0; s < pokehome::gen3::kSlotsPerBox; ++s) {
        const auto mon =
            pokehome::gen3::ReadBoxPokemonFrom(pc, b, s);
        if (!mon || mon->species == 0) continue;
        if (!primeiro3) out3 += ",\n";
        primeiro3 = false;
        // `species` no gen3 e o INDICE INTERNO, nao a National Dex — a mesma
        // armadilha do PK9. O diff compara `dex`, entao converte aqui.
        const int dex = pokehome::gen3::NationalDex(mon->species);
        char linha[1024];
        std::snprintf(
            linha, sizeof(linha),
            "    {\"box\":%zu,\"slot\":%zu,\"dex\":%d,\"form\":0,"
            "\"nick\":\"%s\",\"ot\":\"\",\"pid\":%u,\"ec\":0,"
            "\"exp\":%u,\"nivel_met\":%u,\"ability\":%u,\"ability_n\":0,"
            "\"gender\":0,\"alpha\":false,\"handler\":0,\"tracker\":0,"
            "\"h_scalar\":0,\"w_scalar\":0,\"scale\":0,\"h_abs\":0.0,"
            "\"w_abs\":0.0,\"moves\":[%u,%u,%u,%u],\"pp\":[%u,%u,%u,%u],"
            "\"ivs\":[%u,%u,%u,%u,%u,%u],\"met_loc\":0,\"egg_loc\":0,"
            "\"ball\":0,\"origem\":0}",
            b + 1, s, dex, J(mon->nickname).c_str(), mon->personality,
            mon->experience, mon->met_level, mon->ability_bit, mon->moves[0],
            mon->moves[1], mon->moves[2], mon->moves[3], mon->pp[0],
            mon->pp[1], mon->pp[2], mon->pp[3], mon->ivs[0], mon->ivs[1],
            mon->ivs[2], mon->ivs[3], mon->ivs[4], mon->ivs[5]);
        out3 += linha;
      }
    }
    out3 += "\n  ]\n}\n";
    if (argc >= 3) {
      std::ofstream o(argv[2]);
      o << out3;
    } else {
      std::fputs(out3.c_str(), stdout);
    }
    return 0;
  }

  auto sd = savew::Load(buf);
  if (!sd) {
    std::fprintf(stderr, "save nao reconhecido: %s\n", argv[1]);
    return 1;
  }

  std::string out = "{\n  \"arquivo\": \"" + J(argv[1]) + "\",\n";
  out += "  \"caixas\": " + std::to_string(sd->box_count) + ",\n";
  out += "  \"slots_por_caixa\": " + std::to_string(sd->slots_per_box) + ",\n";
  out += "  \"pokemon\": [\n";

  bool primeiro = true;
  for (std::size_t b = 0; b < sd->box_count; ++b) {
    for (std::size_t s = 0; s < sd->slots_per_box; ++s) {
      const auto& sl = sd->At(b, s);
      if (!sl.present || sl.mon.empty()) continue;
      const auto& p = sl.mon;
      if (!primeiro) out += ",\n";
      primeiro = false;

      char linha[1024];
      std::snprintf(
          linha, sizeof(linha),
          "    {\"box\":%zu,\"slot\":%zu,\"dex\":%u,\"form\":%u,"
          "\"nick\":\"%s\",\"ot\":\"%s\",\"pid\":%u,\"ec\":%u,"
          "\"exp\":%u,\"nivel_met\":%u,\"ability\":%u,\"ability_n\":%u,"
          "\"gender\":%u,\"alpha\":%s,\"handler\":%u,\"tracker\":%llu,"
          "\"h_scalar\":%u,\"w_scalar\":%u,\"scale\":%u,"
          "\"h_abs\":%.6f,\"w_abs\":%.6f,"
          "\"moves\":[%u,%u,%u,%u],\"pp\":[%u,%u,%u,%u],"
          "\"ivs\":[%u,%u,%u,%u,%u,%u],"
          "\"met_loc\":%u,\"egg_loc\":%u,\"ball\":%u,\"origem\":%u}",
          b + 1, s, static_cast<unsigned>(pkm::NationalDex(p)),
          p.form, J(p.nickname).c_str(), J(p.ot_name).c_str(), p.pid,
          p.encryption_constant, p.exp, p.met_level, p.ability,
          p.ability_number, p.gender, p.is_alpha ? "true" : "false",
          p.current_handler,
          static_cast<unsigned long long>(p.home_tracker),
          p.height_scalar, p.weight_scalar, p.scale,
          static_cast<double>(p.height_absolute),
          static_cast<double>(p.weight_absolute),
          p.moves[0], p.moves[1], p.moves[2], p.moves[3],
          p.pp[0], p.pp[1], p.pp[2], p.pp[3],
          p.ivs[0], p.ivs[1], p.ivs[2], p.ivs[3], p.ivs[4], p.ivs[5],
          p.met_location, p.egg_location, p.ball, p.origin_game);
      out += linha;
    }
  }
  out += "\n  ]\n}\n";

  if (argc >= 3) {
    std::ofstream o(argv[2]);
    o << out;
    if (!o) {
      std::fprintf(stderr, "escrita incompleta em %s\n", argv[2]);
      return 1;
    }
    std::fprintf(stderr, "snapshot: %s\n", argv[2]);
  } else {
    std::fputs(out.c_str(), stdout);
  }
  return 0;
}
