// Decisao pura do commit (spec 128). Ver commit_plan.h para o desenho.
//
// A logica aqui foi TRANSCRITA de `CommitNestBox` sem mudanca de
// comportamento: esta spec e refactor + teste, e qualquer diferenca observavel
// e bug. Os comentarios que explicam POR QUE cada ramo existe vieram junto,
// porque eles sao a memoria das specs 106/111/124/125.
#include "commit_plan.h"

#include <cstring>

#include "gen3_transfer.h"
#include "learnset.h"
#include "lgpe_encontros.h"
#include "personal_tables.h"
#include "za_plus_levels.h"
#include "move_pp.h"
#include "pkm_convert.h"
#include "species_facts.h"

namespace pokehome::commit {
namespace {

namespace g3 = pokehome::gen3;
namespace g3x = pokehome::g3x;
namespace msv = pokehome::moveset;
namespace vw = pokehome::view;

// Jogo do moveset derivado do FORMATO do Pokemon. Transcrito do
// `MsGameOfPkm` da UI, e NAO substituivel por `moveset::OriginBucket`: aquele
// prioriza `origin_game` e devolve outro bucket para o mesmo Pokemon. A
// descida memoriza o moveset atual sob este bucket; mudar o criterio mudaria
// onde ele fica guardado.
msv::Game MsGameOfFormat(const pkm::Pokemon& p) {
  switch (p.format) {
    case pkm::Format::kPB7: return msv::Game::kLgpe;
    case pkm::Format::kPK8: return msv::Game::kSwSh;
    case pkm::Format::kPB8: return msv::Game::kBdsp;
    case pkm::Format::kPA8: return msv::Game::kLegendsArceus;
    case pkm::Format::kPK9:
      return p.origin_game == 52 ? msv::Game::kZA : msv::Game::kSV;
    default: break;
  }
  return msv::Game::kSV;
}

}  // namespace

// O que todo Pokemon recebe ao ENTRAR num save moderno, venha de outro formato
// ou do mesmo. Existe como funcao unica de proposito: enquanto os dois ramos
// tinham copias, o de troca de formato ficou sem o reset de moveset por anos —
// e foi exatamente esse o bug do ovo (spec 143).
void AplicaEntradaNoDestino(pkm::Pokemon& mon, const SaveInfo& save,
                            msv::Memory* memory) {
  if (mon.empty()) return;

  // O tracker do HOME e pre-requisito, nao consequencia: `ApplyOnEntry` so
  // age sobre quem tem tracker, e o PkHeX acusa "HOME Transfer Tracker is
  // missing" em quem entra sem ele. Nunca sobrescreve um existente.
  msv::AssignTracker(mon);

  // G11/G12: ja esteve neste jogo -> restaura o memorizado; primeira vez ->
  // reseta pelo learnset. O PLA e o BDSP tem engine de golpes propria, e um
  // moveset do jogo errado sai como "Invalid Move" no verificador.
  const std::uint16_t dex = pkm::NationalDex(mon);
  const std::uint8_t lvl = pokehome::species::LevelFromExp(dex, mon.exp);
  if (memory && lvl > 0) memory->ApplyOnEntry(mon, save.jogo_ms, lvl);

  // RELEARN: os quatro slots zeram na entrada.
  //
  // Nao sao "golpes que o Pokemon ja conheceu" — sao os EGG MOVES do encontro.
  // Um Pokemon capturado selvagem nao tem nenhum, e o PkHeX espera `(None)`.
  // O campo viajava intacto porque este reset reescrevia o moveset sem tocar
  // nele, e o Pokemon chegava carregando os egg moves do jogo de origem.
  //
  // A regra NAO e de um jogo so. Medido injetando um unico relearn em quem ja
  // estava legal: reprova 447/447 no BDSP, 163/163 no PLA e 453/453 no SV,
  // sempre com "Expected: (None)". Por isso a limpeza e incondicional.
  //
  // Nao se faz na CONVERSAO: la o campo precisa sobreviver, senao a memoria de
  // moveset por tracker perde o que restaurar na volta ao jogo de origem.
  mon.relearn_moves = {};

  // KELDEO: a forma e o golpe sao a MESMA coisa (regra do jogo, nao do
  // PkHeX). Resolute (form 1) existe enquanto ele souber Secret Sword;
  // esquecer o golpe reverte a forma. O reset de moveset acima nao sabe
  // disso e removia o golpe deixando a forma — "Keldeo Move/Form mismatch".
  if (dex == 647) {
    constexpr std::uint16_t kSecretSword = 548;
    bool sabe = false;
    for (int i = 0; i < 4; ++i)
      if (mon.moves[i] == kSecretSword) sabe = true;
    if (mon.form == 1 && !sabe) {
      // devolve o golpe no primeiro slot vago (ou no ultimo, se cheio)
      int slot = 3;
      for (int i = 0; i < 4; ++i)
        if (mon.moves[i] == 0) { slot = i; break; }
      mon.moves[slot] = kSecretSword;
    } else if (mon.form == 0 && sabe) {
      for (int i = 0; i < 4; ++i)
        if (mon.moves[i] == kSecretSword) mon.moves[i] = 0;
    }
  }

  // O PP acompanha o golpe. Reescrever o moveset sem reescrever o PP troca
  // "Invalid Move" por "Move N PP is above the amount allowed".
  for (int i = 0; i < 4; ++i) {
    mon.pp[i] = mon.moves[i]
                    ? pokehome::movepp::Modern(
                          static_cast<std::uint8_t>(mon.format), mon.moves[i])
                    : 0;
  }

  // Nivel de obediencia (spec 120): nos nativos do gen9 ele acompanha o
  // nivel, e o PkHeX acusa "Invalid Obedience Level" quando fica zerado.
  // Vale para todo destino PK9 — SV e Z-A usam o mesmo campo.
  if (mon.format == pkm::Format::kPK9 && lvl > 0) mon.obedience_level = lvl;

  // Plus flags do Z-A (spec 122): o jogo exige a flag de CADA golpe que o
  // Pokemon conhece pelo learnset ate o nivel dele — nao so dos quatro
  // equipados. Sem elas o Z-A nao monta o registro e desenha um OVO.
  //
  // Vivia em `gen3_transfer.cpp`, alcancavel so pela rota gen3 (spec 145). O
  // gerador de lote nao passava por la e produzia 176 flags de Tackle
  // faltando — o dono viu o simbolo de invalido na box, com a suite verde.
  // A regra depende do JOGO de destino, entao o lugar dela e aqui, onde o
  // `SaveInfo` existe.
  if (save.jogo_ms == moveset::Game::kZA) {
    // Tamanho no Z-A: os TRES campos ficam em zero. O PkHeX acusa "Height/
    // Weight does not match the expected value" para qualquer outro valor —
    // medido campo a campo em `tools/pkhex-za2` contra o registro real: com
    // os tres zerados passa; com scalar=1, 114, 128 ou 255 falha; zerando so
    // os scalars e deixando o Scale, ainda falha.
    //
    // O Z-A deriva o tamanho na hora de exibir, em vez de guardar. Os campos
    // sao heranca do SV, que os usa de verdade — e por isso o lote nascia com
    // 363 de 364 defeituosos.
    mon.height_scalar = 0;
    mon.weight_scalar = 0;
    mon.scale = 0;

    // O bit NAO e indexado pelo id do golpe: e a POSICAO dele na lista
    // PlusMoveIndexes da especie (za_plus.h). Guardamos os ids; o writer do
    // pk9 traduz para o bit.
    // O corte e pelo nivel da lista PLUS, nao pelo do learnset: sao
    // grandezas diferentes (spec 145). O Bulbasaur aprende Tackle no nivel 1
    // e a flag so vale do 10; o Kadabra herda Confusion da pre-evolucao
    // (learnset 0) e a flag vale do 19. Filtrar por `e[i].level` descartava
    // justamente os herdados — 55 dos 364 do lote ficavam sem flag.
    mon.za_plus_moves.clear();
    const learnset::Entry* e = nullptr;
    std::size_t n = 0;
    if (lvl > 0 && learnset::Find(learnset::Game::kZA, dex, mon.form, &e, &n)) {
      for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t nivel_plus = za_plus_levels::Nivel(e, i);
        if (nivel_plus == 0 || nivel_plus > lvl) continue;
        mon.za_plus_moves.push_back(e[i].move);
      }
    }
    // Os equipados so entram se JA estiverem na lista do learnset ate o
    // nivel: ligar a flag de um golpe que o Pokemon ainda nao aprenderia
    // produz "Multiple Plus Move flags are invalid" (medido no PkHeX).
    for (int i = 0; i < 4; ++i) {
      const std::uint16_t mv = mon.moves[i];
      if (!mv) continue;
      bool ja = false;
      for (const std::uint16_t x : mon.za_plus_moves) {
        if (x == mv) { ja = true; break; }
      }
      if (!ja) mon.za_plus_moves.push_back(mv);
    }
    mon.za_plus_dex = dex;  // a lista de bits e por especie

    // Slot de golpe VAZIO no Z-A guarda PP 35, nao 0. O jogo trata o golpe 0
    // ("—") como entrada real e o PkHeX acusa "Move N PP is below the amount
    // expected" — medido com `SetMaximumPPCurrent` sobre o nosso Weedle, que
    // so tem 2 golpes: o esperado sai 35,40,35,35.
    //
    // `move_pp.h` devolve 0 para o golpe 0, que e o certo para os outros
    // jogos; so o Z-A cobra. Por isso a correcao mora aqui e nao la.
    for (int i = 0; i < 4; ++i)
      if (mon.moves[i] == 0) mon.pp[i] = 35;
  }

  // HP atual: zero e DESMAIADO. O jogo mostra o Pokemon com o icone cinza de
  // "FAINTED" na caixa e HP 0/51 no painel — foi o que o dono viu, e o que
  // levou meia sessao a ser diagnosticado como "ovo" ou "registro invalido".
  // Nao era nenhum dos dois: o registro sempre esteve certo, so chegava morto.
  //
  // A formula ja existia em `species::MaxHp`, mas so era chamada na rota
  // gen3 (gen3_transfer.cpp:229). Quem nasce direto no destino — o gerador de
  // lote — nunca passava por la. Mesmo padrao das plus flags (spec 145).
  //
  // So preenche quem esta com 0: um Pokemon que chega ferido de outro jogo
  // continua ferido, que e o comportamento do HOME.
  if (mon.hp_current == 0 && lvl > 0) {
    mon.hp_current = pokehome::species::MaxHp(dex, mon.ivs[0], mon.evs[0], lvl);
  }

  // ITEM SEGURADO nunca viaja (pesquisa do HOME, §7: "o HOME nao armazena
  // itens — ao depositar, o item volta para a Bag do jogo de origem").
  //
  // Medido em 2026-08-20: 10 Pokemon de evento (Jirachi, Porygon2, Zamazenta)
  // atravessaram segurando o item do encontro, e o PkHeX acusou "Held item is
  // unreleased" / "does not match Form" no destino.
  //
  // O guard de procedencia importa: este ponto tambem roda para movimento
  // LOCAL na caixa, e um nativo movido nao pode perder o que segura. So quem
  // vem de OUTRO jogo larga o item.
  //
  // PENDENCIA registrada: o HOME devolve o item a Bag da ORIGEM; nos apenas o
  // removemos. Devolver exige escrever na bag do save de origem no mesmo
  // commit (bag_writer existe) — fica para decisao do dono. E o deposito no
  // banco guarda os bytes crus COM o item: retirar no proprio jogo de origem
  // ainda o traz de volta, o que diverge do HOME mas nao produz ilegalidade.
  if (msv::OriginBucket(mon) != save.jogo_ms) {
    mon.held_item = 0;

    // FORMA QUE SO EXISTE SEGURADA PELO ITEM. Fora do PLA, Giratina Origin
    // exige o Griseous Orb — um form 1 sem item e "Held item does not match
    // Form" no PkHeX (2 casos medidos, ambos Giratina Origin vindos do PLA,
    // onde a forma vive solta). O EntityConverter NAO faz este revert (form 1
    // item 0 sai dele tambem), mas o produto dele e ilegal do mesmo jeito — a
    // autoridade aqui e a regra do jogo de destino, nao o converter.
    //
    // Dialga/Palkia Origin e Zacian/Zamazenta Crowned seguem a mesma regra
    // (Adamant Crystal / Lustrous Globe / Rusted Sword / Rusted Shield).
    // O PLA fica fora do revert: la as formas Origin existem sem item.
    if (save.jogo_ms != moveset::Game::kLegendsArceus && mon.form == 1) {
      switch (dex) {
        case 483:  // Dialga
        case 484:  // Palkia
        case 487:  // Giratina
        case 888:  // Zacian
        case 889:  // Zamazenta
          mon.form = 0;
          break;
        default: break;
      }
    }
  }

  // ABILITY re-derivada pela tabela do DESTINO. A hidden ability MUDA entre
  // jogos — Piplup: Defiant (128) no BDSP, Competitive (172) no SV — e nos
  // preservavamos o VALOR cru, que nao casa com slot nenhum no destino.
  // Medido no EntityConverter (sonda P22): PB8(abil 128, H) -> PK9 vira 172.
  // O que viaja e o SLOT (ability_number), nao o valor.
  //
  // So os slots 1/2/4; um numero fora disso e dado que nao sabemos derivar, e
  // inventar seria pior que deixar o PkHeX apontar.
  {
    const auto jogo_p = [&]() -> pokehome::personal::Jogo {
      using MG = moveset::Game;
      using PJ = pokehome::personal::Jogo;
      switch (save.jogo_ms) {
        case MG::kLgpe: return PJ::kLgpe;
        case MG::kSwSh: return PJ::kSwSh;
        case MG::kBdsp: return PJ::kBdsp;
        case MG::kLegendsArceus: return PJ::kLa;
        case MG::kZA: return PJ::kZA;
        default: return PJ::kSV;
      }
    }();
    if (const auto* e = pokehome::personal::Find(jogo_p, dex, mon.form)) {
      switch (mon.ability_number) {
        case 1: mon.ability = e->ability1; break;
        case 2: mon.ability = e->ability2; break;
        case 4: mon.ability = e->ability_hidden; break;
        default: break;
      }
    }
  }

  // ENTRADA NO SwSh — o PK8 nao representa origem em jogo POSTERIOR.
  //
  // O HOME nao preserva Version/MetLocation ao descer para o Sword/Shield:
  // reescreve a versao para SW/SH e codifica a origem REAL num local 60000-.
  // Medido no EntityConverter (sondas P8/P9/P10/P11, 2026-08-20):
  //
  //   PLA (47) -> SW(44) met 60000      SL (50) -> SW(44) met 59997
  //   BD  (48) -> SW(44) met 59999      VL (51) -> SH(45) met 59996
  //   SP  (49) -> SH(45) met 59998      LGPE    -> preservado (gen8 nativa)
  //
  // MetLevel e preservado. Nascido de OVO recebe o sentinela 65534 no
  // egg_location (65535 NAO e ovo: e o "sem ovo" do proprio BDSP). Ball so
  // muda se nao existir na gen8 (as de Hisui, >= 27, viram Poke Ball).
  //
  // E EXCLUSIVO do SwSh: BDSP e SV preservam a origem intacta — o BDSP
  // aceitava 206/207 vindos do PLA enquanto o SwSh recusava TODOS os 740
  // transferidos com "Unable to match an encounter from origin game".
  //
  // Vive AQUI e nao no Convert de proposito: o Convert e puro e sem perda
  // (pb8->pk8->pb8 preserva met_location, e o teste garante). A reescrita e
  // regra de ENTRADA — depende do jogo de destino, como as outras deste
  // arquivo. O que se perde na VOLTA (o met original nao e restaurado ao
  // sair do SwSh) fica registrado como pendencia — o HOME real restaura do
  // registro mestre, que nos nao guardamos ainda.
  if (save.jogo_ms == moveset::Game::kSwSh) {
    std::uint16_t met_home = 0;
    std::uint8_t ver = 0;
    switch (mon.origin_game) {
      case 47: ver = 44; met_home = 60000; break;  // PLA -> SW
      case 48: ver = 44; met_home = 59999; break;  // BD  -> SW
      case 49: ver = 45; met_home = 59998; break;  // SP  -> SH
      case 50: ver = 44; met_home = 59997; break;  // SL  -> SW
      case 51: ver = 45; met_home = 59996; break;  // VL  -> SH
      default: break;                              // SwSh/LGPE: intacto
    }
    if (met_home != 0) {
      mon.origin_game = ver;
      mon.met_location = met_home;
      const bool nasceu_de_ovo =
          mon.egg_location != 0 && mon.egg_location != 65535;
      mon.egg_location = nasceu_de_ovo ? 65534 : 0;
      if (mon.ball >= 27) mon.ball = 4;
    }
  }

  // ENTRADA NO LET'S GO — o PB7 nao tem local de transferencia.
  //
  // Os outros jogos aceitam um met de "veio do HOME" (30001) ou de origem
  // anterior (os 59996-60000 do SwSh acima). O Let's Go nao: ele so conhece
  // encontros NATIVOS e o GO Park, e um registro com met_location=0 reprova
  // com "Unable to match an encounter from origin game" — medido, 151 de 151
  // numa rota BDSP -> LGPE.
  //
  // Entao o Pokemon que chega passa a apontar para um encontro REAL daquela
  // especie no jogo de destino: met, nivel e bola do encontro. E o mesmo
  // padrao que o lote usa desde a spec 147, e o que faz o "Unable to match"
  // sumir (4 de 5 casos ficaram LEGAIS na sonda P59; o quinto era artefato
  // da data de teste no futuro).
  //
  // TD: isto MENTE sobre onde o Pokemon foi capturado — ele diz ter nascido
  // no Let's Go. A alternativa era deixar o registro invalido, que o jogo
  // aceita mas nenhum verificador aprova. O dono escolheu a rota existir
  // (spec 150), e o README avisa que ela nao alcanca o padrao de
  // legitimidade das outras.
  //
  // Especie sem encontro no LGPE (evolucoes: Ivysaur, Venusaur...) fica como
  // esta — nao ha encontro a apontar, e inventar um seria pior.
  if (save.jogo_ms == moveset::Game::kLgpe &&
      msv::OriginBucket(mon) != moveset::Game::kLgpe) {
    // O encontro escolhido e o de maior nivel que CABE: met_level acima do
    // nivel atual reprova com "Current level is below met level".
    const std::uint8_t nivel_atual =
        pokehome::species::LevelFromExp(pkm::NationalDex(mon), mon.exp);
    if (const auto* e =
            pokehome::lgpe::Acha(pkm::NationalDex(mon), nivel_atual)) {
      // A versao fica no par: `savew::Game` tem um `kLGPE` so e
      // `compat::Game` um `kLetsGo` so, entao nao da para saber aqui se o
      // save e GP ou GE. Nao importa para o veredito — o LGPE e par de
      // versoes e o verificador aceita encontro de qualquer uma das duas.
      // ponytail: fixar a versao quando a SaveInfo souber distinguir.
      mon.origin_game = 42;  // Let's Go, Pikachu! (o par)
      mon.met_location = e->met;
      mon.met_level = e->nivel;
      mon.egg_location = 0;
      if (mon.ball >= 27) mon.ball = 4;

      // AV de HP e dos demais stats: o LGPE da 1 AV por nivel GANHO, e o
      // verificador exige `AV >= nivel_atual - met_level` em cada um. A
      // mesma regra ja existia na subida gen3 (`gen3_transfer.cpp:251`),
      // mas so la — quem chega dos jogos modernos passava sem AV nenhum e
      // reprovava com "Defense AV should be greater than 1".
      //
      // Preenche o MINIMO: o AV soma direto no stat, entao encher alem
      // inventaria HP e CP que o Pokemon nao teve.
      if (nivel_atual > e->nivel) {
        const std::uint8_t piso =
            static_cast<std::uint8_t>(nivel_atual - e->nivel);
        for (auto& av : mon.awakening_values)
          if (av < piso) av = piso;
      }
    }
  }

  // Handler: quem NAO nasceu neste jogo esta sendo SEGURADO, nao e mais o
  // treinador original — `Current handler cannot be the OT` no PkHeX.
  //
  // A regra de `modern_box_view.cpp:119` compara os nomes, e por isso nao
  // dispara quando o OT do Pokemon e o do save sao o mesmo treinador (o caso
  // do dono: "Amaral" nos dois). O criterio correto e a PROCEDENCIA, nao o
  // nome: um Shinx do BDSP num save do PLA e transferido mesmo que o OT
  // coincida. Um HT ja preenchido por outro jogo nunca e sobrescrito.
  if (msv::OriginBucket(mon) != save.jogo_ms && mon.current_handler == 0) {
    mon.current_handler = 1;
    if (mon.ht_name.empty() && !save.trainer_name.empty()) {
      mon.ht_name = save.trainer_name;
      mon.ht_name_raw = {};  // quem muda o texto zera o raw (spec 145)
      // 50 e a amizade base medida na sonda do `AdaptToSaveFile`
      // (transfer.cpp:245), nao um chute.
      mon.ht_friendship = 50;
      mon.ht_language = mon.language;
    }
  }
}


Plan BuildPlan(const std::vector<Change>& changes, const SaveInfo& save,
               msv::Memory* memory) {
  Plan plan;

  // Separa as alteracoes por lado ANTES de decidir qualquer coisa: e o que
  // permite abortar com tudo intacto.
  for (const Change& c : changes) {
    if (c.to_nest) {
      plan.nest_writes.push_back(c);
      continue;
    }
    plan.save_changes[c.box * g3::kSlotsPerBox + c.slot] = c.mon;
    plan.modern_changes.push_back({c.box, c.slot, c.mon});
  }

  if (plan.touches_save() && save.kind == SaveKind::kNenhum) {
    plan.error = "alteracoes no painel do save, mas a fonte nao e gravavel";
    return plan;
  }

  // Quem pode evoluir por troca ao ser GUARDADO (spec 146, DEC-2).
  //
  // So no sentido jogo -> NestBox: guardar e a operacao que o jogo leria como
  // troca. Isto apenas OFERECE — nada e alterado aqui. Quem aplica e
  // `AplicaEvolucoes`, depois da resposta do dono.
  for (std::size_t i = 0; i < plan.nest_writes.size(); ++i) {
    const g3::BoxPokemon& mon = plan.nest_writes[i].mon;
    if (mon.empty() || mon.is_egg) continue;  // ovo nao evolui

    // Mesmo padrao de resolucao de dex que a UI usa: fonte moderna traz o
    // national_dex pronto, gen3 deriva do indice interno.
    const int dex =
        mon.national_dex ? mon.national_dex : g3::NationalDex(mon.species);
    const int alvo = evo::AlvoDaTroca(dex);
    if (alvo == 0) continue;

    CandidatoEvolucao cand;
    cand.indice = i;
    cand.dex_base = dex;
    cand.dex_alvo = alvo;
    // O jogo de onde ele saiu aceita o evoluido? Falso em 52 casos medidos —
    // o dialogo avisa que o Pokemon nao podera voltar. Jogo desconhecido nao
    // gera aviso: melhor omitir do que chutar.
    cand.origem_aceita_alvo =
        save.jogo_origem == compat::Game::kCount ||
        compat::HasSpecies(save.jogo_origem, alvo);
    plan.candidatos_evolucao.push_back(cand);
  }

  // Conversao entre geracoes no COMMIT (spec 111). O dry-run do drop ja
  // aprovou cada gesto; aqui a conversao roda de verdade, com a memoria de
  // moveset — que sera gravada no mesmo ciclo. Uma falha aqui aborta com TUDO
  // intacto, porque nada foi escrito ainda.
  if (save.kind == SaveKind::kModerno) {
    for (vw::BoxChange& ch : plan.modern_changes) {
      if (ch.mon.empty()) continue;

      if (!ch.mon.modern) {
        // Subida gen3 -> Switch: o snapshot gen3 entra na memoria e o moveset
        // vira o do jogo (ou o memorizado de la, G11).
        auto up = g3x::ConvertUp(ch.mon.raw, save.formato, save.jogo_ms,
                                 memory);
        if (!up) {
          plan.error = "ConvertUp recusou (caixa " + std::to_string(ch.box) +
                       " slot " + std::to_string(ch.slot) + ")";
          return plan;
        }
        ch.mon.modern = std::make_shared<const pkm::Pokemon>(std::move(*up));
      } else if (ch.mon.modern->format != save.formato) {
        const pkm::Format origem_fmt = ch.mon.modern->format;
        auto conv = pkm::Convert(*ch.mon.modern, save.formato);
        if (!conv) {
          plan.error = "Convert entre formatos recusou (caixa " +
                       std::to_string(ch.box) + " slot " +
                       std::to_string(ch.slot) + ")";
          return plan;
        }
        // Spec 143: ate aqui o deposito pela TELA pulava os ajustes de
        // entrada que o `transfer::Commit` fazia — e um Pokemon do BDSP
        // entrava no PLA com o sentinela de egg location do BDSP, virando
        // ovo no jogo. Um caminho de escrita sem isto esta errado.
        pkm::AjustesDeEntrada(*conv, origem_fmt);
        // ...e pulava tambem o RESTO da entrada, que o ramo de mesmo formato
        // logo abaixo sempre fez. A assimetria era o bug: o caso que MAIS
        // precisa do reset — trocar de formato — era o unico sem ele.
        //
        // Medido em 2026-08-19 com o jogo rodando (Ryujinx + pkhex-verify):
        // um Shinx BDSP -> PLA gravado por aqui saia `legal=false` com
        // "Invalid Move 1/2", "Move 1/3 PP is above the amount allowed" e
        // "Pokemon HOME Transfer Tracker is missing" — e o PLA renderiza
        // registro ilegal como OVO.
        AplicaEntradaNoDestino(*conv, save, memory);
        ch.mon.modern = std::make_shared<const pkm::Pokemon>(std::move(*conv));
      } else {
        // MESMO formato (spec 125): a entrada num jogo que nao e o de origem
        // tambem restaura o moveset memorizado de la (ou reseta pelo
        // learnset, G11/G12) — antes so a conversao fazia isso, e o Pokemon
        // voltava da NestBox com os golpes originais em vez dos do jogo.
        //
        // O `home_tracker != 0` saiu da condicao (spec 143): quem NAO tinha
        // tracker pulava a entrada inteira, e como e `AplicaEntradaNoDestino`
        // que ATRIBUI o tracker, o ciclo se auto-excluia — o Pokemon nunca
        // ganhava tracker e nunca era ajustado. Levava os golpes de origem
        // para o jogo.
        //
        // E isso NAO e cosmetico: medido em 2026-08-19, um golpe que nao
        // existe na engine do PLA (id 903, de gen9) **MATA O JOGO** em ~11 s,
        // sem excecao no log. Especie fora do jogo e forma impossivel ele
        // tolera; golpe invalido, nao.
        //
        // Quem reseta o moveset e o HOME, nao o jogo de destino — e aqui o
        // NestBox faz esse papel. O TD-02 da spec 038 assumia o contrario
        // ("o destino reseta por conta propria"); a medicao desmentiu.
        pkm::Pokemon copy = *ch.mon.modern;
        if (msv::OriginBucket(copy) != save.jogo_ms) {
          AplicaEntradaNoDestino(copy, save, memory);
          ch.mon.modern = std::make_shared<const pkm::Pokemon>(std::move(copy));
        }
      }
    }
    // O save moderno nao usa o mapa por indice linear: zera para o plano nao
    // sugerir duas escritas do mesmo conteudo.
    plan.save_changes.clear();
  }

  if (save.kind == SaveKind::kGen3) {
    for (auto& [idx, mon] : plan.save_changes) {
      if (mon.empty() || !mon.modern) continue;
      // Descida Switch -> gen3: o moveset moderno fica memorizado e o raw
      // gen3 e construido (com o gen3 original restaurado, se houver).
      std::uint8_t raw[80];
      // `src_ms` sai de MsGameOfPkm (o mapa por FORMATO da UI), e nao de
      // OriginBucket (que prioriza `origin_game`). Os dois discordam, e
      // trocar um pelo outro mudaria o bucket onde o moveset e memorizado —
      // esta spec e refactor, entao o comportamento tem de ficar identico.
      if (!g3x::ConvertDown(*mon.modern, save.learnset_gen3,
                            MsGameOfFormat(*mon.modern), memory,
                            save.origem_gen3, raw)) {
        plan.error = "ConvertDown recusou (indice " + std::to_string(idx) + ")";
        return plan;
      }
      std::memcpy(mon.raw, raw, sizeof(mon.raw));
      mon.modern.reset();
    }
    // Simetrico ao caso moderno: o save gen3 escreve pelo mapa.
    plan.modern_changes.clear();
  }

  // Chegada a NestBox (spec 125): o Pokemon "relembra" os golpes do jogo de
  // ORIGEM ja aqui, sem precisar voltar la — regra do dono. O moveset do jogo
  // de onde ele saiu fica memorizado para a proxima ida. So age em mon
  // moderno vindo de um save moderno aberto; mover dentro da caixa nao
  // re-memoriza (guarda do RestoreOnBank).
  if (save.kind == SaveKind::kModerno) {
    for (Change& c : plan.nest_writes) {
      if (c.mon.empty() || !c.mon.modern) continue;
      pkm::Pokemon copy = *c.mon.modern;
      if (!msv::RestoreOnBank(copy, *memory, save.jogo_ms)) continue;
      copy.raw = vw::WriteModern(copy);
      if (copy.raw.empty()) continue;  // falhou: mantem o original
      c.mon.modern = std::make_shared<const pkm::Pokemon>(std::move(copy));
    }
  }

  return plan;
}

void AplicaEvolucoes(Plan& plan, const std::vector<std::size_t>& aceitos) {
  for (const std::size_t k : aceitos) {
    if (k >= plan.candidatos_evolucao.size()) continue;
    const CandidatoEvolucao& cand = plan.candidatos_evolucao[k];
    if (cand.indice >= plan.nest_writes.size()) continue;

    g3::BoxPokemon& mon = plan.nest_writes[cand.indice].mon;
    if (mon.empty()) continue;

    // A espécie de exibicao e sempre atualizada: e o que a caixa mostra.
    mon.national_dex = static_cast<std::uint16_t>(cand.dex_alvo);
    mon.species_name = g3::SpeciesNameByDex(cand.dex_alvo);

    if (!mon.modern) continue;  // gen3 puro: so a exibicao muda por ora

    pkm::Pokemon copy = *mon.modern;
    copy.species = static_cast<std::uint16_t>(cand.dex_alvo);

    // O APELIDO so acompanha quem NUNCA foi apelidado. Um Haunter chamado
    // "Fantasminha" continua "Fantasminha" depois de virar Gengar — e o que
    // o jogo faz, e mexer nisso apagaria escolha do dono.
    if (!copy.is_nicknamed) copy.nickname = g3::SpeciesNameByDex(cand.dex_alvo);

    // A HABILIDADE e re-derivada pelo SLOT, nunca copiada como valor: o
    // Gengar tem outra lista de habilidades que o Haunter, e manter o id cru
    // produziria uma habilidade que nao casa com slot nenhum. Mesma regra da
    // entrada no destino (ver "ABILITY re-derivada" acima).
    if (const auto* e = pokehome::personal::Find(pokehome::personal::Jogo::kSV,
                                                 cand.dex_alvo, copy.form)) {
      switch (copy.ability_number) {
        case 1: copy.ability = e->ability1; break;
        case 2: copy.ability = e->ability2; break;
        case 4: copy.ability = e->ability_hidden; break;
        default: break;
      }
    }

    // O `met` NAO muda, de proposito. Um evoluido por troca carrega para
    // sempre o local onde foi capturado como o estagio anterior — foi assim
    // que a sonda mediu `legal=True`, e e como se reconhece um de verdade.

    copy.raw = vw::WriteModern(copy);
    if (copy.raw.empty()) continue;  // falhou: mantem o original intacto
    mon.modern = std::make_shared<const pkm::Pokemon>(std::move(copy));
  }
}

}  // namespace pokehome::commit
