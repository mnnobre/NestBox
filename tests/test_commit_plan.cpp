// Decisao pura do commit (spec 128).
//
// `CommitNestBox` era a unica parte critica do app sem teste: tudo o que ele
// CHAMA tem ctest, mas o fio que costura — separar por lado, converter em
// cada direcao, decidir o relembrar da chegada — nao tinha. Este teste cobre
// esse fio, agora que ele mora em `source/core/commit_plan.cpp`.
//
// Os sete cenarios que a spec pede: subida gen3->moderno, descida,
// mesmo formato com ApplyOnEntry, chegada a NestBox com RestoreOnBank,
// conversao recusada (erro e NADA gravado), mistura dos dois lados, e os
// casos "so NestBox" / "so save".
#include <cstdio>
#include <cstring>
#include <string>

#include "commit_plan.h"
#include "gen3_save.h"
#include "gen3_transfer.h"
#include "moveset_memory.h"
#include "pkm_convert.h"

namespace g3 = pokehome::gen3;
namespace g3x = pokehome::g3x;
namespace ms = pokehome::moveset;
namespace ls = pokehome::learnset;
namespace cmt = pokehome::commit;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  } else {
    std::printf("  ok: %s\n", what.c_str());
  }
}

// Um Pikachu gen3 de FireRed, sintetico (mesmo molde do test_gen3_transfer,
// cujo roundtrip a spec 108 ja provou).
static g3::FullRecord Pikachu() {
  g3::FullRecord r;
  r.personality = 0x00010002;
  r.ot_id = 0x00010001;
  g3::EncodeGen3String("PIKACHU", r.nickname_raw, sizeof(r.nickname_raw));
  r.language = 2;
  r.flags = 0x02;
  g3::EncodeGen3String("ASH", r.ot_name_raw, sizeof(r.ot_name_raw));
  r.species = 25;
  r.experience = 100000;
  r.friendship = 180;
  r.moves[0] = 85;
  r.moves[1] = 86;
  r.moves[2] = 98;
  r.moves[3] = 21;
  for (int i = 0; i < 4; ++i) r.pp[i] = 15;
  const std::uint32_t ivs[6] = {31, 30, 29, 28, 27, 26};
  for (int i = 0; i < 6; ++i) r.iv32 |= ivs[i] << (i * 5);
  r.origins = 5 | (4u << 7) | (4u << 11);
  return r;
}

// O Pikachu acima como BoxPokemon gen3 (sem parte moderna).
static g3::BoxPokemon Gen3Mon() {
  std::uint8_t raw[80];
  g3::EncodeFullRecord(Pikachu(), raw);
  return g3::ParseBoxPokemonRecord(raw);
}

// SaveInfo de um save moderno de Z-A (pk9).
static cmt::SaveInfo ZaInfo() {
  cmt::SaveInfo info;
  info.kind = cmt::SaveKind::kModerno;
  info.formato = pkm::Format::kPK9;
  info.jogo_ms = ms::Game::kZA;
  return info;
}

// SaveInfo de um save gen3 de FireRed.
static cmt::SaveInfo Gen3Info() {
  cmt::SaveInfo info;
  info.kind = cmt::SaveKind::kGen3;
  info.learnset_gen3 = ls::Game::kFireRed;
  info.origem_gen3 = 4;
  return info;
}

int main() {
  // ===================================================================
  // 1) Subida gen3 -> moderno: o deposito num save do Switch converte.
  // ===================================================================
  std::printf("=== 1: subida gen3 -> moderno ===\n");
  {
    ms::Memory memory;
    std::vector<cmt::Change> changes = {
        {/*to_nest=*/false, /*box=*/0, /*slot=*/0, Gen3Mon()}};

    const cmt::Plan plan = cmt::BuildPlan(changes, ZaInfo(), &memory);
    Check(plan.ok(), "plano valido");
    Check(plan.modern_changes.size() == 1, "uma alteracao para o save moderno");
    Check(plan.save_changes.empty(),
          "o mapa gen3 fica VAZIO num save moderno (senao gravaria duas vezes)");
    if (plan.modern_changes.size() == 1) {
      const auto& mon = plan.modern_changes[0].mon;
      Check(mon.modern != nullptr, "o gen3 foi CONVERTIDO para moderno");
      if (mon.modern) {
        Check(mon.modern->format == pkm::Format::kPK9, "formato do save alvo");
      }
    }
  }

  // ===================================================================
  // 2) Descida moderno -> gen3: o deposito num save de GBA converte.
  // ===================================================================
  std::printf("=== 2: descida moderno -> gen3 ===\n");
  {
    ms::Memory memory;
    // Sobe primeiro para ter um moderno legitimo em maos.
    std::uint8_t raw[80];
    g3::EncodeFullRecord(Pikachu(), raw);
    auto up = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, &memory);
    Check(up.has_value(), "preparo: a subida funciona");
    if (!up) return 1;

    // O mon guarda AS DUAS partes, como no app: o slot do NestBox tem o
    // registro gen3 E o moderno. Montar so a parte moderna daria um
    // `empty()` verdadeiro (species == 0) e a descida pularia o registro —
    // foi o que a primeira versao deste teste fez, e o falso-vermelho
    // apontou para o teste, nao para o codigo.
    g3::BoxPokemon mon = Gen3Mon();
    mon.modern = std::make_shared<const pkm::Pokemon>(*up);

    std::vector<cmt::Change> changes = {{false, 0, 0, mon}};
    const cmt::Plan plan = cmt::BuildPlan(changes, Gen3Info(), &memory);

    Check(plan.ok(), "plano valido");
    Check(plan.save_changes.size() == 1, "uma alteracao para o save gen3");
    Check(plan.modern_changes.empty(),
          "a lista moderna fica VAZIA num save gen3");
    if (plan.save_changes.size() == 1) {
      const g3::BoxPokemon& out = plan.save_changes.begin()->second;
      Check(out.modern == nullptr,
            "a parte moderna e SOLTA na descida: o slot gen3 guarda so os 80 bytes");
      Check(!out.empty(), "o registro gen3 foi construido");
    }
  }

  // ===================================================================
  // 3) Mesmo formato, jogo diferente: ApplyOnEntry troca o moveset.
  //    Spec 125 — voltar da NestBox para OUTRO jogo pk9 reaprende os
  //    golpes de la, em vez de manter os originais.
  // ===================================================================
  std::printf("=== 3: mesmo formato, jogo diferente (ApplyOnEntry) ===\n");
  {
    ms::Memory memory;
    std::uint8_t raw[80];
    g3::EncodeFullRecord(Pikachu(), raw);
    // Sobe para o Z-A: fica com home_tracker e origem de Z-A.
    auto up = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, &memory);
    if (!up) { Check(false, "preparo: subida para Z-A"); return 1; }

    g3::BoxPokemon mon = Gen3Mon();
    mon.modern = std::make_shared<const pkm::Pokemon>(*up);
    const std::uint16_t antes[4] = {up->moves[0], up->moves[1], up->moves[2],
                                    up->moves[3]};

    // Medido pela sonda: o ConvertUp copia `origin_game` do registro GEN3
    // (4 = FireRed), entao o OriginBucket deste Pokemon e kGen3 — nao kZA.
    // E o que faz a condicao do ramo (`bucket != destino`) ser verdadeira ao
    // depositar em SV, e portanto o ApplyOnEntry rodar.
    Check(ms::OriginBucket(*up) == ms::Game::kGen3,
          "o subido carrega a origem GEN3, nao a do jogo de destino");

    // Agora deposita num save de SV — mesmo formato pk9, jogo diferente.
    cmt::SaveInfo sv = ZaInfo();
    sv.jogo_ms = ms::Game::kSV;

    std::vector<cmt::Change> changes = {{false, 0, 0, mon}};
    const cmt::Plan plan = cmt::BuildPlan(changes, sv, &memory);
    Check(plan.ok(), "plano valido");
    Check(plan.modern_changes.size() == 1, "uma alteracao");
    if (plan.modern_changes.size() == 1 && plan.modern_changes[0].mon.modern) {
      const auto& depois = *plan.modern_changes[0].mon.modern;
      Check(depois.format == pkm::Format::kPK9, "o formato NAO muda");
      // O ramo do mesmo-formato exige home_tracker != 0 — a subida o deriva.
      Check(depois.home_tracker != 0,
            "o tracker sobrevive: e ele que habilita o ramo do mesmo formato");
      // O ApplyOnEntry rodou. Nao ha moveset de SV memorizado para este
      // Pokemon (ele nunca esteve la), entao a regra e o reset pelo learnset
      // do destino — o resultado pode COINCIDIR com os golpes que a subida
      // ja tinha posto. O que este teste garante e que o caminho nao
      // corrompeu nada e que o PP foi recalculado para o formato.
      bool pp_coerente = true;
      for (int i = 0; i < 4; ++i) {
        if (depois.moves[i] == 0 && depois.pp[i] != 0) pp_coerente = false;
      }
      Check(pp_coerente, "golpe vazio tem PP zero apos o ApplyOnEntry");
      (void)antes;
    }
  }

  // ===================================================================
  // 4) Chegada a NestBox: RestoreOnBank age no lado do banco.
  // ===================================================================
  std::printf("=== 4: chegada a NestBox (RestoreOnBank) ===\n");
  {
    ms::Memory memory;
    std::uint8_t raw[80];
    g3::EncodeFullRecord(Pikachu(), raw);
    auto up = g3x::ConvertUp(raw, pkm::Format::kPK9, ms::Game::kZA, &memory);
    if (!up) { Check(false, "preparo"); return 1; }

    g3::BoxPokemon mon;
    mon.modern = std::make_shared<const pkm::Pokemon>(*up);

    // to_nest = true: o Pokemon esta SAINDO do save e entrando no banco.
    std::vector<cmt::Change> changes = {{true, 0, 0, mon}};
    const cmt::Plan plan = cmt::BuildPlan(changes, ZaInfo(), &memory);

    Check(plan.ok(), "plano valido");
    Check(plan.nest_writes.size() == 1, "uma escrita no banco");
    Check(!plan.touches_save(), "o save NAO e tocado");
    if (plan.nest_writes.size() == 1) {
      Check(plan.nest_writes[0].mon.modern != nullptr,
            "o Pokemon chega ao banco com a parte moderna");
    }
  }

  // ===================================================================
  // 5) Conversao recusada: erro descritivo e NADA para gravar.
  //    E o cenario que protege o save: a falha custa zero porque acontece
  //    antes de qualquer escrita.
  // ===================================================================
  std::printf("=== 5: conversao recusada aborta o plano ===\n");
  {
    ms::Memory memory;
    // Um ovo nao sobe (TD-D4 da spec 109).
    g3::FullRecord egg = Pikachu();
    egg.iv32 |= (1u << 30);  // bit de ovo
    std::uint8_t raw[80];
    g3::EncodeFullRecord(egg, raw);

    std::vector<cmt::Change> changes = {
        {false, 0, 0, g3::ParseBoxPokemonRecord(raw)}};
    const cmt::Plan plan = cmt::BuildPlan(changes, ZaInfo(), &memory);

    Check(!plan.ok(), "o plano FALHA");
    Check(plan.error.find("ConvertUp") != std::string::npos,
          "o erro diz o que recusou: " + plan.error);
  }

  // ===================================================================
  // 6) Sem save gravavel: alteracao no lado do save e erro.
  // ===================================================================
  std::printf("=== 6: alteracao no save sem fonte gravavel ===\n");
  {
    ms::Memory memory;
    std::vector<cmt::Change> changes = {{false, 0, 0, Gen3Mon()}};
    cmt::SaveInfo nenhum;  // kNenhum
    const cmt::Plan plan = cmt::BuildPlan(changes, nenhum, &memory);
    Check(!plan.ok(), "o plano FALHA");
    Check(plan.error.find("nao e gravavel") != std::string::npos,
          "o erro explica: " + plan.error);
  }

  // ===================================================================
  // 7) Mistura dos dois lados, e os casos "so um lado".
  // ===================================================================
  std::printf("=== 7: mistura e casos de um lado so ===\n");
  {
    ms::Memory memory;
    // Um para o banco, um para o save.
    std::vector<cmt::Change> changes = {
        {true, 1, 5, Gen3Mon()},
        {false, 0, 0, Gen3Mon()},
    };
    const cmt::Plan plan = cmt::BuildPlan(changes, ZaInfo(), &memory);
    Check(plan.ok(), "plano valido com alteracoes nos dois lados");
    Check(plan.nest_writes.size() == 1, "uma escrita no banco");
    Check(plan.modern_changes.size() == 1, "uma alteracao no save");
    Check(plan.nest_writes[0].box == 1 && plan.nest_writes[0].slot == 5,
          "a coordenada do banco sobrevive");
  }
  {
    ms::Memory memory;
    // So NestBox: o save nao e tocado, e nem precisa ser gravavel.
    std::vector<cmt::Change> changes = {{true, 0, 0, Gen3Mon()}};
    cmt::SaveInfo nenhum;
    const cmt::Plan plan = cmt::BuildPlan(changes, nenhum, &memory);
    Check(plan.ok(), "so NestBox: valido mesmo sem save gravavel");
    Check(!plan.touches_save(), "o save nao e tocado");
  }
  {
    ms::Memory memory;
    // So save.
    std::vector<cmt::Change> changes = {{false, 2, 7, Gen3Mon()}};
    const cmt::Plan plan = cmt::BuildPlan(changes, ZaInfo(), &memory);
    Check(plan.ok(), "so save: valido");
    Check(plan.nest_writes.empty(), "nada a gravar no banco");
    Check(plan.modern_changes.size() == 1 &&
              plan.modern_changes[0].box == 2 &&
              plan.modern_changes[0].slot == 7,
          "a coordenada do save sobrevive");
  }
  {
    // Limpar um slot: mon vazio nao converte e nao vira erro.
    ms::Memory memory;
    std::vector<cmt::Change> changes = {{false, 0, 3, g3::BoxPokemon{}}};
    const cmt::Plan plan = cmt::BuildPlan(changes, ZaInfo(), &memory);
    Check(plan.ok(), "limpar slot: plano valido");
    Check(plan.modern_changes.size() == 1 &&
              plan.modern_changes[0].mon.empty(),
          "a limpeza chega ao save como slot vazio");
  }

  // ===================================================================
  // Spec 143: o deposito pela TELA faz os ajustes de entrada.
  //
  // Esta e a TRAVA. Ate a 143 existiam dois caminhos de escrita: o
  // `transfer::Commit` (que os testes exercitavam) compensava o formato, e o
  // `commit_plan` -> `ApplyBoxChanges` (que a TELA usa) nao compensava nada.
  // O bug chegou ao console do dono: um Bidoof do Brilliant Diamond entrou no
  // Legends: Arceus carregando `egg_location = 0xFFFF` — o sentinela do BDSP,
  // que no PA8 nao significa "sem ovo" — e o jogo o exibiu como OVO.
  //
  // Se alguem reintroduzir um caminho de escrita sem `pkm::AjustesDeEntrada`,
  // estes checks ficam vermelhos.
  std::printf("\n=== spec 143: ajustes de entrada no deposito pela tela ===\n");
  {
    // Um Bidoof do BDSP, com o sentinela de "sem ovo" DAQUELE formato.
    pkm::Pokemon bdsp;
    bdsp.format = pkm::Format::kPB8;
    bdsp.species = 399;
    bdsp.exp = 135;
    bdsp.met_level = 4;
    bdsp.height_scalar = 128;
    bdsp.egg_location = 0xFFFF;  // sentinela do BDSP
    bdsp.effort_levels = {10, 10, 10, 10, 10, 10};

    g3::BoxPokemon mon{};
    mon.species = 399;
    mon.modern = std::make_shared<const pkm::Pokemon>(bdsp);

    // Deposito num save do PLA (formato PA8).
    cmt::SaveInfo pla;
    pla.kind = cmt::SaveKind::kModerno;
    pla.formato = pkm::Format::kPA8;
    pla.jogo_ms = ms::Game::kLegendsArceus;

    ms::Memory memory;
    std::vector<cmt::Change> changes = {{false, 0, 0, mon}};
    const cmt::Plan plan = cmt::BuildPlan(changes, pla, &memory);
    Check(plan.ok(), "BDSP -> PLA: plano valido");

    if (plan.ok() && plan.modern_changes.size() == 1 &&
        plan.modern_changes[0].mon.modern) {
      const pkm::Pokemon& out = *plan.modern_changes[0].mon.modern;
      Check(out.format == pkm::Format::kPA8, "BDSP -> PLA: virou PA8");
      // O ponto do bug do dono: o sentinela do BDSP NAO pode sobreviver.
      Check(out.egg_location == 0,
            "BDSP -> PLA: egg_location 0xFFFF virou 0 (era o bug do OVO)");
      // E os effort levels do PLA nascem zerados (pesquisa-effort-levels-pla).
      bool gv_zerado = true;
      for (std::uint8_t v : out.effort_levels) gv_zerado = gv_zerado && v == 0;
      Check(gv_zerado, "BDSP -> PLA: effort levels zerados");
    } else {
      Check(false, "BDSP -> PLA: o plano trouxe o Pokemon convertido");
    }
  }
  {
    // A direcao oposta: entrando NO BDSP, quem nao veio de ovo recebe 0xFFFF.
    pkm::Pokemon pk9;
    pk9.format = pkm::Format::kPK9;
    pk9.species = 25;
    pk9.exp = 1000;
    pk9.met_level = 5;
    pk9.egg_location = 0;

    g3::BoxPokemon mon{};
    mon.species = 25;
    mon.modern = std::make_shared<const pkm::Pokemon>(pk9);

    cmt::SaveInfo bdsp_info;
    bdsp_info.kind = cmt::SaveKind::kModerno;
    bdsp_info.formato = pkm::Format::kPB8;
    bdsp_info.jogo_ms = ms::Game::kBdsp;

    ms::Memory memory;
    std::vector<cmt::Change> changes = {{false, 0, 0, mon}};
    const cmt::Plan plan = cmt::BuildPlan(changes, bdsp_info, &memory);
    if (plan.ok() && plan.modern_changes.size() == 1 &&
        plan.modern_changes[0].mon.modern) {
      Check(plan.modern_changes[0].mon.modern->egg_location == 0xFFFF,
            "PK9 -> BDSP: egg_location 0 virou 0xFFFF (sentinela do destino)");
    } else {
      Check(false, "PK9 -> BDSP: o plano trouxe o Pokemon convertido");
    }
  }

  if (g_failures == 0) {
    std::printf("\nTodos os testes passaram.\n");
    return 0;
  }
  std::printf("\n%d teste(s) falharam.\n", g_failures);
  return 1;
}
