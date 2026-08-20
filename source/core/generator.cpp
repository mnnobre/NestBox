#include "generator.h"

#include <algorithm>
#include <cstring>

#include "gen3_save.h"
#include "learnset.h"
#include "legality.h"
#include "moldes.h"
#include "move_names.h"
#include "move_pp.h"
#include "pa8.h"
#include "pb7.h"
#include "pb8.h"
#include "pk4.h"
#include "pk8.h"
#include "pk9.h"
#include "pkm_convert.h"

namespace pokehome::generator {
namespace {

namespace ls = pokehome::learnset;
namespace g3 = pokehome::gen3;

// A struct MapaJogo vive em generator.h desde a spec 145: o gerador de lote
// consulta a MESMA tabela, em vez de repetir as decisoes por jogo.
constexpr MapaJogo kMapa[] = {
    // O origin_game sai da numeracao do PkHeX, a mesma que
    // moveset_memory.cpp:100 documenta a partir de save real.
    {personal::Jogo::kRubySapphire, pkm::Format::kPK4, ls::Game::kRubySapphire, true, 1},
    {personal::Jogo::kEmerald, pkm::Format::kPK4, ls::Game::kEmerald, true, 3},
    {personal::Jogo::kFireRed, pkm::Format::kPK4, ls::Game::kFireRed, true, 4},
    {personal::Jogo::kLeafGreen, pkm::Format::kPK4, ls::Game::kLeafGreen, true, 5},
    {personal::Jogo::kLgpe, pkm::Format::kPB7, ls::Game::kLgpe, true, 42},
    {personal::Jogo::kSwSh, pkm::Format::kPK8, ls::Game::kSwSh, true, 44},
    {personal::Jogo::kBdsp, pkm::Format::kPB8, ls::Game::kBdsp, true, 48},
    {personal::Jogo::kLa, pkm::Format::kPA8, ls::Game::kLegendsArceus, true, 47},
    {personal::Jogo::kSV, pkm::Format::kPK9, ls::Game::kSV, true, 50},
    {personal::Jogo::kZA, pkm::Format::kPK9, ls::Game::kZA, true, 52},
};



// Nome legivel do golpe, para o texto do problema. Fora da faixa devolve o
// numero, porque "golpe 9999" ainda diz mais ao jogador que uma string vazia.
std::string NomeGolpe(std::uint16_t move) {
  if (move < modern::kMoveNameCount && modern::kMoveNames[move][0] != '\0')
    return modern::kMoveNames[move];
  return "golpe " + std::to_string(move);
}

// A bola existe naquela geracao? A tabela e pequena de proposito: guarda a
// GERACAO em que cada bola foi introduzida, que e tudo o que a checagem
// precisa. Uma bola de gen 7 num encontro de gen 3 e impossivel.
//
// Numeracao das bolas: a do PkHeX (1=Master, 2=Ultra, 3=Great, 4=Poke...).
std::uint8_t GeracaoDaBola(std::uint8_t ball) {
  if (ball == 0) return 1;
  if (ball <= 12) return 1;   // Master..Premier: gen 1/2 (Premier e gen3, mas
                              // ja existia na 2 como item)
  if (ball <= 16) return 3;   // Repeat, Timer, Nest, Net (gen 3)
  if (ball <= 21) return 3;   // Dive, Luxury, Heal, Quick, Dusk
  if (ball <= 24) return 4;   // Cherish, Fast, Level
  if (ball <= 26) return 4;   // Lure, Heavy
  if (ball <= 32) return 4;   // Love, Friend, Moon, Sport, Dream, Beast(?)
  return 7;                   // Beast e as posteriores
}

std::uint8_t GeracaoDoJogo(personal::Jogo j) {
  switch (j) {
    case personal::Jogo::kRubySapphire:
    case personal::Jogo::kEmerald:
    case personal::Jogo::kFireRed:
    case personal::Jogo::kLeafGreen:
      return 3;
    case personal::Jogo::kLgpe:
    case personal::Jogo::kSwSh:
      return 8;
    case personal::Jogo::kBdsp:
    case personal::Jogo::kLa:
      return 8;
    case personal::Jogo::kSV:
    case personal::Jogo::kZA:
      return 9;
    default:
      return 9;
  }
}

// A habilidade daquele slot, ou 0 se a especie nao tem aquele slot.
std::uint16_t AbilidadeDoSlot(const personal::EntryFull& e, std::uint8_t slot) {
  switch (slot) {
    case 1: return e.ability1;
    case 2: return e.ability2;
    case 4: return e.ability_hidden;
    default: return 0;
  }
}

// O genero e possivel para aquela razao? A razao e a do PkHeX: 0=so macho,
// 254=so femea, 255=sem genero, e os intermediarios permitem os dois.
bool GeneroPossivel(std::uint8_t ratio, std::uint8_t genero) {
  if (ratio == 255) return genero == 2;   // sem genero
  if (ratio == 254) return genero == 1;   // so femea
  if (ratio == 0) return genero == 0;     // so macho
  return genero == 0 || genero == 1;
}

std::uint8_t GeneroForcado(std::uint8_t ratio) {
  if (ratio == 255) return 2;
  if (ratio == 254) return 1;
  return 0;
}

}  // namespace

const MapaJogo* Mapa(personal::Jogo j) {
  for (const MapaJogo& m : kMapa)
    if (m.jogo == j) return &m;
  return nullptr;
}

pkm::Format FormatOf(personal::Jogo jogo) {
  const MapaJogo* m = Mapa(jogo);
  return m ? m->formato : pkm::Format::kNone;
}

const personal::EntryFull* PersonalOf(const GeneratorState& s) {
  return personal::Find(s.jogo, s.dex, s.form);
}

// --- Orcamento de EV -------------------------------------------------------

std::uint16_t EvTotal(const GeneratorState& s) {
  std::uint16_t t = 0;
  for (int i = 0; i < 6; ++i) t += s.evs[i];
  return t;
}

std::uint8_t SetEv(GeneratorState& s, int i, int valor) {
  if (i < 0 || i >= 6) return 0;
  int v = std::clamp(valor, 0, static_cast<int>(kEvStatMax));
  // O teto do TOTAL e sobre os OUTROS cinco: o proprio campo esta sendo
  // reescrito, entao ele nao conta contra si mesmo.
  const int outros = EvTotal(s) - s.evs[i];
  const int teto = std::max(0, static_cast<int>(kEvTotalMax) - outros);
  v = std::min(v, teto);
  s.evs[i] = static_cast<std::uint8_t>(v);
  return s.evs[i];
}

std::uint8_t SetIv(GeneratorState& s, int i, int valor) {
  if (i < 0 || i >= 6) return 0;
  s.ivs[i] = static_cast<std::uint8_t>(
      std::clamp(valor, 0, static_cast<int>(kIvMax)));
  return s.ivs[i];
}

std::uint8_t MetLevelDoMolde(personal::Jogo jogo) {
  const MapaJogo* m = Mapa(jogo);
  if (m == nullptr) return 0;
  std::size_t n = 0;
  const std::uint8_t* bytes = molde::Bytes(m->formato, &n);
  if (bytes == nullptr || n == 0) return 0;

  std::optional<pkm::Pokemon> p;
  switch (m->formato) {
    case pkm::Format::kPB7: p = pb7::Parse(bytes, n); break;
    case pkm::Format::kPK8: p = pk8::Parse(bytes, n); break;
    case pkm::Format::kPB8: p = pb8::Parse(bytes, n); break;
    case pkm::Format::kPA8: p = pa8::Parse(bytes, n); break;
    case pkm::Format::kPK9: p = pk9::Parse(bytes, n); break;
    case pkm::Format::kPK4: p = pk4::Parse(bytes, n); break;
    default: return 0;
  }
  return p ? p->met_level : 0;
}

int GolpesAteNivel(const GeneratorState& s, std::uint8_t nivel,
                   std::uint16_t out[4]) {
  for (int i = 0; i < 4; ++i) out[i] = 0;
  const MapaJogo* m = Mapa(s.jogo);
  if (m == nullptr || !m->tem_learnset) return 0;
  return ls::MovesAtLevel(m->learnset, s.dex, s.form, nivel, out);
}

// --- Verificacao -----------------------------------------------------------

std::vector<Issue> Verify(const GeneratorState& s) {
  std::vector<Issue> out;

  // 1. A especie existe no jogo escolhido? E a primeira pergunta porque, sem
  //    entrada na tabela, nenhuma das outras checagens tem em que se apoiar.
  const personal::EntryFull* pe = PersonalOf(s);
  if (pe == nullptr) {
    out.push_back({"especie_fora_do_jogo",
                   "Esta especie (ou esta forma) nao existe no jogo de origem "
                   "escolhido.",
                   Severity::kErro, Section::kEspecie, ""});
    // Sem personal nao da para checar habilidade, genero nem stats. As
    // checagens que NAO dependem dele seguem abaixo.
  }

  // 2. Habilidade: o slot escolhido existe naquela especie?
  if (pe != nullptr && AbilidadeDoSlot(*pe, s.ability_slot) == 0) {
    out.push_back({"habilidade_invalida",
                   "Esta especie nao tem habilidade nesse slot.",
                   Severity::kErro, Section::kEspecie, "Usar a habilidade 1"});
  }

  // 3. Genero contra a razao da especie.
  if (pe != nullptr && !GeneroPossivel(pe->gender, s.gender)) {
    const std::uint8_t certo = GeneroForcado(pe->gender);
    out.push_back({"genero_impossivel",
                   pe->gender == 255
                       ? "Esta especie nao tem genero."
                       : "Esta especie so pode ter o outro genero.",
                   Severity::kErro, Section::kEspecie,
                   certo == 2 ? "Marcar como sem genero"
                              : (certo == 1 ? "Trocar para femea"
                                            : "Trocar para macho")});
  }

  // 4. Bola: a Beast Ball e da gen 7; um encontro de gen 3 so pode usar as
  //    bolas que existiam na epoca.
  if (GeracaoDaBola(s.ball) > GeracaoDoJogo(s.jogo)) {
    out.push_back({"bola_anacronica",
                   "Esta Pokebola nao existia no jogo de origem escolhido.",
                   Severity::kErro, Section::kEspecie, "Usar Poke Ball"});
  }

  // 5. Nivel de encontro acima do nivel atual: o Pokemon nao pode ter sido
  //    encontrado num nivel que ele ainda nao alcancou.
  if (s.met_level > s.level) {
    out.push_back({"met_level_alto",
                   "O nivel de encontro e maior que o nivel atual.",
                   Severity::kErro, Section::kOrigem,
                   "Igualar ao nivel atual"});
  }

  // 6. Nivel de encontro diferente do que o molde traz. MEDIDO: gerar um
  //    Pikachu de SV com met_level 5 faz o PkHeX acusar "Unable to match an
  //    encounter from origin game"; com o met_level do molde (10), o mesmo
  //    Pokemon sai LEGAL.
  //
  //    A causa e o LOCAL de encontro, que vem do molde e so tem encontro
  //    naquele nivel. Oferecer os outros pares validos exigiria o encounter
  //    DB do PkHeX (fora de escopo). Mas dizer ao jogador qual e o unico par
  //    que conhecemos e honesto e barato — melhor que ele descobrir quando o
  //    Pokemon for recusado.
  if (const std::uint8_t molde_lvl = MetLevelDoMolde(s.jogo)) {
    if (s.met_level != molde_lvl && s.met_level <= s.level) {
      out.push_back(
          {"met_level_fora_do_encontro",
           "O local de encontro deste jogo so tem encontro no nivel " +
               std::to_string(molde_lvl) +
               ". Com outro nivel, um verificador rigoroso recusa o Pokemon.",
           Severity::kAviso, Section::kOrigem,
           "Usar nivel " + std::to_string(molde_lvl)});
    }
  }

  // 6. Golpes. Duas checagens distintas: aprendivel e duplicado.
  const MapaJogo* m = Mapa(s.jogo);
  for (int i = 0; i < 4; ++i) {
    const std::uint16_t mv = s.moves[i];
    if (mv == 0) continue;

    if (m != nullptr && m->tem_learnset &&
        ls::LevelOf(m->learnset, s.dex, s.form, mv) == 0) {
      // NAO aprende por nivel naquele jogo. O learnset do repo so tem os
      // golpes de NIVEL — TM, tutor e ovo nao estao la (TD-01 da spec 065).
      // Por isso isto e AVISO, nao erro: um golpe de TM cairia aqui como
      // falso positivo, e acusar o jogador de algo que nao sabemos checar e
      // pior que deixar passar.
      out.push_back({"golpe_" + std::to_string(i),
                     NomeGolpe(mv) +
                         " nao aparece no aprendizado por nivel deste jogo. "
                         "Pode vir de TM, tutor ou ovo — isto o NestBox nao "
                         "sabe conferir.",
                     Severity::kAviso, Section::kGolpes,
                     "Trocar por um golpe de nivel"});
    }

    for (int k = 0; k < i; ++k) {
      if (s.moves[k] == mv) {
        out.push_back({"golpe_duplicado_" + std::to_string(i),
                       NomeGolpe(mv) + " esta repetido.", Severity::kErro,
                       Section::kGolpes, "Remover a repeticao"});
        break;
      }
    }
  }

  // 7. Nenhum golpe: o jogo nao guarda Pokemon sem golpe nenhum.
  if (s.moves[0] == 0 && s.moves[1] == 0 && s.moves[2] == 0 &&
      s.moves[3] == 0) {
    out.push_back({"sem_golpe", "O Pokemon precisa de pelo menos um golpe.",
                   Severity::kErro, Section::kGolpes,
                   "Preencher pelo aprendizado de nivel"});
  }

  // 8. A regua interna do proprio app (spec 079), sobre o Pokemon montado.
  //    Roda por ultimo porque depende do Build, que e a operacao mais cara.
  if (std::optional<pkm::Pokemon> montado = Build(s)) {
    const legality::LegalityResult r = legality::CheckLegality(*montado);
    for (const legality::LegalityIssue& li : r.issues) {
      out.push_back({"legality_" + li.code, li.reason, Severity::kErro,
                     Section::kEspecie, ""});
    }
  }

  return out;
}

bool ApplyFix(GeneratorState& s, const std::string& code) {
  const personal::EntryFull* pe = PersonalOf(s);

  if (code == "habilidade_invalida") {
    s.ability_slot = 1;
    return true;
  }
  if (code == "genero_impossivel") {
    if (pe == nullptr) return false;
    s.gender = GeneroForcado(pe->gender);
    return true;
  }
  if (code == "bola_anacronica") {
    s.ball = 4;  // Poke Ball existe em toda geracao
    return true;
  }
  if (code == "met_level_alto") {
    s.met_level = s.level;
    return true;
  }
  if (code == "met_level_fora_do_encontro") {
    const std::uint8_t lvl = MetLevelDoMolde(s.jogo);
    if (lvl == 0 || lvl > s.level) return false;
    s.met_level = lvl;
    return true;
  }
  if (code == "sem_golpe" || code.rfind("golpe_", 0) == 0) {
    // Preenche pelo aprendizado de nivel do jogo escolhido — a mesma fonte
    // que `AplicaEntradaNoDestino` usa ao resetar moveset na chegada, entao
    // o resultado e coerente com o que o app ja faz.
    const MapaJogo* m = Mapa(s.jogo);
    if (m == nullptr || !m->tem_learnset) return false;
    std::uint16_t novos[4] = {0, 0, 0, 0};
    const int n = ls::MovesAtLevel(m->learnset, s.dex, s.form, s.level, novos);
    if (n <= 0) return false;
    for (int i = 0; i < 4; ++i) s.moves[i] = novos[i];
    return true;
  }
  // `especie_fora_do_jogo` nao tem conserto automatico: trocar a especie ou o
  // jogo por conta propria seria decidir pelo jogador qual dos dois ele quis.
  return false;
}

// --- Montagem --------------------------------------------------------------

std::optional<pkm::Pokemon> Build(const GeneratorState& s) {
  const MapaJogo* m = Mapa(s.jogo);
  if (m == nullptr) return std::nullopt;

  const personal::EntryFull* pe = PersonalOf(s);
  if (pe == nullptr) return std::nullopt;  // especie fora do jogo

  // TD-04: parte do MOLDE, nunca de zeros.
  std::size_t n = 0;
  const std::uint8_t* molde = molde::Bytes(m->formato, &n);
  if (molde == nullptr || n == 0) return std::nullopt;

  std::optional<pkm::Pokemon> p;
  switch (m->formato) {
    case pkm::Format::kPB7: p = pb7::Parse(molde, n); break;
    case pkm::Format::kPK8: p = pk8::Parse(molde, n); break;
    case pkm::Format::kPB8: p = pb8::Parse(molde, n); break;
    case pkm::Format::kPA8: p = pa8::Parse(molde, n); break;
    case pkm::Format::kPK9: p = pk9::Parse(molde, n); break;
    case pkm::Format::kPK4: p = pk4::Parse(molde, n); break;
    default: return std::nullopt;
  }
  if (!p) return std::nullopt;

  // --- Identidade
  // `species` NAO e sempre National Dex: no PK9 e indice interno do gen9.
  // Copiar o campo direto troca a especie em silencio (armadilha 1 do
  // pkm_convert.h) — por isso a conversao passa pelo helper.
  const std::uint16_t alvo = pkm::SpeciesForFormat(s.dex, m->formato);
  if (alvo == 0) return std::nullopt;
  p->species = alvo;
  p->form = s.form;
  p->gender = s.gender;
  p->nature = s.nature;
  p->stat_nature = s.nature;
  p->ability_number = s.ability_slot;
  p->ability = AbilidadeDoSlot(*pe, s.ability_slot);
  p->held_item = s.held_item;
  p->ball = s.ball;
  p->language = s.language;

  // --- Nivel. NENHUM formato guarda o nivel: todos guardam EXPERIENCIA, e o
  // nivel exibido no jogo deriva dela pela curva da especie. Gravar a exp
  // errada faz o Pokemon nascer com outro nivel na tela, sem erro nenhum no
  // caminho (achado A1 da spec 144).
  p->exp = g3::ExpForLevel(s.level, pe->growth);

  // --- Treinador e encontro
  p->ot_name = s.ot_name;
  p->tid = s.tid;
  p->sid = s.sid;
  p->met_level = s.met_level;
  p->met_date = {s.met_year, s.met_month, s.met_day};
  p->origin_game = m->origin_game;

  // --- Stats
  for (int i = 0; i < 6; ++i) {
    p->ivs[i] = std::min(s.ivs[i], kIvMax);
    p->evs[i] = std::min(s.evs[i], kEvStatMax);
  }

  // --- Golpes e PP. O PP acompanha o golpe: reescrever um sem o outro troca
  // "Invalid Move" por "Move N PP is above the amount allowed" — a licao que
  // a spec 143 registrou.
  for (int i = 0; i < 4; ++i) {
    p->moves[i] = s.moves[i];
    p->pp[i] = s.moves[i] ? movepp::Modern(static_cast<std::uint8_t>(m->formato),
                                           s.moves[i])
                          : 0;
    p->pp_ups[i] = 0;
  }

  // --- Shiny. Nao ha campo "shiny": ele DERIVA de PID ^ TID ^ SID < 16
  // (pkm_model.h:196). Para atender a escolha da tela sem mexer no par do
  // treinador, o PID e ajustado ate a relacao bater.
  {
    const std::uint32_t alto = (p->pid >> 16) & 0xFFFF;
    const std::uint32_t baixo = p->pid & 0xFFFF;
    const std::uint32_t xt = static_cast<std::uint32_t>(p->tid) ^
                             static_cast<std::uint32_t>(p->sid);
    if (s.shiny) {
      // Escolhe a metade baixa que zera o xor — shiny "quadrado", o caso
      // mais limpo. A metade alta e preservada, entao o PID continua nao
      // trivial.
      p->pid = (alto << 16) | (alto ^ xt);
    } else if (pkm::IsShiny(*p)) {
      // Nao-shiny pedido mas o molde/PID resultou shiny: mexe um bit da
      // metade baixa, o suficiente para sair da faixa de 16.
      p->pid = (alto << 16) | ((baixo ^ 0x0100) & 0xFFFF);
    }
  }

  // O checksum tem de ficar COERENTE com o conteudo, nao zerado.
  //
  // A primeira versao zerava o campo ("o Write recalcula mesmo"), e isso fez
  // `legality::CheckLegality` acusar `checksum` em todo Pokemon recem-montado
  // — o gerador nascia com um erro que ele mesmo criara. O `Write` de fato
  // recalcula na serializacao, mas o verificador roda ANTES disso, sobre o
  // modelo.
  //
  // Serializar e reler e o jeito de obter o valor certo sem duplicar a conta
  // de checksum de cada formato aqui (que divergiria na primeira correcao).
  {
    std::vector<std::uint8_t> bytes;
    switch (m->formato) {
      case pkm::Format::kPB7: bytes = pb7::Write(*p); break;
      case pkm::Format::kPK8: bytes = pk8::Write(*p); break;
      case pkm::Format::kPB8: bytes = pb8::Write(*p); break;
      case pkm::Format::kPA8: bytes = pa8::Write(*p); break;
      case pkm::Format::kPK9: bytes = pk9::Write(*p); break;
      case pkm::Format::kPK4: bytes = pk4::Write(*p); break;
      default: break;
    }
    if (!bytes.empty()) {
      std::optional<pkm::Pokemon> relido;
      switch (m->formato) {
        case pkm::Format::kPB7: relido = pb7::Parse(bytes.data(), bytes.size()); break;
        case pkm::Format::kPK8: relido = pk8::Parse(bytes.data(), bytes.size()); break;
        case pkm::Format::kPB8: relido = pb8::Parse(bytes.data(), bytes.size()); break;
        case pkm::Format::kPA8: relido = pa8::Parse(bytes.data(), bytes.size()); break;
        case pkm::Format::kPK9: relido = pk9::Parse(bytes.data(), bytes.size()); break;
        case pkm::Format::kPK4: relido = pk4::Parse(bytes.data(), bytes.size()); break;
        default: break;
      }
      if (relido) p = relido;
    }
  }

  return p;
}

}  // namespace pokehome::generator
