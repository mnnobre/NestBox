// Ciclo de vida do save (spec 081).
//
// A pergunta que nenhum teste anterior responde: o save sobrevive ao USO REAL?
// Todo teste de 063..080 faz UMA operacao e confere. O fluxo do app e outro:
//
//     backup -> salvar -> movimentar -> salvar de novo -> reabrir -> validar
//
// As classes de bug que SO aparecem aqui, e o motivo de cada etapa existir:
//   - erro que se ACUMULA entre gravacoes (o arquivo "deriva" a cada Save);
//   - estado que nao e LIMPO entre ciclos (por isso reabrimos do DISCO, e nao
//     reusamos o SaveData em memoria);
//   - backup SOBRESCRITO quando nao devia (por isso a rotacao vai a disco).
//
// LICAO DA SPEC 080 APLICADA: o portao G03 passava verde LENDO ZERO Pokemon —
// regravar sem ter lido nada devolve o arquivo intacto por construcao. Um ciclo
// que nao move nada "passa" pelo mesmo motivo. Por isso todo ciclo aqui EXIGE
// movimentacao real e a confere (contagem e identidade do Pokemon movido).
//
// GUARDRAIL: tests/saves-limpos/ e SOMENTE LEITURA. Todo ciclo roda dentro de
// um sandbox::SaveSandbox, e o SHA256 das fixtures e conferido no fim.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "moveset_memory.h"
#include "pkm_convert.h"
#include "save_backup.h"
#include "save_sandbox.h"
#include "save_writer.h"
#include "sha256.h"
#include "transfer.h"

namespace fs = std::filesystem;
namespace cp = pokehome::compat;
namespace bk = pokehome::backup;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (ok) {
    std::printf("  ok: %s\n", what.c_str());
  } else {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  }
}

// std::filesystem::path e obrigatorio no ifstream (Windows).
static std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(fs::path(path), std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

static bool WriteFile(const std::string& path,
                      const std::vector<std::uint8_t>& b) {
  std::ofstream f(fs::path(path), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(b.data()),
          static_cast<std::streamsize>(b.size()));
  return f.good();
}

static std::string Hex(const sha256::Digest& d) {
  std::string out;
  char buf[3];
  for (std::uint8_t x : d) {
    std::snprintf(buf, sizeof(buf), "%02x", x);
    out += buf;
  }
  return out;
}

static std::string HashOf(const std::string& path) {
  const auto b = ReadFile(path);
  if (b.empty()) return "<vazio>";
  return Hex(sha256::Hash(b.data(), b.size()));
}

// ---------------------------------------------------------------------------
// Os 5 saves limpos da spec 077 (40 Pokemon 100% legais, OT=NESTBOX).
// ---------------------------------------------------------------------------
struct Jogo {
  const char* nome;
  const char* rel;
  savew::Game game;
};

static const Jogo kJogos[] = {
    {"swsh", "swsh/main", savew::Game::kSwSh},
    {"sv", "sv/main", savew::Game::kSV},
    {"bdsp", "bdsp/SaveData.bin", savew::Game::kBDSP},
    {"pla", "pla/main", savew::Game::kPLA},
    {"lgpe", "lgpe/savedata.bin", savew::Game::kLGPE},
};
static constexpr std::size_t kNJogos = sizeof(kJogos) / sizeof(kJogos[0]);

static std::string CaminhoLimpo(const Jogo& j) {
  return std::string(CLEAN_SAVES) + j.rel;
}

struct Aberto {
  sandbox::SaveSandbox sb;
  savew::SaveData sd;
};

static std::optional<Aberto> Abrir(const Jogo& j) {
  auto sb = sandbox::SaveSandbox::Create(CaminhoLimpo(j));
  if (!sb) return std::nullopt;
  auto sd = savew::Load(ReadFile(sb->path()), j.game);
  if (!sd) return std::nullopt;
  return Aberto{std::move(*sb), std::move(*sd)};
}

// ---------------------------------------------------------------------------
// CONFERENCIA INTERNA DE INTEGRIDADE — o valor GRAVADO bate com o RECALCULADO?
//
// Por que isto e obrigatorio e nao redundante com o juiz externo: em BDSP e
// LGPE o PkHeX **aceita save com integridade quebrada** — ele so recalcula
// MD5/CRC16 ao gravar, nao confere ao ler (contexto-tecnico.md). Nesses dois
// formatos "o PkHeX abriu" NAO prova integridade, e o nosso proprio Load()
// tambem nao confere o CRC do LGPE. Sem esta funcao, uma gravacao que esqueca
// de refazer o checksum passa VERDE nos dois oraculos ao mesmo tempo.
//
// Medido: foi exatamente o que aconteceu na violacao plantada da spec 081 (T8).
// Suprimir o recalculo do bloco 9 (PC) do LGPE corrompeu o save de verdade e
// NENHUM assert caiu ate esta conferencia existir.
//
// Devolve quantas regioes de integridade estao quebradas.
static int IntegridadeQuebrada(const std::vector<std::uint8_t>& b,
                               savew::Game g) {
  int quebrados = 0;
  if (g == savew::Game::kLGPE) {
    for (std::size_t id = 0; id < savew::kLgpeBlockCount; ++id) {
      const auto& blk = savew::kLgpeBlocks[id];
      if (blk.offset + blk.size > b.size()) continue;
      const std::size_t co = savew::LgpeChecksumOffset(id);
      if (co + 1 >= b.size()) continue;
      const std::uint16_t gravado =
          static_cast<std::uint16_t>(b[co] | (b[co + 1] << 8));
      if (gravado != savew::Crc16Arc(b.data() + blk.offset, blk.size))
        ++quebrados;
    }
  } else if (g == savew::Game::kBDSP) {
    // MD5 do arquivo inteiro com os 16 bytes do proprio hash zerados.
    const std::size_t off = 0xE9818;
    if (off + 16 > b.size()) return 1;
    std::vector<std::uint8_t> copia = b;
    std::uint8_t gravado[16];
    std::memcpy(gravado, copia.data() + off, 16);
    std::memset(copia.data() + off, 0, 16);
    std::uint8_t calc[16];
    savew::Md5(copia.data(), copia.size(), calc);
    if (std::memcmp(calc, gravado, 16) != 0) ++quebrados;
  }
  // SwSh/SV/PLA: o SHA256 do SwishCrypto e conferido pelo proprio Load(), que
  // devolve nullopt se nao fechar — e o PkHeX os REJEITA de verdade. Ali o
  // "reabriu" ja e prova, e nao ha o que conferir a mao.
  return quebrados;
}

// Identidade de um Pokemon para comparacao entre ciclos. Os bytes CRUS, e nao
// os campos modelados: o modelo nao enxerga o que esta atras do terminador de
// apelido (contexto-tecnico.md, spec 063), e e justamente isso que uma
// gravacao descuidada destroi em silencio.
static std::string IdSlot(const savew::SaveData& sd, std::size_t b,
                          std::size_t s) {
  const auto& sl = sd.At(b, s);
  if (!sl.present) return "<vazio>";
  if (sl.mon.raw.empty()) return "<sem raw>";
  return Hex(sha256::Hash(sl.mon.raw.data(), sl.mon.raw.size()));
}

// Um retrato de TODOS os slots, para achar quem mudou sem ter sido tocado.
static std::vector<std::string> Retrato(const savew::SaveData& sd) {
  std::vector<std::string> out;
  out.reserve(sd.box_count * sd.slots_per_box);
  for (std::size_t b = 0; b < sd.box_count; ++b)
    for (std::size_t s = 0; s < sd.slots_per_box; ++s)
      out.push_back(IdSlot(sd, b, s));
  return out;
}

// ---------------------------------------------------------------------------
// T1+T2 — BACKUP e ROTACAO, em disco de verdade.
//
// O `save_backup.h` e logica pura (nomes, Verify, ToRemove) e o I/O e do
// chamador. Um teste que so exercitasse as funcoes puras provaria que a
// aritmetica da rotacao fecha, e NAO que o backup existe no cartao — que e a
// rede de seguranca que a regra do produto exige.
//
// O carimbo NAO vem do relogio: o teste roda em menos de um segundo e carimbos
// iguais colidiriam, escondendo justamente o bug de "backup sobrescrito".
// ---------------------------------------------------------------------------
static void TesteBackupERotacao(const Jogo& j) {
  std::printf("\n--- [%s] T1 backup + T2 rotacao ---\n", j.nome);

  auto sb = sandbox::SaveSandbox::Create(CaminhoLimpo(j));
  if (!sb) {
    Check(false, std::string(j.nome) + ": sandbox nao criou");
    return;
  }
  const std::string dir = sb->dir();
  const std::vector<std::uint8_t> original = ReadFile(sb->path());
  Check(!original.empty(), std::string(j.nome) + ": save original lido");

  // T1 — o backup antes da primeira escrita.
  const std::string nome1 = bk::MakeFilename(sb->path(), "20260816-100000");
  const std::string cam1 = dir + "/" + nome1;
  Check(WriteFile(cam1, original), j.nome + std::string(": backup gravado ") + nome1);

  const auto readback = ReadFile(cam1);
  Check(bk::Verify(original, readback),
        std::string(j.nome) + ": backup::Verify aprova o backup");
  Check(HashOf(cam1) == HashOf(sb->path()),
        std::string(j.nome) + ": backup byte-identico ao original (sha256)");

  // O nome volta a ser parseavel — a lista da tela depende disso.
  bk::Entry e;
  Check(bk::ParseFilename(nome1, &e) && e.save_name == bk::SaveName(sb->path()) &&
            e.stamp == "20260816-100000",
        std::string(j.nome) + ": ParseFilename devolve save+carimbo");

  // T2 — rotacao: kMaxPerSave + 3 backups, os 3 mais antigos saem.
  const std::size_t extra = 3;
  std::vector<bk::Entry> todos;
  todos.push_back(e);
  for (std::size_t i = 1; i < bk::kMaxPerSave + extra; ++i) {
    char stamp[16];
    std::snprintf(stamp, sizeof(stamp), "20260816-1000%02zu", i);
    const std::string n = bk::MakeFilename(sb->path(), stamp);
    if (!WriteFile(dir + "/" + n, original)) {
      Check(false, std::string(j.nome) + ": gravando backup " + n);
      return;
    }
    bk::Entry ei;
    bk::ParseFilename(n, &ei);
    todos.push_back(ei);
  }
  Check(todos.size() == bk::kMaxPerSave + extra,
        std::string(j.nome) + ": " + std::to_string(todos.size()) +
            " backups criados em disco");

  // Um arquivo alheio no mesmo diretorio nao pode virar entrada da lista.
  Check(!bk::ParseFilename("naoehbackup.txt", nullptr),
        std::string(j.nome) + ": arquivo alheio nao vira entrada de backup");

  const auto remover = bk::ToRemove(todos);
  Check(remover.size() == extra,
        std::string(j.nome) + ": ToRemove indica " + std::to_string(extra) +
            " (excedente)");
  // Os indicados sao os MAIS ANTIGOS — nao "uns quaisquer".
  Check(std::find(remover.begin(), remover.end(), nome1) != remover.end(),
        std::string(j.nome) + ": o mais antigo esta entre os removidos");

  for (const auto& n : remover) fs::remove(fs::path(dir + "/" + n));

  std::size_t restantes = 0;
  for (const auto& de : fs::directory_iterator(fs::path(dir)))
    if (bk::ParseFilename(de.path().filename().string(), nullptr)) ++restantes;
  Check(restantes == bk::kMaxPerSave,
        std::string(j.nome) + ": sobraram exatamente " +
            std::to_string(bk::kMaxPerSave) + " backups em disco (achei " +
            std::to_string(restantes) + ")");
  Check(!fs::exists(fs::path(cam1)),
        std::string(j.nome) + ": o backup mais antigo saiu do disco");

  // E os que sobraram continuam validos — rotacao nao pode corromper vizinho.
  bool todos_ok = true;
  for (const auto& de : fs::directory_iterator(fs::path(dir))) {
    const std::string fn = de.path().filename().string();
    if (!bk::ParseFilename(fn, nullptr)) continue;
    if (!bk::Verify(original, ReadFile(de.path().string()))) todos_ok = false;
  }
  Check(todos_ok, std::string(j.nome) + ": os backups restantes continuam integros");
}

// ---------------------------------------------------------------------------
// T4 — IDEMPOTENCIA. Load+Save sem alterar, N vezes SEGUIDAS, sempre o mesmo
// arquivo.
//
// O G03 (spec 068) prova a PRIMEIRA gravacao. Se o Save() acumulasse qualquer
// coisa — um contador, um timestamp, um checksum calculado sobre si mesmo — a
// segunda passada divergiria e nenhum teste existente veria.
//
// Cada passada RELE DO DISCO: reusar o SaveData em memoria testaria a funcao
// pura, e nao o ciclo.
// ---------------------------------------------------------------------------
// A idempotencia e sobre GRAVACOES SUCESSIVAS, e a primeira e um caso a parte:
// se o arquivo de ENTRADA nao satisfaz a propria definicao de integridade, a
// primeira gravacao diverge apenas por CONSERTA-LO, e isso e o writer certo
// fazendo a coisa certa.
//
// A distincao nao e teorica: a fixture bdsp de tests/saves-limpos/ (spec 077)
// tem o MD5 de 0xE9818 desatualizado — a spec 077 trocou o OT nos registros de
// Hall of Fame em bytes crus e nao refez o hash (contexto-tecnico.md, secao "O
// modelo do PkHeX nao cobre o save inteiro"), e como o PkHeX NAO valida
// integridade de BDSP na leitura, nada acusou ate agora. O save REAL do dono
// regrava com 0 bytes divergentes, o que prova que o writer esta certo.
//
// Por isso a baliza da idempotencia e a PRIMEIRA gravacao, nao o arquivo de
// entrada — e a divergencia inicial, quando existe, e RELATADA em vez de
// tolerada em silencio. Ver P-01 da spec 081.
static void TesteIdempotencia(const Jogo& j, int n) {
  std::printf("\n--- [%s] T4 idempotencia (%d gravacoes) ---\n", j.nome, n);
  auto a = Abrir(j);
  if (!a) {
    Check(false, std::string(j.nome) + ": sandbox/Load falhou");
    return;
  }
  const std::string h_entrada = HashOf(a->sb.path());
  const std::size_t n0 = a->sd.Count();

  // A primeira gravacao, medida a parte.
  {
    auto sd = savew::Load(ReadFile(a->sb.path()), j.game);
    if (!sd || !WriteFile(a->sb.path(), savew::Save(*sd))) {
      Check(false, std::string(j.nome) + ": a primeira gravacao falhou");
      return;
    }
  }
  const std::string h0 = HashOf(a->sb.path());
  if (h0 != h_entrada) {
    std::printf(
        "  NOTA: %s — a 1a gravacao mudou o arquivo de entrada. O writer esta\n"
        "        consertando integridade que ja vinha quebrada na FIXTURE; ver\n"
        "        P-01. A idempotencia e medida da 1a gravacao em diante.\n",
        j.nome);
  }

  int derivou_em = 0;
  for (int i = 1; i <= n; ++i) {
    auto sd = savew::Load(ReadFile(a->sb.path()), j.game);
    if (!sd) {
      Check(false, std::string(j.nome) + ": nao reabriu na passada " +
                       std::to_string(i));
      return;
    }
    if (sd->Count() != n0) {
      Check(false, std::string(j.nome) + ": contagem mudou na passada " +
                       std::to_string(i));
      return;
    }
    const auto gravado = savew::Save(*sd);
    if (!WriteFile(a->sb.path(), gravado)) {
      Check(false, std::string(j.nome) + ": gravacao falhou na passada " +
                       std::to_string(i));
      return;
    }
    if (IntegridadeQuebrada(gravado, j.game) != 0) {
      Check(false, std::string(j.nome) +
                       ": INTEGRIDADE QUEBRADA na passada " + std::to_string(i));
      return;
    }
    if (HashOf(a->sb.path()) != h0 && derivou_em == 0) derivou_em = i;
  }
  Check(derivou_em == 0,
        std::string(j.nome) + ": " + std::to_string(n) +
            " gravacoes sem alterar produzem sempre o mesmo arquivo" +
            (derivou_em ? " (DERIVOU na passada " + std::to_string(derivou_em) +
                              ")"
                        : ""));
  // A trava da spec 080: idempotencia com ZERO Pokemon passa por construcao.
  Check(n0 > 0, std::string(j.nome) + ": o save lido tem " +
                    std::to_string(n0) + " Pokemon (nao e verde-lendo-zero)");

  // A consistencia da FIXTURE, declarada. So o bdsp diverge hoje (P-01); se um
  // dia outro save entrar nessa lista, o teste conta em voz alta em vez de
  // deixar a nota passar batido.
  std::printf("  fixture de entrada %s: %s\n", j.nome,
              h0 == h_entrada ? "ja consistente (regrava identica)"
                              : "INCONSISTENTE — a 1a gravacao a corrige");
}

// ---------------------------------------------------------------------------
// T3+T5 — MULTIPLAS GRAVACOES com movimentacao, reabrindo do DISCO entre elas.
//
// Cada ciclo: reabre do disco, move UM Pokemon para um slot livre da mesma
// caixa (movimentacao interna — nao depende de um segundo save existir nem das
// regras de compatibilidade), grava, descarta o objeto.
//
// No fim: contagem intacta, e os slots NAO TOCADOS byte-identicos ao inicio.
// Esse ultimo assert e o que pega erro acumulado — a spec 063 registra que o
// roundtrip semantico nao ve o que esta atras do terminador.
// ---------------------------------------------------------------------------
struct ResultadoCiclos {
  int ciclos = 0;
  bool ok = false;
};

static ResultadoCiclos TesteCiclos(const Jogo& j, int n) {
  std::printf("\n--- [%s] T3+T5 %d ciclos (salvar/mover/salvar, reabrindo) ---\n",
              j.nome, n);
  ResultadoCiclos res;
  auto a = Abrir(j);
  if (!a) {
    Check(false, std::string(j.nome) + ": sandbox/Load falhou");
    return res;
  }
  const std::size_t n0 = a->sd.Count();
  if (n0 == 0) {
    std::printf("  n/a: %s tem 0 Pokemon nas caixas (a party fica fora — "
                "decisao do dono); sem base para movimentar\n", j.nome);
    res.ok = true;
    return res;
  }
  const auto retrato0 = Retrato(a->sd);
  const std::string caminho = a->sb.path();

  // Os slots que ESTE teste toca, para separar "mudou porque movi" de "mudou
  // sozinho".
  std::vector<std::size_t> tocados;
  const auto plano = [&](std::size_t b, std::size_t s) {
    return b * a->sd.slots_per_box + s;
  };

  for (int c = 1; c <= n; ++c) {
    auto sd = savew::Load(ReadFile(caminho), j.game);
    if (!sd) {
      Check(false, std::string(j.nome) + ": nao reabriu no ciclo " +
                       std::to_string(c));
      return res;
    }
    if (sd->Count() != n0) {
      Check(false, std::string(j.nome) + ": contagem virou " +
                       std::to_string(sd->Count()) + " no ciclo " +
                       std::to_string(c));
      return res;
    }

    // Acha um ocupado e um livre. Percorre o save inteiro: pegar "o slot 0" e a
    // amostragem por conveniencia que a spec 069 condenou.
    std::size_t ob = 0, os = 0, lb = 0, ls = 0;
    bool tem_o = false, tem_l = false;
    for (std::size_t b = 0; b < sd->box_count && !(tem_o && tem_l); ++b)
      for (std::size_t s = 0; s < sd->slots_per_box && !(tem_o && tem_l); ++s) {
        if (sd->At(b, s).present) {
          if (!tem_o) { ob = b; os = s; tem_o = true; }
        } else if (!tem_l) {
          lb = b; ls = s; tem_l = true;
        }
      }
    if (!tem_o || !tem_l) {
      Check(false, std::string(j.nome) + ": sem par ocupado/livre no ciclo " +
                       std::to_string(c));
      return res;
    }

    const pkm::Pokemon mon = sd->At(ob, os).mon;
    const std::string id_antes = IdSlot(*sd, ob, os);

    // MOVER: escreve no destino e esvazia a origem. `Set` com um Pokemon vazio
    // e como o writer representa "slot livre".
    if (!sd->Set(lb, ls, mon)) {
      Check(false, std::string(j.nome) + ": Set no destino falhou (ciclo " +
                       std::to_string(c) + ")");
      return res;
    }
    if (!sd->Set(ob, os, pkm::Pokemon{})) {
      Check(false, std::string(j.nome) + ": Set (limpar origem) falhou (ciclo " +
                       std::to_string(c) + ")");
      return res;
    }
    tocados.push_back(plano(ob, os));
    tocados.push_back(plano(lb, ls));

    const auto gravado = savew::Save(*sd);
    if (!WriteFile(caminho, gravado)) {
      Check(false, std::string(j.nome) + ": gravacao falhou no ciclo " +
                       std::to_string(c));
      return res;
    }

    // A CADA gravacao, e nao so no fim: um checksum que para de ser refeito na
    // segunda gravacao e justamente o bug que esta spec caca, e ele
    // desapareceria numa conferencia feita so no ultimo ciclo.
    const int quebrados = IntegridadeQuebrada(gravado, j.game);
    if (quebrados != 0) {
      Check(false, std::string(j.nome) + ": INTEGRIDADE QUEBRADA no ciclo " +
                       std::to_string(c) + " — " + std::to_string(quebrados) +
                       " regiao(oes) com checksum gravado != recalculado");
      return res;
    }

    // Reabre e confere que a movimentacao PEGOU. Sem isto, um ciclo que nao
    // move nada passaria trivialmente — o falso-verde da spec 080.
    auto rel = savew::Load(ReadFile(caminho), j.game);
    if (!rel) {
      Check(false, std::string(j.nome) + ": nao reabriu apos gravar (ciclo " +
                       std::to_string(c) + ")");
      return res;
    }
    if (rel->Count() != n0) {
      Check(false, std::string(j.nome) + ": contagem mudou apos o ciclo " +
                       std::to_string(c) + " (" + std::to_string(rel->Count()) +
                       " != " + std::to_string(n0) + ")");
      return res;
    }
    if (rel->At(ob, os).present) {
      Check(false, std::string(j.nome) + ": a origem nao esvaziou (ciclo " +
                       std::to_string(c) + ")");
      return res;
    }
    if (IdSlot(*rel, lb, ls) != id_antes) {
      Check(false, std::string(j.nome) +
                       ": o Pokemon movido nao chegou IDENTICO (ciclo " +
                       std::to_string(c) + ")");
      return res;
    }
    res.ciclos = c;
    // O SaveData sai de escopo aqui — o proximo ciclo reabre do disco (T5).
  }

  Check(res.ciclos == n, std::string(j.nome) + ": aguentou os " +
                             std::to_string(n) + " ciclos (chegou a " +
                             std::to_string(res.ciclos) + ")");

  // O assert que so o ciclo completo consegue fazer: os NAO TOCADOS continuam
  // byte-identicos ao estado inicial.
  auto fim = savew::Load(ReadFile(caminho), j.game);
  if (!fim) {
    Check(false, std::string(j.nome) + ": o save final nao reabre");
    return res;
  }
  const auto retratoN = Retrato(*fim);
  std::size_t divergentes = 0;
  for (std::size_t i = 0; i < retrato0.size() && i < retratoN.size(); ++i) {
    if (std::find(tocados.begin(), tocados.end(), i) != tocados.end()) continue;
    if (retrato0[i] != retratoN[i]) ++divergentes;
  }
  Check(divergentes == 0,
        std::string(j.nome) + ": apos " + std::to_string(n) +
            " ciclos, os Pokemon NAO tocados continuam byte-identicos (" +
            std::to_string(divergentes) + " divergentes)");
  res.ok = divergentes == 0;

  // T9 — o save apos N ciclos fica em disco para o JUIZ EXTERNO
  // (tools/pkhex-verify). "Passa no nosso teste" nao fecha criterio: o veredito
  // do PkHeX fecha. O sandbox apaga o dele no destrutor, entao a copia sai para
  // um diretorio proprio.
  std::error_code ec;
  fs::create_directories(fs::path(CICLO_OUT), ec);
  const std::string destino =
      std::string(CICLO_OUT) + j.nome + "-apos-" + std::to_string(n) + "-ciclos.bin";
  if (WriteFile(destino, ReadFile(caminho)))
    std::printf("  [juiz externo] %s\n", destino.c_str());
  else
    Check(false, std::string(j.nome) + ": nao consegui exportar para o juiz externo");

  return res;
}

// ---------------------------------------------------------------------------
// T7 — RESTAURACAO. Depois de N ciclos, o backup devolve o save byte-a-byte.
//
// A rede de seguranca que a regra do produto exige so vale se ela de fato
// desfizer. Um backup que restaura "quase" e pior que nenhum: muda o
// comportamento de quem confia nele (TD-01 da spec 032).
// ---------------------------------------------------------------------------
static void TesteRestauracao(const Jogo& j, int n) {
  std::printf("\n--- [%s] T7 restauracao apos %d ciclos ---\n", j.nome, n);
  auto sb = sandbox::SaveSandbox::Create(CaminhoLimpo(j));
  if (!sb) {
    Check(false, std::string(j.nome) + ": sandbox nao criou");
    return;
  }
  const auto original = ReadFile(sb->path());
  const std::string h0 = Hex(sha256::Hash(original.data(), original.size()));

  // O backup, ANTES da primeira escrita — a ordem e a regra.
  const std::string cam =
      sb->dir() + "/" + bk::MakeFilename(sb->path(), "20260816-090000");
  if (!WriteFile(cam, original)) {
    Check(false, std::string(j.nome) + ": backup nao gravou");
    return;
  }

  // N ciclos de escrita real por cima.
  int feitos = 0;
  for (int c = 0; c < n; ++c) {
    auto sd = savew::Load(ReadFile(sb->path()), j.game);
    if (!sd) break;
    // Move o primeiro ocupado para o primeiro livre, se houver.
    std::size_t ob = 0, os = 0, lb = 0, ls = 0;
    bool tem_o = false, tem_l = false;
    for (std::size_t b = 0; b < sd->box_count && !(tem_o && tem_l); ++b)
      for (std::size_t s = 0; s < sd->slots_per_box && !(tem_o && tem_l); ++s) {
        if (sd->At(b, s).present) {
          if (!tem_o) { ob = b; os = s; tem_o = true; }
        } else if (!tem_l) {
          lb = b; ls = s; tem_l = true;
        }
      }
    if (tem_o && tem_l) {
      sd->Set(lb, ls, sd->At(ob, os).mon);
      sd->Set(ob, os, pkm::Pokemon{});
    }
    if (!WriteFile(sb->path(), savew::Save(*sd))) break;
    ++feitos;
  }
  Check(feitos == n, std::string(j.nome) + ": " + std::to_string(feitos) +
                         "/" + std::to_string(n) + " ciclos gravados");

  // O save de PLA tem 0 nas caixas: os ciclos gravam, mas nao MUDAM nada. Nesse
  // caso "o hash mudou" nao e exigivel — mas a restauracao continua sendo.
  const bool mudou = HashOf(sb->path()) != h0;
  auto sd0 = savew::Load(original, j.game);
  const bool tinha_mon = sd0 && sd0->Count() > 0;
  if (tinha_mon) {
    Check(mudou, std::string(j.nome) +
                     ": os ciclos de fato alteraram o arquivo (senao a "
                     "restauracao provaria nada)");
  } else {
    std::printf("  n/a: %s tem 0 Pokemon nas caixas; os ciclos nao alteram\n",
                j.nome);
  }

  // Restaura: copiar o backup de volta por cima.
  const auto bkp = ReadFile(cam);
  Check(bk::Verify(original, bkp),
        std::string(j.nome) + ": o backup ainda confere antes de restaurar");
  Check(WriteFile(sb->path(), bkp),
        std::string(j.nome) + ": backup restaurado por cima do save");
  Check(HashOf(sb->path()) == h0,
        std::string(j.nome) + ": apos restaurar, o save e BYTE-A-BYTE o original");

  auto rel = savew::Load(ReadFile(sb->path()), j.game);
  Check(rel && sd0 && rel->Count() == sd0->Count(),
        std::string(j.nome) + ": o save restaurado reabre com a contagem original");
}

// ---------------------------------------------------------------------------
// T6 — IDA E VOLTA COMPLETA, com gravacao e RELEITURA nos dois lados.
//
// A diferenca para o cenario 9 da spec 078: ali a volta parte do SaveData que
// a ida deixou em memoria em um dos lados. Aqui TODOS os quatro estados sao
// relidos do disco, que e o que o app faz entre sessoes.
// ---------------------------------------------------------------------------
static bool AchaNovo(const savew::SaveData& antes, const savew::SaveData& depois,
                     std::size_t* b, std::size_t* s) {
  for (std::size_t bi = 0; bi < depois.box_count; ++bi)
    for (std::size_t si = 0; si < depois.slots_per_box; ++si)
      if (depois.At(bi, si).present && !antes.At(bi, si).present) {
        *b = bi;
        *s = si;
        return true;
      }
  return false;
}

static void TesteIdaEVolta(const Jogo& A, const Jogo& B) {
  std::printf("\n--- [%s <-> %s] T6 ida e volta com releitura do disco ---\n",
              A.nome, B.nome);
  auto a = Abrir(A), b = Abrir(B);
  if (!a || !b) {
    Check(false, "sandbox/Load falhou");
    return;
  }
  if (a->sd.Count() == 0) {
    std::printf("  n/a: %s tem 0 Pokemon nas caixas\n", A.nome);
    return;
  }

  const cp::Game destB = transfer::ToCompatGame(B.game);
  const cp::Game destA = transfer::ToCompatGame(A.game);
  pokehome::moveset::Memory mem;

  // O primeiro Pokemon que o destino ACEITA — varrer e obrigatorio.
  transfer::Request req;
  req.level = 100;
  bool achou = false;
  for (std::size_t bi = 0; bi < a->sd.box_count && !achou; ++bi)
    for (std::size_t si = 0; si < a->sd.slots_per_box && !achou; ++si) {
      if (!a->sd.At(bi, si).present) continue;
      transfer::Request r = req;
      r.src_box = bi;
      r.src_slot = si;
      if (transfer::Prepare(a->sd, b->sd, a->sb.path(), b->sb.path(), destB, r,
                            mem)
              .result.ok()) {
        req = r;
        achou = true;
      }
    }
  if (!achou) {
    std::printf("  n/a: nenhum Pokemon de %s e aceito por %s\n", A.nome, B.nome);
    return;
  }

  const pkm::Pokemon origem = a->sd.At(req.src_box, req.src_slot).mon;
  const std::size_t nA = a->sd.Count(), nB = b->sd.Count();
  const std::string caminho_a = a->sb.path(), caminho_b = b->sb.path();
  const savew::SaveData antes_b = b->sd;

  // IDA.
  auto ida = transfer::Prepare(a->sd, b->sd, caminho_a, caminho_b, destB, req,
                               mem);
  if (!ida.result.ok()) {
    Check(false, "ida recusada: " + ida.result.message);
    return;
  }
  const auto r_ida = transfer::Commit(ida);
  if (!r_ida.ok()) {
    Check(false, std::string("Commit da ida: ") +
                     transfer::StatusName(r_ida.status));
    return;
  }
  mem = ida.memory;

  // TUDO relido do disco — nada de aproveitar o Plan em memoria (T5).
  auto disco_a = savew::Load(ReadFile(caminho_a), A.game);
  auto disco_b = savew::Load(ReadFile(caminho_b), B.game);
  if (!disco_a || !disco_b) {
    Check(false, "os saves nao reabrem apos a ida");
    return;
  }
  Check(disco_a->Count() == nA - 1,
        std::string(A.nome) + ": origem caiu 1 apos a ida (releitura do disco)");
  Check(disco_b->Count() == nB + 1,
        std::string(B.nome) + ": destino subiu 1 apos a ida (releitura do disco)");
  // O Commit e um caminho de escrita DIFERENTE do Save() direto dos ciclos, e
  // por isso precisa da mesma conferencia de integridade.
  Check(IntegridadeQuebrada(ReadFile(caminho_a), A.game) == 0,
        std::string(A.nome) + ": integridade intacta apos o Commit da ida");
  Check(IntegridadeQuebrada(ReadFile(caminho_b), B.game) == 0,
        std::string(B.nome) + ": integridade intacta apos o Commit da ida");

  std::size_t vb, vs;
  if (!AchaNovo(antes_b, *disco_b, &vb, &vs)) {
    Check(false, "nao achou o Pokemon em " + std::string(B.nome));
    return;
  }

  // VOLTA, partindo dos objetos RELIDOS.
  const savew::SaveData antes_a = *disco_a;
  transfer::Request volta;
  volta.src_box = vb;
  volta.src_slot = vs;
  volta.level = 100;
  auto pv = transfer::Prepare(*disco_b, *disco_a, caminho_b, caminho_a, destA,
                              volta, mem);
  if (!pv.result.ok()) {
    Check(false, "volta recusada: " + pv.result.message);
    return;
  }
  const auto r_volta = transfer::Commit(pv);
  if (!r_volta.ok()) {
    Check(false, std::string("Commit da volta: ") +
                     transfer::StatusName(r_volta.status));
    return;
  }

  auto final_a = savew::Load(ReadFile(caminho_a), A.game);
  auto final_b = savew::Load(ReadFile(caminho_b), B.game);
  if (!final_a || !final_b) {
    Check(false, "os saves nao reabrem apos a volta");
    return;
  }
  Check(final_a->Count() == nA,
        std::string(A.nome) + ": contagem de volta ao inicial (" +
            std::to_string(final_a->Count()) + "/" + std::to_string(nA) + ")");
  Check(final_b->Count() == nB,
        std::string(B.nome) + ": contagem de volta ao inicial (" +
            std::to_string(final_b->Count()) + "/" + std::to_string(nB) + ")");
  Check(IntegridadeQuebrada(ReadFile(caminho_a), A.game) == 0,
        std::string(A.nome) + ": integridade intacta apos o Commit da volta");
  Check(IntegridadeQuebrada(ReadFile(caminho_b), B.game) == 0,
        std::string(B.nome) + ": integridade intacta apos o Commit da volta");

  std::size_t fb, fs2;
  if (!AchaNovo(antes_a, *final_a, &fb, &fs2)) {
    Check(false, "o Pokemon nao voltou a " + std::string(A.nome));
    return;
  }
  const pkm::Pokemon& f = final_a->At(fb, fs2).mon;

  // Campos [HOME] atravessaram A->disco->B->disco->A->disco.
  Check(pkm::NationalDex(f) == pkm::NationalDex(origem),
        "especie preservada na ida e volta com releitura");
  Check(f.pid == origem.pid, "PID preservado");
  Check(f.ivs == origem.ivs, "IVs preservados");
  Check(f.ot_name == origem.ot_name, "OT preservado");
  // O PB7 (Let's Go) nao tem campo de tracker — propriedade do formato, nao bug
  // nosso (registrado como P-01 da spec 078).
  if (A.game != savew::Game::kLGPE && B.game != savew::Game::kLGPE)
    Check(f.home_tracker != 0, "HOME tracker preservado na volta");
}

// ---------------------------------------------------------------------------
int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // A linha de base do guardrail, ANTES de tudo.
  std::vector<std::pair<std::string, std::string>> baseline;
  for (const Jogo& j : kJogos)
    baseline.emplace_back(j.nome, HashOf(CaminhoLimpo(j)));

  constexpr int kCiclos = 6;  // a spec exige no minimo 5

  std::printf("=== CICLO DE VIDA DO SAVE (spec 081) ===\n");
  std::printf("fixture: tests/saves-limpos/ (SOMENTE LEITURA, via SaveSandbox)\n");

  std::map<std::string, int> aguentou;
  for (const Jogo& j : kJogos) {
    TesteBackupERotacao(j);
    TesteIdempotencia(j, kCiclos);
    aguentou[j.nome] = TesteCiclos(j, kCiclos).ciclos;
    TesteRestauracao(j, kCiclos);
  }

  // Ida e volta: os pares que a spec 075 ja provou funcionarem, agora com
  // releitura do disco em cada etapa.
  TesteIdaEVolta(kJogos[0], kJogos[1]);  // swsh <-> sv
  TesteIdaEVolta(kJogos[1], kJogos[0]);  // sv <-> swsh
  TesteIdaEVolta(kJogos[2], kJogos[0]);  // bdsp <-> swsh
  TesteIdaEVolta(kJogos[4], kJogos[2]);  // lgpe <-> bdsp

  std::printf("\n=== CICLOS AGUENTADOS ===\n");
  for (const Jogo& j : kJogos)
    std::printf("  %-6s %d/%d\n", j.nome, aguentou[j.nome], kCiclos);

  std::printf("\n=== GUARDRAIL: tests/saves-limpos/ ===\n");
  for (const auto& par : baseline) {
    const Jogo* j = nullptr;
    for (const Jogo& x : kJogos)
      if (par.first == x.nome) j = &x;
    const std::string agora = HashOf(CaminhoLimpo(*j));
    Check(agora == par.second,
          par.first + ": sha256 inalterado (" + par.second.substr(0, 16) + "...)");
  }

  if (g_failures) {
    std::printf("\n%d FALHA(S)\n", g_failures);
    return 1;
  }
  std::printf("\nCICLO DE VIDA OK\n");
  return 0;
}
