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

// Quais jogos memorizam moveset. Ate a spec 108 eram so PLA/BDSP (engine de
// golpes propria); a transferencia entre geracoes (TD-D5 da descoberta)
// estendeu a substituicao a TODO cruzamento de geracao, entao cada alvo tem a
// sua memoria. O valor e SERIALIZADO no arquivo do NestBox: nunca renumere,
// so acrescente no fim.
enum class Game : std::uint8_t {
  kLegendsArceus = 0,
  kBdsp = 1,
  kGen3 = 2,   // familia GBA inteira: o moveset gen3 original de um Pokemon
  kSwSh = 3,
  kSV = 4,
  kZA = 5,
  kLgpe = 6,
  kCount,
};

// O que se guarda de um moveset. Golpes e PP-ups; o PP corrente nao entra
// porque e estado de batalha, nao configuracao (o jogo o restaura cheio).
// O que ficou guardado de um Pokemon NAQUELE jogo. O moveset veio da spec
// 071; o resto entrou na 139, fechando a L4 da pesquisa de mecanicas.
//
// A regra que estes campos implementam (DEC-1 do dono): a transferencia e
// transversal, e "quando volta e restaurado caso fique limitado em algo". Um
// tera type nao existe no SwSh — mas nao pode SUMIR por ter passado por la.
struct Snapshot {
  std::array<std::uint16_t, 4> moves{};
  std::array<std::uint8_t, 4> pp_ups{};

  // Mecanicas retidas por jogo (spec 139). Zero significa "nada guardado",
  // que e o mesmo estado de uma entrada gravada por uma versao anterior do
  // arquivo — por isso a migracao nao precisa de valor sentinela.
  std::uint8_t tera_type = 0;       // SV
  std::uint8_t dynamax_level = 0;   // SwSh
  bool gmax = false;                // SwSh
  bool alpha = false;               // PLA
  std::array<std::uint8_t, 6> effort_levels{};  // GVs do PLA

  // IDENTIDADE DE ORIGEM (spec 145). O SwSh nao representa origem em jogo
  // posterior: ao entrar la, o Pokemon tem `origin_game` reescrito para SW/SH
  // e o local trocado por um codigo 59996-60000. Sem guardar o original, a
  // VOLTA nao tem como restaura-lo — um Charizard de Hisui viraria "veio do
  // PLA" sem o local, para sempre.
  //
  // O HOME oficial faz isso com um registro mestre. O NestBox e o HOME: o
  // dono decidiu (2026-08-20) reter o registro em vez de aceitar a perda.
  //
  // `origin_game == 0` significa "nada guardado" — o mesmo estado de uma
  // entrada de versao anterior, entao a migracao nao precisa de sentinela.
  std::uint8_t origin_game = 0;
  std::uint16_t met_location = 0;
  std::uint16_t egg_location = 0;
  std::uint8_t ball = 0;
};

// Uma entrada da memoria: 24 bytes serializados (ver kEntryBytes).
struct Record {
  std::uint64_t tracker = 0;
  Game game = Game::kLegendsArceus;
  Snapshot snapshot;
};

// 24 na v1 (spec 071), 32 a partir da spec 139. A entrada cresceu para caber
// as mecanicas retidas; os 3 bytes que a v1 deixou livres nao davam conta dos
// 6 GVs do PLA sozinhos.
//
// `Decode` LE AS DUAS: uma secao gravada com entradas de 24 bytes continua
// sendo lida, e os campos novos entram zerados — que e exatamente o que
// significam ("este Pokemon nunca guardou nada disso"). Ver TD-01 da spec.
// v3 (spec 145): 40 bytes. A identidade de origem sao 6 (origin_game,
// met_location, egg_location, ball) e os 2 de padding da v2 nao davam. As
// TRES sao lidas; os campos que a versao antiga nao tinha entram zerados, que
// e exatamente o que significam.
inline constexpr std::size_t kEntryBytes = 40;
inline constexpr std::size_t kEntryBytesV2 = 32;
inline constexpr std::size_t kEntryBytesV1 = 24;

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
//
// kGen3 e a FAMILIA: a memoria nao distingue FireRed de Emerald (o moveset
// original e um so), mas o learnset e por jogo — o reset ao ENTRAR num gen3
// especifico usa a tabela daquele jogo, que o chamador conhece (spec 110).
// Aqui kGen3 cai no Emerald como representante.
inline learnset::Game ToLearnsetGame(Game g) {
  switch (g) {
    case Game::kLegendsArceus: return learnset::Game::kLegendsArceus;
    case Game::kBdsp: return learnset::Game::kBdsp;
    case Game::kSwSh: return learnset::Game::kSwSh;
    case Game::kSV: return learnset::Game::kSV;
    case Game::kZA: return learnset::Game::kZA;
    case Game::kLgpe: return learnset::Game::kLgpe;
    case Game::kGen3: return learnset::Game::kEmerald;
    case Game::kCount: break;
  }
  return learnset::Game::kLegendsArceus;
}

// --- Regras da NestBox (spec 125) ------------------------------------------

// O bucket de memoria do jogo de ORIGEM do Pokemon, derivado do codigo
// `origin_game` (com fallback pelo formato quando o codigo nao mapeia).
Game OriginBucket(const pkm::Pokemon& p);

// Chegada a NestBox: memoriza o moveset ATUAL sob `src` (o jogo de onde o
// Pokemon saiu) e restaura o do jogo de ORIGEM — a regra do dono: "assim que
// for pra nestbox e salvarmos, ele relembra os golpes". Devolve true se o
// moveset mudou (o chamador reserializa o registro).
//
// Nao age quando: sem tracker; origem == src (nativo indo para a box);
// origem sem snapshot; moveset ja e o original (mover dentro da box).
bool RestoreOnBank(pkm::Pokemon& p, Memory& m, Game src);

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
