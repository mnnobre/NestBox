// Gerador de LOTE por geracao (spec 143) — enche um save com Pokemon para o
// ciclo save -> jogo -> save.
//
// PONTO DE ENTRADA UNICO, por decisao do dono (2026-08-19): pode haver
// quantas funcoes auxiliares se quiser, mas TODO Pokemon deste gerador nasce
// em `Gerar()`. Foi ter dois caminhos de escrita que produziu o bug do ovo —
// a correcao entrava so num deles. Aqui o funil e um so: as listas
// (legitimos, erros forcados) apenas descrevem O QUE pedir; quem constroi e
// sempre a mesma funcao.
//
// Uso:
//   make_batch --pla legit  <main>   uma entrada por (especie, forma) do PLA
//   make_batch --pla erros  <main>   Pokemon deliberadamente invalidos
//   make_batch --pla vazio  <main>   limpa as caixas (linha de base)
//
// LIMITE CONHECIDO — leia antes de usar como gate de legalidade:
//
// O PkHeX ainda reprova o lote com `Unable to match an encounter from origin
// game`. Nao e campo faltando: ele exige que o Pokemon corresponda a um
// ENCONTRO REAL do jogo (tabela de spawn, local, nivel e metodo casando entre
// si), e um Pokemon sintetico nao tem de onde tirar isso.
//
// O que o gerador ja resolveu, medido: nickname, ability, genero, ribbon
// afixada, data de encontro e alpha — de 6 motivos para 1.
//
// Consequencia para o teste: o gate NAO pode ser `legal=true` no lote
// sintetico. O que este lote mede e o DIFF entre as tres fases — se um campo
// muda sozinho ao passar pelo jogo, e bug, independente da legalidade. Para
// exigir `legal=true` seria preciso derivar os encontros do PLA, o que e
// outra spec.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "alpha_table.h"
#include "bdsp_personal.h"
#include "pa8.h"
#include "pb7.h"
#include "pb8.h"
#include "pk8.h"
#include "pk9.h"
#include "body_size.h"
#include "commit_plan.h"
#include "generator.h"
#include "lgpe_personal.h"
#include "learnset.h"
#include "move_pp.h"
#include "personal_tables.h"
#include "pkm_convert.h"
#include "pkm_model.h"
#include "pla_personal.h"
#include "save_writer.h"
#include "sv_personal.h"
#include "swsh_personal.h"
#include "species_facts.h"
#include "za_personal.h"

namespace {

namespace body = pokehome::body;
namespace ls = pokehome::learnset;
namespace alpha = pokehome::alpha;
namespace gen = pokehome::generator;
namespace moveset = pokehome::moveset;
namespace per = pokehome::personal;

// De qual jogo e este save? O FORMATO nao basta como discriminador: Z-A e SV
// gravam ambos em kPK9. Quem decide e o save.
per::Jogo JogoDoSave(savew::Game g) {
  switch (g) {
    case savew::Game::kPLA:  return per::Jogo::kLa;
    case savew::Game::kBDSP: return per::Jogo::kBdsp;
    case savew::Game::kZA:   return per::Jogo::kZA;
    case savew::Game::kSV:   return per::Jogo::kSV;
    case savew::Game::kSwSh: return per::Jogo::kSwSh;
    case savew::Game::kLGPE: return per::Jogo::kLgpe;
  }
  return per::Jogo::kCount;
}

moveset::Game JogoMs(per::Jogo j) {
  switch (j) {
    case per::Jogo::kLa:   return moveset::Game::kLegendsArceus;
    case per::Jogo::kBdsp: return moveset::Game::kBdsp;
    case per::Jogo::kZA:   return moveset::Game::kZA;
    case per::Jogo::kSV:   return moveset::Game::kSV;
    case per::Jogo::kSwSh: return moveset::Game::kSwSh;
    case per::Jogo::kLgpe: return moveset::Game::kLgpe;
    default:               return moveset::Game::kSwSh;
  }
}

// O catalogo de (especie, forma) daquele jogo.
//
// Nao e a `personal_tables.h` que o gerador de producao usa: aquela tem
// growth e base stats, mas nao tem `so_em_batalha` nem `nome`, que o lote
// precisa (o primeiro evita 6 diferencas falsas na fase 3; o segundo e o
// apelido). Produzir `so_em_batalha` exige um molde por jogo no PkHeX, e so
// PLA/BDSP/Z-A tem. Enquanto nao houver, o lote fica nestas tres.
struct Catalogo {
  const per::Entry* tab;
  std::size_t n;
};

Catalogo CatalogoDe(per::Jogo jogo) {
  using namespace pokehome::personal;
  switch (jogo) {
    case per::Jogo::kLa:   return {kLa, sizeof(kLa) / sizeof(kLa[0])};
    case per::Jogo::kBdsp: return {kBdsp, sizeof(kBdsp) / sizeof(kBdsp[0])};
    case per::Jogo::kZA:   return {kZa, sizeof(kZa) / sizeof(kZa[0])};
    case per::Jogo::kSwSh: return {kSwSh, sizeof(kSwSh) / sizeof(kSwSh[0])};
    case per::Jogo::kSV:   return {kSV, sizeof(kSV) / sizeof(kSV[0])};
    case per::Jogo::kLgpe: return {kLgpe, sizeof(kLgpe) / sizeof(kLgpe[0])};
    default:               return {nullptr, 0};
  }
}

const per::Entry* AchaNoCatalogo(per::Jogo jogo, std::uint16_t dex,
                                 std::uint8_t form) {
  const Catalogo c = CatalogoDe(jogo);
  for (std::size_t i = 0; i < c.n; ++i)
    if (c.tab[i].dex == dex && c.tab[i].form == form) return &c.tab[i];
  return nullptr;
}

// O que se pede ao gerador. Campos fora do comum ficam explicitos para que a
// lista de ERROS possa torce-los sem uma segunda funcao de construcao.
struct Pedido {
  std::uint16_t dex = 0;
  std::uint8_t form = 0;
  std::uint8_t level = 5;
  bool alpha = false;
  bool shiny = false;
  std::uint16_t move_forcado = 0;  // 0 = escolhe pelo learnset
  std::uint8_t nivel_forcado = 0;  // 0 = usa `level`
  bool pular_moveset = false;      // sem golpe nenhum
};

// >>> O UNICO ponto de entrada. Tudo passa por aqui. <<<
pkm::Pokemon Gerar(const Pedido& p, per::Jogo jogo) {
  // Formato, learnset e origin_game saem do MAPA do gerador de producao
  // (generator.h). Antes eram ternarios `destino == kPA8 ? PLA : BDSP`, o que
  // so funciona com dois jogos: Z-A e SV gravam ambos em kPK9.
  const gen::MapaJogo* mj = gen::Mapa(jogo);
  const pkm::Format destino = mj ? mj->formato : pkm::Format::kNone;
  pkm::Pokemon m;
  m.format = destino;
  // O campo `species` guarda o numero NO FORMATO, que nem sempre e a National
  // Dex: o PK9 usa um indice interno do gen9 (Pawmo e 922 na nacional e 955
  // no binario — gen9_species_id.h). Abaixo da dex 926 os dois coincidem, e
  // por isso PLA e BDSP nunca expuseram isto; do Fidough em diante divergem, e
  // o Z-A mostrava o Pokemon como registro invalido na tela.
  // A funcao e a MESMA que a transferencia usa (pkm_convert.h) — nao ha copia
  // da tabela aqui.
  m.species = pkm::SpeciesForFormat(p.dex, destino);
  m.form = p.form;

  const std::uint8_t lvl = p.nivel_forcado ? p.nivel_forcado : p.level;
  // Exp minima do nivel: LevelFromExp(exp) devolve exatamente `lvl`.
  m.exp = 0;
  for (std::uint32_t e = 0; e < 2000000u; ++e) {
    if (pokehome::species::LevelFromExp(p.dex, e) >= lvl) {
      m.exp = e;
      break;
    }
  }

  // Identidade deterministica na dex: o lote e reproduzivel byte a byte.
  const std::uint32_t seed = 0x9E3779B9u * (p.dex + 1u) + p.form;
  m.pid = seed;
  m.encryption_constant = seed ^ 0x5BF03635u;
  m.tid = 51051;
  m.sid = 6;
  m.ot_name = "Amaral";
  m.language = 2;  // ingles
  m.ot_gender = 0;

  for (int i = 0; i < 6; ++i)
    m.ivs[i] = static_cast<std::uint8_t>((seed >> (i * 5)) & 31);
  m.height_scalar = static_cast<std::uint8_t>(seed & 0xFF);
  m.weight_scalar = static_cast<std::uint8_t>((seed >> 8) & 0xFF);
  m.scale = m.height_scalar;

  if (p.shiny) {
    const std::uint16_t lo = static_cast<std::uint16_t>(m.pid & 0xFFFF);
    m.pid = (static_cast<std::uint32_t>(m.tid ^ m.sid ^ lo) << 16) | lo;
  }

  // Moveset pelo learnset do DESTINO: e o que o jogo espera de quem nasce
  // nele. Golpe invalido se pede por `move_forcado`.
  if (!p.pular_moveset) {
    const ls::Entry* e = nullptr;
    std::size_t n = 0;
    int k = 0;
    if (mj && mj->tem_learnset &&
        ls::Find(mj->learnset, p.dex, p.form, &e, &n)) {
      for (std::size_t i = 0; i < n && k < 4; ++i) {
        if (e[i].level == 0 || e[i].level > lvl) continue;
        m.moves[k++] = e[i].move;
      }
    }
    if (p.move_forcado) m.moves[0] = p.move_forcado;
    for (int i = 0; i < 4; ++i)
      m.pp[i] = m.moves[i]
                    ? pokehome::movepp::Modern(
                          static_cast<std::uint8_t>(destino), m.moves[i])
                    : 0;
  }

  // Habilidade e genero saem da tabela do PROPRIO jogo (pla_personal.h,
  // gerada por tools/pkhex-personal). Sem isso o verificador reprova com
  // "Ability is not valid for species/form" e "Genderless Pokemon should not
  // have a gender" — o projeto so PRESERVA esses campos, nunca os criou.
  const per::Entry* pi = AchaNoCatalogo(jogo, p.dex, p.form);
  if (pi) {
    m.ability = pi->ability1;
    m.ability_number = 1;
    // razao do PkHeX: 255 = sem genero, 254 = so femea, 0 = so macho.
    m.gender = pi->gender == 255 ? 2 : (pi->gender == 254 ? 1 : 0);
    m.nickname = pi->nome;
  }

  // O apelido vazio e reprovado ("Nickname is empty"): quem nao foi apelidado
  // carrega o NOME DA ESPECIE no campo, nao uma string vazia.
  m.is_nicknamed = false;

  if (mj) m.origin_game = mj->origin_game;
  m.ball = 4;  // Poke Ball
  if (destino == pkm::Format::kPA8) {
    m.is_alpha = p.alpha;
    m.met_location = 30012;  // encontro generico do PLA
  }
  m.met_level = lvl;
  m.current_handler = 0;
  // Data de encontro: zerada e "Met Date is not a valid calendar date". O
  // formato guarda ano-2000, mes, dia.
  m.met_date = {26, 8, 19};  // 2026-08-19
  m.affixed_ribbon = 0xFF;      // 0xFF = nenhuma; 0 seria "Kalos Champion"

  // Altura/peso: MESMA funcao do caminho de producao. Se ela mudar, o lote
  // muda junto — nao ha copia da formula aqui.
  // So PB7 (LGPE) e PA8 (PLA) gravam altura/peso ABSOLUTO no binario. O PK9
  // — SV e Z-A — guarda apenas os scalars crus (`pk9.cpp:280`), entao nao ha
  // absoluto a calcular nem tabela de body_size a consultar.
  if (destino == pkm::Format::kPA8 || destino == pkm::Format::kPB7) {
    const bool pla = destino == pkm::Format::kPA8;
    const body::Entry* tab = pla ? body::kLa : body::kGg;
    const std::size_t nt = pla ? sizeof(body::kLa) / sizeof(body::kLa[0])
                               : sizeof(body::kGg) / sizeof(body::kGg[0]);
    std::uint16_t bh = 0, bw = 0;
    if (body::Lookup(tab, nt, p.dex, p.form, &bh, &bw) && bh && bw) {
      body::Absolutes(pla ? body::kRatioPla : body::kRatioLgpe, bh, bw,
                      m.height_scalar, m.weight_scalar, &m.height_absolute,
                      &m.weight_absolute);
    }
  }

  // A MESMA entrada que a transferencia usa (spec 145). Sem isto o lote nao
  // recebia as plus flags do Z-A nem o obedience_level, e o jogo desenhava o
  // Pokemon como registro invalido — com a suite inteira verde, porque o
  // diff A->C compara o save consigo mesmo.
  //
  // `memory = nullptr`: o lote NASCE no jogo de destino, entao nao ha moveset
  // anterior a restaurar. O moveset ja veio do learnset certo, acima.
  pokehome::commit::SaveInfo si;
  si.kind = pokehome::commit::SaveKind::kModerno;
  si.formato = destino;
  si.jogo_ms = JogoMs(jogo);
  si.trainer_name = m.ot_name;
  pokehome::commit::AplicaEntradaNoDestino(m, si, nullptr);
  return m;
}

// --- As listas: descrevem O QUE pedir, nunca COMO construir ---------------

std::vector<Pedido> ListaLegit(per::Jogo jogo) {
  // Uma entrada por (especie, forma) do PLA — mas `body_size.h:kLa` NAO e a
  // lista canonica de formas guardaveis, como parecia. O ciclo de lote
  // mostrou o jogo reescrevendo a forma de 6 Pokemon, por duas razoes:
  //
  //   FORMA DE BATALHA (Cherrim Sunshine): estado temporario, o jogo o
  //     desfaz ao carregar. E a regra do CLAUDE.md — "estado de batalha
  //     nunca e armazenado". Hoje `pla_personal.h` marca com
  //     `so_em_batalha`, perguntado ao verificador do PkHeX.
  //
  //   FORMA 0 AUSENTE (Arcanine, Electrode, Lilligant, Avalugg): em Hisui a
  //     forma de Kanto/Unova nao existe, e a tabela comeca na 1. Pedir a
  //     forma 2 fazia o jogo normalizar para 1. Basta ficar na MENOR forma
  //     presente por especie.
  //
  // Sem os dois filtros o teste acusa 6 diferencas que sao comportamento
  // legitimo do jogo — ruido que esconderia um bug de verdade.
  std::vector<Pedido> v;
  const Catalogo c = CatalogoDe(jogo);
  for (std::size_t i = 0; i < c.n; ++i) {
    const std::uint16_t dex = c.tab[i].dex;
    const std::uint8_t form = c.tab[i].form;

    if (c.tab[i].so_em_batalha) continue;

    // a menor forma presente desta especie e a que o jogo mantem
    bool ha_menor = false;
    for (std::size_t j = 0; j < c.n; ++j)
      if (c.tab[j].dex == dex && c.tab[j].form < form) {
        ha_menor = true;
        break;
      }
    if (ha_menor) continue;

    Pedido p;
    p.dex = dex;
    p.form = form;
    p.level = 20;
    // ALPHA fica desligado no lote GERAL, e a tabela de especies nao resolve.
    //
    // Medido em 2026-08-20: `alpha_table.h` diz quais especies tem variante
    // alpha (274 pares, do PkHeX), e ligar a flag nelas produziu 224 de 242
    // com "Alpha Flag mismatch". O verificador nao pergunta se a ESPECIE tem
    // variante — ele exige que o ENCONTRO daquele Pokemon seja alpha, com
    // nivel, local e metodo casando. Um lote sintetico nao tem isso.
    //
    // A mecanica alpha precisa de um lote proprio, gerado a partir de
    // encontros reais (`EncounterMovesetGenerator`), nao de um flag ligado
    // por cima. Ver `--mecanicas`.
    p.alpha = false;
    p.shiny = (i % 11 == 0);
    v.push_back(p);
  }
  return v;
}

// As especies do PLA que NAO existem em nenhum save de origem do dono
// (spec 143). O teto da matriz de rotas era 119 de 242 — nao por limite do
// app, mas porque os saves nao tinham as outras. Gerar aqui, no formato de
// ORIGEM, e transferir exercita a conversao de verdade: nao e o mesmo que
// nascer no destino.
std::vector<Pedido> ListaFaltantes(const std::vector<std::uint32_t>& ja_tem,
                                   per::Jogo jogo) {
  std::vector<Pedido> v;
  const Catalogo c = CatalogoDe(jogo);
  const std::size_t n = c.n;
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint16_t dex = c.tab[i].dex;
    const std::uint8_t form = c.tab[i].form;

    // O filtro e por (especie, FORMA), nao so por especie. Alakazam,
    // Qwilfish, Goodra e Avalugg existiam nos saves do dono — mas na forma de
    // Galar/Kalos, que o PLA nao aceita. Filtrar so pela dex os excluia da
    // geracao e deixava 10 buracos na cobertura.
    const std::uint32_t chave = (static_cast<std::uint32_t>(dex) << 8) | form;
    bool tem = false;
    for (const auto d : ja_tem)
      if (d == chave) { tem = true; break; }
    if (tem) continue;

    // Aqui NAO se filtra pela menor forma, ao contrario da ListaLegit: o
    // objetivo e justamente cobrir TODAS as formas que o jogo aceita.
    //
    // E nao sao "cosmeticas": Rotom 1-5 e Arceus 1-18 mudam o TIPO, Wormadam
    // muda tipo, Vulpix/Sneasel forma 1 sao as variantes de Hisui, e
    // Tornadus/Thundurus/Landorus/Shaymin/Enamorus tem forma alternativa com
    // stats proprios. Sao Pokemon que o usuario TEM — de evento, de item, de
    // captura — e que quebrariam por serem raros.
    //
    // A unica exclusao continua sendo a forma de BATALHA, que o jogo desfaz
    // ao carregar (Cherrim Sunshine).
    if (c.tab[i].so_em_batalha) continue;

    Pedido p;
    p.dex = dex;
    p.form = form;
    p.level = 20;
    v.push_back(p);
  }
  return v;
}

std::vector<Pedido> ListaErros() {
  // Cada linha e uma PERGUNTA ao jogo, nao uma expectativa. O produto e o
  // mapa de tolera / normaliza / rejeita.
  std::vector<Pedido> v;
  Pedido p;
  p.level = 20;

  p.dex = 403; p.form = 0; p.move_forcado = 903;   v.push_back(p);  // golpe gen9
  p = Pedido{}; p.level = 20;
  p.dex = 403; p.pular_moveset = true;             v.push_back(p);  // sem golpe
  p = Pedido{}; p.level = 20;
  p.dex = 403; p.nivel_forcado = 100;              v.push_back(p);  // nivel teto
  p = Pedido{}; p.level = 20;
  p.dex = 403; p.alpha = true;                     v.push_back(p);  // alpha indevido
  p = Pedido{}; p.level = 20;
  p.dex = 1;                                       v.push_back(p);  // fora do PLA
  p = Pedido{}; p.level = 20;
  p.dex = 905;                                     v.push_back(p);  // dex alta
  p = Pedido{}; p.level = 20;
  p.dex = 403; p.form = 9;                         v.push_back(p);  // forma impossivel
  // [7] O caso que fecha a brecha do tracker (spec 143): golpe de gen9 num
  // Pokemon que entra pelo ramo de MESMO formato. Antes da correcao ele
  // pulava `AplicaEntradaNoDestino` por nao ter tracker, e o golpe chegava
  // ao save — matando o jogo. Depois da correcao o moveset e reescrito.
  p = Pedido{}; p.level = 20;
  p.dex = 403; p.move_forcado = 903;               v.push_back(p);
  return v;
}

enum class Qual { kLegit, kErros, kVazio, kFaltantes, kArquivo };

int Escrever(const char* path, Qual qual, int so,
             const std::vector<std::uint32_t>& ja_tem,
             const char* bin = nullptr, std::size_t desde = 0) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "nao abriu %s\n", path);
    return 1;
  }
  std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
  in.close();

  auto sd = savew::Load(buf);
  if (!sd) {
    std::fprintf(stderr, "save nao reconhecido: %s\n", path);
    return 1;
  }

  // O LGPE nao tem lote sintetico: o Let's Go exige que met_location e
  // met_level casem com um encontro REAL, e cada especie tem o seu (medido na
  // spec 147: Bulbasaur met=31 lv12, Pikachu met=28 lv5, Mewtwo met=46 lv70).
  // Nao ha numero generico como o 30012 do PLA — montar sintetico aqui
  // produzia 153 de 153 ilegais com "Unable to match an encounter", e o jogo
  // desenhava OVO no lugar do Pokemon.
  //
  // Falhar e deliberado. A alternativa — gravar assim mesmo — ja aconteceu, e
  // o lote invalido passou despercebido porque o diff A->C compara o save
  // consigo mesmo e nao acusa nada.
  if (sd->game == savew::Game::kLGPE && qual == Qual::kLegit) {
    std::fprintf(stderr,
                 "LGPE nao aceita lote sintetico (spec 147). Gere por "
                 "encontro real:\n"
                 "  cd tools/pkhex-lote && dotnet run -- pb7  <lote.bin>  "
                 "# Pikachu\n"
                 "  cd tools/pkhex-lote && dotnet run -- pb7e <lote.bin>  "
                 "# Eevee\n"
                 "  make_batch --lgpe arquivo <save> <lote.bin>\n");
    return 2;
  }

  const per::Jogo jogo = JogoDoSave(sd->game);
  if (CatalogoDe(jogo).n == 0) {
    std::fprintf(stderr, "sem catalogo para o jogo deste save (%s)\n",
                 savew::GameName(sd->game));
    return 1;
  }

  // A lista so pode ser montada AGORA: ela depende do catalogo do jogo de
  // destino, que sai do proprio save.
  const bool limpar = qual == Qual::kVazio;
  std::vector<Pedido> lista;
  switch (qual) {
    case Qual::kLegit: lista = ListaLegit(jogo); break;
    case Qual::kErros:
      lista = ListaErros();
      if (so >= 0) {
        if (static_cast<std::size_t>(so) >= lista.size()) {
          std::fprintf(stderr, "caso %d nao existe (a lista tem %zu)\n", so,
                       lista.size());
          return 2;
        }
        lista = {lista[static_cast<std::size_t>(so)]};
      }
      break;
    case Qual::kFaltantes: lista = ListaFaltantes(ja_tem, jogo); break;
    case Qual::kVazio: break;
    case Qual::kArquivo: break;  // a lista vem do .bin, nao do gerador
  }

  // MODO ARQUIVO: os registros vem prontos de `tools/pkhex-lote`, que os
  // gerou a partir de ENCONTROS REAIS do PkHeX. E a unica forma de ter um
  // lote que o verificador aprova: um Pokemon sintetico sempre reprova com
  // "Unable to match an encounter from origin game", e por isso as mecanicas
  // (alpha, gmax) nunca puderam ser medidas — ligar a flag por cima da erro
  // pior ("Alpha Flag mismatch" em 224 de 242, medido em 2026-08-20).
  std::vector<pkm::Pokemon> doArquivo;
  if (qual == Qual::kArquivo) {
    std::ifstream b(bin ? bin : "", std::ios::binary);
    if (!b) {
      std::fprintf(stderr, "nao abriu o lote: %s\n", bin ? bin : "(vazio)");
      return 1;
    }
    const std::vector<std::uint8_t> raw(
        (std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
    // O tamanho do registro sai do JOGO do save; o `tools/pkhex-lote` grava
    // exatamente esse formato, entao um resto na divisao significa lote do
    // jogo errado — vale abortar em vez de escrever lixo nas caixas.
    // O tamanho e o do registro DE CAIXA, que o PkHeX exporta em
    // `DecryptedBoxData` — menor que o de party, que carrega os stats
    // calculados. Confundir os dois faz a divisao dar resto e o lote inteiro
    // ser recusado (aconteceu: 328 vs 344 no PK8/PK9).
    std::size_t tam = 0;
    switch (sd->game) {
      case savew::Game::kSwSh:
      case savew::Game::kSV:
      case savew::Game::kZA:
      case savew::Game::kBDSP: tam = 328; break;
      case savew::Game::kPLA:  tam = 360; break;
      case savew::Game::kLGPE: tam = 260; break;
      default: break;
    }
    if (tam == 0 || raw.size() % tam != 0) {
      std::fprintf(stderr,
                   "lote de %zu bytes nao divide por %zu (formato do save)\n",
                   raw.size(), tam);
      return 1;
    }
    for (std::size_t o = 0; o + tam <= raw.size(); o += tam) {
      std::optional<pkm::Pokemon> m;
      switch (sd->game) {
        case savew::Game::kSwSh: m = pk8::Parse(raw.data() + o, tam); break;
        case savew::Game::kSV:
        case savew::Game::kZA:   m = pk9::Parse(raw.data() + o, tam); break;
        case savew::Game::kPLA:  m = pa8::Parse(raw.data() + o, tam); break;
        case savew::Game::kBDSP: m = pb8::Parse(raw.data() + o, tam); break;
        case savew::Game::kLGPE: m = pb7::Parse(raw.data() + o, tam); break;
        default: break;
      }
      if (m) doArquivo.push_back(*m);
    }
    std::printf("lote lido: %zu registros de %zu bytes\n", doArquivo.size(),
                tam);
  }

  std::size_t escritos = 0, pulados = 0;
  const std::size_t vagas = sd->box_count * sd->slots_per_box;
  // `desde`: o lote comeca DEPOIS dos Pokemon que ja estao no save. O
  // `make_batch` foi feito para save descartavel e grava a partir do slot 0 —
  // num save REAL isso apaga o que estava la. Aconteceu: o Pikachu parceiro
  // do dono (forma 8, IVs 31) foi sobrescrito pelo Bulbasaur do lote.
  for (std::size_t i = desde; i < vagas; ++i) {
    const std::size_t b = i / sd->slots_per_box, s = i % sd->slots_per_box;
    const std::size_t j = i - desde;   // indice DENTRO do lote
    if (limpar) {
      sd->Set(b, s, pkm::Pokemon{});
      continue;
    }
    if (qual == Qual::kArquivo) {
      if (j >= doArquivo.size()) break;
      if (!sd->Set(b, s, doArquivo[j])) { ++pulados; continue; }
      ++escritos;
      continue;
    }
    if (j >= lista.size()) break;
    const pkm::Pokemon m = Gerar(lista[j], jogo);
    if (!sd->Set(b, s, m)) {
      ++pulados;
      continue;
    }
    ++escritos;
  }

  const std::vector<std::uint8_t> out = savew::Save(*sd);
  std::ofstream o(path, std::ios::binary);
  o.write(reinterpret_cast<const char*>(out.data()),
          static_cast<std::streamsize>(out.size()));
  if (!o) {
    std::fprintf(stderr, "escrita incompleta\n");
    return 1;
  }

  if (limpar)
    std::printf("caixas limpas: %zu vagas\n", vagas);
  else
    std::printf("lote gravado: %zu escritos, %zu pulados, %zu vagas\n",
                escritos, pulados, vagas);
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::fprintf(
        stderr, "uso: make_batch --pla <legit|erros|vazio> <caminho do save>\n");
    return 2;
  }
  const std::string modo = argv[2];
  const char* path = argv[3];
  // `--so N`: grava so o caso N da lista de erros. Existe para BISSECAO — o
  // lote matou o jogo sem excecao no log, e a unica forma de saber qual caso
  // e responsavel e um por vez.
  int so = -1;
  // `--desde N`: o lote comeca no slot N, preservando o que ja esta no save.
  // Obrigatorio ao escrever num save REAL do dono — sem isto o lote grava a
  // partir do slot 0 e apaga o que estava la. Aconteceu: o Pikachu parceiro
  // do dono (forma 8, IVs 31) foi sobrescrito pelo Bulbasaur do lote.
  std::size_t desde = 0;
  for (int i = 4; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--so") == 0) so = std::atoi(argv[i + 1]);
    if (std::strcmp(argv[i], "--desde") == 0)
      desde = static_cast<std::size_t>(std::atoi(argv[i + 1]));
  }

  // `faltantes`: cada argumento extra e (dex << 8 | forma) do que JA existe.
  std::vector<std::uint32_t> ja_tem;
  if (modo == "faltantes")
    for (int i = 4; i < argc; ++i)
      ja_tem.push_back(static_cast<std::uint32_t>(std::atoi(argv[i])));

  if (modo == "legit") return Escrever(path, Qual::kLegit, so, ja_tem);
  if (modo == "erros") return Escrever(path, Qual::kErros, so, ja_tem);
  if (modo == "vazio") return Escrever(path, Qual::kVazio, so, ja_tem);
  if (modo == "faltantes") return Escrever(path, Qual::kFaltantes, so, ja_tem);
  // `arquivo <lote.bin>`: grava os registros ja prontos de tools/pkhex-lote.
  if (modo == "arquivo") {
    if (argc < 5) {
      std::fprintf(stderr,
                   "uso: make_batch --<jogo> arquivo <save> <lote.bin> "
                   "[--desde N]\n");
      return 2;
    }
    return Escrever(path, Qual::kArquivo, so, ja_tem, argv[4], desde);
  }
  std::fprintf(stderr, "modo desconhecido: %s\n", modo.c_str());
  return 2;
}
