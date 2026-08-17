// HOME Tracker e memoria de moveset por jogo (spec 071; G10/G11/G12 da
// descoberta escrita-e-transferencia).
//
// A regra da §7 de docs/pesquisa-pokemon-home.md, na integra:
//
//   HOME Tracker: na primeira entrada no HOME, o servidor atribui um tracker
//   de 64 bits, unico e IMUTAVEL, gravado nos dados do Pokemon. Serve para
//   antifraude/anticlonagem e indexa um snapshot por jogo.
//
//   Moves: ao ir para jogo com engine de golpes diferente (PLA, BDSP), o
//   moveset e RESETADO (recalculado por nivel). Mas o HOME MEMORIZA o ultimo
//   moveset por jogo (via tracker): ao voltar a um jogo onde ja esteve, o
//   moveset daquele jogo e RESTAURADO.
//
// Como o resto do core, isto nao depende de plataforma: recebe modelo e bytes,
// devolve modelo e bytes. O I/O de verdade fica na UI.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "learnset.h"
#include "pkm_model.h"

namespace pokehome::moveset {

// --- G10: HOME Tracker -----------------------------------------------------

// Deriva o tracker que ESTE Pokemon teria. Determinista e sem estado externo:
// o mesmo Pokemon sempre devolve o mesmo valor (TD-01 da spec 071).
//
// Nao temos o servidor da Nintendo, entao o valor sai de um SHA-256 dos campos
// de IDENTIDADE imutaveis — encryption constant, PID, especie, TID, SID, OT,
// data/local de origem, ball, jogo de origem — truncado em 64 bits.
//
// Por que hash e nao contador: um contador precisaria de estado global, e o
// mesmo Pokemon que saisse e voltasse ao banco ganharia tracker novo, o que
// destroi tanto o anticlonagem quanto o indice da memoria de moveset.
//
// Nunca devolve zero (o bit alto e forcado): zero significa "sem tracker".
std::uint64_t DeriveTracker(const pkm::Pokemon& p);

// Atribui o tracker se — e SOMENTE se — o Pokemon ainda nao tiver um.
//
// A imutabilidade e o ponto inteiro da feature. Um Pokemon que ja passou pela
// HOME de verdade traz o tracker do servidor da Nintendo; sobrescrever isso
// destruiria dado real e quebraria o anticlonagem. Devolve true se atribuiu.
bool AssignTracker(pkm::Pokemon& p);

// --- G11: memoria de moveset por (tracker, jogo) ---------------------------

// Quais jogos memorizam moveset. Sao os de engine de golpes propria, os mesmos
// que a §7 cita como causadores do reset — e os mesmos que learnset.h cobre.
enum class Game : std::uint8_t {
  kLegendsArceus = 0,
  kBdsp = 1,
  kCount,
};

// O que se guarda de um moveset. Golpes e PP-ups; o PP corrente nao entra
// porque e estado de batalha, nao configuracao (o jogo o restaura cheio).
struct Snapshot {
  std::array<std::uint16_t, 4> moves{};
  std::array<std::uint8_t, 4> pp_ups{};
};

// Uma entrada da memoria: 24 bytes serializados (ver kEntryBytes).
struct Record {
  std::uint64_t tracker = 0;
  Game game = Game::kLegendsArceus;
  Snapshot snapshot;
};

inline constexpr std::size_t kEntryBytes = 24;

// A memoria inteira. Poucas dezenas de entradas na pratica; busca linear.
// ponytail: busca linear por (tracker, jogo); indexar se o banco crescer.
class Memory {
 public:
  // Guarda (ou substitui) o moveset deste Pokemon no jogo dado. Pokemon sem
  // tracker e ignorado — sem indice nao ha o que memorizar.
  void Remember(const pkm::Pokemon& p, Game game);

  // Devolve o moveset memorizado, ou nullptr se nunca esteve nesse jogo.
  const Snapshot* Recall(std::uint64_t tracker, Game game) const;

  // Aplica ao Pokemon o moveset que ele deve ter ao ENTRAR neste jogo:
  //
  //   - ja esteve la  -> restaura o moveset memorizado          (G11)
  //   - primeira vez  -> recalcula pelo learnset, por nivel     (G12)
  //
  // Devolve true se restaurou da memoria, false se resetou por nivel. O nivel
  // vem de fora porque a curva de crescimento e por especie e ja vive no
  // gen3_save/save readers — este modulo nao duplica a tabela.
  bool ApplyOnEntry(pkm::Pokemon& p, Game game, std::uint8_t level) const;

  const std::vector<Record>& records() const { return records_; }
  void set_records(std::vector<Record> r) { records_ = std::move(r); }
  std::size_t size() const { return records_.size(); }

 private:
  std::vector<Record> records_;
};

// --- G12: reset por nivel --------------------------------------------------

// Sobrescreve os golpes do Pokemon pelos 4 que o learnset daquele jogo daria no
// nivel informado. PP-ups zeram — golpe novo nunca vem com PP-up. O PP corrente
// nao e mexido: quem escreve no save recalcula pelo golpe.
//
// Especie que o jogo nao conhece nao e mexida (o bloqueio por especie e G07,
// nao daqui) e devolve 0.
int ResetMovesByLevel(pkm::Pokemon& p, Game game, std::uint8_t level);

// Ponte para o enum do learnset. Os dois existem separados de proposito: o
// learnset e tabela gerada, esta e a API do produto.
inline learnset::Game ToLearnsetGame(Game g) {
  return g == Game::kBdsp ? learnset::Game::kBdsp
                          : learnset::Game::kLegendsArceus;
}

// --- Serializacao (secao v4 do arquivo NestBox) ----------------------------

// Bytes da secao: contador u32 + n * kEntryBytes.
std::vector<std::uint8_t> Encode(const Memory& m);

// Le a secao a partir de `bytes[offset]`. Devolve false — e deixa `m` vazia —
// se a secao estiver truncada. Entrada com tracker zero ou jogo desconhecido e
// DESCARTADA em silencio: dado sem indice nao e usavel, e recusar o arquivo
// inteiro por causa dela apagaria o banco de quem atualizasse o app.
bool Decode(const std::vector<std::uint8_t>& bytes, std::size_t offset,
            Memory* m);

}  // namespace pokehome::moveset
