#include "moveset_memory.h"

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

// --- G12 -------------------------------------------------------------------

int ResetMovesByLevel(pkm::Pokemon& p, Game game, std::uint8_t level) {
  std::uint16_t out[4] = {0, 0, 0, 0};
  const int n = learnset::MovesAtLevel(ToLearnsetGame(game), p.species, p.form,
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
    PushU64(out, r.tracker);
    out.push_back(static_cast<std::uint8_t>(r.game));
    out.push_back(0);  // reserva: alinha a entrada e abre espaco para flag
    for (int i = 0; i < 4; ++i) PushU16(out, r.snapshot.moves[i]);
    for (int i = 0; i < 4; ++i) out.push_back(r.snapshot.pp_ups[i]);
    out.push_back(0);  // padding para fechar em kEntryBytes
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
  const std::size_t need = static_cast<std::size_t>(n) * kEntryBytes;
  // Truncado: recusa. Ler a memoria pela metade daria moveset restaurado
  // errado, que e pior que memoria vazia.
  if (offset + 4 + need > bytes.size()) return false;

  std::vector<Record> recs;
  recs.reserve(n);
  for (std::uint32_t k = 0; k < n; ++k) {
    const std::uint8_t* e = bytes.data() + offset + 4 + k * kEntryBytes;
    Record r;
    r.tracker = ReadU64(e);
    const std::uint8_t g = e[8];
    // Entrada sem indice ou de um jogo que este app nao conhece: descarta so
    // ela, nao o arquivo.
    if (r.tracker == 0 || g >= static_cast<std::uint8_t>(Game::kCount)) continue;
    r.game = static_cast<Game>(g);
    for (int i = 0; i < 4; ++i) r.snapshot.moves[i] = ReadU16(e + 10 + i * 2);
    for (int i = 0; i < 4; ++i) r.snapshot.pp_ups[i] = e[18 + i];
    recs.push_back(r);
  }
  m->set_records(std::move(recs));
  return true;
}

}  // namespace pokehome::moveset
