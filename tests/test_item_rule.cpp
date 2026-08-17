// Testes da regra do item segurado (spec 072, G08).
//
//     "o HOME nao armazena itens — ao depositar, o item volta automaticamente
//      para a Bag do jogo de ORIGEM. Nunca viaja, nunca se perde"
//     — docs/pesquisa-pokemon-home.md §7
//
// GUARDRAIL: os saves do simulador sao do dono e SOMENTE LEITURA. Toda escrita
// acontece dentro de um SaveSandbox (spec 064).
//
// FORMATOS: SwSh e LGPE (spec 072, lista esparsa) + SV e BDSP (spec 074,
// tabela densa indexada pelo id). PLA fica de fora de proposito: Pokemon de
// Legends Arceus nao seguram item, entao o WithdrawHeldItem — a unica origem
// de escrita de bag no produto — nunca dispara la.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "bag_tables.h"
#include "bag_writer.h"
#include "save_sandbox.h"
#include "save_writer.h"

namespace fs = std::filesystem;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  } else {
    std::printf("  ok: %s\n", what.c_str());
  }
}

static std::vector<std::uint8_t> ReadFile(const std::string& path) {
  // std::filesystem::path: ifstream com std::string nao abre nome com
  // caractere especial no Windows.
  std::ifstream f(fs::path(path), std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

static bool WriteFile(const std::string& path,
                      const std::vector<std::uint8_t>& data) {
  std::ofstream f(fs::path(path), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(data.data()),
          static_cast<std::streamsize>(data.size()));
  return bool(f);
}

struct Caso {
  const char* nome;
  const char* rel;
  savew::Game jogo;
  std::size_t mons;       // contagem de Pokemon (oraculo: tools/pkhex-verify)
  std::size_t itens_bag;  // contagem de itens na bag (oraculo: tools/pkhex-bag)
};

// As contagens da bag saem de `tools/pkhex-bag dump`, somando as pouches que
// o bag_writer cobre (KeyItems e TMHMs ficam de fora — MaxCount=1, e Pokemon
// nao segura key item).
static const Caso kCasos[] = {
    // Shield: Medicine 36 + Balls 25 + BattleItems 10 + Berries 53 +
    //         Items 242 + Treasure 20 + Candy 25 = 411
    {"Shield", "01008DB008C2C000/Amaral/main", savew::Game::kSwSh, 194, 411},
    // LGPE: Medicine 22 + Candy 99 + ZCrystals 8 + Balls 14 +
    //       BattleItems 24 + Items 53 = 220
    {"LGPE", "0100187003A36000/Amaral/savedata.bin", savew::Game::kLGPE, 92, 220},
    // --- spec 074, tabela densa --------------------------------------------
    // Aqui a contagem inclui TODAS as pouches, inclusive KeyItems e TMHMs: a
    // leitura densa varre a tabela inteira e o slot nao diz a que pouch
    // pertence. Isso NAO abre caminho para escrever 999 num KeyItem — a
    // escrita e sempre de held item, e Pokemon nao segura key item.
    //
    // ATENCAO ao ler o oraculo: `pkhex-bag dump` lista uma linha por SLOT
    // LEGAL da pouch, inclusive os de contagem ZERO (item que o jogador nao
    // tem). O `ReadBag` devolve o que o jogador POSSUI. Os dois numeros sao
    // diferentes de proposito e a divergencia foi investigada, nao ajustada:
    //
    //   SV    1113 linhas no dump, das quais 820 com count > 0.
    //         (Medicine 25/27, Balls 23/28, BattleItems 9/9, Berries 48/53,
    //          Items 188/257, TMHMs 171/230, Treasure 16/16,
    //          Ingredients 142/166, KeyItems 17/67, Candy 181/260)
    //         No SV o slot nao-possuido e marcado com Pouch = 0xFFFFFFFF.
    {"SV", "0100A3D008C5C000/Amaral/main", savew::Game::kSV, 440, 820},
    //   BDSP  457 linhas no dump e 457 com count > 0 — todo item listado e
    //         possuido, entao aqui os dois numeros coincidem.
    {"BDSP", "0100000011D90000/Amaral/SaveData.bin", savew::Game::kBDSP, 255, 457},
};

static bool Denso(savew::Game g) {
  return g == savew::Game::kSV || g == savew::Game::kBDSP;
}

// Abre a copia do save no sandbox. nullopt se algo faltou.
struct Aberto {
  sandbox::SaveSandbox sb;
  std::vector<std::uint8_t> file;
  savew::SaveData sd;
};

static std::optional<Aberto> Abrir(const Caso& c) {
  auto sb = sandbox::SaveSandbox::Create(std::string(SIM_SAVES) + c.rel);
  if (!sb) {
    std::printf("FALHOU: %s: sandbox nao criado (save ausente?)\n", c.nome);
    ++g_failures;
    return std::nullopt;
  }
  auto file = ReadFile(sb->path());
  auto sd = savew::Load(file, c.jogo);
  if (!sd) {
    std::printf("FALHOU: %s: Load falhou\n", c.nome);
    ++g_failures;
    return std::nullopt;
  }
  return Aberto{std::move(*sb), std::move(file), std::move(*sd)};
}

// ---------------------------------------------------------------------------
// 1. LEITURA — a contagem tem de bater com o PkHeX. Divergiu, investiga; nao
//    ajusta o numero para passar.
// ---------------------------------------------------------------------------
static void TestLeitura(const Caso& c) {
  std::printf("[%s]\n", c.nome);
  auto a = Abrir(c);
  if (!a) return;

  Check(bagw::Supported(c.jogo), std::string(c.nome) + ": bag suportada");
  const auto bag = bagw::ReadBag(a->sd);
  Check(bag.size() == c.itens_bag,
        std::string(c.nome) + ": " + std::to_string(bag.size()) +
            " itens na bag (PkHeX: " + std::to_string(c.itens_bag) + ")");

  // Nenhum id duplicado: a bag do jogo nunca tem o mesmo item em dois slots.
  std::size_t dup = 0;
  for (std::size_t i = 0; i < bag.size(); ++i)
    for (std::size_t j = i + 1; j < bag.size(); ++j)
      if (bag[i].id == bag[j].id) ++dup;
  Check(dup == 0, std::string(c.nome) + ": nenhum item duplicado na leitura (" +
                      std::to_string(dup) + ")");

  // Todo item lido tem contagem > 0. E o que separa a nossa leitura das
  // linhas do dump do PkHeX, que incluem slot legal com contagem ZERO. Sem
  // esta trava a divergencia 1113 x 820 do SV poderia voltar disfarcada.
  std::size_t vazios = 0;
  for (const auto& it : bag)
    if (it.count == 0) ++vazios;
  Check(vazios == 0, std::string(c.nome) +
                         ": nenhum item com contagem zero na leitura (" +
                         std::to_string(vazios) + ")");

  // A regiao lida e a mesma que o Save() reescreve: sem patch, o roundtrip
  // continua byte-identico (o portao G03 nao pode regredir).
  Check(savew::Save(a->sd) == a->file,
        std::string(c.nome) + ": G03 preservado — sem patch, save identico");
}

// ---------------------------------------------------------------------------
// 2. ITEM QUE O JOGADOR JA TEM: incrementa o slot, nao cria duplicado.
// ---------------------------------------------------------------------------
static void TestIncrementaExistente(const Caso& c) {
  auto a = Abrir(c);
  if (!a) return;
  const auto bag = bagw::ReadBag(a->sd);
  if (bag.empty()) return;

  // Um item que ainda cabe: contagem abaixo do teto.
  bagw::Item alvo{};
  bool achou = false;
  for (const auto& it : bag)
    if (it.count > 0 && it.count < bagw::kMaxCount) { alvo = it; achou = true; break; }
  if (!achou) {
    std::printf("  N/A: %s: todo item ja esta no teto\n", c.nome);
    return;
  }

  Check(bagw::AddItemToBag(a->sd, alvo.id, 1),
        std::string(c.nome) + ": AddItemToBag aceita item existente");

  const auto depois = bagw::ReadBag(a->sd);
  Check(depois.size() == bag.size(),
        std::string(c.nome) + ": nenhum slot novo ocupado (" +
            std::to_string(depois.size()) + " vs " + std::to_string(bag.size()) + ")");
  Check(bagw::CountOf(a->sd, alvo.id) == alvo.count + 1,
        std::string(c.nome) + ": item " + std::to_string(alvo.id) + " foi de " +
            std::to_string(alvo.count) + " para " +
            std::to_string(bagw::CountOf(a->sd, alvo.id)));
}

// ---------------------------------------------------------------------------
// 3b. ITEM NOVO na TABELA DENSA (spec 074, TD-05).
//
//     O save de SV do dono tem as 1113 posicoes legais TODAS preenchidas, e o
//     de BDSP quase. O caminho de "item novo" — justamente o que precisa da
//     tabela de pouch e do SortOrder — nao tem fixture natural.
//
//     Pela regra da spec 066 ("campo que so tem fixture zerada nao esta
//     coberto"), deixar isso passar por omissao seria falso-verde. Entao o
//     teste FABRICA o caso: zera o slot de um item na copia em memoria, o que
//     o torna nao-possuido, e exige que o Add o reconstrua direito.
// ---------------------------------------------------------------------------
static void TestItemNovoDenso(const Caso& c) {
  if (!Denso(c.jogo)) return;
  auto a = Abrir(c);
  if (!a) return;

  const auto bag = bagw::ReadBag(a->sd);
  if (bag.empty()) return;

  // Zera o slot na COPIA do arquivo: e o mesmo efeito de "o jogador nunca
  // pegou este item". O id escolhido tem pouch conhecida na tabela gerada.
  const std::uint16_t alvo = bag[bag.size() / 2].id;
  const std::size_t base =
      c.jogo == savew::Game::kBDSP ? savew::kBdspBagOffset : 0;

  if (c.jogo == savew::Game::kBDSP) {
    std::vector<std::uint8_t> f = a->file;
    std::fill(f.begin() + base + alvo * 16, f.begin() + base + alvo * 16 + 16, 0);
    auto sd = savew::Load(f, c.jogo);
    if (!sd) { std::printf("FALHOU: %s: save fabricado nao abre\n", c.nome); ++g_failures; return; }

    Check(bagw::CountOf(*sd, alvo) == 0,
          std::string(c.nome) + ": item " + std::to_string(alvo) +
              " fabricado como ausente (slot zerado)");
    Check(bagw::AddItemToBag(*sd, alvo, 1),
          std::string(c.nome) + ": AddItemToBag aceita item novo");
    Check(bagw::CountOf(*sd, alvo) == 1,
          std::string(c.nome) + ": item novo entrou com quantidade 1");
    Check(bagw::ReadBag(*sd).size() == bag.size(),
          std::string(c.nome) + ": a bag voltou ao tamanho original (" +
              std::to_string(bagw::ReadBag(*sd).size()) + ")");

    // O CAMPO QUE DECIDE se o item aparece: SortOrder = 0 e "nao adquirido",
    // e o item some da bag mesmo com contagem gravada.
    const auto reg = savew::BagRegion(*sd);
    const std::uint16_t so =
        std::uint16_t(reg[alvo * 16 + 12]) | std::uint16_t(reg[alvo * 16 + 13] << 8);
    Check(so != 0, std::string(c.nome) + ": SortOrder != 0 no item novo (" +
                       std::to_string(so) + ") — sem isso o item some da bag");
    return;
  }

  // SV: o campo que decide e a POUCH, gravada no proprio slot.
  std::vector<std::uint8_t> f = a->file;
  auto sd0 = savew::Load(f, c.jogo);
  if (!sd0) return;
  // Marca o slot como invalido do jeito que o SV marca: Pouch = 0xFFFFFFFF.
  std::uint8_t zerado[16] = {0xFF, 0xFF, 0xFF, 0xFF};
  sd0->bag_patches.push_back({std::size_t(alvo) * 16, {zerado, zerado + 16}});

  Check(bagw::CountOf(*sd0, alvo) == 0,
        std::string(c.nome) + ": item " + std::to_string(alvo) +
            " fabricado como ausente (Pouch=0xFFFFFFFF)");
  Check(bagw::AddItemToBag(*sd0, alvo, 1),
        std::string(c.nome) + ": AddItemToBag aceita item novo");
  Check(bagw::CountOf(*sd0, alvo) == 1,
        std::string(c.nome) + ": item novo entrou com quantidade 1");

  const auto reg = savew::BagRegion(*sd0);
  const std::uint32_t pouch = std::uint32_t(reg[alvo * 16]) |
                              (std::uint32_t(reg[alvo * 16 + 1]) << 8) |
                              (std::uint32_t(reg[alvo * 16 + 2]) << 16) |
                              (std::uint32_t(reg[alvo * 16 + 3]) << 24);
  // O ORACULO e a tabela gerada do PkHeX, nao o valor que estava la antes.
  const std::uint8_t esperado = pokehome::bag::SvPouchOf(alvo);
  Check(pouch == esperado,
        std::string(c.nome) + ": item novo recebeu a pouch da tabela (" +
            std::to_string(pouch) + ", PkHeX diz " +
            std::to_string(esperado) + ")");

  const std::uint32_t flags = std::uint32_t(reg[alvo * 16 + 8]);
  Check(flags == 5, std::string(c.nome) +
                        ": Flags=5 (IsNew|IsObtained) como o PkHeX grava, veio " +
                        std::to_string(flags));

  // Item que o SV nao conhece e RECUSADO, em vez de gravado numa pouch
  // inventada. Sem isto, o campo Pouch viraria 0 = Medicine para qualquer id.
  std::uint16_t ilegal = 0;
  for (std::uint16_t id = 1; id < pokehome::bag::kSvMaxItem && ilegal == 0; ++id)
    if (pokehome::bag::SvPouchOf(id) == pokehome::bag::kPouchNone) ilegal = id;
  if (ilegal != 0)
    Check(!bagw::AddItemToBag(*sd0, ilegal, 1),
          std::string(c.nome) + ": item " + std::to_string(ilegal) +
              " (fora da tabela do SV) e RECUSADO");
}

// ---------------------------------------------------------------------------
// 3. ITEM NOVO: ocupa um slot vazio.
// ---------------------------------------------------------------------------
static void TestItemNovo(const Caso& c) {
  // Na tabela densa nao existe "slot vazio a ocupar": o id E o endereco.
  // O caso equivalente esta no TestItemNovoDenso.
  if (Denso(c.jogo)) return;
  auto a = Abrir(c);
  if (!a) return;
  const auto bag = bagw::ReadBag(a->sd);

  // Um id que o jogador NAO tem. Varre a faixa de itens comuns.
  std::uint16_t novo = 0;
  for (std::uint16_t id = 1; id < 1000 && novo == 0; ++id) {
    bool tem = false;
    for (const auto& it : bag)
      if (it.id == id) { tem = true; break; }
    if (!tem) novo = id;
  }
  if (novo == 0) {
    std::printf("  N/A: %s: o jogador tem todos os ids da faixa\n", c.nome);
    return;
  }

  Check(bagw::CountOf(a->sd, novo) == 0,
        std::string(c.nome) + ": item " + std::to_string(novo) +
            " ausente antes");
  Check(bagw::AddItemToBag(a->sd, novo, 1),
        std::string(c.nome) + ": AddItemToBag aceita item novo");
  Check(bagw::CountOf(a->sd, novo) == 1,
        std::string(c.nome) + ": item novo entrou com quantidade 1");
  Check(bagw::ReadBag(a->sd).size() == bag.size() + 1,
        std::string(c.nome) + ": exatamente UM slot novo ocupado");
}

// ---------------------------------------------------------------------------
// 4. LIMITE: a quantidade nao passa de kMaxCount (999, fonte:
//    InventoryPouch.MaxCount do PkHeX 25.12.21).
// ---------------------------------------------------------------------------
static void TestLimite(const Caso& c) {
  auto a = Abrir(c);
  if (!a) return;
  const auto bag = bagw::ReadBag(a->sd);
  if (bag.empty()) return;

  const std::uint16_t alvo = bag[0].id;
  Check(bagw::AddItemToBag(a->sd, alvo, bagw::kMaxCount),
        std::string(c.nome) + ": AddItemToBag com quantidade que estoura");
  Check(bagw::CountOf(a->sd, alvo) == bagw::kMaxCount,
        std::string(c.nome) + ": saturou em " +
            std::to_string(bagw::kMaxCount) + ", ficou " +
            std::to_string(bagw::CountOf(a->sd, alvo)));

  // Somar de novo nao passa do teto.
  Check(bagw::AddItemToBag(a->sd, alvo, 5), std::string(c.nome) + ": soma no teto");
  Check(bagw::CountOf(a->sd, alvo) == bagw::kMaxCount,
        std::string(c.nome) + ": continua no teto");
}

// ---------------------------------------------------------------------------
// 5. A REGRA. Deposito: o item sai do Pokemon E entra na bag deste save.
// ---------------------------------------------------------------------------
static void TestDeposito(const Caso& c) {
  auto a = Abrir(c);
  if (!a) return;

  // Um Pokemon que segure item. Se nenhum segurar, planta um — o que importa
  // e a regra, e o save do dono pode nao ter o caso.
  std::size_t bi = 0, si = 0;
  bool achou = false;
  for (std::size_t b = 0; b < a->sd.box_count && !achou; ++b)
    for (std::size_t s = 0; s < a->sd.slots_per_box && !achou; ++s)
      if (a->sd.At(b, s).present && a->sd.At(b, s).mon.held_item != 0) {
        bi = b; si = s; achou = true;
      }

  if (!achou) {
    for (std::size_t b = 0; b < a->sd.box_count && !achou; ++b)
      for (std::size_t s = 0; s < a->sd.slots_per_box && !achou; ++s)
        if (a->sd.At(b, s).present) { bi = b; si = s; achou = true; }
    if (!achou) {
      std::printf("  N/A: %s: caixas vazias\n", c.nome);
      return;
    }
    pkm::Pokemon m = a->sd.At(bi, si).mon;
    m.held_item = 1;  // Master Ball — id 1, existe nos dois jogos
    a->sd.Set(bi, si, m);
    std::printf("  (nenhum Pokemon segurava item; plantado item 1 em %zu/%zu)\n",
                bi, si);
  }

  const std::uint16_t item = a->sd.At(bi, si).mon.held_item;
  const std::uint16_t antes = bagw::CountOf(a->sd, item);

  Check(bagw::WithdrawHeldItem(a->sd, bi, si),
        std::string(c.nome) + ": WithdrawHeldItem no Pokemon com item " +
            std::to_string(item));
  Check(a->sd.At(bi, si).mon.held_item == 0,
        std::string(c.nome) + ": held_item do Pokemon zerou");
  const std::uint16_t depois = bagw::CountOf(a->sd, item);
  Check(depois == antes + 1 || (antes == bagw::kMaxCount && depois == antes),
        std::string(c.nome) + ": bag foi de " + std::to_string(antes) +
            " para " + std::to_string(depois));

  // Sem item, nao ha o que devolver — e isso NAO e sucesso silencioso.
  Check(!bagw::WithdrawHeldItem(a->sd, bi, si),
        std::string(c.nome) + ": Withdraw de novo recusa (nao segura nada)");

  // ROUNDTRIP: o save alterado grava, reabre e mantem a contagem de Pokemon.
  const auto escrito = savew::Save(a->sd);
  Check(escrito.size() == a->file.size(),
        std::string(c.nome) + ": o tamanho do arquivo nao mudou");
  Check(WriteFile(a->sb.path(), escrito),
        std::string(c.nome) + ": gravado na COPIA do sandbox");

  auto relido = savew::Load(ReadFile(a->sb.path()), c.jogo);
  if (!relido) {
    std::printf("FALHOU: %s: o save alterado nao reabre\n", c.nome);
    ++g_failures;
    return;
  }
  Check(relido->Count() == c.mons,
        std::string(c.nome) + ": contagem de Pokemon inalterada (" +
            std::to_string(relido->Count()) + ")");
  Check(relido->At(bi, si).mon.held_item == 0,
        std::string(c.nome) + ": o Pokemon relido nao segura mais nada");
  Check(bagw::CountOf(*relido, item) == depois,
        std::string(c.nome) + ": a bag relida tem a quantidade nova");
}

// ---------------------------------------------------------------------------
// 6. VIOLACAO PLANTADA. Um gate verde nao prova que o recalculo de integridade
//    existe — a prova e quebra-lo e exigir o VERMELHO.
//
//    SwSh: o SHA256 dos 32 bytes finais. O nosso proprio Load recusa.
//    LGPE: o PkHeX NAO valida integridade na leitura (contexto-tecnico.md),
//          entao a prova tem de ser a CONFERENCIA INTERNA — o CRC gravado
//          contra o recalculado sobre o bloco da bag.
// ---------------------------------------------------------------------------
static void TestViolacaoPlantada(const Caso& c) {
  auto a = Abrir(c);
  if (!a) return;
  const auto bag = bagw::ReadBag(a->sd);
  if (bag.empty()) return;
  if (!bagw::AddItemToBag(a->sd, bag[0].id, 1)) return;
  const auto gravado = savew::Save(a->sd);
  if (gravado.empty()) return;

  if (c.jogo == savew::Game::kSwSh || c.jogo == savew::Game::kSV) {
    // Com a integridade intacta, abre.
    Check(savew::Load(gravado, c.jogo).has_value(),
          std::string(c.nome) + ": o save com bag editada abre normalmente");
    std::vector<std::uint8_t> quebrado = gravado;
    quebrado[quebrado.size() - 1] ^= 0xFF;
    Check(!savew::Load(quebrado, c.jogo).has_value(),
          std::string(c.nome) +
              ": VIOLACAO PLANTADA — hash adulterado e RECUSADO");
    return;
  }

  // BDSP: o PkHeX NAO valida integridade na leitura (contexto-tecnico.md), e o
  // nosso Load tambem nao — ele reconhece o BDSP pelo TAMANHO. Entao nao existe
  // juiz que recuse, nem externo nem interno-por-Load: a prova tem de ser a
  // conferencia do digest gravado contra o recalculado.
  //
  // O digest e o MD5 do arquivo INTEIRO com os proprios 16 bytes zerados
  // (0xE9818), ja implementado desde a spec 068 — a bag entra antes dele.
  if (c.jogo == savew::Game::kBDSP) {
    const auto md5_de = [](const std::vector<std::uint8_t>& f) {
      std::vector<std::uint8_t> tmp = f;
      std::fill(tmp.begin() + 0xE9818, tmp.begin() + 0xE9818 + 16, 0);
      std::vector<std::uint8_t> d(16);
      savew::Md5(tmp.data(), tmp.size(), d.data());
      return d;
    };
    const auto md5_gravado = [](const std::vector<std::uint8_t>& f) {
      return std::vector<std::uint8_t>(f.begin() + 0xE9818,
                                       f.begin() + 0xE9818 + 16);
    };

    Check(md5_de(gravado) == md5_gravado(gravado),
          std::string(c.nome) +
              ": o MD5 gravado bate com o recalculado (bag editada)");

    // A violacao: mexer num byte da BAG sem recalcular o digest.
    std::vector<std::uint8_t> quebrado = gravado;
    quebrado[savew::kBdspBagOffset + 1] ^= 0x08;
    Check(md5_de(quebrado) != md5_gravado(quebrado),
          std::string(c.nome) +
              ": VIOLACAO PLANTADA — bag alterada sem recalculo tem MD5 "
              "DIVERGENTE");
    // E o registro do que o juiz externo NAO faz: o save corrompido ainda abre.
    Check(savew::Load(quebrado, c.jogo).has_value(),
          std::string(c.nome) +
              ": (registro) o save com MD5 quebrado AINDA ABRE — por isso a "
              "prova e interna");
    return;
  }

  // LGPE: conferencia interna do CRC do bloco da bag.
  const auto crc_de = [](const std::vector<std::uint8_t>& f) {
    return savew::Crc16Arc(f.data() + savew::kLgpeBagOffset,
                           savew::kLgpeBagBlockSize);
  };
  const auto crc_gravado = [](const std::vector<std::uint8_t>& f) {
    return std::uint16_t(f[savew::kLgpeBagChecksum] |
                         (f[savew::kLgpeBagChecksum + 1] << 8));
  };

  Check(crc_de(gravado) == crc_gravado(gravado),
        std::string(c.nome) + ": o CRC da bag gravado bate com o recalculado");

  // A violacao: mexer num byte da bag SEM recalcular o CRC.
  std::vector<std::uint8_t> quebrado = gravado;
  quebrado[savew::kLgpeBagOffset + 1] ^= 0x08;
  Check(crc_de(quebrado) != crc_gravado(quebrado),
        std::string(c.nome) +
            ": VIOLACAO PLANTADA — bag alterada sem recalculo tem CRC "
            "DIVERGENTE (calc 0x" +
            std::to_string(crc_de(quebrado)) + " vs gravado 0x" +
            std::to_string(crc_gravado(quebrado)) + ")");
}

// ---------------------------------------------------------------------------
// 7. Os jogos SEM suporte recusam explicitamente, em vez de fingir que deu.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 8. BAG CHEIA (spec 076 — pendencia 3 da spec 072).
//
//    `AddItemToBag` recusa item NOVO quando nao ha slot vazio. Nenhuma
//    fixture exercitava isso: as bags reais do dono tem folga, entao o `return
//    false` do "sem slot vazio" nunca rodava. Pela regra da spec 066, esse
//    caminho NAO estava coberto por mais verde que a suite estivesse.
//
//    So vale para o layout ESPARSO (SwSh/LGPE): na tabela densa o slot de cada
//    item e fixo pelo id, entao "bag cheia" nao existe como estado — nao ha
//    disputa por slot. Fingir o contrario ali seria inventar um caso.
//
//    A fixture e sintetica e mora EM MEMORIA, na copia do sandbox: enche a bag
//    pela propria API publica, com ids distintos, ate ela recusar. Nao ha
//    escrita em save real em momento nenhum.
// ---------------------------------------------------------------------------
static void TestBagCheia(const Caso& c) {
  if (Denso(c.jogo)) return;  // ver o cabecalho: nao se aplica
  auto a = Abrir(c);
  if (!a) return;

  const auto bag_inicial = bagw::ReadBag(a->sd);
  const std::size_t inicio = bag_inicial.size();

  // A TESTEMUNHA: o primeiro item da bag, com a contagem que ele tem agora.
  // Se a recusa der lugar a um "sobrescreve o slot 0" — o jeito mais provavel
  // de esta regra quebrar, e mais perigoso que recusar — este item some ou
  // troca de identidade. Guardar antes e conferir depois e o que transforma
  // um travamento em um vermelho que NOMEIA a regra.
  if (bag_inicial.empty()) return;
  const bagw::Item testemunha = bag_inicial[0];

  // Enche pela API publica. Cada id e NOVO (nao esta na bag), entao cada
  // aceite consome exatamente um slot vazio.
  //
  // O TETO E APERTADO de proposito: a soma dos slots das pouches esparsas e
  // 940 (SwSh) / 760 (LGPE), entao 1200 aceites ja provam que a bag deixou de
  // recusar. Um teto folgado transformaria a violacao plantada num
  // TRAVAMENTO — vermelho que nao diz qual regra quebrou. Assim o teste
  // falha rapido e nomeando.
  //
  // CUSTO: `AddItemToBag` chama `BagRegion`, que no SwSh DECIFRA o save de
  // 1,5 MB inteiro a cada chamada. Por isso os ids ja presentes sao
  // descartados por uma lista lida UMA vez, em vez de um `CountOf` por
  // candidato — que dobraria as decifragens e levava o teste a mais de um
  // minuto.
  // `vector<char>`, NAO `vector<bool>`: com o `vector<bool>` (a especializacao
  // de bits) este binario passou a morrer ANTES do main, exit -1 e zero byte
  // em stdout/stderr, com o mesmo codigo em volta. Trocado por char, roda.
  // Registrado aqui porque o sintoma nao aponta para a causa.
  std::vector<char> ja_tem(0x8000, 0);
  for (const auto& it : bag_inicial) {
    if (it.id < ja_tem.size()) ja_tem[it.id] = 1;
  }

  const std::size_t kMaxAceites = 1200;
  std::size_t aceitos = 0;
  std::uint16_t id = 0;
  bool recusou = false;
  for (std::uint32_t cand = 1; cand <= 0x7FFF && aceitos < kMaxAceites; ++cand) {
    const std::uint16_t c16 = static_cast<std::uint16_t>(cand);
    if (ja_tem[c16]) continue;  // ja existe: nao e slot novo
    if (bagw::AddItemToBag(a->sd, c16, 1)) {
      ++aceitos;
      continue;
    }
    // Recusou um item NOVO: e o caminho da bag cheia.
    id = c16;
    recusou = true;
    break;
  }

  Check(recusou,
        std::string(c.nome) + ": a bag encheu e AddItemToBag RECUSOU o item " +
            std::to_string(id) + " (aceitou " + std::to_string(aceitos) +
            " antes; teto do teste " + std::to_string(kMaxAceites) + ")");

  // A TESTEMUNHA continua intacta: encher a bag nao pode ter sobrescrito o
  // item que o jogador ja tinha. Este assert e o que distingue "recusou" de
  // "aceitou destruindo dado" — os dois deixam a bag cheia, so um perde item.
  Check(bagw::CountOf(a->sd, testemunha.id) >= testemunha.count,
        std::string(c.nome) + ": o item testemunha (id " +
            std::to_string(testemunha.id) + ") sobreviveu — tinha " +
            std::to_string(testemunha.count) + ", tem " +
            std::to_string(bagw::CountOf(a->sd, testemunha.id)));

  if (!recusou) return;

  // A recusa nao pode ser "recusa tudo": o item que JA esta na bag continua
  // aceito, porque incrementar slot existente nao precisa de slot vazio. Sem
  // este assert, um `return false` no topo da funcao passaria no teste acima.
  const auto bag = bagw::ReadBag(a->sd);
  bool achou_incrementavel = false;
  for (const auto& it : bag) {
    if (it.count > 0 && it.count < bagw::kMaxCount) {
      const std::uint16_t antes = bagw::CountOf(a->sd, it.id);
      Check(bagw::AddItemToBag(a->sd, it.id, 1),
            std::string(c.nome) +
                ": com a bag CHEIA, item que ja existe ainda e aceito (id " +
                std::to_string(it.id) + ")");
      Check(bagw::CountOf(a->sd, it.id) == antes + 1,
            std::string(c.nome) + ": e a contagem subiu de " +
                std::to_string(antes) + " para " +
                std::to_string(bagw::CountOf(a->sd, it.id)));
      achou_incrementavel = true;
      break;
    }
  }
  Check(achou_incrementavel,
        std::string(c.nome) +
            ": havia item incrementavel para provar que a recusa e seletiva");

  // A bag cheia nao pode ter PERDIDO item do jogador: os que ja existiam
  // continuam la. Um Add que sobrescrevesse slot ocupado passaria nos asserts
  // acima e seria destrutivo.
  Check(bag.size() >= inicio,
        std::string(c.nome) + ": nenhum item original sumiu (" +
            std::to_string(inicio) + " -> " + std::to_string(bag.size()) + ")");

  // E o save resultante ainda e gravavel e reabre: encher a bag nao produz
  // arquivo corrompido.
  const auto gravado = savew::Save(a->sd);
  Check(!gravado.empty() && savew::Load(gravado, c.jogo).has_value(),
        std::string(c.nome) + ": o save com a bag cheia grava e reabre");
}

static void TestNaoSuportados() {
  std::printf("[nao suportados]\n");
  // PLA e o unico que sobra, e por DECISAO de escopo (spec 074), nao por
  // dificuldade: a bag dele e a mais simples das tres, mas Pokemon de Legends
  // Arceus nao seguram item — o WithdrawHeldItem nunca dispara la.
  Check(!bagw::Supported(savew::Game::kPLA),
        "Legends Arceus: bag NAO suportada — nao ha held item em PLA (spec 074)");
}

int main() {
  for (const Caso& c : kCasos) {
    TestLeitura(c);
    TestIncrementaExistente(c);
    TestItemNovo(c);
    TestItemNovoDenso(c);
    TestLimite(c);
    TestDeposito(c);
    TestViolacaoPlantada(c);
    TestBagCheia(c);  // spec 076
  }
  TestNaoSuportados();

  if (g_failures) {
    std::printf("\n%d FALHA(S)\n", g_failures);
    return 1;
  }
  std::printf("\ntodos os testes passaram\n");
  return 0;
}
