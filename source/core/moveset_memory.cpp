#include "moveset_memory.h"

#include "move_pp.h"      // PP base ao restaurar (spec 125)
#include "pkm_convert.h"  // NationalDex: o learnset e indexado pela dex nacional
#include "sha256.h"

namespace pokehome::moveset {
namespace {

void PushU16(std::vector<std::uint8_t>& v, std::uint16_t x) {
  v.push_back(static_cast<std::uint8_t>(x & 0xFF));
  v.push_back(static_cast<std::uint8_t>(x >> 8));
}

void PushU64(std::vector<std::uint8_t>& v, std::uint64_t x) {
  for (int i = 0; i < 8; ++i) {
    v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF));
  }
}

std::uint64_t ReadU64(const std::uint8_t* p) {
  std::uint64_t x = 0;
  for (int i = 0; i < 8; ++i) {
    x |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  }
  return x;
}

std::uint16_t ReadU16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

// Alimenta o hash com um inteiro em little-endian, largura fixa. Largura fixa
// importa: sem ela, campos vizinhos poderiam se confundir na fronteira.
template <typename T>
void FeedInt(sha256::Context& ctx, T value) {
  std::uint8_t buf[sizeof(T)];
  auto x = static_cast<std::uint64_t>(value);
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    buf[i] = static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF);
  }
  ctx.Update(buf, sizeof(T));
}

}  // namespace

// --- G10 -------------------------------------------------------------------

std::uint64_t DeriveTracker(const pkm::Pokemon& p) {
  sha256::Context ctx;

  // Rotulo de dominio: impede que este hash colida com outro uso de SHA-256 no
  // projeto se um dia alimentarem os mesmos bytes.
  const char kLabel[] = "NestBox HOME tracker v1";
  ctx.Update(reinterpret_cast<const std::uint8_t*>(kLabel), sizeof(kLabel) - 1);

  // So campos IMUTAVEIS. Nada que mude com nivel, golpe, item, apelido ou
  // handling trainer — senao o tracker deixaria de ser estavel, que e
  // exatamente o que ele precisa ser.
  FeedInt<std::uint32_t>(ctx, p.encryption_constant);
  FeedInt<std::uint32_t>(ctx, p.pid);
  FeedInt<std::uint16_t>(ctx, p.species);
  FeedInt<std::uint8_t>(ctx, p.form);
  FeedInt<std::uint16_t>(ctx, p.tid);
  FeedInt<std::uint16_t>(ctx, p.sid);
  FeedInt<std::uint8_t>(ctx, p.ot_gender);
  FeedInt<std::uint8_t>(ctx, p.language);
  FeedInt<std::uint16_t>(ctx, p.met_location);
  FeedInt<std::uint16_t>(ctx, p.egg_location);
  FeedInt<std::uint8_t>(ctx, p.ball);
  FeedInt<std::uint8_t>(ctx, p.origin_game);
  for (auto b : p.met_date) FeedInt<std::uint8_t>(ctx, b);
  for (auto b : p.egg_date) FeedInt<std::uint8_t>(ctx, b);
  for (auto b : p.ivs) FeedInt<std::uint8_t>(ctx, b);
  // O nome do OT entra com o tamanho na frente: sem isso "AB"+"C" e "A"+"BC"
  // dariam o mesmo hash.
  FeedInt<std::uint16_t>(ctx, static_cast<std::uint16_t>(p.ot_name.size()));
  ctx.Update(reinterpret_cast<const std::uint8_t*>(p.ot_name.data()),
             p.ot_name.size());

  const sha256::Digest d = ctx.Finish();
  std::uint64_t t = ReadU64(d.data());
  // Zero significa "sem tracker", entao o valor nunca pode ser zero. Forcar o
  // bit alto garante isso sem enviesar os outros 63 bits.
  t |= 0x8000000000000000ULL;
  return t;
}

bool AssignTracker(pkm::Pokemon& p) {
  // A IMUTABILIDADE mora nesta linha. Tracker existente e do servidor da
  // Nintendo (ou de uma atribuicao nossa anterior) e nunca e sobrescrito.
  if (p.home_tracker != 0) return false;
  if (p.empty()) return false;
  p.home_tracker = DeriveTracker(p);
  return true;
}

// --- Regras da NestBox (spec 125) ------------------------------------------

Game OriginBucket(const pkm::Pokemon& p) {
  // Codigos de origem (GameVersion do PkHeX): gen3 1..5 e 15 (Colo/XD);
  // LGPE 42/43; SwSh 44/45; PLA 47; BDSP 48/49; SV 50/51; Z-A 52 (conferido
  // em save real — memoria za-origin-code-52).
  // ANTES do codigo de versao: a descida para o SwSh reescreve origin_game
  // para SW/SH e codifica a origem REAL no met_location (pkm_convert, medido
  // no EntityConverter — o PK8 nao representa origem em jogo posterior). Sem
  // esta leitura, um Pokemon do PLA dentro do SwSh parece NATIVO: o handler
  // nao dispara ("Current handler cannot be the OT" em 330 registros) e a
  // memoria de moveset o arquiva no bucket errado.
  switch (p.met_location) {
    case 60000: return Game::kLegendsArceus;          // PLA
    case 59999: case 59998: return Game::kBdsp;       // BD / SP
    case 59997: case 59996: return Game::kSV;         // SL / VL
    default: break;
  }

  const std::uint8_t o = p.origin_game;
  if ((o >= 1 && o <= 5) || o == 15) return Game::kGen3;
  if (o == 42 || o == 43) return Game::kLgpe;
  if (o == 44 || o == 45) return Game::kSwSh;
  if (o == 47) return Game::kLegendsArceus;
  if (o == 48 || o == 49) return Game::kBdsp;
  if (o == 50 || o == 51) return Game::kSV;
  if (o == 52) return Game::kZA;
  // Origem desconhecida: o formato e o palpite menos danoso.
  switch (p.format) {
    case pkm::Format::kPB7: return Game::kLgpe;
    case pkm::Format::kPK8: return Game::kSwSh;
    case pkm::Format::kPB8: return Game::kBdsp;
    case pkm::Format::kPA8: return Game::kLegendsArceus;
    default: return Game::kSV;
  }
}

bool RestoreOnBank(pkm::Pokemon& p, Memory& m, Game src) {
  if (p.home_tracker == 0 || p.empty()) return false;
  const Game origem = OriginBucket(p);
  if (origem == src) return false;  // nativo: moveset ja e "dele"

  const Snapshot* s = m.Recall(p.home_tracker, origem);
  if (!s) return false;

  // IDENTIDADE DE ORIGEM (spec 145) — ANTES do atalho de moveset abaixo.
  //
  // O SwSh reescreve origin_game/met/egg/ball ao receber (commit_plan), e o
  // registro que volta dele carrega o codigo do HOME (59996-60000) no lugar
  // do local verdadeiro. Aqui e a volta: o bucket de origem tem o original
  // guardado, e este e o ponto onde ele se restaura.
  //
  // Fica fora do `if (s->moves == p.moves) return false` de proposito: o
  // moveset pode estar igual (nada a restaurar nele) e a identidade ainda
  // precisar voltar. Guardar `mexeu` para nao devolver false quando o unico
  // trabalho feito foi este.
  bool mexeu = false;
  if (s->origin_game != 0 && p.met_location >= 59996 &&
      p.met_location <= 60000) {
    p.origin_game = s->origin_game;
    p.met_location = s->met_location;
    p.egg_location = s->egg_location;
    if (s->ball != 0) p.ball = s->ball;
    mexeu = true;
  }

  if (s->moves == p.moves) return mexeu;  // ja restaurado (mover na box)

  // Memoriza o moveset do jogo de onde saiu ANTES de sobrescrever — e o que
  // a proxima ida aquele jogo restaura.
  m.Remember(p, src);

  p.moves = s->moves;
  p.pp_ups = s->pp_ups;
  for (int i = 0; i < 4; ++i) {
    p.pp[i] = p.moves[i]
                  ? pokehome::movepp::Modern(
                        static_cast<std::uint8_t>(p.format), p.moves[i])
                  : 0;
  }

  // Mecanicas de volta (spec 139, L4). So restaura o que FOI guardado: zero
  // significa "nunca houve", e sobrescrever com zero apagaria o que o
  // Pokemon tem agora. E o que faz um tera sobreviver a uma ida ao SwSh.
  //
  // Quem decide se o campo APARECE e o `Caps` do pkm_convert, no formato de
  // destino — a memoria so garante que o dado nao se perdeu no caminho.
  if (s->tera_type != 0) p.tera_type_original = s->tera_type;
  if (s->dynamax_level != 0) p.dynamax_level = s->dynamax_level;
  if (s->gmax) p.can_gigantamax = true;
  if (s->alpha) p.is_alpha = true;
  for (int i = 0; i < 6; ++i) {
    if (s->effort_levels[i] != 0) p.effort_levels[i] = s->effort_levels[i];
  }
  return true;
}

// --- G12 -------------------------------------------------------------------

int ResetMovesByLevel(pkm::Pokemon& p, Game game, std::uint8_t level) {
  std::uint16_t out[4] = {0, 0, 0, 0};
  // Pela dex NACIONAL (spec 109): o learnset e indexado por ela, e no pk9 o
  // `p.species` cru e o indice interno do gen9 — consultar com ele erraria a
  // especie em 106 casos, o mesmo bug da spec 076/069.
  const int n = learnset::MovesAtLevel(ToLearnsetGame(game),
                                       pkm::NationalDex(p), p.form,
                                       level, out);
  if (n == 0) return 0;
  for (int i = 0; i < 4; ++i) {
    p.moves[i] = out[i];
    p.pp_ups[i] = 0;  // golpe recem-aprendido nunca vem com PP-up
  }
  return n;
}

// --- G11 -------------------------------------------------------------------

void Memory::Remember(const pkm::Pokemon& p, Game game) {
  if (p.home_tracker == 0) return;  // sem indice nao ha memoria
  Snapshot s;
  s.moves = p.moves;
  s.pp_ups = p.pp_ups;
  // Mecanicas do jogo de onde ele esta saindo (spec 139). O destino pode nao
  // ter o campo — e por isso que ele fica guardado aqui.
  s.tera_type = p.tera_type_original;
  s.dynamax_level = p.dynamax_level;
  s.gmax = p.can_gigantamax;
  s.alpha = p.is_alpha;
  s.effort_levels = p.effort_levels;

  // IDENTIDADE DE ORIGEM (spec 145): so no bucket do jogo de ONDE ele veio.
  // Ela e do Pokemon, nao do jogo — grava-la em toda entrada duplicaria o
  // dado em N lugares que poderiam divergir. `RestoreOnBank` a le do bucket
  // de origem, que e o unico com autoridade sobre ela.
  //
  // A condicao e `OriginBucket(p) == game`: guardamos quando o Pokemon sai do
  // jogo que o criou, que e o unico momento em que os campos ainda sao os
  // verdadeiros. Depois de entrar no SwSh eles ja estao reescritos.
  // O met 59996-60000 e a MARCA de que ja foi reescrito: guardar dali seria
  // gravar o codigo do HOME como se fosse o local verdadeiro, e a restauracao
  // devolveria lixo. So o registro ainda intacto tem autoridade.
  const bool ja_reescrito =
      p.met_location >= 59996 && p.met_location <= 60000;
  if (!ja_reescrito && OriginBucket(p) == game) {
    s.origin_game = p.origin_game;
    s.met_location = p.met_location;
    s.egg_location = p.egg_location;
    s.ball = p.ball;
  }

  for (auto& r : records_) {
    if (r.tracker == p.home_tracker && r.game == game) {
      r.snapshot = s;  // o ULTIMO moveset naquele jogo, nao o primeiro
      return;
    }
  }
  records_.push_back(Record{p.home_tracker, game, s});
}

const Snapshot* Memory::Recall(std::uint64_t tracker, Game game) const {
  if (tracker == 0) return nullptr;
  for (const auto& r : records_) {
    if (r.tracker == tracker && r.game == game) return &r.snapshot;
  }
  return nullptr;
}

bool Memory::ApplyOnEntry(pkm::Pokemon& p, Game game,
                          std::uint8_t level) const {
  if (const Snapshot* s = Recall(p.home_tracker, game)) {
    p.moves = s->moves;
    p.pp_ups = s->pp_ups;
    return true;
  }
  ResetMovesByLevel(p, game, level);
  return false;
}

// --- Serializacao ----------------------------------------------------------

std::vector<std::uint8_t> Encode(const Memory& m) {
  std::vector<std::uint8_t> out;
  const auto& recs = m.records();
  const auto n = static_cast<std::uint32_t>(recs.size());
  out.reserve(4 + recs.size() * kEntryBytes);
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>((n >> (8 * i)) & 0xFF));
  }
  for (const auto& r : recs) {
    PushU64(out, r.tracker);                                    // 0..7
    out.push_back(static_cast<std::uint8_t>(r.game));           // 8
    // Byte 9: flags de 1 bit. Era a "reserva" que a v1 deixou — e o lugar
    // que ela abriu de proposito para isto (spec 139).
    std::uint8_t flags = 0;
    if (r.snapshot.gmax) flags |= 0x01;
    if (r.snapshot.alpha) flags |= 0x02;
    out.push_back(flags);                                       // 9
    for (int i = 0; i < 4; ++i) PushU16(out, r.snapshot.moves[i]);   // 10..17
    for (int i = 0; i < 4; ++i) out.push_back(r.snapshot.pp_ups[i]); // 18..21
    out.push_back(r.snapshot.tera_type);                        // 22
    out.push_back(r.snapshot.dynamax_level);                    // 23
    // 24..29: os GVs do PLA. Sao eles que nao cabiam nos 3 bytes livres da
    // v1 e obrigaram a entrada a crescer.
    for (int i = 0; i < 6; ++i) out.push_back(r.snapshot.effort_levels[i]);
    out.push_back(0);  // 30..31: padding herdado da v2
    out.push_back(0);
    // 32..37: IDENTIDADE DE ORIGEM (v3, spec 145). O que o SwSh sobrescreve
    // ao receber e nao tem como devolver sozinho.
    out.push_back(r.snapshot.origin_game);                      // 32
    PushU16(out, r.snapshot.met_location);                      // 33..34
    PushU16(out, r.snapshot.egg_location);                      // 35..36
    out.push_back(r.snapshot.ball);                             // 37
    out.push_back(0);  // 38..39: padding, para a proxima extensao
    out.push_back(0);
  }
  return out;
}

bool Decode(const std::vector<std::uint8_t>& bytes, std::size_t offset,
            Memory* m) {
  m->set_records({});
  if (offset + 4 > bytes.size()) return false;
  std::uint32_t n = 0;
  for (int i = 0; i < 4; ++i) {
    n |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
  }
  // MIGRACAO (spec 139): a entrada cresceu de 24 para 32 bytes. O tamanho nao
  // esta gravado em lugar nenhum — a secao e "contador + n entradas" —, entao
  // ele e DEDUZIDO do espaco disponivel.
  //
  // Sem isto, um banco gravado antes da 139 seria recusado por truncamento
  // (`need` calculado com 32) e o dono PERDERIA a memoria inteira. E o TD-01
  // da spec: um Decode errado aqui corrompe dado real.
  const std::size_t disponivel = bytes.size() - (offset + 4);
  std::size_t entry = kEntryBytes;
  if (n > 0) {
    const std::size_t precisa_novo = static_cast<std::size_t>(n) * kEntryBytes;
    const std::size_t precisa_v2 = static_cast<std::size_t>(n) * kEntryBytesV2;
    const std::size_t precisa_v1 = static_cast<std::size_t>(n) * kEntryBytesV1;
    // A deducao e por IGUALDADE, nao por "cabe". Com `>=`, uma secao de 32
    // bytes truncada em 4 sobraria com 28 — que ainda "cabe" em 24 — e seria
    // lida como v1, aceitando dado corrompido em silencio. Um teste que ja
    // existia (`memoria truncada recusa o arquivo`) pegou isto.
    //
    // A secao e o ULTIMO bloco do arquivo, entao `disponivel` bate exatamente
    // com o tamanho gravado; sobra so quando ha lixo depois, que tambem nao
    // deve ser aceito como se fosse a versao antiga.
    if (disponivel == precisa_novo) {
      entry = kEntryBytes;      // v3: com identidade de origem (spec 145)
    } else if (disponivel == precisa_v2) {
      entry = kEntryBytesV2;    // v2: spec 139, sem a identidade
    } else if (disponivel == precisa_v1) {
      entry = kEntryBytesV1;    // v1: anterior a spec 139
    } else {
      // Nao bate com nenhum dos dois: recusa. Ler a memoria pela metade daria
      // moveset restaurado errado, que e pior que memoria vazia.
      return false;
    }
  }

  std::vector<Record> recs;
  recs.reserve(n);
  for (std::uint32_t k = 0; k < n; ++k) {
    const std::uint8_t* e = bytes.data() + offset + 4 + k * entry;
    Record r;
    r.tracker = ReadU64(e);
    const std::uint8_t g = e[8];
    // Entrada sem indice ou de um jogo que este app nao conhece: descarta so
    // ela, nao o arquivo.
    if (r.tracker == 0 || g >= static_cast<std::uint8_t>(Game::kCount)) continue;
    r.game = static_cast<Game>(g);
    for (int i = 0; i < 4; ++i) r.snapshot.moves[i] = ReadU16(e + 10 + i * 2);
    for (int i = 0; i < 4; ++i) r.snapshot.pp_ups[i] = e[18 + i];
    // Os campos da 139 so existem na entrada de 32 bytes. Numa entrada v1
    // eles ficam ZERADOS, que e o mesmo que "este Pokemon nunca guardou nada
    // disso" — nao ha valor sentinela a inventar.
    if (entry >= kEntryBytesV2) {
      r.snapshot.gmax = (e[9] & 0x01) != 0;
      r.snapshot.alpha = (e[9] & 0x02) != 0;
      r.snapshot.tera_type = e[22];
      r.snapshot.dynamax_level = e[23];
      for (int i = 0; i < 6; ++i) r.snapshot.effort_levels[i] = e[24 + i];
    }
    // A identidade de origem so existe na v3. Comparar com kEntryBytes (40)
    // aqui e correto; usar a mesma constante no bloco acima faria a v2 perder
    // os campos da 139 que ela TEM.
    if (entry >= kEntryBytes) {
      r.snapshot.origin_game = e[32];
      r.snapshot.met_location = ReadU16(e + 33);
      r.snapshot.egg_location = ReadU16(e + 35);
      r.snapshot.ball = e[37];
    }
    recs.push_back(r);
  }
  m->set_records(std::move(recs));
  return true;
}

}  // namespace pokehome::moveset
