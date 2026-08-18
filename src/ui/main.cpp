// NestBox — interface grafica.
//
// Tela principal: dois paineis de caixa lado a lado, no layout NestBox
// (ver layout/NestBox - Telas de Console.dc.html e a spec 007).
//
// Estrutura vertical: barra superior (96) / paineis (flex) / rodape de stats
// (152) / barra de botoes (72).

#include <borealis.hpp>
// Decodificacao de PNG para tint de Selector em runtime (spec 084). So a
// declaracao — a implementacao ja e compilada dentro do nanovg (borealis).
#include <borealis/extern/nanovg/stb_image.h>

#include <cmath>
#include <cctype>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// dirent tambem no PC: a listagem de backups (spec 032) roda nas duas
// plataformas, e o MinGW oferece a mesma API.
#include <dirent.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "device_saves.h"
#include "box_move.h"
#include "box_sort.h"
#include "dex_count.h"
#include "game_moves.h"
#include "game_species.h"
#include "save_backup.h"
#include "nestbox_ab.h"
#include "nestbox_file.h"
#include "ability_names.h"  // nomes de habilidade modernos (spec 099)
#include "gen9_base_stats.h"  // kTypeIds para os icones de tipo (spec 099)
#include "item_names.h"     // nomes de item modernos (spec 103)
#include "move_names.h"     // nome de cada golpe ate a gen9 (spec 106)
#include "move_types.h"     // tipo de cada golpe, para os icones (spec 103)
#include "summary_facts.h"  // characteristic e memo do summary (spec 103)
#include "gen3_save.h"
#include "za_save.h"
#include "legality.h"
#include "modern_box_view.h"
#include "nlog.h"
#include "save_writer.h"
#include "updater.h"

namespace g3 = pokehome::gen3;
namespace bx = pokehome::box;
namespace dx = pokehome::dex;
namespace nest = pokehome::nest;
namespace bk = pokehome::backup;
namespace cp = pokehome::compat;
namespace vw = pokehome::view;

namespace {

constexpr int kCols = 6;
constexpr int kRows = 5;
// Slots por caixa: a grade 6x5, igual ao PC dos jogos (§2 da pesquisa).
constexpr std::size_t kSlotsPerBox = kCols * kRows;
// Caixas do NestBox. A capacidade e DERIVADA — antes era um 200 solto que nao
// batia com as 14 caixas herdadas do gen3, e a spec 021 tornou possivel mover
// Pokemon para slots fora da capacidade declarada (ver spec 022).
// 400 caixas desde a spec 090 — numero fixo, como o HOME. Nao cresce sob
// demanda: o banco e alocado inteiro no MakeEmpty e o offset de cada slot sai
// de aritmetica pura.
constexpr std::size_t kNestBoxBoxes = 400;
constexpr std::size_t kNestBoxCapacity = kNestBoxBoxes * kSlotsPerBox;

// A trava que a spec 022 existe para criar: capacidade e numero de caixas nao
// podem divergir de novo. Em vez de teste em runtime (as constantes vivem
// aqui, fora do pokehome_core que o ctest linka), o compilador cobra.
static_assert(kNestBoxCapacity == kNestBoxBoxes * kSlotsPerBox,
              "capacidade do NestBox precisa ser derivada do numero de caixas");
static_assert(kSlotsPerBox == static_cast<std::size_t>(kCols * kRows),
              "a grade desenhada e os slots por caixa precisam bater");

// --- Paleta (extraida do layout) -------------------------------------------

// Extraidas da captura do Home no console: o topo puxa para o amarelo-esverdeado
// e a base para o verde-agua. As cores antigas vinham do HTML de layout e eram
// claras demais perto da referencia.
const NVGcolor kBgTop = nvgRGB(0xE4, 0xEF, 0xB8);
const NVGcolor kBgMid = nvgRGB(0xB8, 0xE4, 0xB4);
const NVGcolor kBgBottom = nvgRGB(0x8E, 0xDC, 0xC4);
const NVGcolor kTextPrimary = nvgRGB(0x3F, 0x4F, 0x45);
const NVGcolor kTextSecondary = nvgRGB(0x5D, 0x6D, 0x63);
const NVGcolor kTextTertiary = nvgRGB(0x7D, 0x8C, 0x83);
const NVGcolor kAccent = nvgRGB(0xE8, 0x85, 0x3F);
const NVGcolor kDarkBar = nvgRGB(0x3F, 0x4F, 0x45);
const NVGcolor kBubble = nvgRGB(0x33, 0x40, 0x3A);
// Faixa de nome do cursor da caixa (spec 089). MEDIDA na captura do HOME a
// 1280x720 (scratchpad/switch-album/home-10.jpg, coluna x=200): cinza NEUTRO
// #6B6B6B, e nao o esverdeado da bolha do resto do app.
//
// O alpha vem de um palpite calibrado: no print o cinza aparece opaco sobre o
// fundo claro da caixa, mas com leve variacao entre amostras (0x68..0x6F),
// o que indica translucidez pequena. 0xE8 reproduz isso.
const NVGcolor kCursorBar = nvgRGBA(0x6B, 0x6B, 0x6B, 0xE8);
const NVGcolor kWhite = nvgRGB(0xFF, 0xFF, 0xFF);

// Moldura da caixa (spec 046). Valores dados pelo dono, nao amostrados da
// captura: o cartao troca de cor conforme o cursor esta nele ou no outro.
// Cor chapada nos dois — a referencia nao tem degrade dentro do cartao.
const NVGcolor kBoxBodyOff = nvgRGB(0xEA, 0xFF, 0xE1);
const NVGcolor kBoxBodyOn = nvgRGB(0xFE, 0xFF, 0xF5);
const NVGcolor kBoxBarOff = nvgRGB(0x70, 0xC5, 0xAD);
const NVGcolor kBoxBarOn = nvgRGB(0x1F, 0x9F, 0x9E);
const NVGcolor kBoxBorderOn = nvgRGB(0x33, 0xC6, 0xCD);
// Slot vazio: circulo suave com a borda esfumada — na referencia ele quase
// some no fundo, nao e forma solida. O centro acompanha o estado do PAINEL
// (cores dadas pelo dono), porque o corpo do cartao tambem muda com o foco.
const NVGcolor kSlotTintOn = nvgRGB(0xD7, 0xFF, 0xEF);
const NVGcolor kSlotTintOff = nvgRGB(0xDD, 0xF7, 0xDD);
// Cursor do HOME: a seta vermelha e o filete da barra de nome (rodada 5).
const NVGcolor kCursorRed = nvgRGB(0xE0, 0x44, 0x2C);
// A cor da faixa de modos saiu daqui na spec 094: agora sao duas
// (#8FCFA8 nas pontas, #7FC79C no miolo), declaradas no proprio draw() do
// ModeStrip junto das paradas do degrade que as usam.
// Largura dos blocos laterais da barra superior. O da esquerda tem o icone e
// o titulo; o da direita e vazio, so para equilibrar e centrar os modos.
constexpr float kTopBarSideBlock = 240.0f;
// Rotulo dos campos da barra de status: azulado, como na referencia.
const NVGcolor kStatLabel = nvgRGB(0x4A, 0x8C, 0xA8);
// Fundo da faixa de identificacao da barra de status (referencia do HOME).
const NVGcolor kStatusHeadBg = nvgRGB(0x38, 0xB6, 0xAB);   // header do cartao (dono, 2026-08-17)
const NVGcolor kStatusBodyBg = nvgRGB(0xEA, 0xF9, 0xF2);   // corpo do cartao
const NVGcolor kStatusChipBg = nvgRGB(0xDA, 0xF3, 0xEF);   // tarja dos rotulos

// Medidas fixas da caixa (specs 048 e 101). Os dois paineis sao IDENTICOS e
// nao dependem do espaco disponivel: slot, sprite e grade tem tamanho em pixel.
// Antes tudo era flex e o painel do save saia maior que o do NestBox.
//
// A DERIVACAO INVERTEU na spec 101. Ate aqui o painel saia do slot
// (kPanelWidth = kCols * (kSlotW + gap) + pad); agora o dono especificou o
// CARTAO — 595 x 507, faixas de 60 em cima e embaixo — e o slot e 70 x 70
// fixo. Quem sobra e o VAO, calculado do espaco que resta. Escrever assim
// mantem o mockup como fonte da verdade: mexer no slot nao estoura o cartao,
// so aperta ou folga o vao.
constexpr float kPanelWidth = 595.0f;
constexpr float kPanelHeight = 507.0f;
constexpr float kPanelRadius = 24.0f;
// Faixas de cima (titulo) e de baixo (rodape). Mesma altura: no mockup elas
// sao espelho uma da outra, com o raio nos cantos externos.
constexpr float kPanelBarHeight = 60.0f;
constexpr float kPanelFooterHeight = 60.0f;
// Borda de selecao. Em foco ela e desenhada para DENTRO — o cartao continua
// 595 x 507 no total e o painel vizinho nao e empurrado (decisao do dono).
constexpr float kPanelBorderOn = 8.0f;
constexpr float kPanelBorderOff = 0.0f;

constexpr float kPanelPad = 14.0f;
// Slot QUADRADO (spec 101). Era deitado (81 x 62) desde a spec 048, quando a
// largura da tela e que mandava; com o cartao especificado em pixel, o dono
// pediu 70 x 70 e o resto virando vao.
constexpr float kSlotW = 70.0f;
constexpr float kSlotH = 70.0f;
constexpr float kSpriteSize = 56.0f;  // arte do Pokemon dentro da celula

// Icones de aviso do slot (spec 102). Dois quadrados de 24 x 24: o da ESQUERDA
// avisa golpe ausente no jogo do painel, o da DIREITA avisa que o Pokemon nao
// vai sair dali. Substituem o esmaecimento e as bordas coloridas.
//
// As cotas do dono medem da borda EXTERNA do slot ate a face INTERNA do
// quadrado (a voltada para o centro), espelhadas; as verticais medem de baixo
// ate o TOPO. Dai as contas abaixo — deixadas a vista para o proximo ajuste do
// mockup ser uma troca de numero, nao uma reengenharia.
//
// O `- 10` e correcao pedida pelo dono ao ver na tela: na arte final os dois
// ficavam baixos demais sobre o sprite. Sobe os DOIS igual, preservando os 5px
// de desnivel entre eles que o mockup define.
constexpr float kSlotIcon = 24.0f;
// Golpe ausente na barra de status (spec 104): o icone amarelo fica com a
// altura do texto (fonte 18) e pisca num ciclo continuo "bem lento" (pedido
// do dono) — ~2,4s a 60fps. Numeros isolados para o ajuste fino da tela.
constexpr float kMoveWarnIconSize = 18.0f;
constexpr float kMoveWarnBlinkFrames = 144.0f;
constexpr float kSlotIconLeftX = 22.0f - kSlotIcon;   // -2: vaza 2px a esquerda
constexpr float kSlotIconLeftY = kSlotH - 52.0f - 10.0f;   // 8
constexpr float kSlotIconRightX = kSlotW - 30.0f;          // 40
constexpr float kSlotIconRightY = kSlotH - 47.0f - 10.0f;  // 13

// Area util da grade: o miolo entre as duas faixas, menos o padding lateral.
constexpr float kGridWidth = kPanelWidth - kPanelPad * 2;
constexpr float kGridHeight =
    kPanelHeight - kPanelBarHeight - kPanelFooterHeight;
// Vao DERIVADO: o que sobra depois de assentar os slots, dividido igualmente.
// Dá 24,5px na horizontal e 7,4px na vertical com a grade 6x5 de hoje.
constexpr float kSlotGapX = (kGridWidth - kCols * kSlotW) / kCols;
constexpr float kSlotGapY = (kGridHeight - kRows * kSlotH) / kRows;
static_assert(kSlotGapX > 0.0f && kSlotGapY > 0.0f,
              "a grade nao cabe no cartao: reduza o slot ou aumente o painel");

// Espaco entre a barra superior da tela e o topo dos paineis.
// 22 -80% (rodada 3) -50% (rodada 4): os cartoes encostam na barra.
constexpr float kPanelTopGap = 2.2f;
// Margem lateral da tela e vao entre os dois paineis.
constexpr float kScreenSideMargin = 40.0f;
constexpr float kPanelGap = 24.0f;
// Folga dos controles ate a borda do cabecalho (pedido do dono).
constexpr float kHeaderSidePad = 16.0f;
// Quadrado reservado para a logo do jogo no rodape do painel. A imagem ainda
// nao existe como asset — o BoxFrame desenha um quadrado branco no lugar.
constexpr float kFooterLogoBox = 46.0f;
// Capa do jogo no rodape do cartao (spec 101): 42 x 42, raio 8. Menor que o
// kFooterLogoBox que o LAYOUT reserva — a linha do rodape continua com 46 para
// o numero e a pilula ficarem na mesma altura de antes.
constexpr float kFooterCover = 42.0f;
constexpr float kFooterCoverRadius = 8.0f;
// Distancia entre a logo e a numeracao da caixa (pedido do dono).
constexpr float kFooterLogoGap = 32.0f;

// Glifos dos botoes do Switch. Vem de switch_icons.ttf, que o borealis carrega
// como fallback da fonte normal — basta escrever o codepoint num Label.
// O mapa completo esta em borealis/library/lib/views/hint.cpp.
// Ombros L e R na variante OUTLINE (U+E0A4, U+E0A5). Os de
// hint.cpp:105-108 (U+E0E4/E0E5) sao os solidos, com a tecla
// branca preenchida — o HOME usa o contorno.
//
// As setas do cabecalho da caixa sao CHEVRONS (U+E149, U+E14A), nao os
// glifos de d-pad (U+E0ED/E0EE): estes tem o disco branco em volta, que o
// HOME nao usa no cabecalho. Achados varrendo o switch_icons.ttf e
// renderizando os candidatos - a fonte nao tem nome descritivo de glifo.
constexpr const char* kGlyphChevronLeft = "";
constexpr const char* kGlyphChevronRight = "";
constexpr const char* kGlyphL = "";
// ZL e ZR outline, do mesmo conjunto E0A0 dos ombros L/R.
constexpr const char* kGlyphZL = "";
constexpr const char* kGlyphZR = "";
constexpr const char* kGlyphR = "";
constexpr const char* kGlyphMinus = "";
constexpr const char* kGlyphB = "";
constexpr const char* kGlyphA = "";
// X e Y seguem A/B na mesma sequencia E0E0 da fonte.
constexpr const char* kGlyphY = "";

// X segue A/B/Y na sequencia E0E0 da fonte; o + fica ao lado do − (E0F0).
// Escapes \u para nao depender do editor renderizar a Private Use Area.
constexpr const char* kGlyphX = "";
constexpr const char* kGlyphPlus = "";

// Marca da multissselecao (spec 021). Verde, como o cursor de selecao multipla
// do HOME (§5 da pesquisa), e distinto do laranja do foco — os dois estados
// aparecem juntos e precisam ser distinguiveis.
const NVGcolor kMarked = nvgRGB(0x3F, 0xA9, 0x6B);
// Fundo da celula de um shiny (spec 025). Dourado translucido: destaca sem
// competir com a borda de foco nem com a marca verde de selecao.
const NVGcolor kShinyBg = nvgRGBA(0xFF, 0xD9, 0x5C, 200);
// Vermelho do bloqueio por incompatibilidade (spec 034). Equivale ao icone de
// proibido do HOME (§2 da pesquisa).
const NVGcolor kBlocked = nvgRGB(0xD9, 0x3B, 0x3B);
// Amarelo do aviso de golpe (spec 038). Equivale ao icone amarelo do HOME (§2
// da pesquisa): o golpe nao existe no destino, mas o movimento e PERMITIDO —
// por isso e uma cor distinta do vermelho, que recusa.
const NVGcolor kWarned = nvgRGB(0xE0, 0xA8, 0x1C);

std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

// Applet mode vs application mode. Quem decide e o hbloader, nao o NRO:
//   album             -> applet mode,      ~448 MB, sem acesso a save data
//   segurar R num jogo -> application mode, ~3 GB, permissoes do titulo
//
// O app nao consegue forcar o modo, mas consegue detectar e avisar — e o
// mesmo motivo pelo qual o hbmenu mostra "Applet Mode" na tela.
bool IsAppletMode() {
#ifdef __SWITCH__
  const AppletType type = appletGetAppletType();
  return type != AppletType_Application && type != AppletType_SystemApplication;
#else
  return false;
#endif
}

// Paleta do fundo. Cada secao do produto tem a sua (verde = caixas,
// roxo = enciclopedia); a estrutura do desenho e a mesma.
struct BackgroundPalette {
  NVGcolor top, mid, bottom;
};

// Fundo do app: gradiente vertical, sombreado radial por cima e a faixa clara
// em diagonal atras da barra superior. O borealis so pinta cor solida, entao
// esta e a unica View com draw() proprio — o resto e flexbox puro.
class GradientBackground : public brls::Box {
 public:
  GradientBackground()
      : palette_{kBgTop, kBgMid, kBgBottom} {}
  explicit GradientBackground(const BackgroundPalette& palette)
      : palette_(palette) {}

  // A faixa diagonal so faz sentido em tela que tem barra superior.
  void setHeaderBand(float height) { band_height_ = height; }

  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    // Duas paradas: o layout tem a cor do meio em 55%, que nvgLinearGradient
    // nao expressa direto. Pintar em dois trechos aproxima sem custo.
    const float mid = y + h * 0.55f;

    NVGpaint top = nvgLinearGradient(vg, x, y, x, mid, palette_.top,
                                     palette_.mid);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h * 0.55f);
    nvgFillPaint(vg, top);
    nvgFill(vg);

    NVGpaint bottom = nvgLinearGradient(vg, x, mid, x, y + h, palette_.mid,
                                        palette_.bottom);
    nvgBeginPath(vg);
    nvgRect(vg, x, mid, w, h * 0.45f);
    nvgFillPaint(vg, bottom);
    nvgFill(vg);

    DrawVignette(vg, x, y, w, h);
    if (band_height_ > 0.0f) DrawHeaderBand(vg, x, y, w);

    brls::Box::draw(vg, x, y, w, h, style, ctx);
  }

 private:
  // Sombreado radial: escuro no centro, sumindo ate nada nas bordas (spec 046).
  //
  // Substituiu a tabela de blocos translucidos (kBgBlocks) — o dono comparou
  // com a referencia e o que ela tem e este degrade, nao as "bolas" espalhadas.
  //
  // nvgRadialGradient e elipse so por acidente de escala: ele desenha circulo.
  // Para cobrir uma tela deitada, o raio externo sai da diagonal.
  void DrawVignette(NVGcontext* vg, float x, float y, float w, float h) const {
    const float cx = x + w / 2;
    const float cy = y + h / 2;
    const float outer = std::sqrt(w * w + h * h) * 0.5f;

    NVGpaint paint = nvgRadialGradient(
        vg, cx, cy, outer * 0.12f, outer,
        nvgRGBAf(0.10f, 0.26f, 0.20f, 0.20f),
        nvgRGBAf(0.10f, 0.26f, 0.20f, 0.0f));
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
  }

  // Faixa clara atras da barra superior, terminando em diagonal a direita —
  // como no Home. E um path, nao um Box: flexbox nao corta em diagonal.
  //
  // Sao duas camadas: uma verde-agua por baixo, mais longa, e a branca por
  // cima. A diagonal desce da esquerda para a direita — e mais estreita no
  // topo e mais larga na base, nao o contrario.
  void DrawHeaderBand(NVGcontext* vg, float x, float y, float w) const {
    // Fundo proprio da barra: gradiente HORIZONTAL, verde saturado a esquerda
    // e amarelo-claro a direita. O gradiente do fundo e vertical e nao produz
    // isso sozinho.
    NVGpaint barPaint = nvgLinearGradient(vg, x, y, x + w, y,
                                          nvgRGB(0x86, 0xD4, 0xB0),
                                          nvgRGB(0xEC, 0xF1, 0xB8));
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, band_height_);
    nvgFillPaint(vg, barPaint);
    nvgFill(vg);

    // Diagonal deitada, mas nao demais: ~1,5x a altura da barra.
    const float slant = band_height_ * 1.5f;

    // O verde e so uma borda fina acompanhando a diagonal branca: mesma
    // inclinacao, deslocada alguns pixels para fora. Nao e uma cunha larga.
    const float frontWidth = w * 0.32f + 12.0f;
    const float backEnd = frontWidth + 16.0f;
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, y);
    nvgLineTo(vg, x + backEnd, y);
    nvgLineTo(vg, x + backEnd + slant, y + band_height_);
    nvgLineTo(vg, x, y + band_height_);
    nvgClosePath(vg);
    nvgFillColor(vg, nvgRGBA(0xC2, 0xE8, 0xD4, 225));
    nvgFill(vg);

    // Camada da frente, quase branca.
    const float frontEnd = frontWidth;
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, y);
    nvgLineTo(vg, x + frontEnd, y);
    nvgLineTo(vg, x + frontEnd + slant, y + band_height_);
    nvgLineTo(vg, x, y + band_height_);
    nvgClosePath(vg);
    nvgFillColor(vg, nvgRGB(0xF1, 0xF4, 0xEE));
    nvgFill(vg);
  }

  BackgroundPalette palette_;
  float band_height_ = 0.0f;
};

// Sprites do Pokemon Home (PokeAPI), em duas resolucoes. Os originais sao
// 512x512 = 1 MB de VRAM cada; o pool do borealis no Switch tem 4 MB e a
// lista mostra 78 de uma vez. Ver scripts/prepare-sprites.py.
// Nome de especie para exibicao: fontes nao-gen3 ja trazem o nome pronto.
std::string DisplaySpecies(const g3::BoxPokemon& mon) {
  if (!mon.species_name.empty()) return mon.species_name;
  // Fontes gen3 trazem o indice INTERNO; a tabela do gen3 resolve. Se o dex
  // nacional vier preenchido (fonte de outra geracao), usa a tabela por dex,
  // que alcanca 1025 (spec 035).
  if (mon.national_dex != 0) {
    const std::string by_dex = g3::SpeciesNameByDex(mon.national_dex);
    if (!by_dex.empty()) return by_dex;
  }
  return g3::SpeciesName(mon.species);
}

// Nome de golpe para exibicao (spec 106). A numeracao de golpe e NACIONAL e
// compartilhada por gen3 e modernos, entao uma tabela so cobre as duas pontas
// — a `kMoveNames` do gen3 parava em 354 e a barra mostrava "golpe #920" para
// tudo acima disso, que era o que o dono via.
//
// Vazio para golpe 0 (slot vazio) e para id fora da tabela; quem chama decide
// o que mostrar no lugar.
std::string DisplayMove(std::uint16_t move) {
  if (move == 0) return "";
  if (move < pokehome::modern::kMoveNameCount) {
    const char* n = pokehome::modern::kMoveNames[move];
    if (n && *n) return n;
  }
  return "";
}

// Existe arquivo neste caminho? Usado para o fallback do shiny: se o conjunto
// shiny nao foi gerado no romfs, mostra o normal em vez de celula vazia
// (TD-02 da spec 036).
bool FileExists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

// Caminho do sprite, escolhendo o conjunto shiny quando for o caso (spec 036).
// O nome do arquivo e o mesmo nos dois — so o diretorio muda, como na PokeAPI.
std::string SpritePathFor(int dex, bool shiny, bool big) {
  if (dex <= 0) return "";
  const std::string file = std::to_string(dex) + ".png";
  if (shiny) {
    const std::string s = std::string(big ? POKEHOME_SPRITES_BIG_SHINY
                                          : POKEHOME_SPRITES_SHINY) + file;
    if (FileExists(s)) return s;
    // Sem o arquivo shiny: cai no normal. A marca dourada da spec 025 continua
    // sinalizando que e shiny.
  }
  return std::string(big ? POKEHOME_SPRITES_BIG : POKEHOME_SPRITES) + file;
}

std::string SpritePath(const g3::BoxPokemon& mon) {
  const int dex =
      mon.national_dex ? mon.national_dex : g3::NationalDex(mon.species);
  return SpritePathFor(dex, mon.is_shiny(), /*big=*/false);
}

// 256x256, para telas que mostram um sprite de cada vez.
std::string SpritePathBig(const g3::BoxPokemon& mon) {
  const int dex =
      mon.national_dex ? mon.national_dex : g3::NationalDex(mon.species);
  return SpritePathFor(dex, mon.is_shiny(), /*big=*/true);
}

// Textura da SILHUETA de um sprite: mesmo contorno, RGB todo escuro, alpha
// original preservado. Serve a sombra do Pokemon levantado (spec 085).
//
// Precisa ser uma textura propria porque nanovg nao sabe tingir imagem: o
// nvgImagePattern so aplica alpha, e recortar com NVG_ATOP nao funciona (ele
// testa o alpha do FRAMEBUFFER, que o fundo opaco do slot ja preencheu). E a
// mesma solucao que o Selector usa para colorir mascara (spec 084).
//
// Cache por caminho: cada sprite gera a silhueta uma vez.
int SilhouetteHandle(NVGcontext* vg, const std::string& sprite_path, int& iw,
                     int& ih) {
  struct Cached {
    std::string path;
    int handle;
    int w, h;
  };
  static std::vector<Cached> cache;

  for (const auto& c : cache) {
    if (c.path == sprite_path) {
      iw = c.w;
      ih = c.h;
      return c.handle;
    }
  }

  int w, h, n;
  unsigned char* px = stbi_load(sprite_path.c_str(), &w, &h, &n, 4);
  if (!px) {
    cache.push_back({sprite_path, 0, 0, 0});
    iw = ih = 0;
    return 0;
  }

  for (int i = 0; i < w * h; ++i) {
    unsigned char* p = px + i * 4;
    if (p[3] == 0) continue;  // fora da silhueta: continua invisivel
    p[0] = p[1] = p[2] = 30;
  }

  const int handle = nvgCreateImageRGBA(vg, w, h, NVG_IMAGE_NEAREST, px);
  stbi_image_free(px);
  cache.push_back({sprite_path, handle, w, h});
  iw = w;
  ih = h;
  return handle;
}

// --- Arquivo do NestBox ----------------------------------------------------
//
// Grava so em sdmc:/switch/nestbox/, area que o guardrail do CLAUDE.md permite
// e onde o app ja mantem o cache de capas. NENHUM save de jogo e tocado — a
// escrita em save continua sendo a v3, com backup obrigatorio.

#ifdef __SWITCH__
constexpr const char* kNestBoxDir = "sdmc:/switch/nestbox";
constexpr const char* kNestBoxPath = "sdmc:/switch/nestbox/nestbox.bin";
constexpr const char* kNestBoxPathA = "sdmc:/switch/nestbox/bank.a";
constexpr const char* kNestBoxPathB = "sdmc:/switch/nestbox/bank.b";
#else
constexpr const char* kNestBoxDir = "nestbox";
constexpr const char* kNestBoxPath = "nestbox/nestbox.bin";
constexpr const char* kNestBoxPathA = "nestbox/bank.a";
constexpr const char* kNestBoxPathB = "nestbox/bank.b";
#endif

// Le um arquivo inteiro. Ausente devolve vazio — nunca erro fatal.
std::vector<std::uint8_t> ReadWholeFile(const char* path) {
  std::vector<std::uint8_t> bytes;
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return bytes;
  std::uint8_t buf[4096];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
    bytes.insert(bytes.end(), buf, buf + n);
  }
  std::fclose(f);
  return bytes;
}

// Arquivo ausente ou invalido devolve banco vazio — nunca erro fatal, nunca
// dado interpretado errado (Decode ja recusa magic/versao/truncado).
// Le a dupla A/B e devolve o banco da copia valida mais recente (spec 091).
//
// Ordem: bank.a / bank.b primeiro; so se nenhum dos dois servir e que cai no
// .nestbox legado. Instalacao que so tem o legado abre normal e vira bank.a
// geracao 1 no primeiro commit — e o legado NAO e apagado, fica como backup
// natural (TD-03 da spec 091).
nest::NestData LoadNestBox() {
  const nest::ab::Slot a = nest::ab::Unwrap(ReadWholeFile(kNestBoxPathA));
  const nest::ab::Slot b = nest::ab::Unwrap(ReadWholeFile(kNestBoxPathB));

  const nest::ab::Slot* fonte = nullptr;
  if (nest::ab::PickSource(a, b, &fonte)) {
    NLOG_ACT("NestBox lido de bank.%c (geracao %llu)",
             (fonte == &b) ? 'b' : 'a',
             static_cast<unsigned long long>(fonte->generation));
    return nest::Decode(fonte->payload);
  }

  // Nenhuma copia A/B valida. Pode ser instalacao nova (arquivo ausente) ou
  // instalacao antiga, de antes da spec 091.
  const auto legado = ReadWholeFile(kNestBoxPath);
  if (!legado.empty()) {
    NLOG_ACT("NestBox: sem bank.a/bank.b validos; lendo o .nestbox legado");
    return nest::Decode(legado);
  }
  return {};
}

// Grava o banco.
//
// NAO usa rename(), de proposito. O projeto ja pagou por essa licao: o
// comentario em src/ui/updater.cpp:358-364 registra que a arquitetura do
// Sphaira evita rename por causa da janela entre o delete e o rename, e que
// stdio sozinho nao basta no Switch — sem fsFsCommit a escrita pode nao chegar
// ao cartao. Mesmo padrao aqui.
//
// O arquivo e escrito inteiro de uma vez a partir de um buffer ja pronto:
// Encode() so devolve conteudo completo ou vazio, entao nao ha estado
// intermediario para uma queda de energia pegar no meio da serializacao.
bool SaveNestBox(const nest::NestData& d) {
  const auto payload = nest::Encode(d);
  if (payload.empty()) {
    NLOG_ACT("FALHA gravar NestBox: nest::Encode devolveu 0 bytes");
    return false;
  }

  // Escolhe o destino: SEMPRE a copia inativa. A copia boa nao e tocada, entao
  // uma queda de energia aqui no meio nao custa o banco (spec 091).
  const nest::ab::Slot a = nest::ab::Unwrap(ReadWholeFile(kNestBoxPathA));
  const nest::ab::Slot b = nest::ab::Unwrap(ReadWholeFile(kNestBoxPathB));
  const nest::ab::Target target = nest::ab::PickTarget(a, b);
  const char* path = target.write_b ? kNestBoxPathB : kNestBoxPathA;

  const auto bytes = nest::ab::Wrap(payload, target.generation);

  // O mkdir do MinGW nao aceita modo; nas outras plataformas aceita.
#ifdef _WIN32
  mkdir(kNestBoxDir);
#else
  mkdir(kNestBoxDir, 0777);
#endif

  std::FILE* f = std::fopen(path, "wb");
  if (!f) {
    NLOG_ACT("FALHA gravar NestBox: fopen(\"%s\",\"wb\") recusou", path);
    return false;
  }
  const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fflush(f);
  std::fclose(f);
  if (written != bytes.size()) {
    NLOG_ACT("FALHA gravar NestBox: esperado %zu bytes, gravado %zu em \"%s\"",
             bytes.size(), written, path);
    return false;
  }
  NLOG_ACT("NestBox gravado: %zu bytes em \"%s\" (geracao %llu)", bytes.size(),
           path, static_cast<unsigned long long>(target.generation));

#ifdef __SWITCH__
  // O commit e o que faz a escrita chegar ao cartao.
  FsFileSystem* sdmc = fsdevGetDeviceFileSystem("sdmc");
  if (sdmc) fsFsCommit(sdmc);
#endif
  return true;
}

// --- Backup de save (spec 032) ---------------------------------------------
//
// Rede de seguranca exigida por spec/memory/produto.md antes de qualquer
// escrita em save. Le o save e escreve em sdmc:/switch/nestbox/backups/ —
// as duas pontas dentro do que o guardrail do CLAUDE.md permite.

#ifdef __SWITCH__
constexpr const char* kBackupDir = "sdmc:/switch/nestbox/backups";
#else
constexpr const char* kBackupDir = "nestbox/backups";
#endif

void MakeDir(const char* path) {
#ifdef _WIN32
  mkdir(path);
#else
  mkdir(path, 0777);
#endif
}

std::string BackupPath(const std::string& filename) {
  return std::string(kBackupDir) + "/" + filename;
}

// Carimbo ordenavel como texto: AAAAMMDD-hhmmss.
std::string TimeStamp() {
  const std::time_t now = std::time(nullptr);
  const std::tm* t = std::localtime(&now);
  char buf[32];
  if (!t) return "00000000-000000";
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
                t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour,
                t->tm_min, t->tm_sec);
  return buf;
}

std::vector<bk::Entry> ListBackups() {
  std::vector<bk::Entry> out;
  DIR* dir = opendir(kBackupDir);
  if (!dir) return out;
  while (struct dirent* ent = readdir(dir)) {
    bk::Entry e;
    if (bk::ParseFilename(ent->d_name, &e)) out.push_back(e);
  }
  closedir(dir);
  return out;
}

// Grava o backup e CONFERE relendo do cartao.
//
// A verificacao nao e paranoia: um backup nao conferido da falsa seguranca, o
// que e pior que nao ter backup nenhum — muda o comportamento de quem confia
// nele (TD-01 da spec 032). Se isto devolver false, a escrita no save NAO pode
// prosseguir.
bool BackupSave(const std::string& save_path,
                const std::vector<std::uint8_t>& bytes) {
  if (bytes.empty()) {
    NLOG_ACT("FALHA backup de \"%s\": 0 bytes para gravar", save_path.c_str());
    return false;
  }

  MakeDir(kNestBoxDir);
  MakeDir(kBackupDir);

  const std::string name = bk::MakeFilename(save_path, TimeStamp());
  const std::string full = BackupPath(name);

  std::FILE* f = std::fopen(full.c_str(), "wb");
  if (!f) {
    NLOG_ACT("FALHA backup: fopen(\"%s\",\"wb\") recusou", full.c_str());
    return false;
  }
  const std::size_t put = std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fflush(f);
  std::fclose(f);
  if (put != bytes.size()) {
    NLOG_ACT("FALHA backup \"%s\": esperado %zu bytes, gravado %zu", full.c_str(),
             bytes.size(), put);
    std::remove(full.c_str());
    return false;
  }

#ifdef __SWITCH__
  FsFileSystem* sdmc = fsdevGetDeviceFileSystem("sdmc");
  if (sdmc) fsFsCommit(sdmc);
#endif

  // Rele do cartao e compara. Nao confia no retorno do fwrite.
  const std::vector<std::uint8_t> readback = ReadFile(full);
  if (!bk::Verify(bytes, readback)) {
    // O caso mais perigoso do app: backup gravado mas DIFERENTE. Sem o par de
    // tamanhos aqui, "o backup falhou" nao diz se o cartao truncou ou se
    // devolveu lixo.
    NLOG_ACT(
        "FALHA backup \"%s\": releitura NAO confere — esperado %zu bytes, "
        "lido %zu. A escrita no save NAO prossegue.",
        full.c_str(), bytes.size(), readback.size());
    std::remove(full.c_str());
    return false;
  }

  // Rotaciona: mantem os mais recentes deste save.
  for (const std::string& old :
       bk::ToRemove(bk::ForSave(ListBackups(), save_path))) {
    NLOG_ACT("backup antigo removido: \"%s\"", old.c_str());
    std::remove(BackupPath(old).c_str());
  }
  NLOG_ACT("backup OK e CONFERIDO: \"%s\" (%zu bytes)", full.c_str(),
           bytes.size());
  return true;
}

// --- Log de diagnostico (spec 083) -----------------------------------------
//
// O core (source/core/nlog.h) formata e decide QUANDO rotacionar; o I/O e daqui,
// pela mesma regra do backup — "o I/O fica na UI, que e quem conhece o cartao".
//
// Destino no Switch: sdmc:/switch/nestbox/logs/. NAO sdmc:/nestbox/: o app ja
// concentra tudo em sdmc:/switch/nestbox/ (nestbox.bin, backups, capas) e uma
// segunda raiz seria mais um lugar para o dono procurar. Continua dentro do que
// o guardrail do CLAUDE.md permite — sdmc: e so sdmc:.
//
// PARA DESLIGAR TUDO NO RELEASE: NESTBOX_LOG_LEVEL 0 em source/core/nlog.h.

#ifdef __SWITCH__
constexpr const char* kLogDir = "sdmc:/switch/nestbox/logs";
#else
constexpr const char* kLogDir = "nestbox/logs";
#endif

std::string LogPath(int index) {
  // index 0 = o corrente; 1..n = os rotacionados.
  return std::string(kLogDir) + "/nestbox" +
         (index == 0 ? "" : "." + std::to_string(index)) + ".log";
}

std::FILE* g_log_file = nullptr;

// Fecha, empurra nestbox.log -> nestbox.1.log -> nestbox.2.log e reabre.
// O mais antigo e apagado; `keep` diz quantos sobrevivem.
void RotateLog(int keep) {
  if (g_log_file) {
    std::fclose(g_log_file);
    g_log_file = nullptr;
  }
  std::remove(LogPath(keep).c_str());
  for (int i = keep - 1; i >= 0; --i) {
    std::rename(LogPath(i).c_str(), LogPath(i + 1).c_str());
  }
  g_log_file = std::fopen(LogPath(0).c_str(), "wb");
}

void OpenLog() {
#if NESTBOX_LOG_LEVEL == 0
  return;  // release: nem o diretorio e criado
#else
  MakeDir(kNestBoxDir);
  MakeDir(kLogDir);

  // "ab", nao "wb": a sessao anterior costuma ser justamente a que deu
  // problema. Zerar o arquivo ao abrir apagaria a evidencia antes de alguem
  // ler.
  g_log_file = std::fopen(LogPath(0).c_str(), "ab");

  // O tamanho do que ja existe entra na conta da rotacao. Sem isto o contador
  // reiniciaria a cada execucao e o arquivo cresceria sem limite entre
  // sessoes — o teto so valeria dentro de UMA sessao.
  if (g_log_file) {
    std::fseek(g_log_file, 0, SEEK_END);
    const long size = std::ftell(g_log_file);
    pokehome::nlog::SetWrittenBytes(size > 0 ? static_cast<std::size_t>(size)
                                             : 0);
  }

  pokehome::nlog::SetRotator(RotateLog);
  pokehome::nlog::SetSink([](const std::string& line) {
    // Console tambem: no PC e o stdout do terminal; no Switch, o nxlink.
    std::fputs(line.c_str(), stdout);
    if (!g_log_file) return;
    std::fwrite(line.data(), 1, line.size(), g_log_file);
    // Flush a cada linha, de proposito. O caso que mais importa e o app
    // MORRER: um buffer nao descarregado leva embora exatamente as ultimas
    // linhas, que sao as que explicam o crash.
    std::fflush(g_log_file);
  });
#endif
}

// Entrada e saida de tela, em uma linha por Activity (spec 083).
//
// Membro, nao classe-base: as Activities do app ja herdam de brls::Activity e
// varias tem construtor proprio: um membro entra com uma linha em cada uma e
// nao mexe na hierarquia. O destrutor cobre a saida por qualquer caminho — B,
// popActivity de outro lugar ou fim do app —, que e justamente o que uma
// chamada no handler do B deixaria escapar.
struct LogScreen {
  const char* name;
  explicit LogScreen(const char* n) : name(n) { NLOG_NAV("-> ENTROU %s", name); }
  ~LogScreen() { NLOG_NAV("<- SAIU %s", name); }
};

// --- Confirmacoes ----------------------------------------------------------

// A caixa de mensagem padrao do app (spec 044). Definida mais abaixo, junto do
// resto dos componentes visuais; declarada aqui porque as telas que a chamam
// vem antes dela no arquivo. **Nao use brls::Dialog.**
class MessageBox : public brls::Box {
 public:
  using Callback = std::function<void()>;

  // Um botao da caixa. `glyph` e opcional: o de cancelar mostra o glifo de B.
  struct Button {
    std::string label;
    Callback on_click;
    std::string glyph;
  };

  static void Show(const std::string& text, std::vector<Button> buttons,
                   bool cancelable = true);

  class PillButton;
  class Bar;
};

// Menu de contexto do modo Mover (spec 095). Declarado aqui porque a
// BoxActivity o abre e vem antes no arquivo; a definicao esta junto do
// MessageBox, com o resto das Activities de overlay.
class ContextMenuActivity;
void ShowContextMenu(
    brls::Rect anchor,
    std::vector<std::pair<std::string, std::function<void()>>> items);

// Check Summary do HOME (spec 103). Declarada aqui porque a ListActivity e a
// BoxActivity a abrem e vem antes no arquivo. `source` nulo desliga a
// navegacao L/R (tela aberta de uma lista, sem caixa em volta).
class BoxSource;
void ShowSummary(const g3::BoxPokemon& mon, BoxSource* source, std::size_t box,
                 std::size_t slot);

// Pergunta antes de uma acao que descarta estado (spec 020).
//
// O botao de CANCELAR vem primeiro de proposito: o gesto apressado (confirmar
// no primeiro botao) deve ser o inofensivo. B tambem fecha sem fazer nada,
// que e o padrao do brls::Dialog (setCancelable).
//
// `on_confirm` so roda se o usuario escolher explicitamente o segundo botao.
//
// Implementado junto do MessageBox (spec 044), que so e declarado mais abaixo.
void ConfirmDialog(const std::string& text, const std::string& cancel_label,
                   const std::string& confirm_label,
                   std::function<void()> on_confirm);

// Aviso de um botao so: informa e fecha. Mesma caixa, sem escolha a fazer.
void NoticeDialog(const std::string& text, const std::string& button_label,
                  std::function<void()> on_close = nullptr,
                  bool cancelable = true);

// --- Fonte de dados --------------------------------------------------------

class BoxSource {
 public:
  virtual ~BoxSource() = default;
  virtual std::string Title() const = 0;
  virtual std::string Warning() const { return ""; }
  virtual std::size_t Capacity() const { return 0; }
  // Caixas da fonte. O gen3 tem 14; o Legends Z-A tem 32.
  virtual std::size_t BoxCount() const { return g3::kBoxCount; }
  virtual std::size_t Count() const { return 0; }
  virtual g3::BoxPokemon At(std::size_t box, std::size_t slot) const = 0;

  // Esta fonte pode RECEBER um Pokemon? Falso por padrao: mover so acontece
  // onde a spec 019 autoriza, e a movimentacao vive num overlay em memoria
  // (box_move.h) — nada e gravado no save. Ver TD-01 da spec 019.
  virtual bool CanAccept() const { return false; }

  // Qual jogo esta fonte representa, para a matriz de compatibilidade
  // (spec 034). kCount = "nao e um jogo" — o NestBox aceita qualquer especie,
  // como o HOME, que guarda tudo e so restringe na descida para um jogo.
  virtual cp::Game GameId() const { return cp::Game::kCount; }

  // Por que este slot NAO pode SAIR daqui? Vazio = pode.
  //
  // Hoje so o verificador de legalidade responde (spec 079/082): o Pokemon
  // reprovado CONTINUA LISTADO e navegavel — o bloqueio e da TRANSFERENCIA,
  // nunca da leitura (decisao do dono, spec/discovery/escrita-e-transferencia).
  virtual std::string BlockedReason(std::size_t, std::size_t) const {
    return "";
  }

  // Avisa a fonte qual caixa o painel abriu, para que `Title()` possa
  // acompanhar (spec 022). A maioria das fontes ignora — o nome delas vem do
  // arquivo de save e nao muda por caixa.
  virtual void SetCurrentBox(std::size_t) {}
};

class SaveSource : public BoxSource {
 public:
  SaveSource(std::vector<std::uint8_t> file, g3::SaveFile save,
             std::string path)
      : file_(std::move(file)), save_(save), path_(std::move(path)) {
    const auto& active = g3::ActiveSlot(save_);
    for (const auto& s : active.sections) {
      if (s.checksum_ok()) ++checksum_ok_;
    }
    pc_ = g3::BuildPcBuffer(file_, save_);

    for (std::size_t b = 0; b < g3::kBoxCount; ++b) {
      for (std::size_t s = 0; s < g3::kSlotsPerBox; ++s) {
        const auto mon = g3::ReadBoxPokemonFrom(pc_, b, s);
        if (mon && !mon->empty()) ++count_;
      }
    }
  }

  // Nome da CAIXA aberta, nao do arquivo (spec 049). O cabecalho do painel
  // mostrava o nome do save, que e a informacao errada: quem esta ali e uma
  // caixa entre 14.
  //
  // O gen3 guarda os nomes das caixas no PC buffer, mas o parser ainda nao os
  // le — enquanto isso, o numerado, como o HOME faz nas caixas sem nome.
  std::string Title() const override {
    return "CAIXA " + std::to_string(current_box_ + 1);
  }
  void SetCurrentBox(std::size_t box) override { current_box_ = box; }

  std::string Warning() const override {
    if (checksum_ok_ == static_cast<int>(g3::kSectionCount)) return "";
    return std::to_string(g3::kSectionCount - checksum_ok_) +
           " secoes com checksum invalido";
  }

  std::size_t Capacity() const override {
    return g3::kBoxCount * g3::kSlotsPerBox;
  }
  std::size_t Count() const override { return count_; }

  g3::BoxPokemon At(std::size_t box, std::size_t slot) const override {
    const auto mon = g3::ReadBoxPokemonFrom(pc_, box, slot);
    return mon ? *mon : g3::BoxPokemon{};
  }

  // Aceita receber de volta — no HOME o movimento e de mao dupla (§4.7 da
  // pesquisa). Durante a sessao isto NAO escreve no save: o destino e o
  // overlay. A escrita so acontece no commit (spec 033).
  bool CanAccept() const override { return true; }

  // Save gen3. Os quatro jogos do gen3 (RS, E, FR, LG) tem EXATAMENTE a mesma
  // lista de 386 especies — verificado comparando os bitmaps da matriz —,
  // entao nao e preciso distinguir qual deles e para responder
  // compatibilidade. Se um dia a distincao importar (itens, locais), ai sim
  // sera preciso ler o game code do save.
  cp::Game GameId() const override { return cp::Game::kFireRed; }

  // --- Escrita (spec 033) --------------------------------------------------

  const std::string& path() const { return path_; }
  const std::vector<std::uint8_t>& bytes() const { return file_; }

  // Aplica as alteracoes ao PC buffer e grava o arquivo.
  //
  // NAO chama backup: quem chama e o commit, que aborta se o backup falhar.
  // Deixar o backup aqui esconderia a decisao mais importante do fluxo.
  bool WriteChanges(const std::map<std::size_t, g3::BoxPokemon>& changes) {
    if (changes.empty()) return true;

    std::vector<std::uint8_t> pc = pc_;
    for (const auto& [index, mon] : changes) {
      const std::size_t box = index / g3::kSlotsPerBox;
      const std::size_t slot = index % g3::kSlotsPerBox;
      const bool ok = mon.empty()
                          ? g3::WriteBoxPokemonTo(pc, box, slot, nullptr)
                          : g3::WriteBoxPokemonTo(pc, box, slot, mon.raw);
      if (!ok) return false;
    }

    std::vector<std::uint8_t> out = file_;
    if (!g3::ApplyPcBuffer(out, save_, pc)) return false;

    std::FILE* f = std::fopen(path_.c_str(), "wb");
    if (!f) return false;
    const std::size_t put = std::fwrite(out.data(), 1, out.size(), f);
    std::fflush(f);
    std::fclose(f);
    if (put != out.size()) return false;

#ifdef __SWITCH__
    FsFileSystem* sdmc = fsdevGetDeviceFileSystem("sdmc");
    if (sdmc) fsFsCommit(sdmc);
#endif

    // O estado em memoria passa a refletir o disco: sem isto a tela mostraria
    // o save antigo ate reabrir.
    file_ = std::move(out);
    pc_ = std::move(pc);
    count_ = 0;
    for (std::size_t b = 0; b < g3::kBoxCount; ++b) {
      for (std::size_t s = 0; s < g3::kSlotsPerBox; ++s) {
        const auto mon = g3::ReadBoxPokemonFrom(pc_, b, s);
        if (mon && !mon->empty()) ++count_;
      }
    }
    return true;
  }

 private:
  std::vector<std::uint8_t> file_;
  g3::SaveFile save_;
  std::string path_;
  std::vector<std::uint8_t> pc_;
  int checksum_ok_ = 0;
  std::size_t count_ = 0;
  std::size_t current_box_ = 0;
};

// O NestBox ainda nao guarda nada — o formato entra na spec 014. Existe aqui
// para a tela ter os dois paineis desde ja.
// Save do Legends Z-A aberto para leitura. Converte cada slot para o
// g3::BoxPokemon de exibicao (com national_dex e species_name preenchidos —
// as tabelas gen3 nao alcancam a dex 1025).
class ZaSaveSource : public BoxSource {
 public:
  ZaSaveSource(za::ZaSave save, std::string title)
      : save_(std::move(save)), title_(std::move(title)) {
    for (std::size_t b = 0; b < za::kBoxCount; ++b) {
      for (std::size_t s = 0; s < za::kSlotsPerBox; ++s) {
        if (za::ReadZaBoxPokemon(save_, b, s)) ++count_;
      }
    }
  }

  // Nome da CAIXA, nao do jogo (spec 049). O `title_` continua guardado: ele
  // e o nome do save e aparece na tela de restauracao.
  std::string Title() const override {
    return "CAIXA " + std::to_string(current_box_ + 1);
  }
  void SetCurrentBox(std::size_t box) override { current_box_ = box; }
  cp::Game GameId() const override { return cp::Game::kLegendsZA; }
  std::size_t Capacity() const override {
    return za::kBoxCount * za::kSlotsPerBox;
  }
  std::size_t BoxCount() const override { return za::kBoxCount; }
  std::size_t Count() const override { return count_; }

  g3::BoxPokemon At(std::size_t box, std::size_t slot) const override {
    const auto mon = za::ReadZaBoxPokemon(save_, box, slot);
    if (!mon) return {};
    g3::BoxPokemon out;
    out.species = mon->species;  // != 0 para empty() funcionar
    out.national_dex = mon->species;
    out.species_name = za::ZaSpeciesName(mon->species);
    out.nickname = mon->nickname;
    out.met_level = mon->level;
    return out;
  }

 private:
  za::ZaSave save_;
  std::string title_;
  std::size_t count_ = 0;
  std::size_t current_box_ = 0;
};

// Save moderno aberto para leitura (spec 082): Sword/Shield, Scarlet/Violet,
// BDSP, Legends Arceus, Let's Go e Legends Z-A. O motor e o `savew::Load` das
// specs 068/080, cujo roundtrip byte-identico ja passou no portao G03.
//
// Duas coisas resolvidas UMA VEZ na abertura, e nao a cada `At()`:
//
//   1. a conversao para `g3::BoxPokemon` (a tela nao conhece `pkm::Pokemon`);
//   2. o veredito de legalidade — `CheckLegality` percorre dezenas de
//      coerencias, e a tela chama `At()` uma vez por celula por quadro.
class ModernSaveSource : public BoxSource {
 public:
  // Quem sabe gravar os bytes de volta (arquivo no SD ou save data montado) e
  // quem abriu o save — o writer chega injetado (spec 086). Vazio = fonte
  // somente leitura, como era ate a spec 082.
  using Writer = std::function<bool(const std::vector<std::uint8_t>&)>;

  ModernSaveSource(savew::SaveData save, std::string title, Writer writer = {},
                   std::string backup_label = {})
      : save_(std::move(save)),
        title_(std::move(title)),
        writer_(std::move(writer)),
        backup_label_(std::move(backup_label)) {
    RebuildView();
  }

  // Nome da CAIXA, nao do jogo (spec 049) — como o gen3 e o Z-A ja fazem.
  std::string Title() const override {
    return "CAIXA " + std::to_string(current_box_ + 1);
  }
  void SetCurrentBox(std::size_t box) override { current_box_ = box; }

  std::string Warning() const override {
    if (suspect_ == 0) return "";
    return std::to_string(suspect_) + " Pokemon nao podem ser transferidos";
  }

  std::size_t Capacity() const override { return save_.box.size(); }
  std::size_t BoxCount() const override { return save_.box_count; }
  std::size_t Count() const override { return count_; }

  g3::BoxPokemon At(std::size_t box, std::size_t slot) const override {
    const std::size_t i = Index(box, slot);
    return i < view_.size() ? view_[i] : g3::BoxPokemon{};
  }

  std::string BlockedReason(std::size_t box, std::size_t slot) const override {
    const std::size_t i = Index(box, slot);
    return i < blocked_.size() ? blocked_[i] : std::string();
  }

  cp::Game GameId() const override {
    switch (save_.game) {
      case savew::Game::kSwSh: return cp::Game::kSwordShield;
      case savew::Game::kSV: return cp::Game::kScarletViolet;
      case savew::Game::kPLA: return cp::Game::kLegendsArceus;
      case savew::Game::kBDSP: return cp::Game::kBdsp;
      case savew::Game::kLGPE: return cp::Game::kLetsGo;
      case savew::Game::kZA: return cp::Game::kLegendsZA;
    }
    return cp::Game::kCount;
  }

  // Aceita movimentacao NA SESSAO quando ha um writer (spec 086) — o modelo do
  // HOME: tudo em memoria ate o "Salvar", que e quem grava (com backup antes).
  bool CanAccept() const override { return static_cast<bool>(writer_); }

  const std::string& title() const { return title_; }

  // Para o backup obrigatorio do commit: os bytes ORIGINAIS do arquivo e um
  // nome de arquivo valido para o .bak.
  const std::vector<std::uint8_t>& bytes() const { return save_.file; }
  const std::string& backup_label() const { return backup_label_; }

  // Aplica as alteracoes da sessao e grava pelo writer (spec 086).
  //
  // NAO chama backup — como no SaveSource gen3, quem exige o backup e o
  // commit, e ele aborta antes de chegar aqui se o backup falhar.
  //
  // Tudo-ou-nada: aplica numa COPIA do SaveData; qualquer recusa (payload
  // ausente, Save() vazio, writer falhou) devolve false com o estado em
  // memoria e o disco intactos.
  bool WriteChanges(const std::vector<vw::BoxChange>& changes) {
    if (changes.empty()) return true;
    if (!writer_) return false;

    savew::SaveData next = save_;
    if (!vw::ApplyBoxChanges(next, changes)) return false;

    const std::vector<std::uint8_t> out = savew::Save(next);
    if (out.empty()) return false;
    if (!writer_(out)) return false;

    // O estado em memoria passa a refletir o disco (mesma regra do gen3).
    next.file = out;
    save_ = std::move(next);
    RebuildView();
    return true;
  }

 private:
  std::size_t Index(std::size_t box, std::size_t slot) const {
    return box * save_.slots_per_box + slot;
  }

  // A conversao para a tela e o veredito de legalidade, uma vez por abertura
  // E uma vez por commit — os motivos do cabecalho da classe.
  void RebuildView() {
    view_.assign(save_.box.size(), g3::BoxPokemon{});
    blocked_.assign(save_.box.size(), std::string());
    count_ = 0;
    suspect_ = 0;
    for (std::size_t i = 0; i < save_.box.size(); ++i) {
      const auto& slot = save_.box[i];
      if (!slot.present || slot.mon.species == 0) continue;
      view_[i] = vw::ToBoxPokemon(slot.mon);
      if (view_[i].empty()) continue;  // especie que nao mapeia: slot vazio
      ++count_;

      const legality::LegalityResult r = legality::CheckLegality(slot.mon);
      if (!r.suspect) continue;
      // A primeira razao basta para a tela — a lista inteira e log, nao UI.
      blocked_[i] = r.issues.empty() ? "Pokemon suspeito" : r.issues[0].reason;
      ++suspect_;
    }
  }

  savew::SaveData save_;
  std::string title_;
  Writer writer_;
  std::string backup_label_;
  std::vector<g3::BoxPokemon> view_;
  std::vector<std::string> blocked_;
  std::size_t count_ = 0;
  std::size_t suspect_ = 0;
  std::size_t current_box_ = 0;
};

// Painel vazio: quando a box do NestBox e aberta sozinha, ou quando nenhum
// save foi encontrado. Substituiu o SampleSource, que fabricava seis Pokemon
// de mentira — dado inventado numa tela de save confunde mais do que ajuda.
class EmptyBoxSource : public BoxSource {
 public:
  std::string Title() const override { return "Sem save"; }

  std::string Warning() const override {
#ifdef __SWITCH__
    return "Nenhum save encontrado em sdmc:/nestbox/";
#else
    return "Nenhum save carregado";
#endif
  }
  std::size_t Capacity() const override {
    return g3::kBoxCount * g3::kSlotsPerBox;
  }
  g3::BoxPokemon At(std::size_t, std::size_t) const override { return {}; }
};

class NestBoxSource : public BoxSource {
 public:
  NestBoxSource()
      : data_(nest::MakeEmpty(static_cast<std::uint16_t>(kNestBoxBoxes),
                              static_cast<std::uint16_t>(kSlotsPerBox))) {}

  // Substitui o conteudo pelo que veio do arquivo. Banco de dimensoes
  // diferentes e recusado: e arquivo de outra versao do app, e interpreta-lo
  // poria Pokemon em caixas erradas (spec 028).
  void Load(const nest::NestData& d) {
    if (d.boxes != kNestBoxBoxes || d.slots != kSlotsPerBox) return;
    data_ = d;
    // Semeia a dex global com o que ja esta no banco. Necessario ao abrir um
    // arquivo v1, que nao tinha essa secao: sem isto, quem ja usava o app
    // apareceria com historico zerado apesar de ter Pokemon guardados
    // (spec 029). Em arquivo v2 e inofensivo — sao os mesmos bits.
    MarkAllStored();
  }

  // Marca na dex global tudo que esta guardado agora.
  void MarkAllStored() {
    for (std::size_t b = 0; b < data_.boxes; ++b) {
      for (std::size_t s = 0; s < data_.slots; ++s) {
        const std::uint8_t* rec = data_.At(b, s);
        if (!rec || nest::SlotEmpty(rec)) continue;
        // So os slots gen3 sao parseaveis por aqui. Formato moderno guardado no
        // banco tem a dex semeada por quem o depositou; varrer aqui exigiria o
        // parser certo por formato, que e escopo da spec de UI multi-geracao.
        if (nest::SlotFormatOf(rec) != nest::kGen3) continue;
        const g3::BoxPokemon mon =
            g3::ParseBoxPokemonRecord(nest::SlotPayload(rec));
        data_.MarkSeen(g3::NationalDex(mon.species));
      }
    }
  }

  const nest::NestData& data() const { return data_; }

  // Grava os 80 bytes crus de um Pokemon num slot. `mon.raw` vem do parser.
  void Put(std::size_t box, std::size_t slot, const g3::BoxPokemon& mon) {
    std::uint8_t* dst = data_.At(box, slot);
    if (!dst) return;
    // Formato nativo: entra como gen3 porque veio de um save gen3. A conversao
    // acontece so na saida, ao depositar (D2 da spec 090).
    nest::SlotWrite(dst, nest::kGen3, mon.raw, sizeof(mon.raw));
    // Depositar e o que registra na dex global — o mesmo gatilho do HOME
    // (§10 da pesquisa). Sacar depois NAO desregistra (spec 029).
    data_.MarkSeen(g3::NationalDex(mon.species));
  }

  void Clear(std::size_t box, std::size_t slot) {
    std::uint8_t* dst = data_.At(box, slot);
    if (!dst) return;
    nest::SlotClear(dst);
  }

  // O titulo acompanha a caixa aberta, como as caixas numeradas do HOME. A
  // fonte precisa saber qual caixa o painel esta mostrando — ver TD-01 da
  // spec 022.
  std::string Title() const override {
    // Nome dado pelo usuario tem precedencia; sem nome, o default numerado
    // (spec 030). Nome vazio nao vira dado no arquivo.
    const std::string name = data_.BoxName(current_box_);
    if (!name.empty()) return name;
    return "NESTBOX " + std::to_string(current_box_ + 1);
  }

  void RenameBox(std::size_t box, const std::string& name) {
    data_.SetBoxName(box, name);
  }
  void SetCurrentBox(std::size_t box) override { current_box_ = box; }

  std::size_t Capacity() const override { return kNestBoxCapacity; }
  std::size_t BoxCount() const override { return kNestBoxBoxes; }

  std::size_t Count() const override {
    std::size_t n = 0;
    for (std::size_t b = 0; b < data_.boxes; ++b) {
      for (std::size_t s = 0; s < data_.slots; ++s) {
        if (!nest::SlotEmpty(data_.At(b, s))) ++n;
      }
    }
    return n;
  }

  g3::BoxPokemon At(std::size_t box, std::size_t slot) const override {
    const std::uint8_t* rec = data_.At(box, slot);
    if (!rec || nest::SlotEmpty(rec)) return {};
    // A tela ainda so sabe desenhar gen3 (o BoxSource devolve g3::BoxPokemon).
    // Slot de formato moderno existe no banco e sobrevive ao round-trip, mas
    // aparece vazio aqui ate a spec de UI multi-geracao.
    if (nest::SlotFormatOf(rec) != nest::kGen3) return {};
    // Parseia a cada chamada, como o SaveSource ja faz. Se pesar no console,
    // vira cache — ver TD-02 da spec 028.
    return g3::ParseBoxPokemonRecord(nest::SlotPayload(rec));
  }

  // O NestBox comeca vazio e recebe o que vier — e o destino natural da
  // movimentacao. O conteudo mora no overlay da sessao, nao aqui: fechar o
  // app perde tudo, igual a sair do HOME sem salvar (TD-03 da spec 019).
  bool CanAccept() const override { return true; }

 private:
  std::size_t current_box_ = 0;
  nest::NestData data_;
};

// UNICO ponto do app que decide qual fonte um arquivo de save vira (spec 082).
//
// Antes essa decisao estava copiada em dois lugares (o menu e a tela de
// carregamento), e foi por isso que os saves modernos ficaram invisiveis em
// um deles ate aqui: ligar so um dos dois e um bug que a tela nao denuncia.
//
// A ordem NAO e arbitraria:
//   1. gen3 primeiro — `savew::Load` nao reconhece .sav de GBA, e o inverso
//      tambem vale, mas o gen3 e o caminho com escrita ja implementada.
//   2. `savew::Load` — os SEIS jogos modernos, motor das specs 068/080.
//   3. `za::ParseZaSave` — fallback do Z-A. Continua aqui porque e o unico
//      que le o TREINADOR do Z-A; se o savew reconhecer, ele nem e chamado.
// Grava um arquivo inteiro. O fsFsCommit e o mesmo do SaveSource gen3: sem
// ele a escrita no cartao pode ficar so no cache.
bool WriteWholeFile(const std::string& path,
                    const std::vector<std::uint8_t>& data) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const std::size_t put = std::fwrite(data.data(), 1, data.size(), f);
  std::fflush(f);
  const bool ok = std::fclose(f) == 0 && put == data.size();
#ifdef __SWITCH__
  if (ok) {
    FsFileSystem* sdmc = fsdevGetDeviceFileSystem("sdmc");
    if (sdmc) fsFsCommit(sdmc);
  }
#endif
  return ok;
}

// Titulo de jogo vira nome de arquivo de backup — ":" e companhia quebrariam
// o fopen no Windows e no FAT32 do cartao.
std::string SanitizeBackupLabel(const std::string& label) {
  std::string out = label;
  for (char& c : out) {
    if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|') {
      c = '_';
    }
  }
  return out;
}

BoxSource* OpenBoxSource(std::vector<std::uint8_t> file,
                         const std::string& label,
                         const std::string& title,
                         ModernSaveSource::Writer modern_writer = {}) {
  NLOG_ACT("abrir save: \"%s\" (%zu bytes)", label.c_str(), file.size());
  if (!file.empty()) {
    if (auto parsed = g3::ParseSave(file)) {
      auto* src = new SaveSource(std::move(file), *parsed, label);
      NLOG_ACT("  gen3 OK: jogo=%s caixas=%zu mons=%zu",
               cp::GameName(src->GameId()), src->BoxCount(), src->Count());
      return src;
    }
    if (auto modern = savew::Load(file)) {
      auto* src = new ModernSaveSource(std::move(*modern), title,
                                       std::move(modern_writer),
                                       SanitizeBackupLabel(label));
      // O Warning() ja e a contagem de suspeitos em texto — sem inventar um
      // segundo acessor so para o log.
      NLOG_ACT("  moderno OK: jogo=%s caixas=%zu mons=%zu suspeitos=\"%s\"",
               cp::GameName(src->GameId()), src->BoxCount(), src->Count(),
               src->Warning().c_str());
      return src;
    }
    if (auto za_parsed = za::ParseZaSave(file)) {
      auto* src = new ZaSaveSource(std::move(*za_parsed), title);
      NLOG_ACT("  Z-A OK: caixas=%zu mons=%zu", src->BoxCount(), src->Count());
      return src;
    }
    // Sem esta linha o diagnostico de "abriu vazio" viraria adivinhacao: o
    // tamanho separa arquivo truncado de formato desconhecido.
    NLOG_ACT(
        "  FALHA: nenhum parser reconheceu (%zu bytes) — nem gen3, nem "
        "savew::Load, nem za::ParseZaSave",
        file.size());
  } else {
    NLOG_ACT("  FALHA: arquivo vazio ou ilegivel em \"%s\"", label.c_str());
  }
  return new EmptyBoxSource();
}

// --- Icones vetoriais da moldura (spec 046) --------------------------------

// Desenhados em nanovg em vez de carregados do romfs: sao poucas primitivas,
// escalam em qualquer tamanho e evitam mais um asset por origem de caixa.

// Definida junto ao menu principal; usada aqui pela moldura do painel.
void DrawSoftShadow(NVGcontext* vg, float x, float y, float w, float h,
                    float radius);

void DrawPokeball(NVGcontext* vg, float cx, float cy, float r) {
  nvgBeginPath(vg);
  nvgCircle(vg, cx, cy, r);
  nvgFillColor(vg, nvgRGB(0xF5, 0xF5, 0xF5));
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgArc(vg, cx, cy, r, NVG_PI, 0, NVG_CW);
  nvgClosePath(vg);
  nvgFillColor(vg, nvgRGB(0xE3, 0x35, 0x3A));
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgRect(vg, cx - r, cy - r * 0.13f, r * 2, r * 0.26f);
  nvgFillColor(vg, nvgRGB(0x30, 0x30, 0x36));
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgCircle(vg, cx, cy, r * 0.34f);
  nvgFillColor(vg, nvgRGB(0x30, 0x30, 0x36));
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgCircle(vg, cx, cy, r * 0.20f);
  nvgFillColor(vg, nvgRGB(0xF5, 0xF5, 0xF5));
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgCircle(vg, cx, cy, r);
  nvgStrokeColor(vg, nvgRGB(0x30, 0x30, 0x36));
  nvgStrokeWidth(vg, r * 0.16f);
  nvgStroke(vg);
}

// Cubo em perspectiva do botao "Box Spaces": um hexagono com as tres faces
// separadas por um Y. Tres poligonos, sem projecao de verdade.
void DrawCube(NVGcontext* vg, float cx, float cy, float r) {
  const float hx = r * 0.86f;   // meia largura
  const float hy = r * 0.50f;   // meia altura da face do topo

  const NVGcolor top = nvgRGB(0xF2, 0xF6, 0xF3);
  const NVGcolor left = nvgRGB(0xC2, 0xD2, 0xC8);
  const NVGcolor right = nvgRGB(0xA2, 0xB8, 0xAC);

  // Face de cima: losango.
  nvgBeginPath(vg);
  nvgMoveTo(vg, cx, cy - r);
  nvgLineTo(vg, cx + hx, cy - r + hy);
  nvgLineTo(vg, cx, cy);
  nvgLineTo(vg, cx - hx, cy - r + hy);
  nvgClosePath(vg);
  nvgFillColor(vg, top);
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgMoveTo(vg, cx - hx, cy - r + hy);
  nvgLineTo(vg, cx, cy);
  nvgLineTo(vg, cx, cy + r);
  nvgLineTo(vg, cx - hx, cy + r - hy);
  nvgClosePath(vg);
  nvgFillColor(vg, left);
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgMoveTo(vg, cx + hx, cy - r + hy);
  nvgLineTo(vg, cx, cy);
  nvgLineTo(vg, cx, cy + r);
  nvgLineTo(vg, cx + hx, cy + r - hy);
  nvgClosePath(vg);
  nvgFillColor(vg, right);
  nvgFill(vg);
}

// Pilula com o cubo desenhado a esquerda ("Box Spaces", spec 046). O cubo fica
// preso na borda esquerda, e o padding da pilula abre o espaco para ele.
class PillWithCube : public brls::Box {
 public:
  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    brls::Box::draw(vg, x, y, w, h, style, ctx);
    const float r = h * 0.30f;
    DrawCube(vg, x + 24.0f, y + h / 2, r);
  }

  // Quem marca o foco aqui e a SETA do BoxFrame, como nas celulas — o
  // highlight padrao do borealis fica escondido (spec 105). O callback e
  // ligado no MakePanel, que conhece o frame.
  std::function<void()> on_focus;
  void onFocusGained() override {
    brls::Box::onFocusGained();
    if (on_focus) on_focus();
  }
};

// --- Seletor (spec 047, cor dinamica na spec 084) ---------------------------

// O seletor do app. A SETA e desenhada por path nanovg (spec 092): gradiente,
// borda de 2px e cantos arredondados, coisas que a textura tingida nao dava.
// Os PNG de seta (selector_primary/secondary/shape_arrow) foram apagados junto.
//
// As demais formas — os quatro cantos da area de selecao (spec 088) e a mao —
// continuam vindo de textura tingida em runtime, e por isso `TintedHandle` e
// `kSelectorShapeFiles` seguem existindo.
//
// Toda seta de foco/cursor NOVA usa este componente. Regra no CLAUDE.md.

// Mascaras de forma para Variant::kCustom (spec 084) — contorno neutro +
// preenchimento branco, tingidas em runtime. kArrow gira com Dir (nasce
// para a DIREITA, TD-02); os cantos e a mao sao estaticos, sempre a mesma
// pose, sem rotacao — cada arquivo ja e a orientacao final.
enum class SelectorShape {
  kArrow,
  kCornerTopLeft,
  kCornerTopRight,
  kCornerBottomLeft,
  kCornerBottomRight,
  kHand,
};
constexpr const char* kSelectorShapeFiles[] = {
    // kArrow NAO carrega arquivo desde a spec 092 — a seta virou path. A
    // entrada fica porque o enum indexa este array por posicao; tirar a linha
    // deslocaria todos os cantos em um. O nome sobrevive so como marcador.
    "(seta: path, sem arquivo)",
    "selector_shape_corner_top_left.png",
    "selector_shape_corner_top_right.png",
    "selector_shape_corner_bottom_left.png",
    "selector_shape_corner_bottom_right.png",
    "selector_shape_hand.png",
};

// Cores de marca (spec 084) para uso com Selector::Variant::kCustom.
const NVGcolor kSelectorGreen = nvgRGB(0x31, 0xBF, 0x43);
const NVGcolor kSelectorBlue = nvgRGB(0x20, 0x94, 0xEB);
const NVGcolor kSelectorOrange = nvgRGB(0xE7, 0x52, 0x29);
// Teal da seta de MENU (spec 093) — a variante 36x60 de borda branca.
const NVGcolor kSelectorTeal = nvgRGB(0x2F, 0xA5, 0xA0);

class Selector : public brls::Image {
 public:
  enum class Variant { kPrimary, kSecondary, kCustom };
  // Sentido horario a partir de cima — a conta de rotacao depende da ordem.
  enum class Dir { kUp, kRight, kDown, kLeft };

  explicit Selector(Variant variant, Dir dir = Dir::kRight,
                    NVGcolor color = kSelectorGreen,
                    SelectorShape shape = SelectorShape::kArrow)
      : variant_(variant), color_(color), shape_(shape) {
    // Nada de imagem no construtor: a seta e path (spec 092) e as demais
    // formas sao tingidas em runtime pelo draw().
    // A arte nasce apontando para a DIREITA em todas as variantes (TD-02).
    quarter_turns_ = static_cast<int>(dir) - static_cast<int>(Dir::kRight);
    quarter_turns_ = (quarter_turns_ + 4) % 4;
  }

  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    (void)style; (void)ctx;
    const float cx = x + w / 2;
    const float cy = y + h / 2;

    // A SETA e path (spec 092), em qualquer variante. O bico nasce apontando
    // para baixo; `quarter_turns_` ja traz o giro pedido no construtor, e
    // como a Dir kRight e a referencia (TD-02), somamos o giro que leva de
    // "baixo" para "direita".
    if (shape_ == SelectorShape::kArrow) {
      nvgSave(vg);
      nvgTranslate(vg, cx, cy);
      nvgRotate(vg, (quarter_turns_ - 1) * NVG_PI / 2.0f);
      // Depois do giro o bico aponta para +Y local: desenha a partir do
      // centro, com metade da altura para cada lado.
      ArrowPath(vg, w, h, color_);
      nvgRestore(vg);
      return;
    }

    // Cantos e mao: textura tingida, girando em torno do centro da view.
    nvgSave(vg);
    nvgTranslate(vg, cx, cy);
    nvgRotate(vg, quarter_turns_ * NVG_PI / 2.0f);
    nvgTranslate(vg, -cx, -cy);
    int iw = 0, ih = 0;
    const int handle = TintedHandle(
        vg, kSelectorShapeFiles[static_cast<int>(shape_)], color_, iw, ih);
    if (handle > 0) Paint(vg, handle, x, y, w, h);
    nvgRestore(vg);
  }

  // --- A seta, desenhada como PATH (spec 092) -------------------------------
  //
  // Era uma textura PNG tingida em runtime; virou path para ganhar o gradiente
  // e a borda, que a textura nao dava. As medidas sao as aprovadas pelo dono
  // na folha de assets: 38 x 22 visiveis, borda 2px, cantos arredondados.
  //
  // O path nasce apontando para BAIXO com o bico na origem — as outras tres
  // direcoes saem por rotacao, e e por isso que a flutuacao pode ser sempre no
  // mesmo eixo local (ver ArrowFloat).
  //
  // Duas VARIANTES de seta (spec 093), mesma forma em proporcoes diferentes:
  //
  //   kSelecao — o cursor da caixa. 38 x 22 (larga e baixa), borda 2px
  //              escura. A cor vem do modo (laranja/azul/verde).
  //   kMenu    — a seta de foco dos menus. 36 x 60 (estreita e alta), borda
  //              4px BRANCA, teal #2FA5A0 fixo.
  //
  // "Largura" e sempre medida na base da seta e "altura" do bico a base, na
  // pose ORIGINAL (apontando para baixo) — quem gira nao precisa reinterpretar.
  static constexpr float kArrowW = 38.0f;
  static constexpr float kArrowH = 22.0f;
  static constexpr float kMenuArrowW = 36.0f;
  static constexpr float kMenuArrowH = 60.0f;

  // Deslocamento da flutuacao, no eixo do BICO. Sempre positivo: o giro do
  // chamador leva para a direcao certa. 2.4px / ciclo de 1.6s a 60fps.
  static float ArrowFloat(unsigned tick) {
    const float t = static_cast<float>(tick) * (2.0f * NVG_PI / 96.0f);
    return (1.0f - std::cos(t)) * 0.5f * 2.4f;
  }

  // Desenha a seta com o bico em (0, 0) apontando para BAIXO, no espaco
  // corrente. Quem chama posiciona e gira.
  //
  // `stroke_w`/`stroke_c` parametrizam a borda: a seta da caixa usa 2px
  // escuros, a de menu 4px brancos (spec 093).
  // `round` escala o arredondamento dos tres vertices. As duas variantes
  // pedem valores diferentes (rodada 5): a de menu, grande, comporta cantos
  // generosos; o cursor da caixa, pequeno, fica melhor mais anguloso.
  static void ArrowPath(NVGcontext* vg, float w, float h, NVGcolor color,
                        float stroke_w = 2.0f,
                        NVGcolor stroke_c = nvgRGB(0x5A, 0x5A, 0x5A),
                        float round = 1.0f) {
    const float hw = w / 2;
    // Os raios saem da MENOR dimensao: numa seta muito mais longa que larga
    // (a de menu, 36x60), derivar so da largura deixava o arredondamento
    // desproporcional ao lado curto.
    //
    // O bico e MAIS arredondado que os ombros, nao menos (rodada 4). O
    // angulo ali e agudo, entao o mesmo raio produz uma ponta visivelmente
    // mais afiada — compensar com um raio maior e o que iguala a leitura.
    const float base = std::min(w, h);
    const float r = base * 0.16f * round;     // ombros
    const float tipr = base * 0.20f * round;  // bico, compensa o angulo agudo
    // TRIANGULO DE LADOS RETOS com os tres vertices arredondados (spec 093
    // rodada 3). A versao anterior curvava os lados com nvgQuadTo desde o
    // ombro, e a seta perdia a cara de triangulo — foi o que o dono viu.
    //
    // nvgArcTo faz exatamente isto: anda em reta ate perto do vertice, curva
    // com o raio pedido e sai em reta para o proximo. Os tres pontos abaixo
    // sao os vertices IDEAIS do triangulo; os arcos so cortam os cantos.
    const float ax = -hw, ay = -h;  // ombro esquerdo
    const float bx = hw,  by = -h;  // ombro direito
    const float cx0 = 0.0f, cy0 = 0.0f;  // bico

    nvgBeginPath(vg);
    // Comeca no meio do lado esquerdo, para o primeiro arco ter reta de sobra.
    nvgMoveTo(vg, (ax + cx0) / 2, (ay + cy0) / 2);
    nvgArcTo(vg, cx0, cy0, bx, by, tipr);  // bico
    nvgArcTo(vg, bx, by, ax, ay, r);       // ombro direito
    nvgArcTo(vg, ax, ay, cx0, cy0, r);     // ombro esquerdo
    nvgClosePath(vg);

    // Gradiente vertical: claro em cima, a cor cheia no bico.
    NVGcolor top = nvgRGBA(
        static_cast<unsigned char>(std::min(255.0f, color.r * 255 + 26)),
        static_cast<unsigned char>(std::min(255.0f, color.g * 255 + 45)),
        static_cast<unsigned char>(std::min(255.0f, color.b * 255 + 33)), 255);
    NVGpaint grad = nvgLinearGradient(vg, 0, -h, 0, 0, top, color);
    nvgFillPaint(vg, grad);
    nvgFill(vg);

    nvgStrokeColor(vg, stroke_c);
    nvgStrokeWidth(vg, stroke_w);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
  }

  // Desenho direto, para o draw() de outra view — e o caso da seta de foco,
  // que flutua FORA do retangulo do botao e anima; um filho em layout nao
  // faria nenhum dos dois. `right_x` e onde a ponta encosta; `tick` e a
  // contagem de frames da view chamadora (cada uma tem a sua, senao setas de
  // telas diferentes pulsariam em sincronia — regra da spec 043).
  //
  // Aponta para a DIREITA: o path nasce para baixo, entao gira -90 graus.
  //
  // Esta e a seta de MENU (spec 093): 36 x 60 fixos, borda 4px branca, teal.
  // O `height` do chamador e ignorado — a variante tem tamanho proprio, e as
  // tres telas que a usam pediam alturas diferentes (40 e 44) so porque a
  // seta antiga derivava a largura da altura.
  static void Draw(NVGcontext* vg, Variant v, float right_x, float cy,
                   float height, unsigned tick,
                   NVGcolor color = kSelectorTeal,
                   SelectorShape shape = SelectorShape::kArrow) {
    (void)v; (void)shape; (void)height;
    nvgSave(vg);
    // Bico em right_x, girado para a direita, flutuando no eixo do bico.
    // -90 graus: o bico, que aponta para +Y (baixo), passa a apontar para +X.
    nvgTranslate(vg, right_x + ArrowFloat(tick), cy);
    nvgRotate(vg, -NVG_PI / 2.0f);
    // EIXOS TROCADOS de proposito. No ArrowPath, `w` e a BASE da seta e `h` o
    // comprimento do bico ate a base. Apontando para a direita, a base fica
    // na VERTICAL da tela (os 60 de altura) e o comprimento na HORIZONTAL
    // (os 36 de largura). Passar na ordem literal deixava a seta deitada —
    // foi o que o dono viu.
    //
    // A borda BRANCA de 4px e o que distingue a variante de menu.
    ArrowPath(vg, kMenuArrowH, kMenuArrowW, color, 4.0f, kWhite);
    nvgRestore(vg);
  }

  // Variante apontando para BAIXO, sem animacao — o cursor de caixa e
  // estatico, como na referencia do HOME. `tip_y` e onde o bico encosta.
  // A tertiary nascia apontando para baixo (nao precisava girar); a mascara
  // kArrow nasce para a DIREITA (TD-02), entao gira 90 graus aqui. As outras
  // formas kCustom (cantos, mao) ja nascem na pose final e nao giram.
  // `width` > 0 desliga a proporcao da mascara e fixa a largura final — o
  // cursor da caixa precisa de uma seta mais larga que alta (spec 089).
  // `width` > 0 fixa a largura final; senao usa a proporcao 38x22 da forma.
  // `tick` anima a flutuacao — passe 0 para uma seta parada.
  static void DrawDown(NVGcontext* vg, Variant v, float cx, float tip_y,
                       float height, NVGcolor color = kSelectorGreen,
                       SelectorShape shape = SelectorShape::kArrow,
                       float width = 0.0f, unsigned tick = 0) {
    // Formas que NAO sao a seta (cantos, mao) continuam vindo de textura
    // tingida — so a seta virou path (spec 092).
    if (shape != SelectorShape::kArrow) {
      int iw = 0, ih = 0;
      const int handle = ResolveHandle(vg, v, color, shape, iw, ih);
      if (handle <= 0) return;
      const float w = width > 0 ? width : height * static_cast<float>(iw) / ih;
      // A flutuacao vale para QUALQUER forma (rodada 12): a mao usa este
      // ramo e precisa do mesmo vai-e-vem da seta. Antes o `tick` so era
      // lido no caminho do path, e a mao ficava parada mesmo recebendo-o.
      Paint(vg, handle, cx - w / 2, tip_y - height + ArrowFloat(tick), w,
            height);
      return;
    }
    (void)v;
    const float w = width > 0 ? width : height * (kArrowW / kArrowH);
    nvgSave(vg);
    nvgTranslate(vg, cx, tip_y + ArrowFloat(tick));
    // round=0.5 -> bico em 0.10 (metade dos 0.20 da de menu). O cursor da
    // caixa e pequeno e ficava redondo demais com o arredondamento cheio.
    ArrowPath(vg, w, height, color, 2.0f, nvgRGB(0x5A, 0x5A, 0x5A), 0.5f);
    nvgRestore(vg);
  }

  // Desenha UM canto da moldura de area, no tamanho e posicao pedidos
  // (spec 088). Existe para a celula da grade nao precisar conhecer textura
  // nem tingimento — ela diz "canto superior esquerdo aqui" e pronto.
  static void DrawCorner(NVGcontext* vg, SelectorShape shape, NVGcolor color,
                         float x, float y, float size) {
    int iw = 0, ih = 0;
    const int handle =
        TintedHandle(vg, kSelectorShapeFiles[static_cast<int>(shape)], color,
                     iw, ih);
    if (handle > 0) Paint(vg, handle, x, y, size, size);
  }

 private:
  Variant variant_ = Variant::kPrimary;
  NVGcolor color_ = kSelectorGreen;
  SelectorShape shape_ = SelectorShape::kArrow;

  // As variantes fixas nao carregam mais PNG (spec 092): a seta virou path e
  // as outras formas sempre foram tingidas. Sobrou este resolvedor, que
  // atende cantos e mao.
  static int ResolveHandle(NVGcontext* vg, Variant v, NVGcolor color,
                           SelectorShape shape, int& iw, int& ih) {
    (void)v;
    return TintedHandle(vg, kSelectorShapeFiles[static_cast<int>(shape)],
                        color, iw, ih);
  }

  // Handle tingido por (arquivo de mascara, cor), criado e cacheado no
  // primeiro uso de cada combinacao — a mascara e decodificada uma vez via
  // stb_image e cada cor pedida gera uma textura nova (spec 084). A mascara
  // separa contorno de preenchimento por SATURACAO HSV (scripts/
  // make-selector-masks.py, TD-01): pixel de baixa saturacao fica cinza
  // neutro (contorno), pixel de alta saturacao vira branco (preenchimento,
  // onde a cor pedida entra).
  static int TintedHandle(NVGcontext* vg, const char* shape_file,
                          NVGcolor color, int& iw, int& ih) {
    struct Cached {
      std::string key;
      int handle;
      int w, h;
    };
    static std::vector<Cached> cache;

    char key_buf[96];
    std::snprintf(key_buf, sizeof(key_buf), "%s|%02x%02x%02x", shape_file,
                 static_cast<int>(color.r * 255),
                 static_cast<int>(color.g * 255),
                 static_cast<int>(color.b * 255));
    const std::string key = key_buf;
    for (const auto& c : cache) {
      if (c.key == key) {
        iw = c.w;
        ih = c.h;
        return c.handle;
      }
    }

    int w, h, n;
    const std::string path = std::string(POKEHOME_UI_ASSETS) + shape_file;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) {
      cache.push_back({key, 0, 0, 0});
      iw = ih = 0;
      return 0;
    }

    const unsigned char gray = 60;
    const unsigned char cr = static_cast<unsigned char>(color.r * 255);
    const unsigned char cg = static_cast<unsigned char>(color.g * 255);
    const unsigned char cb = static_cast<unsigned char>(color.b * 255);
    for (int i = 0; i < w * h; ++i) {
      unsigned char* p = px + i * 4;
      const unsigned char v = p[0];  // mascara: R=G=B (cinza)
      if (p[3] == 0) continue;
      const float t =
          std::max(0.0f, std::min(1.0f, (v - gray) / float(255 - gray)));
      p[0] = static_cast<unsigned char>(gray + t * (cr - gray));
      p[1] = static_cast<unsigned char>(gray + t * (cg - gray));
      p[2] = static_cast<unsigned char>(gray + t * (cb - gray));
    }

    const int handle = nvgCreateImageRGBA(vg, w, h, 0, px);
    stbi_image_free(px);
    cache.push_back({key, handle, w, h});
    iw = w;
    ih = h;
    return handle;
  }

  static void Paint(NVGcontext* vg, int handle, float x, float y, float w,
                    float h) {
    NVGpaint p = nvgImagePattern(vg, x, y, w, h, 0, handle, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, p);
    nvgFill(vg);
  }

  int quarter_turns_ = 0;
};

// Grupo de modos do cabecalho (spec 052): a faixa com os tres icones e o
// seletor quadrado vermelho em volta do ativo, como no HOME.
//
// Um draw() proprio em vez de tres Box: a faixa e um gradiente e o seletor
// tem duas bordas (externa cinza, interna cinza clara) — nada disso sai de
// cor solida com cornerRadius.
// Medidas 1:1 com a folha de assets (spec 094), escaladas de 72 para os 42px
// de altura real da barra — fator 42/72 = 0.583.
//
//   folha        app
//   FUNDO 460x72 -> 268 x 42
//   passo    92  -> 54
//   icone    40  -> 23
constexpr float kModeIcon = 23.0f;   // lado do icone
constexpr float kModeCell = 54.0f;   // passo entre icones
constexpr float kModeCount = 3;
constexpr float kModeStripH = 42.0f;  // a altura de REFERENCIA (spec 094)

// Cores da folha. A placa do ativo e VERDE ESCURO — o vermelho e so a
// moldura, ao contrario do que o app fazia antes.
const NVGcolor kModeSelRed = nvgRGB(0xF0, 0x52, 0x2A);
const NVGcolor kModeSelDark = nvgRGB(0x2B, 0x3A, 0x33);
const NVGcolor kModeSelPlate = nvgRGB(0x3E, 0x7A, 0x63);
const NVGcolor kModeShoulder = nvgRGB(0x4E, 0x6B, 0x58);

class ModeStrip : public brls::Box {
 public:
  // Ordem visual: trocar, mover, selecionar (a arte veio noutra ordem).
  static constexpr const char* kFiles[3] = {
      "mode_trocar.png", "mode_mover.png", "mode_selecionar.png"};

  void SetActive(int i) { active_ = i; }

  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    // Faixa RETA (spec 094): na folha ela nao tem cantos arredondados — o
    // que some nas pontas e a COR, pelo degrade, nao a forma.
    //
    // Duas passadas de linearGradient (uma por metade) no lugar de um
    // boxGradient: o box irradia do centro e concentrava a cor no meio.
    // As paradas da folha (spec 094 rodada 2):
    //
    //   0%    transparente
    //   18%   alpha .55
    //   50%   alpha .75   (o miolo, levemente mais forte)
    //   82%   alpha .55
    //   100%  transparente
    //
    // O nanovg so tem gradiente de DUAS paradas, entao a faixa sai em quatro
    // trechos — um por intervalo. Antes eram dois trechos com a zona de
    // esmaecimento em 30%, o que deixava a borda dura que o dono viu.
    const NVGcolor band = nvgRGB(0x8F, 0xCF, 0xA8);
    const NVGcolor mid = nvgRGB(0x7F, 0xC7, 0x9C);
    auto rgba = [](NVGcolor c, float a) {
      return nvgRGBAf(c.r, c.g, c.b, a);
    };

    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, w, h);
    auto band_seg = [&](float f0, float f1, NVGcolor c0, float a0,
                        NVGcolor c1, float a1) {
      const float x0 = x + w * f0, x1 = x + w * f1;
      nvgBeginPath(vg);
      nvgRect(vg, x0, y, x1 - x0, h);
      // O gradiente e criado nos MESMOS limites do retangulo: fora deles o
      // nanovg repete a cor da ponta, o que emendaria os trechos errado.
      NVGpaint p = nvgLinearGradient(vg, x0, y, x1, y, rgba(c0, a0),
                                     rgba(c1, a1));
      nvgFillPaint(vg, p);
      nvgFill(vg);
    };
    band_seg(0.00f, 0.18f, band, 0.00f, band, 0.55f);
    band_seg(0.18f, 0.50f, band, 0.55f, mid, 0.75f);
    band_seg(0.50f, 0.82f, mid, 0.75f, band, 0.55f);
    band_seg(0.82f, 1.00f, band, 0.55f, band, 0.00f);
    nvgRestore(vg);

    // Os ombros ZL/ZR da folha NAO sao desenhados aqui: o cabecalho ja tem
    // dois Labels com os glifos do Switch (`kGlyphZL`/`kGlyphZR`) fora da
    // faixa. Duplicar produziria dois pares na tela — a folha os mostra
    // dentro do SVG so porque la a barra e um desenho unico.

    const float cy = y + h / 2;
    for (int i = 0; i < 3; ++i) {
      const float cx = x + w / 2 + (i - 1) * kModeCell;

      if (i == active_) {
        // Moldura em TRES CAMADAS (spec 094), de fora para dentro:
        //   escura 2px -> vermelho 8px -> escura 2px -> placa verde
        //
        // A PLACA tem a altura do fundo (42): e o alinhamento da folha, onde
        // a linha interna coincide com a faixa. O vermelho e so a moldura —
        // o app antes pintava a placa inteira de vermelho.
        // A moldura cresce para FORA da faixa, e a barra do cabecalho tem
        // 58px contra os 42 da faixa: sobram 8 de cada lado. Por isso a
        // placa e menor que a altura cheia — assim as tres camadas cabem
        // sem serem cortadas, preservando as espessuras (2 / 8 / 2).
        const float pw = 34.0f, ph = h - 12.0f;
        const float px = cx - pw / 2, py = y + 6.0f;
        auto layer = [&](float grow, NVGcolor c, float rr) {
          nvgBeginPath(vg);
          nvgRoundedRect(vg, px - grow, py - grow, pw + grow * 2,
                         ph + grow * 2, rr);
          nvgFillColor(vg, c);
          nvgFill(vg);
        };
        layer(12.0f, kModeSelDark, 3.0f);   // externa 2
        layer(10.0f, kModeSelRed, 2.0f);    // vermelho 8
        layer(2.0f, kModeSelDark, 2.0f);    // interna 2
        layer(0.0f, kModeSelPlate, 1.0f);   // placa
      }

      int handle = Handle(vg, i);
      if (handle <= 0) continue;
      const float ix = cx - kModeIcon / 2, iy = cy - kModeIcon / 2;
      // O PNG e quase-branco: no ativo fica branco puro sobre o vermelho, e
      // fora dele escurece para aparecer sobre a faixa clara.
      NVGpaint p = nvgImagePattern(vg, ix, iy, kModeIcon, kModeIcon, 0,
                                   handle, i == active_ ? 1.0f : 0.55f);
      nvgBeginPath(vg);
      nvgRect(vg, ix, iy, kModeIcon, kModeIcon);
      nvgFillPaint(vg, p);
      nvgFill(vg);
    }

    brls::Box::draw(vg, x, y, w, h, style, ctx);
  }

 private:
  static int Handle(NVGcontext* vg, int i) {
    static int handle[3] = {0, 0, 0};
    if (handle[i] == 0) {
      handle[i] = nvgCreateImage(
          vg, (std::string(POKEHOME_UI_ASSETS) + kFiles[i]).c_str(), 0);
    }
    return handle[i];
  }

  int active_ = 0;
};

// --- Celula da grade -------------------------------------------------------

// Celula focavel. Em vez de disputar as setas com o borealis, cada celula e
// uma view focavel e o proprio framework move o foco entre elas — e para isso
// que Box::getNextFocus existe. O callback avisa a tela para atualizar o
// rodape.
class GridRow;
// Definida depois de GridRow: avisa a linha qual coluna ganhou foco.
void RememberFocusColumn(brls::View* child);

class SlotCell : public brls::Box {
 public:
  using FocusCallback = std::function<void(std::size_t)>;

  SlotCell(std::size_t index, FocusCallback on_focus)
      : brls::Box(brls::Axis::COLUMN),
        index_(index),
        on_focus_(std::move(on_focus)) {
    setFocusable(true);
    // Tamanho FIXO (spec 048): sem grow/shrink, os dois paineis ficam
    // identicos independentemente do espaco que sobra na tela.
    setSize(brls::Size(kSlotW, kSlotH));
    setMargins(kSlotGapY / 2, kSlotGapX / 2, kSlotGapY / 2, kSlotGapX / 2);
    // O raio serve a borda de foco/selecao, que o borealis desenha seguindo o
    // cornerRadius. O FUNDO nao vem daqui: e o circulo esfumado do draw() —
    // setBackgroundColor pintaria a celula inteira, virando a elipse solida
    // que a rodada 3 da spec 046 mandou tirar.
    setCornerRadius(999.0f);
    // Sem o highlight azul do borealis: o cursor visivel e a seta + barra do
    // BoxFrame. Ele ficou aparente quando a borda laranja propria saiu.
    setHideHighlight(true);
    setJustifyContent(brls::JustifyContent::CENTER);
    setAlignItems(brls::AlignItems::CENTER);

    sprite_ = new brls::Image();
    // Arte em pixel fixo (spec 048), nao porcentagem da celula.
    sprite_->setSize(brls::Size(kSpriteSize, kSpriteSize));
    sprite_->setScalingType(brls::ImageScalingType::STRETCH);
    sprite_->setInterpolation(brls::ImageInterpolation::NEAREST);
    addView(sprite_);

    // Segundo sprite, sobreposto ao primeiro (POSITION_ABSOLUTE tira do fluxo
    // do Box): marca o slot de ORIGEM enquanto o Pokemon esta na mao — fica
    // parado e esmaecido aqui, sem subir com o cursor. Ver SetOrigin().
    origin_ = new brls::Image();
    origin_->setSize(brls::Size(kSpriteSize, kSpriteSize));
    origin_->setScalingType(brls::ImageScalingType::STRETCH);
    origin_->setInterpolation(brls::ImageInterpolation::NEAREST);
    origin_->setVisibility(brls::Visibility::GONE);
    origin_->setPositionType(brls::PositionType::ABSOLUTE);
    origin_->setPositionTop((kSlotH - kSpriteSize) / 2);
    origin_->setPositionLeft((kSlotW - kSpriteSize) / 2);
    addView(origin_);

    // Terceiro sprite: o Pokemon NA MAO, levantado sobre esta celula enquanto
    // o cursor passa por ela. Precisa ser proprio, e nao reaproveitar o
    // sprite_, senao passar o cursor por um slot ocupado apagava o Pokemon
    // que mora nele (spec 085 rodada 8). Ver SetHeld().
    held_sprite_ = new brls::Image();
    held_sprite_->setSize(brls::Size(kSpriteSize, kSpriteSize));
    held_sprite_->setScalingType(brls::ImageScalingType::STRETCH);
    held_sprite_->setInterpolation(brls::ImageInterpolation::NEAREST);
    held_sprite_->setVisibility(brls::Visibility::GONE);
    held_sprite_->setPositionType(brls::PositionType::ABSOLUTE);
    // Levanta 30% da altura do slot (rodada 8; era 50%). Com 50% o Pokemon
    // subia alto demais e a MAO nao alcancava a base da faixa — os dois
    // precisam ler como "a mao esta segurando este Pokemon".
    held_sprite_->setPositionTop((kSlotH - kSpriteSize) / 2 - kSlotH * 0.3f);
    held_sprite_->setPositionLeft((kSlotW - kSpriteSize) / 2);
    addView(held_sprite_);

    addGestureRecognizer(new brls::TapGestureRecognizer(this));
  }

  // Textura dos icones do slot, cacheada por nome de arquivo. Sem cache,
  // `nvgCreateImage` dentro do draw() criaria uma textura por quadro, para
  // cada celula. Handle invalido tambem fica cacheado — e o que impede o
  // nanovg de reabrir um PNG ausente a cada frame.
  static int SlotIconHandle(NVGcontext* vg, const char* file) {
    struct Cached {
      std::string file;
      int handle;
    };
    static std::vector<Cached> cache;
    for (const auto& c : cache) {
      if (c.file == file) return c.handle;
    }
    const int handle =
        nvgCreateImage(vg, (std::string(POKEHOME_UI_ASSETS) + file).c_str(), 0);
    cache.push_back({file, handle});
    return handle;
  }

  // Os dois icones de aviso (spec 102). Desenhados no draw(), nao como Image
  // filho: sao 24px em posicao absoluta e o da esquerda VAZA 2px do slot —
  // como filho, o recorte do Box poderia corta-lo.
  //
  // A arte veio do dono e foi recortada para 24 x 24 em `romfs/ui/`. Ambos
  // ocupam um canvas quadrado do maior lado antes de reduzir, entao os dois
  // pesam igual no slot mesmo com silhuetas diferentes (circulo e triangulo).
  void DrawWarnIcons(NVGcontext* vg, float x, float y) const {
    auto icon = [&](const char* file, float ox, float oy) {
      const int tex = SlotIconHandle(vg, file);
      if (tex <= 0) return;
      NVGpaint p = nvgImagePattern(vg, x + ox, y + oy, kSlotIcon, kSlotIcon, 0,
                                   tex, 1.0f);
      nvgBeginPath(vg);
      nvgRect(vg, x + ox, y + oy, kSlotIcon, kSlotIcon);
      nvgFillPaint(vg, p);
      nvgFill(vg);
    };
    // Esquerda: golpe que nao existe no jogo do painel (spec 038).
    if (warned_) icon("slot_warn.png", kSlotIconLeftX, kSlotIconLeftY);
    // Direita: nao vai sair daqui — especie ausente no jogo (spec 034) ou
    // reprovado pelo verificador (spec 082). Os dois compartilham o icone
    // porque dizem a mesma coisa ao jogador (decisao do dono, spec 102).
    if (blocked_ || suspect_) {
      icon("slot_block.png", kSlotIconRightX, kSlotIconRightY);
    }
  }

  // Fundo do slot: circulo com a borda esfumada, desenhado antes dos filhos.
  // Radial em vez de cor de fundo por dois motivos: a celula e mais larga que
  // alta (cor de fundo viraria elipse) e a referencia nao tem aresta — o
  // circulo esmaece ate sumir no corpo do cartao.
  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    const float cx = x + w / 2;
    const float cy = y + h / 2;
    const float r = std::min(w, h) * 0.5f;

    // Quem marca o foco e a seta do BoxFrame (rodada 5) — a celula nao muda
    // de cor. O fundo acompanha so o estado do PAINEL, como o corpo do cartao.
    //
    // O dourado de shiny (spec 025) saiu na spec 102: o sprite shiny ja tem
    // cor propria, entao o fundo era informacao repetida. Ele tambem estava
    // mentindo — `is_shiny()` cai no calculo gen3 (limiar 8) quando a fonte
    // moderna nao marca `display_shiny`, e num save do Brilliant Diamond a
    // caixa inteira aparecia dourada.
    const NVGcolor tint = panel_focused_ ? kSlotTintOn : kSlotTintOff;
    NVGpaint p = nvgRadialGradient(vg, cx, cy, r * 0.45f, r, tint,
                                   nvgRGBAf(tint.r, tint.g, tint.b, 0.0f));
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgFillPaint(vg, p);
    nvgFill(vg);

    // Sombra do sprite levantado (spec 085): a silhueta do Pokemon, escura.
    // A textura vem de SilhouetteHandle — RGB escurecido, alpha do PNG
    // preservado —, entao o recorte e o contorno do bicho, nao um quadrado.
    // Tingir por composicao nao funciona aqui: NVG_ATOP testa o alpha do
    // FRAMEBUFFER, que o fundo opaco do slot ja preencheu.
    // Deslocada da posicao do sprite: 30% da largura para a direita, 20% da
    // altura para baixo, como a projecao de algo erguido na mao.
    if (held_ && !held_sprite_path_.empty()) {
      int iw = 0, ih = 0;
      const int shadow_tex = SilhouetteHandle(vg, held_sprite_path_, iw, ih);
      if (shadow_tex != 0) {
        const float sw = kSpriteSize;
        const float sh = kSpriteSize;
        const float sx = cx - sw / 2 + sw * 0.3f;
        // O MESMO 0.3f do held_sprite_: se divergirem, a sombra descola do
        // Pokemon que ela projeta.
        const float sy = (cy - kSlotH * 0.3f) - sh / 2 + sh * 0.2f;

        NVGpaint mask =
            nvgImagePattern(vg, sx, sy, sw, sh, 0, shadow_tex, 0.5f);
        nvgBeginPath(vg);
        nvgRect(vg, sx, sy, sw, sh);
        nvgFillPaint(vg, mask);
        nvgFill(vg);
      }
    }

    brls::Box::draw(vg, x, y, w, h, style, ctx);

    // Icones de aviso (spec 102), por cima do sprite e por baixo do veu da
    // area: eles falam do Pokemon, e a area e um gesto que cobre tudo.
    DrawWarnIcons(vg, x, y);

    // Area do modo Selecao (spec 088), DEPOIS dos filhos: o veu passa por
    // cima dos sprites, como nas capturas do HOME. Sem vao entre celulas —
    // o retangulo cobre tambem os gaps, senao a area sairia listrada.
    if (inArea_) {
      // EXATAMENTE meio vao para cada lado — sem folga extra.
      //
      // O veu e translucido: onde duas celulas se sobrepoem, o alpha soma e
      // aquela faixa fica mais escura. Com `+1` de folga (como estava), toda
      // divisa entre celulas ganhava 2px de dupla pintura, e a area saia
      // desenhando uma GRADE em vez de uma cor chapada — foi o que o dono
      // viu. Com meio vao exato as bordas se encontram sem sobrepor.
      const float gx = kSlotGapX / 2;
      const float gy = kSlotGapY / 2;
      nvgBeginPath(vg);
      nvgRect(vg, x - gx, y - gy, w + gx * 2, h + gy * 2);
      nvgFillColor(vg, nvgRGBA(0xC5, 0xDE, 0xC9, 0xB0));
      nvgFill(vg);

      // Os cantos so nas PONTAS do retangulo — a arte ja vem do romfs
      // (spec 084), tingida de verde em runtime.
      const float cs = 20.0f;  // lado do canto
      const float ax = x - gx, ay = y - gy;
      const float bx2 = x + w + gx, by = y + h + gy;
      auto corner = [&](SelectorShape shape, float px, float py) {
        Selector::DrawCorner(vg, shape, kSelectorGreen, px, py, cs);
      };
      if (cornerTL_) corner(SelectorShape::kCornerTopLeft, ax, ay);
      if (cornerTR_) corner(SelectorShape::kCornerTopRight, bx2 - cs, ay);
      if (cornerBL_) corner(SelectorShape::kCornerBottomLeft, ax, by - cs);
      if (cornerBR_) corner(SelectorShape::kCornerBottomRight, bx2 - cs,
                            by - cs);
    }
  }

  // Nenhuma borda de foco aqui: o cursor visivel e a seta vermelha do
  // BoxFrame, que se posiciona sozinha lendo a celula focada (rodada 5).
  void onFocusGained() override {
    brls::Box::onFocusGained();
    RememberFocusColumn(this);
    if (on_focus_) on_focus_(index_);
  }

  void SetMon(const g3::BoxPokemon& mon) {
    const std::string path = mon.empty() ? "" : SpritePath(mon);
    // A visao geral (spec 105) troca a interpolacao para LINEAR; o sprite de
    // Pokemon volta ao NEAREST do pixel art.
    sprite_->setInterpolation(brls::ImageInterpolation::NEAREST);
    if (path.empty()) {
      sprite_->setVisibility(brls::Visibility::GONE);
    } else {
      sprite_->setImageFromFile(path);
      sprite_->setVisibility(brls::Visibility::VISIBLE);
    }
    // Repor o sprite normal encerra TODO o estado transitorio desta celula:
    // o levantado, a sombra E o fantasma da origem. Sem isto eles sobreviviam
    // a um cancelamento (B), a uma troca de caixa ou a uma troca fechada —
    // quem limpava era so o rastro do cursor em OnCursorMoved, e Refresh()
    // zera esse rastro antes de passar por aqui (spec 085 rodada 5).
    //
    // O fantasma entrou aqui na spec 087: a troca que fecha num gesto deixava
    // o "Pokemon claro" empilhado com o novo ocupante do slot.
    held_sprite_->setVisibility(brls::Visibility::GONE);
    held_ = false;
    held_sprite_path_.clear();
    origin_->setVisibility(brls::Visibility::GONE);
    // O esmaecimento de origem tambem morre aqui: sem isto um slot que foi
    // origem continuaria apagado depois que o bloco fosse solto (spec 088).
    origin_faded_sprite_ = false;
    sprite_->setAlpha(1.0f);

  }

  // Sprite do Pokemon que esta na mao, na cor normal e deslocado para cima do
  // slot — o "destacado" do HOME de verdade (spec 085, corrigindo a leitura
  // anterior de 41.jpg na spec 019, que so previa esmaecimento no lugar).
  void SetHeld(const g3::BoxPokemon& mon) {
    const std::string path = mon.empty() ? "" : SpritePath(mon);
    if (path.empty()) {
      held_sprite_->setVisibility(brls::Visibility::GONE);
      held_ = false;
      held_sprite_path_.clear();
      return;
    }
    // NAO mexe no sprite_: ele mostra o Pokemon que MORA neste slot, e o
    // levantado so esta de passagem. Reaproveitar o mesmo Image apagava o
    // ocupante ao passar o cursor por cima dele (spec 085 rodada 8).
    held_sprite_->setImageFromFile(path);
    held_sprite_->setVisibility(brls::Visibility::VISIBLE);
    held_ = true;
    held_sprite_path_ = path;  // o draw() gera a silhueta da sombra a partir dele
  }

  void ClearHeld() {
    held_sprite_->setVisibility(brls::Visibility::GONE);
    held_ = false;
    held_sprite_path_.clear();
  }

  // Marca ESTE slot como o lugar de onde o Pokemon na mao saiu: sprite parado
  // e esmaecido, sem o deslocamento do SetHeld — e o que sinaliza "foi daqui"
  // enquanto o jogador procura onde soltar (spec 085).
  //
  // Dois caminhos, porque as duas formas de "segurar" deixam o slot em
  // estados diferentes (spec 088 rodada 5):
  //
  //   - Pokemon UNICO: Pick() esvazia a origem no overlay, entao o `sprite_`
  //     ja sumiu e o fantasma precisa de um Image proprio (`origin_`).
  //   - BLOCO: levantar nao mexe no overlay, entao o `sprite_` continua ali,
  //     OPACO. Sobrepor `origin_` nao adianta — ele fica escondido atras.
  //     Aqui o certo e esmaecer o proprio `sprite_`.
  void SetOrigin(const g3::BoxPokemon& mon) {
    const std::string path = mon.empty() ? "" : SpritePath(mon);
    if (path.empty()) {
      origin_->setVisibility(brls::Visibility::GONE);
      return;
    }
    if (sprite_->getVisibility() == brls::Visibility::VISIBLE) {
      // O ocupante ainda esta desenhado: esmaece ELE, senao o fantasma
      // ficaria atras de um sprite opaco e nada mudaria na tela.
      sprite_->setAlpha(0.45f);
      origin_->setVisibility(brls::Visibility::GONE);
      origin_faded_sprite_ = true;
      return;
    }
    origin_->setImageFromFile(path);
    origin_->setVisibility(brls::Visibility::VISIBLE);
    origin_->setAlpha(0.45f);
  }

  void ClearOrigin() {
    origin_->setVisibility(brls::Visibility::GONE);
    if (origin_faded_sprite_) {
      sprite_->setAlpha(1.0f);
      origin_faded_sprite_ = false;
    }
  }

  // Visao geral das caixas (spec 105): a celula mostra UMA CAIXA, nao um
  // Pokemon. Chamar SetMon(vazio) antes; aqui so entra a arte do cubo —
  // ocupada (com pokebola) ou vazia (aberta). Os badges continuam pelos
  // setters de sempre (SetBlocked/SetWarned/SetSuspect).
  void SetBox(bool occupied) {
    sprite_->setInterpolation(brls::ImageInterpolation::LINEAR);
    sprite_->setImageFromFile(std::string(POKEHOME_UI_ASSETS) +
                              (occupied ? "box_full.png" : "box_empty.png"));
    sprite_->setVisibility(brls::Visibility::VISIBLE);
  }

  // O cursor esta no painel desta celula? Muda o tom do circulo vazio.
  void SetPanelFocused(bool on) { panel_focused_ = on; }

  // Marca de incompativel (spec 034): o Pokemon na mao nao existe no jogo
  // deste painel. Equivale ao icone vermelho de proibido do HOME (§2).
  void SetBlocked(bool on) {
    blocked_ = on;
    Repaint();
  }

  // Pokemon reprovado pelo verificador de legalidade (spec 079/082).
  //
  // Compartilha o icone da DIREITA com o bloqueio por especie (spec 034), e
  // nao um segundo vocabulario visual: os dois dizem a mesma coisa ao jogador
  // ("este nao vai sair daqui"). O que muda e a origem: aquele e do PAINEL (o
  // que esta na mao nao cabe aqui), este e do SLOT.
  //
  // O "!" que morava aqui saiu na spec 102 — era provisorio desde o inicio,
  // esperando exatamente o espaco de icone que agora existe.
  void SetSuspect(bool on) {
    if (suspect_ == on) return;
    suspect_ = on;
    Repaint();
  }

  // Marca de aviso de golpe (spec 038): a especie cabe, mas o Pokemon na mao
  // conhece um golpe que nao existe no jogo deste painel.
  //
  // Ao contrario de SetBlocked, isto e SO cosmetico — nenhum caminho que
  // decide o movimento consulta este estado. O sprite fica com alpha cheio de
  // proposito: o movimento e permitido, e esmaecer sugeriria o contrario.
  void SetWarned(bool on) {
    warned_ = on;
    Repaint();
  }

  // Slot marcado na multissselecao (spec 021). Borda grossa no tom de destaque
  // — o equivalente ao checkmark do cursor verde do HOME.
  void SetSelected(bool on) {
    selected_ = on;
    Repaint();
  }

  // Area do modo Selecao (spec 088): o veu verde-claro por cima e os cantos
  // verdes so nas PONTAS do retangulo, como nas capturas do HOME. Cada celula
  // desenha a sua parte — quem sabe a geometria e a tela, que passa os
  // quatro flags de canto.
  void SetAreaCell(bool in_area, bool corner_tl, bool corner_tr,
                   bool corner_bl, bool corner_br) {
    inArea_ = in_area;
    cornerTL_ = corner_tl;
    cornerTR_ = corner_tr;
    cornerBL_ = corner_bl;
    cornerBR_ = corner_br;
  }

 private:
  // UM lugar decide borda e opacidade. Antes cada setter repintava por conta
  // propria e a ordem das chamadas no Refresh e que fazia a precedencia — o
  // que ficou insustentavel ao entrar o quarto estado (suspeito, spec 082).
  //
  // Precedencia, do mais forte para o mais fraco (a mesma de antes):
  // selecionado > bloqueado/suspeito (vermelho) > aviso de golpe (amarelo).
  void Repaint() {
    // Bloqueio e aviso NAO mexem mais no sprite (spec 102): quem os comunica
    // sao os dois icones de 24px do DrawWarnIcons. Antes o sprite ia a 35% e a
    // celula ganhava borda vermelha/amarela — o dono trocou os dois
    // vocabularios por um so.
    //
    // O esmaecimento da ORIGEM sobrevive: ele nao e aviso sobre o Pokemon, e
    // sim a marca de "este saiu daqui" enquanto o bloco esta na mao
    // (specs 085/088).
    sprite_->setAlpha(origin_faded_sprite_ ? 0.45f : 1.0f);

    // So a SELECAO desenha borda. Ela e gesto do jogador, nao aviso — some
    // junto com o gesto, e por isso continua (spec 021).
    if (selected_) {
      setBorderColor(kMarked);
      setBorderThickness(5.0f);
    } else {
      setBorderThickness(0.0f);
    }
  }

  std::size_t index_;
  FocusCallback on_focus_;
  brls::Image* sprite_ = nullptr;
  brls::Image* origin_ = nullptr;
  brls::Image* held_sprite_ = nullptr;
  bool held_ = false;
  std::string held_sprite_path_;
  bool origin_faded_sprite_ = false;
  bool inArea_ = false;
  bool cornerTL_ = false, cornerTR_ = false, cornerBL_ = false,
       cornerBR_ = false;
  bool selected_ = false;
  bool panel_focused_ = false;
  bool blocked_ = false;
  bool warned_ = false;
  bool suspect_ = false;
};

// Icone amarelo piscando ao lado do golpe ausente na barra de status
// (spec 104). O mesmo `slot_warn.png` do slot (spec 102), reduzido a altura
// do texto; a textura vem do cache do SlotCell, entao as duas bocas do aviso
// dividem um handle so.
//
// O piscar e um seno sobre o contador de quadros — continuo, "sumindo e
// aparecendo bem lento", como o dono descreveu. Nao ha timer: o proprio
// draw() avanca o tick, igual a flutuacao da seta (spec 092).
class MoveWarnIcon : public brls::Box {
 public:
  MoveWarnIcon() {
    setSize(brls::Size(kMoveWarnIconSize, kMoveWarnIconSize));
    setVisibility(brls::Visibility::GONE);
  }

  void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style,
            brls::FrameContext*) override {
    const int tex = SlotCell::SlotIconHandle(vg, "slot_warn.png");
    if (tex <= 0) return;
    ++tick_;
    const float a =
        0.5f * (1.0f + std::sin(tick_ * 2.0f * NVG_PI / kMoveWarnBlinkFrames));
    NVGpaint p = nvgImagePattern(vg, x, y, w, h, 0, tex, a);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, p);
    nvgFill(vg);
  }

 private:
  int tick_ = 0;
};

// --- Painel de caixa -------------------------------------------------------

// Linha da grade que preserva a coluna ao navegar na vertical.
//
// O borealis escolhe o vizinho por INDICE, nao por posicao na tela. Descendo
// de uma linha ROW, o pai COLUMN chama getDefaultFocus() da proxima linha, que
// consulta lastFocusedView e depois defaultFocusedIndex (box.cpp:351) — ou
// seja, o foco vai para o ultimo cartao visitado daquela linha, nao para a
// coluna de onde se veio.
//
// A solucao e sobrescrever getDefaultFocus e devolver a coluna corrente. Nao
// mexemos em lastFocusedView nem em defaultFocusedIndex: escrever nesses
// campos durante a propagacao do foco derrubou o app.
class GridRow : public brls::Box {
 public:
  // A coluna e compartilhada por shared_ptr, nao por ponteiro cru: BoxPanel e
  // retornado por valor, entao &p.grid_column apontaria para um objeto morto
  // — foi o que derrubou o app ao abrir a caixa.
  explicit GridRow(std::shared_ptr<std::size_t> column)
      : column_(std::move(column)) {}

  brls::View* getDefaultFocus() override {
    auto& kids = getChildren();
    if (kids.empty()) return brls::Box::getDefaultFocus();

    const std::size_t want = std::min(*column_, kids.size() - 1);
    if (brls::View* v = kids[want]->getDefaultFocus()) return v;

    // Coluna sem celula focavel: cai no comportamento padrao.
    return brls::Box::getDefaultFocus();
  }

  // Chamada pela celula ao ganhar foco. So escreve num inteiro proprio — nada
  // de estado interno do borealis enquanto ele propaga o foco.
  void RememberColumn(const brls::View* child) {
    const auto& kids = getChildren();
    for (std::size_t i = 0; i < kids.size(); ++i) {
      if (kids[i] == child) {
        *column_ = i;
        return;
      }
    }
  }

 private:
  std::shared_ptr<std::size_t> column_;
};

void RememberFocusColumn(brls::View* child) {
  if (auto* row = dynamic_cast<GridRow*>(child->getParent())) {
    row->RememberColumn(child);
  }
}

// Cartao do painel (spec 046). O borealis so pinta cor solida e um raio unico,
// e a moldura da referencia precisa do que ele nao da: corpo e barra em cores
// que trocam com o foco, barra so com os cantos de cima arredondados, e a
// borda de selecao contornando o cartao inteiro. Por isso um draw() proprio.
class BoxFrame : public brls::Box {
 public:
  // `logo_left` posiciona o quadrado da logo no canto EXTERNO do cartao:
  // esquerda no painel da esquerda, direita no da direita (spec 051).
  BoxFrame(float bar_height, bool logo_left)
      : brls::Box(brls::Axis::COLUMN),
        bar_height_(bar_height),
        logo_left_(logo_left) {}

  // Qual dos dois paineis o cursor esta usando. Chamado a cada movimento do
  // foco; o borealis redesenha sozinho no quadro seguinte.
  void SetFocused(bool on) { focused_ = on; }

  // Capa do jogo no rodape (spec 101). Quem resolve o slug e a tela — o frame
  // so recebe o caminho pronto, porque `BoxPanel` so existe mais abaixo no
  // arquivo. Caminho vazio = sem arte: cai no quadrado branco de antes.
  //
  // O handle do nanovg e criado na PRIMEIRA pintura e guardado: criar textura
  // dentro do draw() a cada quadro vazaria uma por frame.
  void SetCoverPath(std::string path) { cover_path_ = std::move(path); }

  // Cursor do HOME (spec 046, rodada 5): seta vermelha sobre a celula focada
  // e, com Pokemon no slot, a barra escura com nome e level. label vazio =
  // slot vazio = so a seta. nullptr desliga (painel sem cursor).
  //
  // Vive no frame, nao na celula: a barra e mais larga que um slot e desliza
  // para nao vazar do cartao — so quem conhece os limites do cartao inteiro
  // consegue fazer esse clamp.
  void SetCursor(brls::View* cell, std::string label) {
    cursor_cell_ = cell;
    cursor_label_ = std::move(label);
  }

  // Cor da seta e do filete (spec 089): acompanha o modo de cursor, como as
  // tres cores de cursor do HOME. O BoxFrame nao conhece `CursorMode` — a
  // tela traduz o modo para cor e entrega pronta.
  void SetCursorColor(NVGcolor c) { cursor_color_ = c; }

  // Segurando algo? Troca a SETA pela MAO (spec 093): e o cursor de "pegou",
  // como no HOME. Quem sabe disso e a tela, que consulta a MoveSession.
  void SetCursorHolding(bool on) { cursor_holding_ = on; }

  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    const float r = kPanelRadius;

    DrawSoftShadow(vg, x, y, w, h, r);

    // Corpo e barra trocam de cor com o foco (cores dadas pelo dono).
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgFillColor(vg, focused_ ? kBoxBodyOn : kBoxBodyOff);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x, y, w, bar_height_, r, r, 0, 0);
    nvgFillColor(vg, focused_ ? kBoxBarOn : kBoxBarOff);
    nvgFill(vg);

    // Borda de selecao, ANTES dos filhos (spec 101): ela contorna o cartao mas
    // passa por BAIXO do cabecalho. Desenhada depois, ela cruzava o header e
    // cortava a barra do titulo — o que o dono viu na conferencia.
    //
    // O raio acompanha o recuo: a borda e desenhada t/2 para DENTRO, e usar o
    // mesmo `r` do corpo dava uma curva mais aberta que a do cartao — o corpo
    // claro vazava por fora dela nos quatro cantos.
    //
    // 8px em foco, 0 sem foco. O `t == 0` sai antes de desenhar: o nanovg com
    // espessura zero ainda pinta um fio de meio pixel.
    const float t = focused_ ? kPanelBorderOn : kPanelBorderOff;
    if (t > 0.0f) {
      nvgBeginPath(vg);
      nvgRoundedRect(vg, x + t / 2, y + t / 2, w - t, h - t, r - t / 2);
      nvgStrokeColor(vg, kBoxBorderOn);
      nvgStrokeWidth(vg, t);
      nvgStroke(vg);
    }

    // Filhos por cima do fundo E da borda — inclusive a grade e o cabecalho.
    brls::Box::draw(vg, x, y, w, h, style, ctx);

    // Emblema da origem, no canto externo do rodape. Depois dos filhos: ele
    // flutua sobre o rodape, como na referencia.
    //
    // A CAPA do jogo (spec 101), 42 x 42 com raio 8. A esquerda e sempre o
    // NestBox, entao a capa dali e sempre a do Pokemon HOME; a direita
    // acompanha o save aberto. Quem resolve isso e a tela, via SetCoverPath —
    // aqui so se pinta o que chegou.
    //
    // Sem arte (caminho vazio ou PNG ausente) cai no quadrado branco de antes:
    // um buraco no rodape seria pior que o placeholder.
    const float em = kFooterCover;
    const float logo_x = logo_left_ ? x + kPanelPad : x + w - em - kPanelPad;
    // Centrado na faixa do rodape (spec 101). Era `h - em - kPanelPad`, que
    // dependia de um padding vertical que o cartao nao tem mais — a ancora
    // agora e a propria faixa, que e quem define onde o rodape mora.
    const float logo_y =
        y + h - kPanelFooterHeight + (kPanelFooterHeight - em) / 2;

    const int cover = CoverHandle(vg, cover_path_);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, logo_x, logo_y, em, em, kFooterCoverRadius);
    if (cover > 0) {
      // O pattern cobre exatamente o quadrado do rodape: a capa e quadrada na
      // origem, entao nao ha recorte para calcular.
      NVGpaint p =
          nvgImagePattern(vg, logo_x, logo_y, em, em, 0, cover, 1.0f);
      nvgFillPaint(vg, p);
    } else {
      nvgFillColor(vg, kWhite);
    }
    nvgFill(vg);

    // Cursor por ultimo de tudo: seta e barra flutuam sobre qualquer coisa,
    // inclusive o cabecalho quando a celula e da primeira linha.
    if (focused_ && cursor_cell_) {
      ++cursor_tick_;  // anima a flutuacao da seta (spec 092)
      DrawCursor(vg, x, w);
    }
  }

 private:
  // Textura da capa, cacheada por caminho — mesmo padrao do SpriteHandle.
  // Sem cache, cada quadro criaria uma textura nova e vazaria uma por frame.
  //
  // LINEAR, nao NEAREST: capa e arte fotografica reduzida para 42px, e o
  // NEAREST dos sprites (que existe para preservar o pixel-art) serrilharia.
  //
  // Handle <= 0 fica cacheado do mesmo jeito: e o que impede o nanovg de
  // tentar reabrir um PNG ausente a cada quadro.
  static int CoverHandle(NVGcontext* vg, const std::string& path) {
    if (path.empty()) return 0;
    struct Cached {
      std::string path;
      int handle;
    };
    static std::vector<Cached> cache;
    for (const auto& c : cache) {
      if (c.path == path) return c.handle;
    }
    const int handle = nvgCreateImage(vg, path.c_str(), 0);
    cache.push_back({path, handle});
    return handle;
  }

  // O ponteiro do cursor: SETA normalmente, MAO quando esta segurando algo
  // (spec 093). A mao vem da mascara `selector_shape_hand.png`, tingida em
  // runtime — ela nunca virou path, so a seta virou (spec 092).
  //
  // `tip_y` e a base da faixa: a seta nasce dali para baixo com o bico na
  // ponta, e a mao pousa o topo ali. As duas flutuam com o mesmo `tick` — e
  // o mesmo cursor, so muda o desenho.
  void DrawPointer(NVGcontext* vg, float cx, float tip_y, float ah,
                   float aw) const {
    if (cursor_holding_) {
      // A mascara nasce na pose final e nao gira.
      //
      // TAMANHO: 32 x 35, os valores da folha de assets (rodada 9). A mao e
      // mais ESTREITA que a seta (38 de largura) e mais alta — foi assim que
      // o dono aprovou, e a 46px que tentei na rodada 8 ficou grande demais.
      //
      // O contorno fica fino nesse tamanho (a arte tem 19px de borda em 321,
      // o que da ~1,9px na tela). Engrossar exige reexportar o PNG com a
      // borda mais grossa — escalar a mao inteira nao serve, porque quebra a
      // harmonia com a seta.
      const float hw = 32.0f;
      const float hh = 35.0f;
      // ALINHAMENTO (rodada 11): `DrawDown` posiciona pela PONTA — desenha de
      // `tip_y - altura` ate `tip_y`. A seta encaixa assim porque o bico E a
      // ponta de baixo.
      //
      // A mao precisa SOBREPOR a base da faixa, nao ficar inteira acima nem
      // inteira abaixo dela. `tip_y + hh` (rodada 10) jogou a peca toda para
      // baixo; o certo e descer so uma fracao, deixando o punho entrar na
      // faixa como a seta entra.
      const float overlap = hh * 0.30f;
      Selector::DrawDown(vg, Selector::Variant::kCustom, cx, tip_y + overlap,
                         hh, kWhite, SelectorShape::kHand, hw, cursor_tick_);
      return;
    }
    Selector::DrawDown(vg, Selector::Variant::kCustom, cx, tip_y, ah,
                       cursor_color_, SelectorShape::kArrow, aw, cursor_tick_);
  }

  void DrawCursor(NVGcontext* vg, float x, float w) const {
    // Posicao absoluta da celula — o mesmo recurso do PillWithBall, que le
    // getX() do filho durante o draw.
    const float cx = cursor_cell_->getX() + cursor_cell_->getWidth() / 2;
    const float cell_top = cursor_cell_->getY();

    // Seta: seletor de forma dinamica (spec 084), apontando para baixo,
    // tingida em laranja de marca (equivalente ao vermelho da tertiary
    // antiga, retirada). O bico quase tocando o sprite. Sempre no centro da
    // celula — quem desliza para caber e so a barra.
    //
    // MEDIDAS DA REFERENCIA (spec 089). Tiradas de uma captura do HOME a
    // 1280x720 — a MESMA resolucao deste app —, entao valem em pixel direto,
    // sem conversao: scratchpad/switch-album/home-10.jpg.
    //
    //   seta:   x 79..109 = 30 de largura, y 100..124 = 24 de altura
    //   filete: y 100..103 = 4, cor #FF4D1C
    //   faixa:  y  69..98  = 30 de altura, cinza neutro #6B6B6B
    //
    // Antes estes numeros eram estimados comparando prints no olho.
    const float tip_y = cell_top + 6.0f;
    // As medidas APROVADAS na folha de assets (spec 093 rodada 6). O app
    // estava com 30x24, herdados de antes da vetorizacao — a folha ja usava
    // 38x22, e os dois tinham divergido sem ninguem notar.
    const float ah = Selector::kArrowH;  // 22, altura da seta
    const float aw = Selector::kArrowW;  // 38, largura da seta

    if (cursor_label_.empty()) {
      // Slot vazio: so o ponteiro, sem barra atras para encostar.
      DrawPointer(vg, cx, tip_y, ah, aw);
      return;
    }

    // Barra: nome + level em branco sobre fundo escuro, filete vermelho na
    // base. Centrada na celula, mas sem nunca sair do cartao — nas colunas
    // das pontas ela desliza e a seta continua no lugar.
    nvgFontFaceId(vg, brls::Application::getDefaultFont());
    nvgFontSize(vg, 16.8f);  // 21 - 20% (pedido do dono)

    // Largura pelo TEXTO, com padding fixo (spec 089 rodada 3). Medido na
    // referencia: "Pikachu (Lv. 5)" ocupa x 158..310 = 152px de faixa. Com
    // largura fixa, nome curto sobrava faixa vazia e nome longo era cortado.
    //
    // O minimo evita que um nome de duas letras vire uma pilula minuscula.
    const float pad_x = 16.0f;
    float text_bounds[4];
    nvgTextBounds(vg, 0, 0, cursor_label_.c_str(), nullptr, text_bounds);
    const float bar_h = 32.0f;    // aprovado na folha de assets (era 30)
    const float stripe_h = 4.0f;  // medido: y 100..103
    const float margin = 6.0f;
    // Teto na largura do cartao: sem ele um nome muito longo faria a faixa
    // vazar para fora do painel, que e o que o clamp de `bar_x` nao resolve.
    const float bar_w =
        std::min(w - margin * 2,
                 std::max(90.0f, (text_bounds[2] - text_bounds[0]) + pad_x * 2));
    float bar_x = cx - bar_w / 2;
    bar_x = std::max(x + margin, std::min(bar_x, x + w - margin - bar_w));

    // A pilha vertical APROVADA na folha de assets (spec 093), de cima para
    // baixo:
    //
    //   topo da faixa          cantos ARREDONDADOS (r=6)
    //   ...                    32 de faixa cinza #6B6B6B
    //   filete                  4, DENTRO da faixa
    //   ...                     2 de cinza sobrando abaixo do filete
    //   base da faixa           cantos RETOS — e daqui que o ponteiro nasce
    //   tip_y                   ponta do ponteiro
    //
    // O filete fica DENTRO da faixa, nao abaixo dela: e o que produz os 2px
    // de cinza que aparecem sob a linha colorida na referencia.
    const float bar_y = tip_y - ah - bar_h;
    const float stripe_y = bar_y + bar_h - stripe_h - 2.0f;

    // Faixa do nome: cinza NEUTRO (#6B6B6B), com os cantos de BAIXO retos —
    // so o topo e arredondado. A base encosta no ponteiro.
    const float rr = 6.0f;
    nvgBeginPath(vg);
    nvgMoveTo(vg, bar_x + rr, bar_y);
    nvgLineTo(vg, bar_x + bar_w - rr, bar_y);
    nvgQuadTo(vg, bar_x + bar_w, bar_y, bar_x + bar_w, bar_y + rr);
    nvgLineTo(vg, bar_x + bar_w, bar_y + bar_h);
    nvgLineTo(vg, bar_x, bar_y + bar_h);
    nvgLineTo(vg, bar_x, bar_y + rr);
    nvgQuadTo(vg, bar_x, bar_y, bar_x + rr, bar_y);
    nvgClosePath(vg);
    nvgFillColor(vg, kCursorBar);
    nvgFill(vg);

    // Filete colorido DENTRO da faixa, deixando 2px de cinza abaixo dele.
    nvgBeginPath(vg);
    nvgRect(vg, bar_x, stripe_y, bar_w, stripe_h);
    nvgFillColor(vg, cursor_color_);
    nvgFill(vg);

    // Ponteiro encostado na base da faixa: SETA, ou MAO se estiver segurando
    // (spec 093). Desenhado DEPOIS da faixa, entao cobre a borda de baixo.
    DrawPointer(vg, cx, tip_y, ah, aw);

    // A faixa cresce com o texto, entao ele cabe por construcao. A truncagem
    // fica so para o caso extremo: uma faixa mais larga que o proprio cartao
    // seria fatiada pelo clamp de `bar_x`, e ai o texto vazaria.
    std::string text = cursor_label_;
    const float max_text = bar_w - pad_x * 2;
    float bounds[4];
    nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
    if (bounds[2] - bounds[0] > max_text) {
      while (text.size() > 1) {
        text.pop_back();
        const std::string probe = text + "...";
        nvgTextBounds(vg, 0, 0, probe.c_str(), nullptr, bounds);
        if (bounds[2] - bounds[0] <= max_text) {
          text = probe;
          break;
        }
      }
    }

    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, kWhite);
    nvgText(vg, bar_x + bar_w / 2, bar_y + bar_h / 2 - 1.0f, text.c_str(),
            nullptr);
  }

  float bar_height_;
  bool logo_left_ = false;
  bool focused_ = false;
  // Capa do jogo no rodape (spec 101). Vazio ate a tela chamar SetCoverPath.
  std::string cover_path_;
  brls::View* cursor_cell_ = nullptr;
  std::string cursor_label_;
  // Laranja e o modo Mover, o padrao ao abrir a caixa (spec 089).
  NVGcolor cursor_color_ = kSelectorOrange;
  bool cursor_holding_ = false;
  // Contagem de quadros da flutuacao da seta (spec 092). Propria deste frame:
  // duas telas com setas nao podem pulsar em sincronia (regra da spec 043).
  mutable unsigned cursor_tick_ = 0;
};

struct BoxPanel {
  brls::Box* root = nullptr;
  brls::Box* pill = nullptr;
  brls::Label* title = nullptr;
  brls::Label* counter = nullptr;
  // Ombros L/R do cabecalho. So aparecem no painel em foco: eles anunciam um
  // atalho que, fora do foco, nao faz nada (spec 046).
  brls::Label* shoulder_l = nullptr;
  brls::Label* shoulder_r = nullptr;
  std::vector<SlotCell*> cells;
  BoxSource* source = nullptr;
  std::size_t box = 0;
  // Identifica a fonte no overlay da sessao (box_move.h). 0 = NestBox,
  // 1 = save — os dois paineis da tela.
  int source_id = 0;
  // A sessao de movimentacao e da tela inteira, compartilhada pelos dois
  // paineis: mover e tirar de um e por no outro. nullptr = tela sem
  // movimentacao (o painel so exibe).
  bx::MoveSession* session = nullptr;
  // Jogo do SAVE ABERTO, para os avisos em repouso (spec 104): os dois
  // paineis comparam contra ele, nao contra a propria fonte. kCount = tela
  // sem save (os icones de repouso ficam desligados).
  cp::Game ref_game = cp::Game::kCount;
  // Visao geral das caixas (spec 105): cada celula vira UMA caixa, paginada
  // de 30 em 30. O estado e por painel — o outro continua na vista normal,
  // como no HOME.
  bool overview = false;
  std::size_t ov_page = 0;
  // Coluna do foco na grade. shared_ptr porque o painel e copiado por valor.
  std::shared_ptr<std::size_t> grid_column = std::make_shared<std::size_t>(0);
  bool accent = false;

  // O cursor esta neste painel? Muda a cor do cartao e mostra os ombros L/R.
  void SetFocused(bool on) {
    if (auto* frame = dynamic_cast<BoxFrame*>(root)) {
      frame->SetFocused(on);
      // Painel que perdeu o cursor nao pode ficar com a seta orfã.
      if (!on) frame->SetCursor(nullptr, "");
    }
    // GONE e nao INVISIBLE: invisivel continua ocupando a largura e a pilula
    // do titulo ficaria estreita nos dois paineis.
    const auto vis = on ? brls::Visibility::VISIBLE : brls::Visibility::GONE;
    if (shoulder_l) shoulder_l->setVisibility(vis);
    if (shoulder_r) shoulder_r->setVisibility(vis);
    // O circulo do slot vazio acompanha o painel, como o corpo do cartao.
    for (SlotCell* c : cells) c->SetPanelFocused(on);
  }

  // Posiciona o cursor do HOME (seta + barra de nome) sobre o slot dado.
  void ShowCursor(std::size_t slot) {
    auto* frame = dynamic_cast<BoxFrame*>(root);
    if (!frame || slot >= cells.size()) return;
    // Na visao geral o rotulo e o NOME DA CAIXA, como o tooltip escuro da
    // referencia (spec 105).
    if (overview) {
      const std::size_t b = ov_page * kSlotsPerBox + slot;
      frame->SetCursor(cells[slot],
                       b < source->BoxCount() ? BoxName(b) : "");
      return;
    }
    const g3::BoxPokemon mon = Effective(slot);
    // Slot vazio = label vazio = so a seta (comportamento da referencia).
    const std::string label =
        mon.empty() ? ""
                    : DisplaySpecies(mon) + " (Lv. " +
                          std::to_string(g3::ComputeStats(mon).level) + ")";
    frame->SetCursor(cells[slot], label);
  }

  // Conteudo efetivo de um slot: o overlay manda, a fonte e o fallback.
  g3::BoxPokemon Effective(std::size_t slot) const {
    return EffectiveAt(box, slot);
  }

  // O mesmo, para uma caixa que NAO e a aberta — a visao geral (spec 105)
  // varre todas, e os movimentos pendentes da sessao precisam contar.
  g3::BoxPokemon EffectiveAt(std::size_t b, std::size_t slot) const {
    const g3::BoxPokemon original = source->At(b, slot);
    if (!session) return original;
    return session->Get({source_id, b, slot}, original);
  }

  std::size_t OverviewPages() const {
    return (source->BoxCount() + kSlotsPerBox - 1) / kSlotsPerBox;
  }

  // Nome de uma caixa qualquer. Title() responde pela caixa corrente da
  // fonte, entao aponta, le e devolve o ponteiro ao lugar.
  std::string BoxName(std::size_t b) const {
    source->SetCurrentBox(b);
    std::string name = source->Title();
    source->SetCurrentBox(box);
    return name;
  }

  // Visao geral (spec 105): cada celula e uma caixa da pagina corrente.
  // Cubo com pokebola = tem Pokemon; aberto = vazia. Os badges agregam os
  // ocupantes: vermelho se ALGUM nao entra no jogo de referencia (ou foi
  // reprovado pelo verificador), amarelo se algum so tem golpe ausente.
  void RefreshOverview() {
    title->setText("Box Space");
    pill->setBackgroundColor(kWhite);
    title->setTextColor(kTextPrimary);
    counter->setText(std::to_string(ov_page + 1) + "/ " +
                     std::to_string(OverviewPages()));

    for (std::size_t i = 0; i < cells.size(); ++i) {
      cells[i]->SetMon(g3::BoxPokemon{});
      cells[i]->SetSelected(false);
      const std::size_t b = ov_page * kSlotsPerBox + i;
      if (b >= source->BoxCount()) {
        // Pagina final incompleta: celula sem caixa fica realmente vazia.
        cells[i]->SetBlocked(false);
        cells[i]->SetWarned(false);
        cells[i]->SetSuspect(false);
        continue;
      }
      bool occupied = false, blk = false, wrn = false, sus = false;
      for (std::size_t s = 0; s < kSlotsPerBox; ++s) {
        const g3::BoxPokemon m = EffectiveAt(b, s);
        if (m.empty()) continue;
        occupied = true;
        if (!source->BlockedReason(b, s).empty()) sus = true;
        if (ref_game != cp::Game::kCount) {
          const int dex =
              m.national_dex ? m.national_dex : g3::NationalDex(m.species);
          if (!cp::HasSpecies(ref_game, dex)) {
            blk = true;
          } else if (cp::MissingMoveIn(
                         ref_game, m.moves,
                         sizeof(m.moves) / sizeof(m.moves[0])) != 0) {
            wrn = true;
          }
        }
      }
      cells[i]->SetBox(occupied);
      cells[i]->SetBlocked(blk);
      // Mesma precedencia do slot: o motivo mais grave manda.
      cells[i]->SetWarned(wrn && !blk && !sus);
      cells[i]->SetSuspect(sus);
    }
  }

  // Quantos Pokemon moram na caixa b (para o cartao da visao geral).
  int CountIn(std::size_t b) const {
    int n = 0;
    for (std::size_t s = 0; s < kSlotsPerBox; ++s) {
      if (!EffectiveAt(b, s).empty()) ++n;
    }
    return n;
  }

  void Refresh() {
    if (overview) {
      RefreshOverview();
      return;
    }
    // Antes de ler o titulo: fontes cujo nome depende da caixa (o NestBox)
    // precisam saber qual esta aberta.
    source->SetCurrentBox(box);
    title->setText(source->Title());
    // Pilula branca nos dois paineis (spec 046): o laranja de acento brigava
    // com a barra teal. Quem distingue os paineis agora e a aba e o emblema.
    pill->setBackgroundColor(kWhite);
    title->setTextColor(kTextPrimary);

    // Numeracao da CAIXA aberta, nao a contagem de Pokemon (spec 051): e o
    // "1/ 32" da referencia. O total vem da fonte — 32 no ZA, 14 no gen3,
    // 400 no NestBox.
    counter->setText(std::to_string(box + 1) + "/ " +
                     std::to_string(source->BoxCount()));

    // Segurando algo que nao cabe neste painel? Marca a grade inteira como
    // bloqueada — e o painel, nao o slot, que recusa a especie (spec 034).
    const bool blocked =
        session && session->Holding() && !FitsInPanel(session->Held());

    // Aviso de golpe (spec 038). So faz sentido quando a especie CABE — um
    // Pokemon ja bloqueado esta vermelho, e o motivo mais grave manda.
    const bool warned = session && session->Holding() && !blocked &&
                        MissingMove(session->Held()) != 0;

    for (std::size_t i = 0; i < cells.size(); ++i) {
      const g3::BoxPokemon eff = Effective(i);
      cells[i]->SetMon(eff);
      cells[i]->SetSelected(session &&
                            session->IsSelected({source_id, box, i}));
      // EM REPOUSO, cada slot tambem se compara contra o jogo do SAVE ABERTO
      // (spec 104, rodada 3): especie ausente liga o icone de bloqueio, golpe
      // ausente liga o amarelo — ANTES de o jogador pegar o Pokemon. E o que
      // o dono descreveu na spec 102 ("o jogo que ta aberto o save"); ate
      // aqui os icones so ligavam segurando algo.
      bool rest_block = false, rest_warn = false;
      if (!eff.empty() && ref_game != cp::Game::kCount) {
        const int dex =
            eff.national_dex ? eff.national_dex : g3::NationalDex(eff.species);
        rest_block = !cp::HasSpecies(ref_game, dex);
        // SEM precedencia entre os dois (pedido do dono): o mockup da spec
        // 102 reservou as duas posicoes justamente para os icones conviverem
        // — especie ausente E golpe ausente aparecem juntos.
        rest_warn =
            cp::MissingMoveIn(ref_game, eff.moves,
                              sizeof(eff.moves) / sizeof(eff.moves[0])) != 0;
      }
      cells[i]->SetBlocked(blocked || rest_block);
      cells[i]->SetWarned(warned || rest_warn);
      // Reprovado pelo verificador (spec 082). So marca se o slot ainda tem o
      // que a FONTE julgou: fontes que reprovam sao somente leitura
      // (CanAccept() == false), entao o overlay so pode esvaziar o slot —
      // nunca trocar por outro Pokemon que ninguem julgou.
      cells[i]->SetSuspect(!eff.empty() &&
                           !source->BlockedReason(box, i).empty());
    }
  }

  // A especie existe no jogo deste painel? NestBox aceita tudo.
  bool FitsInPanel(const g3::BoxPokemon& mon) const {
    if (!source || mon.empty()) return true;
    const cp::Game game = source->GameId();
    if (game == cp::Game::kCount) return true;
    const int dex =
        mon.national_dex ? mon.national_dex : g3::NationalDex(mon.species);
    return cp::HasSpecies(game, dex);
  }

  // Qual golpe do Pokemon nao existe no jogo deste painel? 0 = todos cabem.
  //
  // Isto AVISA, nao bloqueia (spec 038, §2 da pesquisa): nenhum caminho que
  // decide o movimento chama esta funcao. O jogo de destino resolve o golpe
  // sozinho; o aviso existe so para o jogador saber que algo vai mudar.
  int MissingMove(const g3::BoxPokemon& mon) const {
    if (!source || mon.empty()) return 0;
    return cp::MissingMoveIn(source->GameId(), mon.moves,
                             sizeof(mon.moves) / sizeof(mon.moves[0]));
  }
};

// Qual arte representa este painel. O NestBox e sempre o Pokemon HOME; o
// outro painel segue o jogo do save aberto.
//
// Vazio = jogo sem arte cadastrada: quem desenha cai no placeholder.
//
// Estava dentro da BoxActivity (spec 098, para a logo da barra de status);
// subiu para o escopo do arquivo na spec 101, quando a capa do rodape passou a
// precisar do mesmo slug. Uma tabela so — duas divergiriam na primeira vez que
// um jogo novo entrasse.
const char* PanelArtSlug(const BoxSource* source, bool is_nest) {
  if (is_nest) return "pokemon-home";
  if (!source) return "";
  switch (source->GameId()) {
    case cp::Game::kFireRed: return "pokemon-firered";
    case cp::Game::kLeafGreen: return "pokemon-leafgreen";
    case cp::Game::kEmerald: return "pokemon-emerald";
    case cp::Game::kRubySapphire: return "pokemon-ruby";
    case cp::Game::kLetsGo: return "pokemon-lets-go-pikachu";
    case cp::Game::kSwordShield: return "pokemon-sword";
    case cp::Game::kBdsp: return "pokemon-brilliant-diamond";
    case cp::Game::kLegendsArceus: return "pokemon-legends-arceus";
    case cp::Game::kScarletViolet: return "pokemon-scarlet";
    case cp::Game::kLegendsZA: return "pokemon-legends-z-a";
    default: return "";
  }
}

BoxPanel MakePanel(BoxSource* source, bool accent,
                   SlotCell::FocusCallback on_focus, int source_id = 0,
                   bx::MoveSession* session = nullptr,
                   std::function<void()> on_spaces = nullptr) {
  BoxPanel p;
  p.source = source;
  p.accent = accent;
  p.source_id = source_id;
  p.session = session;

  // Altura da barra do titulo. O cabecalho e desenhado DENTRO dela, entao os
  // dois precisam do mesmo numero — por isso sai da constante, e nao de um
  // literal local (spec 101).
  constexpr float kBarHeight = kPanelBarHeight;

  // Logo no canto externo: o painel de acento e o da esquerda.
  auto* frame = new BoxFrame(kBarHeight, /*logo_left=*/accent);
  p.root = frame;
  // Capa do jogo no rodape (spec 101). `accent` E o painel do NestBox — daí a
  // esquerda receber sempre a arte do Pokemon HOME.
  if (const char* slug = PanelArtSlug(source, accent); *slug) {
    frame->SetCoverPath(std::string(POKEHOME_UI_ASSETS) + "games/" + slug +
                        "_cover.png");
  }
  // Largura FIXA (spec 048): os dois paineis sao identicos. Com grow, o
  // painel do save saia maior que o do NestBox.
  p.root->setWidth(kPanelWidth);
  // Altura FIXA tambem (spec 101): antes o cartao crescia com o conteudo, e o
  // mockup especifica os dois lados. Sem isto a faixa do rodape nao teria onde
  // se apoiar — ela e desenhada a partir da BASE do cartao.
  p.root->setHeight(kPanelHeight);
  // Padding lateral so: as faixas de cima e de baixo ocupam a altura inteira
  // que sobra, e um padding vertical aqui as empurraria para dentro do miolo.
  p.root->setPadding(0, kPanelPad, 0, kPanelPad);
  // Metade do vao de cada lado: encostados, os dois somam kPanelGap.
  p.root->setMargins(0, kPanelGap / 2, 0, kPanelGap / 2);

  // Cabecalho: ombros, setas e pilula com o nome da caixa, dentro da barra.
  // Sem fundo proprio — quem pinta a barra e o BoxFrame, atras.
  auto* header = new brls::Box(brls::Axis::ROW);
  header->setAlignItems(brls::AlignItems::CENTER);
  // space-between: os controles vao para os cantos da barra e a pilula fica
  // no meio. Antes era CENTER e o "< L" ficava colado nela.
  header->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
  header->setHeight(kBarHeight);
  header->setPadding(0, kHeaderSidePad, 0, kHeaderSidePad);
  // Sem margem embaixo (spec 101): a faixa tem 60px cravados e o miolo comeca
  // exatamente onde ela acaba. Uma margem aqui roubaria altura da grade e o
  // vao derivado sairia errado.

  // Ordem: [seta, ombro L] [pilula] [ombro R, seta] — a seta por FORA, o
  // ombro colado nela. Os dois grupos sao Box proprios: sem eles o
  // space-between espalharia os cinco filhos igualmente, e o "<" ficaria
  // longe do "L". Os ombros somem no painel sem foco (SetFocused).
  auto* leftGroup = new brls::Box(brls::Axis::ROW);
  leftGroup->setAlignItems(brls::AlignItems::CENTER);

  auto* left = new brls::Label();
  left->setText(kGlyphChevronLeft);
  left->setFontSize(23.4f);  // 26 - 10% (pedido do dono)
  left->setTextColor(kWhite);
  left->setMarginRight(8);
  leftGroup->addView(left);

  p.shoulder_l = new brls::Label();
  p.shoulder_l->setText(kGlyphL);
  p.shoulder_l->setFontSize(28);
  p.shoulder_l->setTextColor(kWhite);
  leftGroup->addView(p.shoulder_l);
  header->addView(leftGroup);

  // Label nao aceita padding — a pilula e um Box em volta.
  // Altura fixa: sem ela o Box estica ate a altura do cabecalho e o raio
  // grande transforma a capsula numa elipse.
  p.pill = new brls::Box(brls::Axis::ROW);
  // 342 x 40, do mockup da spec 101. Era uma fracao do vao entre os controles;
  // agora e medida cravada, como o resto do cartao.
  p.pill->setWidth(342.0f);
  p.pill->setHeight(40.0f);
  p.pill->setCornerRadius(20.0f);  // metade da altura: pilula, nao elipse
  p.pill->setMargins(0, 10, 0, 10);
  p.pill->setJustifyContent(brls::JustifyContent::CENTER);
  p.pill->setAlignItems(brls::AlignItems::CENTER);

  p.title = new brls::Label();
  // Acompanha a pilula, que encolheu 30%: 26 nao cabe em 30.8px de altura.
  p.title->setFontSize(20);
  p.title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
  p.pill->addView(p.title);
  header->addView(p.pill);

  auto* rightGroup = new brls::Box(brls::Axis::ROW);
  rightGroup->setAlignItems(brls::AlignItems::CENTER);

  p.shoulder_r = new brls::Label();
  p.shoulder_r->setText(kGlyphR);
  p.shoulder_r->setFontSize(28);
  p.shoulder_r->setTextColor(kWhite);
  rightGroup->addView(p.shoulder_r);

  auto* right = new brls::Label();
  right->setText(kGlyphChevronRight);
  right->setFontSize(23.4f);  // 26 - 10%
  right->setTextColor(kWhite);
  right->setMarginLeft(8);
  rightGroup->addView(right);
  header->addView(rightGroup);

  p.root->addView(header);

  // Grade 6x5. Yoga nao tem grid: 5 linhas de 6 celulas de tamanho fixo.
  auto* grid = new brls::Box(brls::Axis::COLUMN);
  grid->setSize(brls::Size(kGridWidth, kGridHeight));
  grid->setAlignSelf(brls::AlignSelf::CENTER);
  for (int r = 0; r < kRows; ++r) {
    // GridRow, nao Box: o borealis desce por indice e cairia sempre na
    // primeira coluna. Ver o comentario da classe.
    auto* row = new GridRow(p.grid_column);
    row->setAxis(brls::Axis::ROW);
    row->setHeight(kSlotH + kSlotGapY);
    for (int c = 0; c < kCols; ++c) {
      auto* cell = new SlotCell(static_cast<std::size_t>(r * kCols + c),
                                on_focus);
      row->addView(cell);
      p.cells.push_back(cell);
    }
    grid->addView(row);
  }
  p.root->addView(grid);

  // Rodape (spec 051, rodada 2). Painel da ESQUERDA:
  //
  //     [logo] 32px [1/400] ....... ( Espacos ) ....... (vazio)
  //
  // O da direita e o espelho exato: comeca pela borda direita, com a ordem
  // dos elementos invertida. O botao fica centrado no cartao nos dois.
  //
  // Quem PINTA a logo e o BoxFrame::draw; aqui so fica o Box que reserva o
  // espaco no layout.
  auto* footer = new brls::Box(brls::Axis::ROW);
  footer->setAlignItems(brls::AlignItems::CENTER);
  // Faixa de 60px (spec 101), espelho da barra do titulo. Ela NAO tem cor
  // propria — decisao do dono: e so o espaco reservado, e o conteudo (logo,
  // numeracao e pilula "Espacos") fica centrado dentro dela como hoje.
  //
  // Era kFooterLogoBox (46) + 10 de margem; a altura agora e cravada, para o
  // miolo da grade ser exatamente kPanelHeight menos as duas faixas.
  footer->setHeight(kPanelFooterHeight);

  // Grupo logo+numero, na largura de um lado. Ele e o espelho: no painel da
  // direita a ordem interna inverte e o grupo vai para o fim da linha.
  auto* info = new brls::Box(brls::Axis::ROW);
  info->setAlignItems(brls::AlignItems::CENTER);
  // Encosta na borda externa: FLEX_START no painel esquerdo, FLEX_END no
  // direito — e o que faz os dois rodapes serem espelho um do outro.
  info->setJustifyContent(accent ? brls::JustifyContent::FLEX_START
                                 : brls::JustifyContent::FLEX_END);

  auto* logo_gap = new brls::Box(brls::Axis::ROW);
  logo_gap->setSize(brls::Size(kFooterLogoBox, kFooterLogoBox));

  // Numeracao da caixa aberta ("1/ 32"), nao a contagem de Pokemon. Cada
  // fonte tem o seu total: 32 caixas no ZA, 14 no gen3, 10 no NestBox.
  p.counter = new brls::Label();
  p.counter->setFontSize(16);  // 20 - 20% (pedido do dono)
  p.counter->setTextColor(kTextSecondary);
  // Sem isto o borealis quebra "1/ 32" em duas linhas quando o Box do grupo
  // fica mais estreito que o texto.
  p.counter->setSingleLine(true);

  if (accent) {
    p.counter->setMarginLeft(kFooterLogoGap);  // 32px entre logo e numero
    info->addView(logo_gap);
    info->addView(p.counter);
  } else {
    p.counter->setMarginRight(kFooterLogoGap);
    info->addView(p.counter);
    info->addView(logo_gap);
  }

  auto* spaces = new PillWithCube();
  spaces->setHeight(38);
  spaces->setCornerRadius(19);
  spaces->setBackgroundColor(kWhite);
  spaces->setPadding(0, 20, 0, 46);  // folga a esquerda para o cubo
  spaces->setAlignItems(brls::AlignItems::CENTER);
  // Sem borda: aparecia um risco embaixo da pilula. shadowType ja e NONE por
  // padrao (view.hpp:325), entao so a borda precisa ser zerada.
  spaces->setBorderThickness(0.0f);

  auto* spaces_label = new brls::Label();
  spaces_label->setText("Espacos");
  spaces_label->setFontSize(20);
  spaces_label->setTextColor(kTextPrimary);
  spaces->addView(spaces_label);

  // O botao "Espacos" E o acesso a visao geral das caixas (spec 105).
  // Alcancavel de DOIS jeitos (dono, 2026-08-17): toque/clique direto, e
  // pelo d-pad — descer da ultima linha da grade foca a pilula e o A ativa.
  if (on_spaces) {
    spaces->setFocusable(true);
    // A seta do painel aponta para a pilula, como faz com as celulas — sem
    // a borda de highlight do borealis (dono, 2026-08-17).
    spaces->setHideHighlight(true);
    spaces->on_focus = [frame, spaces] { frame->SetCursor(spaces, ""); };
    spaces->registerClickAction([on_spaces](brls::View*) {
      on_spaces();
      return true;
    });
    // O tap dispara a mesma click action registrada acima.
    spaces->addGestureRecognizer(new brls::TapGestureRecognizer(spaces));
  }

  // Espacadores: grow(1) nos dois vaos, mais um bloco VAZIO do outro lado
  // com a largura do `info`. Sem ele o lado sem logo seria maior e o botao
  // sairia do centro do cartao.
  // 62 para o texto: "1/ 400" com fonte 16 cabe folgado, e e o pior caso
  // (o NestBox e a fonte com mais caixas).
  const float info_w = kFooterLogoBox + kFooterLogoGap + 62.0f;
  info->setWidth(info_w);
  auto* mirror = new brls::Box(brls::Axis::ROW);
  mirror->setWidth(info_w);

  auto* gap_a = new brls::Box(brls::Axis::ROW);
  gap_a->setGrow(1.0f);
  auto* gap_b = new brls::Box(brls::Axis::ROW);
  gap_b->setGrow(1.0f);

  if (accent) {
    footer->addView(info);
    footer->addView(gap_a);
    footer->addView(spaces);
    footer->addView(gap_b);
    footer->addView(mirror);
  } else {
    footer->addView(mirror);
    footer->addView(gap_a);
    footer->addView(spaces);
    footer->addView(gap_b);
    footer->addView(info);
  }

  p.root->addView(footer);
  return p;
}

// --- Atualizacao -----------------------------------------------------------

// Barra de progresso. Nasceu dentro da UpdateActivity; virou componente para
// a tela de loading usar a mesma — duas barras seriam dois lugares para
// ajustar a cada mudanca visual.
class ProgressBar : public brls::Box {
 public:
  explicit ProgressBar(float width = 420.0f) {
    setAxis(brls::Axis::ROW);
    setWidth(width);
    setHeight(12);
    setCornerRadius(6);
    setBackgroundColor(nvgRGB(0xE3, 0xEC, 0xE5));

    fill_ = new brls::Box(brls::Axis::ROW);
    fill_->setGrow(0.0f);
    fill_->setHeight(12);
    fill_->setCornerRadius(6);
    fill_->setBackgroundColor(kAccent);
    addView(fill_);

    rest_ = new brls::Box(brls::Axis::ROW);
    rest_->setGrow(1.0f);
    addView(rest_);
  }

  // ratio de 0 a 1; valores fora da faixa sao presos nas pontas.
  void setProgress(float ratio) {
    ratio = std::min(1.0f, std::max(0.0f, ratio));
    fill_->setGrow(ratio);
    rest_->setGrow(1.0f - ratio);
    invalidate();
  }

 private:
  brls::Box* fill_ = nullptr;
  brls::Box* rest_ = nullptr;
};

class UpdateActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"UpdateActivity"};

  explicit UpdateActivity(nestbox::UpdateInfo info) : info_(std::move(info)) {}

  // A raiz precisa de draw() proprio: e o unico "tick" por frame que o
  // borealis oferece por Activity, e o download fatiado depende dele.
  class UpdateRoot : public GradientBackground {
   public:
    explicit UpdateRoot(UpdateActivity* owner) : owner_(owner) {}

    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
      owner_->PumpDownload();
      GradientBackground::draw(vg, x, y, w, h, style, ctx);
    }

   private:
    UpdateActivity* owner_;
  };

  brls::View* createContentView() override {
    auto* root = new UpdateRoot(this);
    root->setAxis(brls::Axis::COLUMN);
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setPadding(40, 80, 40, 80);

    card_ = new brls::Box(brls::Axis::COLUMN);
    card_->setCornerRadius(28);
    card_->setBackgroundColor(nvgRGBA(255, 255, 255, 200));
    card_->setPadding(36, 48, 36, 48);
    card_->setAlignItems(brls::AlignItems::CENTER);

    auto* title = new brls::Label();
    title->setText("Nova versao disponivel");
    title->setFontSize(34);
    title->setTextColor(kTextPrimary);
    title->setMarginBottom(24);
    card_->addView(title);

    auto* versions = new brls::Box(brls::Axis::ROW);
    versions->setMarginBottom(28);
    versions->addView(MakeVersionBox("INSTALADA", nestbox::CurrentVersion(),
                                     kTextSecondary));
    versions->addView(
        MakeVersionBox("DISPONIVEL", info_.latest_version, kAccent));
    card_->addView(versions);

    status_ = new brls::Label();
    status_->setText("O download tem cerca de 27 MB.");
    status_->setFontSize(21);
    status_->setTextColor(kTextSecondary);
    card_->addView(status_);

    // Barra de progresso, escondida ate o download comecar.
    progress_ = new ProgressBar();
    progress_->setMarginTop(20);
    progress_->setVisibility(brls::Visibility::GONE);
    card_->addView(progress_);

    root->addView(card_);

    hint_ = new brls::Label();
    hint_->setText("A  baixar e instalar      B  agora nao");
    hint_->setFontSize(21);
    hint_->setTextColor(kTextSecondary);
    hint_->setMarginTop(28);
    root->addView(hint_);

    root->setFocusable(true);
    root->registerAction(
        "Baixar", brls::BUTTON_A,
        [this](brls::View*) {
          StartDownload();
          return true;
        },
        false);
    root->registerAction(
        "Agora nao", brls::BUTTON_B,
        [this](brls::View*) {
          if (!downloading_) brls::Application::popActivity();
          return true;
        },
        false);
    return root;
  }

 private:
  brls::Box* MakeVersionBox(const char* label, const std::string& value,
                            NVGcolor color) {
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setMargins(0, 22, 0, 22);

    auto* l = new brls::Label();
    l->setText(label);
    l->setFontSize(16);
    l->setTextColor(kTextTertiary);
    box->addView(l);

    auto* v = new brls::Label();
    v->setText("v" + value);
    v->setFontSize(32);
    v->setTextColor(color);
    box->addView(v);
    return box;
  }

  void StartDownload() {
    if (downloading_) return;

    // O download roda fatiado, entre frames: com curl_easy_perform a UI so
    // redesenhava depois de terminar, e a barra pulava direto para 100%.
    if (!nestbox::BeginDownload(info_.download_url)) {
      status_->setText("Nao consegui iniciar o download.");
      return;
    }
    downloading_ = true;
    status_->setText("Baixando...");
    hint_->setText("aguarde");
    progress_->setVisibility(brls::Visibility::VISIBLE);
    progress_->setProgress(0.0f);
  }

  // Chamada a cada frame pelo draw() da raiz enquanto o download acontece.
  void PumpDownload() {
    if (!downloading_) return;

    float p = 0.0f;
    if (nestbox::PumpDownload(&p)) {
      progress_->setProgress(p);
      return;
    }

    // Terminou: o resultado sai aqui.
    const bool ok = nestbox::EndDownload();
    downloading_ = false;
    if (ok) {
      // A instalacao e um rename() na proxima abertura: ou aconteceu, ou nao.
      progress_->setProgress(1.0f);
      // Reiniciar sozinho e melhor que pedir para o usuario fechar e abrir —
      // e foi justamente o "reabrir" que nunca aplicava a troca.
      if (nestbox::RestartIntoUpdate()) {
        status_->setText("Atualizado! Reiniciando...");
        hint_->setText("aguarde");
        brls::Application::quit();
      } else {
        status_->setText("Pronto. Feche e abra o app para usar a nova versao.");
        hint_->setText("B  sair");
      }
    } else {
      status_->setText("Falha no download. Tente de novo mais tarde.");
      hint_->setText("B  continuar");
      progress_->setVisibility(brls::Visibility::GONE);
    }
  }

  nestbox::UpdateInfo info_;
  bool downloading_ = false;
  brls::Box* card_ = nullptr;
  ProgressBar* progress_ = nullptr;
  brls::Label* status_ = nullptr;
  brls::Label* hint_ = nullptr;
};

// --- Aviso de applet mode --------------------------------------------------

// Mostrada na abertura quando o app roda em applet mode. Explica o que isso
// limita e como abrir corretamente, mas NAO bloqueia: ler saves de sdmc:/
// funciona nos dois modos. Bloquear tiraria uma funcionalidade que existe.
class AppletWarningActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"AppletWarningActivity"};

  brls::View* createContentView() override {
    auto* root = new GradientBackground();
    root->setAxis(brls::Axis::COLUMN);
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setPadding(40, 80, 40, 80);

    auto* card = new brls::Box(brls::Axis::COLUMN);
    card->setCornerRadius(28);
    card->setBackgroundColor(nvgRGBA(255, 255, 255, 200));
    card->setPadding(36, 44, 36, 44);
    card->setAlignItems(brls::AlignItems::CENTER);

    auto* badge = new brls::Box(brls::Axis::ROW);
    badge->setHeight(40);
    badge->setCornerRadius(20);
    badge->setPadding(0, 24, 0, 24);
    badge->setJustifyContent(brls::JustifyContent::CENTER);
    badge->setAlignItems(brls::AlignItems::CENTER);
    badge->setBackgroundColor(kAccent);
    badge->setMarginBottom(20);

    auto* bt = new brls::Label();
    bt->setText("MODO APPLET");
    bt->setFontSize(20);
    bt->setTextColor(kWhite);
    badge->addView(bt);
    card->addView(badge);

    auto* title = new brls::Label();
    title->setText("O app esta com memoria limitada");
    title->setFontSize(34);
    title->setTextColor(kTextPrimary);
    title->setMarginBottom(16);
    card->addView(title);

    static const char* kLines[] = {
        "Abrindo pelo album, o homebrew recebe cerca de 448 MB",
        "e nao alcanca os saves dos jogos instalados no console.",
        "",
        "Para abrir com memoria completa (~3 GB) e acesso aos",
        "saves: no menu HOME, segure R e abra um jogo qualquer.",
        "",
        "Voce pode continuar assim — saves copiados para",
        "sdmc:/pokehome/ funcionam normalmente.",
    };
    for (const char* line : kLines) {
      auto* l = new brls::Label();
      l->setText(line);
      l->setFontSize(21);
      l->setTextColor(kTextSecondary);
      l->setMarginBottom(4);
      card->addView(l);
    }

    root->addView(card);

    auto* hint = new brls::Label();
    hint->setText("A  continuar assim mesmo      B  sair");
    hint->setFontSize(21);
    hint->setTextColor(kTextSecondary);
    hint->setMarginTop(28);
    root->addView(hint);

    root->setFocusable(true);
    root->registerAction(
        "Continuar", brls::BUTTON_A,
        [](brls::View*) {
          brls::Application::popActivity();
          return true;
        },
        false);
    root->registerAction(
        "Entendi", brls::BUTTON_B,
        [](brls::View*) {
          brls::Application::popActivity();
          return true;
        },
        false);
    return root;
  }
};

// --- Modo lista ------------------------------------------------------------

constexpr int kListCols = 13;
constexpr int kListRows = 6;
constexpr int kListSize = kListCols * kListRows;  // 78 por tela

// Celula compacta da lista: so o sprite, sem texto. O nome aparece no rodape.
class ListCell : public brls::Box {
 public:
  using FocusCallback = std::function<void(int)>;

  ListCell(int offset, FocusCallback on_focus)
      : brls::Box(brls::Axis::COLUMN),
        offset_(offset),
        on_focus_(std::move(on_focus)) {
    setFocusable(true);
    setGrow(1.0f);
    setShrink(1.0f);
    setMargins(3, 3, 3, 3);
    setCornerRadius(8);
    setJustifyContent(brls::JustifyContent::CENTER);
    setAlignItems(brls::AlignItems::CENTER);
    setBackgroundColor(nvgRGBA(255, 255, 255, 153));

    sprite_ = new brls::Image();
    sprite_->setDimensions(brls::View::AUTO, brls::View::AUTO);
    sprite_->setWidthPercentage(74);
    sprite_->setHeightPercentage(74);
    sprite_->setScalingType(brls::ImageScalingType::STRETCH);
    sprite_->setInterpolation(brls::ImageInterpolation::NEAREST);
    addView(sprite_);

    addGestureRecognizer(new brls::TapGestureRecognizer(this));
  }

  void onFocusGained() override {
    brls::Box::onFocusGained();
    if (on_focus_) on_focus_(offset_);
  }

  void Set(const g3::BoxPokemon& mon) {
    const std::string path = mon.empty() ? "" : SpritePath(mon);
    if (path.empty()) {
      sprite_->setVisibility(brls::Visibility::GONE);
      setBackgroundColor(nvgRGBA(255, 255, 255, 76));
    } else {
      sprite_->setImageFromFile(path);
      sprite_->setVisibility(brls::Visibility::VISIBLE);
      // Fundo dourado marca o shiny. Nao ha sprite shiny no romfs (seriam
      // outras 386 imagens) — ver TD-02 da spec 025.
      setBackgroundColor(mon.is_shiny() ? kShinyBg : nvgRGBA(255, 255, 255, 153));
    }
  }

 private:
  int offset_;
  FocusCallback on_focus_;
  brls::Image* sprite_ = nullptr;
};

class ListActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"ListActivity"};

  explicit ListActivity(BoxSource* source) : source_(source) {}

  brls::View* createContentView() override {
    auto* root = new GradientBackground();
    root->setHeaderBand(58);
    root->setAxis(brls::Axis::COLUMN);

    root->addView(MakeTopBar());

    // Grade + barra de posicao.
    auto* body = new brls::Box(brls::Axis::ROW);
    body->setGrow(1.0f);
    body->setPadding(16, 40, 8, 40);

    auto* grid = new brls::Box(brls::Axis::COLUMN);
    grid->setGrow(1.0f);
    grid->setShrink(1.0f);
    for (int r = 0; r < kListRows; ++r) {
      auto* row = new brls::Box(brls::Axis::ROW);
      row->setGrow(1.0f);
      row->setShrink(1.0f);
      for (int c = 0; c < kListCols; ++c) {
        auto* cell = new ListCell(r * kListCols + c,
                                  [this](int off) { OnCellFocus(off); });
        cells_.push_back(cell);
        row->addView(cell);
      }
      grid->addView(row);
    }
    body->addView(grid);

    // Barra de posicao: mostra onde a janela esta dentro dos 420 slots.
    auto* track = new brls::Box(brls::Axis::COLUMN);
    track->setWidth(12);
    track->setCornerRadius(6);
    track->setBackgroundColor(nvgRGBA(255, 255, 255, 153));
    track->setMarginLeft(12);

    thumbTop_ = new brls::Box(brls::Axis::COLUMN);
    thumbTop_->setGrow(0.0f);
    track->addView(thumbTop_);

    auto* thumb = new brls::Box(brls::Axis::COLUMN);
    thumb->setGrow(1.0f);
    thumb->setCornerRadius(6);
    thumb->setBackgroundColor(nvgRGB(0x9D, 0xBF, 0xA8));
    track->addView(thumb);

    thumbBottom_ = new brls::Box(brls::Axis::COLUMN);
    thumbBottom_->setGrow(1.0f);
    track->addView(thumbBottom_);

    body->addView(track);
    root->addView(body);

    root->addView(MakeStatsBar());


    root->registerAction(
        "Voltar", brls::BUTTON_B,
        [](brls::View*) {
          brls::Application::popActivity();
          return true;
        },
        false);
    root->registerAction(
        "Pagina anterior", brls::BUTTON_LB,
        [this](brls::View*) { return Scroll(-kListSize); }, false);
    root->registerAction(
        "Proxima pagina", brls::BUTTON_RB,
        [this](brls::View*) { return Scroll(kListSize); }, false);
    // Y ordena e A abre detalhes — como no HOME (§5 e §10 da pesquisa). Antes
    // Y era detalhes e A nao tinha uso nesta tela. Ver TD-02 da spec 024.
    root->registerAction(
        "Ordenar", brls::BUTTON_Y,
        [this](brls::View*) {
          sort_ = bx::NextSort(sort_);
          Reorder();
          top_ = 0;  // criterio novo, lista nova: volta ao topo
          RefreshCells();
          UpdateFooter();
          return true;
        },
        false);
    // − alterna o filtro de shiny (spec 025). Mesmo botao que a tela de caixas
    // usa para o modo de selecao — em ambos os casos, "muda o modo da tela".
    root->registerAction(
        "So shiny", brls::BUTTON_BACK,
        [this](brls::View*) {
          filter_ = filter_ == bx::Filter::kNone ? bx::Filter::kShinyOnly
                                                 : bx::Filter::kNone;
          Reorder();
          top_ = 0;
          RefreshCells();
          UpdateFooter();
          return true;
        },
        false);
    root->registerAction(
        "Detalhes", brls::BUTTON_A,
        [this](brls::View*) {
          const g3::BoxPokemon mon = MonAt(top_ + focused_);
          if (mon.empty()) return true;
          // Check Summary (spec 103). Sem fonte de caixa: a lista atravessa
          // caixas e o L/R do summary navega dentro de UMA caixa.
          ShowSummary(mon, nullptr, 0, 0);
          return true;
        },
        false);

    RefreshCells();
    if (!cells_.empty()) brls::Application::giveFocus(cells_[0]);
    return root;
  }

 private:
  static constexpr int kTotalSlots =
      static_cast<int>(g3::kBoxCount * g3::kSlotsPerBox);  // 420

  brls::Box* MakeTopBar() {
    auto* bar = new brls::Box(brls::Axis::ROW);
    bar->setHeight(58);
    bar->setAlignItems(brls::AlignItems::CENTER);
    bar->setPadding(0, 40, 0, 40);
    // Sem fundo proprio: a faixa diagonal do GradientBackground e o fundo
    // desta barra. Um retangulo branco aqui cobriria a diagonal.

    auto* title = new brls::Label();
    title->setText("LISTA DE CRIATURAS");
    title->setFontSize(28);
    title->setTextColor(kTextPrimary);
    bar->addView(title);

    auto* spacer = new brls::Box(brls::Axis::ROW);
    spacer->setGrow(1.0f);
    bar->addView(spacer);

    range_ = new brls::Label();
    range_->setFontSize(22);
    range_->setTextColor(kTextSecondary);
    bar->addView(range_);

    return bar;
  }

  brls::Box* MakeStatsBar() {
    auto* bar = new brls::Box(brls::Axis::ROW);
    bar->setHeight(105);
    bar->setAlignItems(brls::AlignItems::CENTER);
    bar->setPadding(0, 40, 0, 40);
    // Translucida: sobre o fundo saturado um branco forte vira faixa leitosa.
    bar->setBackgroundColor(nvgRGBA(255, 255, 255, 90));

    // Largura maior e sem quebra: "Nv. 100 · Adamant · Caixa 1" nao cabia em
    // 210px e o texto se sobrepunha aos stats.
    auto* nameCol = new brls::Box(brls::Axis::COLUMN);
    nameCol->setJustifyContent(brls::JustifyContent::CENTER);
    nameCol->setWidth(300);
    nameCol->setMarginRight(20);

    statName_ = new brls::Label();
    statName_->setFontSize(26);
    statName_->setTextColor(kTextPrimary);
    nameCol->addView(statName_);

    statSub_ = new brls::Label();
    statSub_->setFontSize(18);
    statSub_->setTextColor(kTextSecondary);
    nameCol->addView(statSub_);
    bar->addView(nameCol);

    // Seis colunas de IV, na ordem do save.
    static const char* kNames[6] = {"HP", "ATQ", "DEF", "VEL", "ATQE", "DEFE"};
    for (int i = 0; i < 6; ++i) {
      auto* col = new brls::Box(brls::Axis::COLUMN);
      col->setWidth(78);
      col->setJustifyContent(brls::JustifyContent::CENTER);

      auto* k = new brls::Label();
      k->setText(kNames[i]);
      k->setFontSize(16);
      k->setTextColor(kTextTertiary);
      col->addView(k);

      statValues_[i] = new brls::Label();
      statValues_[i]->setFontSize(24);
      statValues_[i]->setTextColor(kTextPrimary);
      col->addView(statValues_[i]);

      bar->addView(col);
    }

    return bar;
  }

  // Slot cru, sem ordenacao. E o que a fonte tem naquela posicao fisica.
  g3::BoxPokemon RawAt(int index) const {
    if (index < 0 || index >= kTotalSlots) return {};
    return source_->At(index / g3::kSlotsPerBox, index % g3::kSlotsPerBox);
  }

  // Quantos itens a lista mostra AGORA. Com filtro ativo e menor que
  // kTotalSlots — paginacao, contador e barra de posicao seguem este numero,
  // nao o total fisico (spec 025).
  int VisibleCount() const {
    if (filter_ == bx::Filter::kNone && order_.empty()) return kTotalSlots;
    return static_cast<int>(order_.size());
  }

  // Pokemon na posicao `index` DA LISTA — passa pelo filtro e pela ordenacao
  // ativos. Ambos sao so uma visao: a fonte nunca e alterada (TD-01 da 024).
  g3::BoxPokemon MonAt(int index) const {
    if (index < 0 || index >= VisibleCount()) return {};
    if (order_.empty()) return RawAt(index);
    return RawAt(static_cast<int>(order_[index]));
  }

  // Recalcula a ordem para o criterio ativo. Chamado ao trocar o criterio, nao
  // a cada frame.
  void Reorder() {
    if (sort_ == bx::SortBy::kBox && filter_ == bx::Filter::kNone) {
      order_.clear();  // vazio = ordem crua, sem custo de indirecao
      return;
    }
    std::vector<bx::SortEntry> items;
    items.reserve(kTotalSlots);
    for (int i = 0; i < kTotalSlots; ++i) {
      const g3::BoxPokemon mon = RawAt(i);
      bx::SortEntry e;
      e.index = static_cast<std::size_t>(i);
      e.empty = mon.empty();
      if (!e.empty) {
        e.dex = mon.national_dex ? mon.national_dex : g3::NationalDex(mon.species);
        e.level = g3::ComputeStats(mon).level;
        e.name = DisplaySpecies(mon);
        e.shiny = mon.is_shiny();
      }
      items.push_back(std::move(e));
    }
    order_ = bx::FilterAndSort(items, filter_, sort_);
  }

  bool Scroll(int delta) {
    // max_top pode ser negativo quando o filtro deixa menos itens que uma
    // tela: nesse caso nao ha o que rolar.
    const int max_top = std::max(0, VisibleCount() - kListSize);
    int top = top_ + delta;
    if (top < 0) top = 0;
    if (top > max_top) top = max_top;
    if (top == top_) return true;
    top_ = top;
    RefreshCells();
    UpdateFooter();
    return true;
  }

  void RefreshCells() {
    for (int i = 0; i < kListSize; ++i) cells_[i]->Set(MonAt(top_ + i));

    // Contador e barra seguem o que esta VISIVEL: com filtro ativo a lista
    // encolhe, e mostrar 420 mentiria sobre o quanto ha para percorrer.
    const int total = VisibleCount();
    const int first = total == 0 ? 0 : top_ + 1;
    const int last = std::min(top_ + kListSize, total);
    std::string txt = std::to_string(first) + "-" + std::to_string(last) +
                      " / " + std::to_string(total);
    if (filter_ != bx::Filter::kNone) {
      txt += "  (" + std::string(bx::FilterName(filter_)) + ")";
    }
    range_->setText(txt);

    // Posicao da barra: proporcional ao quanto ja foi rolado.
    const int max_top = total - kListSize;
    const float ratio = max_top > 0 ? static_cast<float>(top_) / max_top : 0.0f;
    thumbTop_->setGrow(ratio * 4.0f);
    thumbBottom_->setGrow((1.0f - ratio) * 4.0f);
  }

  void OnCellFocus(int offset) {
    focused_ = offset;
    UpdateFooter();
  }

  void UpdateFooter() {
    const g3::BoxPokemon mon = MonAt(top_ + focused_);
    if (mon.empty()) {
      statName_->setText("—");
      statSub_->setText("");
      for (int i = 0; i < 6; ++i) statValues_[i]->setText("");
      return;
    }
    const g3::BattleStats bs = g3::ComputeStats(mon);
    statName_->setText(DisplaySpecies(mon));
    // A caixa vem do slot REAL, nao da posicao na lista: com a lista ordenada
    // os dois divergem, e mostrar a posicao ordenada seria mentir sobre onde o
    // Pokemon esta guardado (TD-01 da spec 024).
    const int slot = RealSlot(top_ + focused_);
    std::string sub = "Nv. " + std::to_string(bs.level) + " · " +
                      g3::NatureName(mon.nature()) + " · Caixa " +
                      std::to_string(slot / static_cast<int>(g3::kSlotsPerBox) + 1);
    if (sort_ != bx::SortBy::kBox) {
      sub += "  ·  ordem: " + std::string(bx::SortName(sort_));
    }
    statSub_->setText(sub);
    for (int i = 0; i < 6; ++i) {
      statValues_[i]->setText(std::to_string(bs.values[i]));
    }
  }

  // Slot fisico correspondente a uma posicao da lista. Sem ordenacao os dois
  // coincidem.
  int RealSlot(int index) const {
    if (index < 0 || index >= kTotalSlots) return 0;
    if (order_.empty()) return index;
    return static_cast<int>(order_[index]);
  }

  BoxSource* source_;
  std::vector<ListCell*> cells_;
  int top_ = 0;
  int focused_ = 0;
  // Criterio de ordenacao e a visao resultante. `order_` vazio = ordem crua.
  bx::SortBy sort_ = bx::SortBy::kBox;
  bx::Filter filter_ = bx::Filter::kNone;
  std::vector<std::size_t> order_;
  brls::Label* range_ = nullptr;
  brls::Label* statName_ = nullptr;
  brls::Label* statSub_ = nullptr;
  brls::Label* statValues_[6] = {};
  brls::Box* thumbTop_ = nullptr;
  brls::Box* thumbBottom_ = nullptr;
};

// --- Pokedex ---------------------------------------------------------------

// Paleta roxa: cada secao do produto tem sua cor (verde = caixas, roxo =
// enciclopedia, azul = backups).
const NVGcolor kDexBgTop = nvgRGB(0xF2, 0xEE, 0xFB);
const NVGcolor kDexBgMid = nvgRGB(0xE6, 0xDD, 0xF6);
const NVGcolor kDexBgBottom = nvgRGB(0xD8, 0xCC, 0xF0);
const NVGcolor kDexText = nvgRGB(0x3A, 0x33, 0x48);
const NVGcolor kDexTextSoft = nvgRGB(0x5F, 0x56, 0x77);
const NVGcolor kDexTextFaint = nvgRGB(0x7C, 0x72, 0x95);
const NVGcolor kDexIcon = nvgRGB(0x4B, 0x42, 0x60);

// A Pokedex usa o mesmo fundo do app, so trocando a paleta.
class DexBackground : public GradientBackground {
 public:
  DexBackground()
      : GradientBackground({kDexBgTop, kDexBgMid, kDexBgBottom}) {}
};

// Cores por tipo, para as pilulas. Indexadas pela numeracao do gen3.
NVGcolor TypeColor(std::uint8_t type) {
  static const NVGcolor kColors[] = {
      nvgRGB(0xB8, 0xB8, 0xA8),  // Normal
      nvgRGB(0xC0, 0x36, 0x28),  // Lutador
      nvgRGB(0xA9, 0xCD, 0xF0),  // Voador
      nvgRGB(0xA0, 0x40, 0xA0),  // Venenoso
      nvgRGB(0xE0, 0xC0, 0x68),  // Terrestre
      nvgRGB(0xB8, 0xA0, 0x38),  // Pedra
      nvgRGB(0xA8, 0xB8, 0x20),  // Inseto
      nvgRGB(0x70, 0x58, 0x98),  // Fantasma
      nvgRGB(0xB8, 0xB8, 0xD0),  // Metal
      nvgRGB(0xF0, 0xA2, 0x6B),  // Fogo
      nvgRGB(0x68, 0x90, 0xF0),  // Agua
      nvgRGB(0x78, 0xC8, 0x50),  // Planta
      nvgRGB(0xF8, 0xD0, 0x30),  // Eletrico
      nvgRGB(0xF8, 0x58, 0x88),  // Psiquico
      nvgRGB(0x98, 0xD8, 0xD8),  // Gelo
      nvgRGB(0x70, 0x38, 0xF8),  // Dragao
      nvgRGB(0x70, 0x58, 0x48),  // Sombrio
  };
  constexpr int kCount = sizeof(kColors) / sizeof(kColors[0]);
  if (type >= kCount) return nvgRGB(0x99, 0x99, 0x99);
  return kColors[type];
}

constexpr int kVisibleRows = 9;

// Linha da lista lateral: miniatura, numero e nome. Focavel, como as celulas
// da caixa — o borealis move o foco e o callback avisa a tela.
class DexRow : public brls::Box {
 public:
  using FocusCallback = std::function<void(int)>;

  DexRow(int offset, FocusCallback on_focus)
      : brls::Box(brls::Axis::ROW),
        offset_(offset),
        on_focus_(std::move(on_focus)) {
    setFocusable(true);
    setAlignItems(brls::AlignItems::CENTER);
    setHeight(56);
    setPadding(0, 16, 0, 16);
    setCornerRadius(14);
    setMargins(3, 0, 3, 0);

    icon_ = new brls::Image();
    icon_->setSize(brls::Size(40, 40));
    icon_->setScalingType(brls::ImageScalingType::STRETCH);
    icon_->setInterpolation(brls::ImageInterpolation::NEAREST);
    icon_->setMarginRight(14);
    addView(icon_);

    number_ = new brls::Label();
    number_->setFontSize(18);
    number_->setWidth(62);
    addView(number_);

    name_ = new brls::Label();
    name_->setFontSize(21);
    name_->setGrow(1.0f);
    addView(name_);

    addGestureRecognizer(new brls::TapGestureRecognizer(this));
  }

  void onFocusGained() override {
    brls::Box::onFocusGained();
    if (on_focus_) on_focus_(offset_);
  }

  void Set(int dex, bool owned) {
    number_->setText("N " + std::to_string(dex));
    name_->setText(owned ? g3::SpeciesNameByDex(dex) : "-----");

    number_->setTextColor(owned ? kDexTextSoft : nvgRGBA(0x7C, 0x72, 0x95, 120));
    name_->setTextColor(owned ? kDexText : nvgRGBA(0x7C, 0x72, 0x95, 120));

    if (owned) {
      icon_->setImageFromFile(std::string(POKEHOME_SPRITES) +
                              std::to_string(dex) + ".png");
      icon_->setVisibility(brls::Visibility::VISIBLE);
    } else {
      icon_->setVisibility(brls::Visibility::INVISIBLE);
    }
  }

  // Converte dex nacional -> indice interno do gen3. So serve para a faixa
  // 1..386; acima disso nao existe indice interno. Continua aqui porque a tela
  // de detalhes ainda depende das tabelas do gen3 (tipos e stats).
  //
  // O NOME nao passa mais por aqui: SpeciesNameByDex resolve direto, sem
  // varredura e cobrindo ate 1025 (spec 035).
  static std::uint16_t DexToInternal(int dex) {
    for (std::uint16_t i = 1; i < g3::SpeciesTableSize(); ++i) {
      if (g3::NationalDex(i) == dex) return i;
    }
    return 0;
  }

 private:
  int offset_;
  FocusCallback on_focus_;
  brls::Image* icon_ = nullptr;
  brls::Label* number_ = nullptr;
  brls::Label* name_ = nullptr;
};

class DexActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"DexActivity"};

  // Recebe as DUAS fontes e a sessao. Antes recebia so o save, entao mover um
  // Pokemon para o NestBox o fazia sumir da Pokedex (spec 026).
  //
  // `nest` e `session` sao opcionais: a dex aberta pelo menu, antes de haver
  // tela de caixas, so tem o save.
  explicit DexActivity(BoxSource* save, BoxSource* nest = nullptr,
                       bx::MoveSession* session = nullptr)
      : tally_(kDexMax) {
    Scan(save, kSaveId, session, dx::kSave);
    if (nest) Scan(nest, kNestId, session, dx::kNest);

    // Dex global: o historico gravado no banco, que inclui especies que ja
    // sairam (spec 029). Diferente do tally_, que conta so o que esta guardado
    // agora.
    if (auto* nb = dynamic_cast<NestBoxSource*>(nest)) {
      global_ = nb->data().SeenCount();
    }

    // owned_ e o que a tela ja usava; mantido para nao reescrever a lista.
    owned_.assign(kDexMax + 1, false);
    for (int d = 1; d <= static_cast<int>(kDexMax); ++d) {
      owned_[d] = tally_.Has(d);
    }
    count_ = static_cast<int>(tally_.Count());
  }

  brls::View* createContentView() override {
    auto* root = new DexBackground();
    root->setHeaderBand(58);
    root->setAxis(brls::Axis::COLUMN);

    root->addView(MakeTopBar());

    auto* body = new brls::Box(brls::Axis::ROW);
    body->setGrow(1.0f);
    body->setPadding(20, 40, 20, 40);

    body->addView(MakeDetailPanel());
    body->addView(MakeList());
    root->addView(body);


    root->registerAction(
        "Voltar", brls::BUTTON_B,
        [](brls::View*) {
          brls::Application::popActivity();
          return true;
        },
        false);
    root->registerAction(
        "Pagina anterior", brls::BUTTON_LB,
        [this](brls::View*) { return Scroll(-kVisibleRows); }, false);
    root->registerAction(
        "Proxima pagina", brls::BUTTON_RB,
        [this](brls::View*) { return Scroll(kVisibleRows); }, false);

    RefreshRows();
    if (!rows_.empty()) brls::Application::giveFocus(rows_[0]);
    return root;
  }

 private:
  brls::Box* MakeTopBar() {
    auto* bar = new brls::Box(brls::Axis::ROW);
    bar->setHeight(58);
    bar->setAlignItems(brls::AlignItems::CENTER);
    bar->setPadding(0, 40, 0, 40);
    // Sem fundo proprio: a faixa diagonal do GradientBackground e o fundo
    // desta barra. Um retangulo branco aqui cobriria a diagonal.

    auto* icon = new brls::Box(brls::Axis::ROW);
    icon->setSize(brls::Size(44, 44));
    icon->setCornerRadius(14);
    icon->setBackgroundColor(kDexIcon);
    icon->setMarginRight(20);
    bar->addView(icon);

    auto* title = new brls::Label();
    title->setText("TODAS AS CRIATURAS");
    title->setFontSize(28);
    title->setTextColor(kDexText);
    bar->addView(title);

    auto* spacer = new brls::Box(brls::Axis::ROW);
    spacer->setGrow(1.0f);
    bar->addView(spacer);

    auto* reg = new brls::Label();
    // Dois niveis, como spec/memory/produto.md pede: o que esta guardado
    // agora e o historico do banco (spec 029). O global so aparece quando ha
    // historico — numa primeira execucao os dois seriam iguais e a segunda
    // metade viraria ruido.
    std::string txt = "Registradas: " + std::to_string(count_) + " / " +
                      std::to_string(kDexMax);
    if (global_ > static_cast<std::size_t>(count_)) {
      txt += "   ·   ja vistas: " + std::to_string(global_);
    }
    reg->setText(txt);
    reg->setFontSize(22);
    reg->setTextColor(kDexTextSoft);
    bar->addView(reg);

    return bar;
  }

  brls::Box* MakeDetailPanel() {
    auto* panel = new brls::Box(brls::Axis::COLUMN);
    panel->setGrow(1.0f);
    panel->setMarginRight(24);

    // Cabecalho: numero e nome.
    auto* head = new brls::Box(brls::Axis::ROW);
    head->setAlignItems(brls::AlignItems::CENTER);
    head->setMarginBottom(14);

    dexNumber_ = new brls::Label();
    dexNumber_->setFontSize(26);
    dexNumber_->setTextColor(kDexTextFaint);
    dexNumber_->setMarginRight(16);
    head->addView(dexNumber_);

    dexName_ = new brls::Label();
    dexName_->setFontSize(44);
    dexName_->setTextColor(kDexText);
    head->addView(dexName_);
    panel->addView(head);

    // Area do sprite, com as pilulas de tipo por cima.
    auto* art = new brls::Box(brls::Axis::COLUMN);
    art->setGrow(1.0f);
    art->setCornerRadius(26);
    art->setBackgroundColor(nvgRGBA(255, 255, 255, 128));
    art->setJustifyContent(brls::JustifyContent::CENTER);
    art->setAlignItems(brls::AlignItems::CENTER);
    art->setMarginBottom(14);

    dexSprite_ = new brls::Image();
    dexSprite_->setSize(brls::Size(200, 200));
    dexSprite_->setScalingType(brls::ImageScalingType::STRETCH);
    dexSprite_->setInterpolation(brls::ImageInterpolation::NEAREST);
    art->addView(dexSprite_);

    types_ = new brls::Box(brls::Axis::ROW);
    types_->setJustifyContent(brls::JustifyContent::CENTER);
    types_->setMarginTop(14);
    art->addView(types_);
    panel->addView(art);

    // Base stats.
    stats_ = new brls::Box(brls::Axis::COLUMN);
    stats_->setCornerRadius(22);
    stats_->setBackgroundColor(nvgRGBA(255, 255, 255, 168));
    stats_->setPadding(16, 22, 16, 22);
    panel->addView(stats_);

    return panel;
  }

  brls::Box* MakeList() {
    auto* wrapper = new brls::Box(brls::Axis::COLUMN);
    wrapper->setWidth(360);
    wrapper->setCornerRadius(22);
    wrapper->setBackgroundColor(nvgRGBA(255, 255, 255, 128));
    wrapper->setPadding(12, 10, 12, 10);

    // Janela deslizante em vez de RecyclerFrame: o Recycler anima a rolagem e
    // a animacao le CNTVCT_EL0 (armGetSystemTick), instrucao que o JIT do
    // Ryujinx nao implementa — crash imediato no emulador.
    //
    // Aqui mantemos kVisibleRows linhas instanciadas e trocamos o conteudo ao
    // navegar. Mesmo efeito, sem animacao e sem 386 views.
    auto* list = new brls::Box(brls::Axis::COLUMN);
    list->setGrow(1.0f);
    for (int i = 0; i < kVisibleRows; ++i) {
      auto* cell = new DexRow(i, [this](int offset) { OnRowFocus(offset); });
      rows_.push_back(cell);
      list->addView(cell);
    }
    wrapper->addView(list);

    return wrapper;
  }

  brls::Box* MakeTypePill(std::uint8_t type) {
    auto* pill = new brls::Box(brls::Axis::ROW);
    pill->setHeight(36);
    pill->setCornerRadius(18);
    pill->setPadding(0, 24, 0, 24);
    pill->setMargins(0, 6, 0, 6);
    pill->setJustifyContent(brls::JustifyContent::CENTER);
    pill->setAlignItems(brls::AlignItems::CENTER);
    pill->setBackgroundColor(TypeColor(type));

    auto* t = new brls::Label();
    t->setText(g3::TypeName(type));
    t->setFontSize(20);
    t->setTextColor(kWhite);
    pill->addView(t);
    return pill;
  }

  brls::Box* MakeStatRow(const char* label, std::uint8_t value) {
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setMarginBottom(6);

    auto* l = new brls::Label();
    l->setText(label);
    l->setFontSize(18);
    l->setTextColor(kDexTextSoft);
    l->setWidth(56);
    row->addView(l);

    // Base stat vai ate 255; a barra usa 200 como referencia visual para o
    // grosso das especies nao ficar espremido.
    const float ratio = std::min(1.0f, static_cast<float>(value) / 200.0f);

    auto* track = new brls::Box(brls::Axis::ROW);
    track->setGrow(1.0f);
    track->setShrink(1.0f);
    track->setHeight(10);
    track->setCornerRadius(5);
    track->setBackgroundColor(nvgRGBA(0x7C, 0x72, 0x95, 60));
    track->setMargins(0, 12, 0, 12);

    auto* fill = new brls::Box(brls::Axis::ROW);
    fill->setGrow(ratio);
    fill->setShrink(1.0f);
    fill->setHeight(10);
    fill->setCornerRadius(5);
    fill->setBackgroundColor(nvgRGB(0x8B, 0x7A, 0xC4));
    track->addView(fill);

    auto* rest = new brls::Box(brls::Axis::ROW);
    rest->setGrow(1.0f - ratio);
    rest->setShrink(1.0f);
    track->addView(rest);
    row->addView(track);

    auto* v = new brls::Label();
    v->setText(std::to_string(value));
    v->setFontSize(18);
    v->setTextColor(kDexText);
    v->setWidth(46);
    v->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
    row->addView(v);

    return row;
  }

  void Select(int dex) {
    const bool owned = dex > 0 && dex < static_cast<int>(owned_.size()) &&
                       owned_[dex];
    const auto info = g3::Personal(dex);

    // Onde a especie esta guardada nesta sessao. O produto pede a leitura "por
    // jogo" alem do agregado (spec/memory/produto.md) — aqui isso aparece como
    // a origem de cada entrada (spec 026).
    std::string num = "N " + std::to_string(dex);
    if (owned) {
      const dx::Origin from = tally_.OriginOf(dex);
      const bool in_save = (from & dx::kSave) != 0;
      const bool in_nest = (from & dx::kNest) != 0;
      if (in_save && in_nest) {
        num += "   save + nestbox";
      } else if (in_nest) {
        num += "   nestbox";
      } else if (in_save) {
        num += "   save";
      }
    }
    dexNumber_->setText(num);
    dexName_->setText(owned ? g3::SpeciesNameByDex(dex) : "-----");

    if (owned) {
      dexSprite_->setImageFromFile(std::string(POKEHOME_SPRITES_BIG) +
                                   std::to_string(dex) + ".png");
      dexSprite_->setVisibility(brls::Visibility::VISIBLE);
    } else {
      dexSprite_->setVisibility(brls::Visibility::INVISIBLE);
    }

    // Tipos e stats vem da tabela do jogo, entao existem mesmo para especies
    // nao possuidas — mas so sao mostrados para as registradas, senao a
    // Pokedex entregaria informacao que o jogador ainda nao descobriu.
    types_->clearViews();
    stats_->clearViews();
    if (!owned) return;

    // Acima da dex 386 nao ha personal table no projeto: Personal() devolve
    // entrada zerada, e mostrar stats zerados seria pior que nao mostrar nada
    // — pareceria um Pokemon com 0 de tudo (TD-02 da spec 035).
    if (dex > static_cast<int>(g3::kMaxGen3Dex)) {
      auto* aviso = new brls::Label();
      aviso->setText("Dados de tipo e stats indisponiveis para esta geracao");
      aviso->setFontSize(17);
      aviso->setTextColor(kTextTertiary);
      stats_->addView(aviso);
      return;
    }

    types_->addView(MakeTypePill(info.type1));
    if (!info.single_type()) types_->addView(MakeTypePill(info.type2));

    static const char* kNames[6] = {"HP", "Atq", "Def", "Vel", "AtqE", "DefE"};
    for (int i = 0; i < 6; ++i) {
      stats_->addView(MakeStatRow(kNames[i], info.base_stats[i]));
    }
  }

  // Move a janela de linhas visiveis dentro da dex inteira.
  bool Scroll(int delta) {
    const int max_top = static_cast<int>(kDexMax) - kVisibleRows;
    int top = top_ + delta;
    if (top < 0) top = 0;
    if (top > max_top) top = max_top;
    if (top == top_) return true;
    top_ = top;
    RefreshRows();
    Select(top_ + focused_offset_ + 1);
    return true;
  }

  void RefreshRows() {
    for (int i = 0; i < kVisibleRows; ++i) {
      const int dex = top_ + i + 1;
      rows_[i]->Set(dex, dex < static_cast<int>(owned_.size()) && owned_[dex]);
    }
  }

  void OnRowFocus(int offset) {
    focused_offset_ = offset;
    Select(top_ + offset + 1);
  }

  // Varre uma fonte marcando as especies. Le pelo OVERLAY quando ha sessao:
  // contar a fonte crua ignoraria a movimentacao e registraria o Pokemon na
  // posicao antiga (spec 026).
  void Scan(BoxSource* src, int source_id, bx::MoveSession* session,
            dx::Origin from) {
    if (!src) return;
    const std::size_t boxes = src->BoxCount();
    for (std::size_t b = 0; b < boxes; ++b) {
      for (std::size_t s = 0; s < g3::kSlotsPerBox; ++s) {
        g3::BoxPokemon mon = src->At(b, s);
        if (session) mon = session->Get({source_id, b, s}, mon);
        if (mon.empty()) continue;
        const int dex =
            mon.national_dex ? mon.national_dex : g3::NationalDex(mon.species);
        tally_.Add(dex, from);
      }
    }
  }

  // A dex vai ate a gen9 (spec 035). Os sprites do romfs e a tabela de nomes
  // por dex cobrem toda essa faixa; o que continua limitado a 386 sao tipos e
  // stats, que vem do gen3_personal.h (TD-02 da spec 035).
  static constexpr std::size_t kDexMax = 1025;
  static constexpr int kNestId = 0;
  static constexpr int kSaveId = 1;

  dx::DexTally tally_;
  std::vector<bool> owned_;
  int count_ = 0;
  // Especies ja vistas alguma vez (historico do banco), contra count_, que e o
  // que esta guardado agora.
  std::size_t global_ = 0;
  int top_ = 0;
  int focused_offset_ = 0;
  std::vector<DexRow*> rows_;
  brls::Label* dexNumber_ = nullptr;
  brls::Label* dexName_ = nullptr;
  brls::Image* dexSprite_ = nullptr;
  brls::Box* types_ = nullptr;
  brls::Box* stats_ = nullptr;
};

// --- Tela ------------------------------------------------------------------

// Definida logo abaixo, mas usada aqui: a tela de caixas e o ponto de entrada
// da restauracao, e a restauracao precisa do SaveSource que esta tela abriu.
class RestoreActivity;

// Barra de legenda do rodape (spec 050). Definida junto do FooterTab, que
// depende da MessageBox — por isso so a declaracao aqui.
brls::Box* MakeLegendBar(bool back = true, brls::Label** out_back = nullptr,
                         const std::string& right_hint = "");

// Desenha os Pokemon do bloco que caem FORA da grade (spec 088, rodada 9).
//
// Existe porque a formacao acompanha o cursor sem ser grampeada: encostando na
// borda direita, parte dela fica no vao entre os dois paineis — o "slot
// imaginario" que o dono descreveu. Uma celula da grade nao pode desenhar ali
// (o desenho seria cortado pelo cartao), entao a tela precisa de uma view
// propria, absoluta e por cima de tudo.
//
// Nao guarda estado: le a lista e a geometria do BoxActivity a cada quadro
// pelos callbacks. Assim nao existe uma segunda copia do que ja e verdade la.
class BlockOverflow : public brls::View {
 public:
  // (sprite, x, y, tamanho) de cada Pokemon a desenhar, ja em pixel de tela.
  struct Item {
    std::string sprite;
    float x, y, size;
  };
  using Provider = std::function<std::vector<Item>()>;

  explicit BlockOverflow(Provider provider)
      : provider_(std::move(provider)) {
    // Nao rouba toque nem foco: e enfeite por cima da grade.
    setFocusable(false);
    setHideHighlight(true);
  }

  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    (void)x; (void)y; (void)w; (void)h; (void)style; (void)ctx;
    if (!provider_) return;
    const std::vector<Item> items = provider_();

    // Sombra de TODOS primeiro, sprites depois: desenhando um par por vez, a
    // sombra do proximo cairia por cima do sprite do anterior.
    //
    // O deslocamento (30% da largura para a direita, 20% da altura para
    // baixo) e o mesmo da SlotCell — quem vaza da grade tem de parecer parte
    // da mesma formacao, nao um segundo estilo de sombra.
    for (const Item& it : items) {
      int iw = 0, ih = 0;
      const int shadow = SilhouetteHandle(vg, it.sprite, iw, ih);
      if (shadow == 0) continue;
      const float sx = it.x + it.size * 0.3f;
      const float sy = it.y + it.size * 0.2f;
      NVGpaint p =
          nvgImagePattern(vg, sx, sy, it.size, it.size, 0, shadow, 0.5f);
      nvgBeginPath(vg);
      nvgRect(vg, sx, sy, it.size, it.size);
      nvgFillPaint(vg, p);
      nvgFill(vg);
    }

    for (const Item& it : items) {
      int iw = 0, ih = 0;
      const int tex = SpriteHandle(vg, it.sprite, iw, ih);
      if (tex == 0) continue;
      NVGpaint p =
          nvgImagePattern(vg, it.x, it.y, it.size, it.size, 0, tex, 1.0f);
      nvgBeginPath(vg);
      nvgRect(vg, it.x, it.y, it.size, it.size);
      nvgFillPaint(vg, p);
      nvgFill(vg);
    }
  }

 private:
  // Textura do sprite, cacheada por caminho — o mesmo padrao do
  // SilhouetteHandle (spec 085), sem o escurecimento.
  static int SpriteHandle(NVGcontext* vg, const std::string& path, int& iw,
                          int& ih) {
    struct Cached {
      std::string path;
      int handle, w, h;
    };
    static std::vector<Cached> cache;
    for (const auto& c : cache) {
      if (c.path == path) {
        iw = c.w;
        ih = c.h;
        return c.handle;
      }
    }
    int w = 0, h = 0;
    const int handle = nvgCreateImage(vg, path.c_str(), NVG_IMAGE_NEAREST);
    if (handle > 0) nvgImageSize(vg, handle, &w, &h);
    cache.push_back({path, handle, w, h});
    iw = w;
    ih = h;
    return handle;
  }

  Provider provider_;
};

// Simbolo de sexo desenhado em nanovg (spec 099): a fonte nao tem ♂/♀, e
// glifo ausente vira invisivel — desenho nao depende de fonte. O formato e o
// do HOME: circulo cheio azul/rosa com o simbolo branco por cima.
class GenderIcon : public brls::View {
 public:
  // 0=M 1=F; qualquer outro valor nao desenha nada.
  void Set(int g) { gender_ = g; }

  void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style,
            brls::FrameContext*) override {
    if (gender_ != 0 && gender_ != 1) return;
    const float cx = x + w / 2, cy = y + h / 2, r = h / 2;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgFillColor(vg, gender_ == 0 ? nvgRGB(0x3C, 0x9E, 0xF0)
                                  : nvgRGB(0xF0, 0x6E, 0x96));
    nvgFill(vg);

    nvgStrokeColor(vg, nvgRGB(255, 255, 255));
    nvgStrokeWidth(vg, 1.8f);
    if (gender_ == 0) {
      // ♂: circulo deslocado para baixo-esquerda + seta para nordeste.
      nvgBeginPath(vg);
      nvgCircle(vg, cx - r * 0.18f, cy + r * 0.18f, r * 0.34f);
      nvgStroke(vg);
      const float bx = cx + r * 0.06f, by = cy - r * 0.06f;
      const float tx = cx + r * 0.52f, ty = cy - r * 0.52f;
      nvgBeginPath(vg);
      nvgMoveTo(vg, bx, by);
      nvgLineTo(vg, tx, ty);
      nvgMoveTo(vg, tx - r * 0.36f, ty);
      nvgLineTo(vg, tx, ty);
      nvgLineTo(vg, tx, ty + r * 0.36f);
      nvgStroke(vg);
    } else {
      // ♀: circulo em cima + haste com travessao.
      nvgBeginPath(vg);
      nvgCircle(vg, cx, cy - r * 0.22f, r * 0.34f);
      nvgStroke(vg);
      nvgBeginPath(vg);
      nvgMoveTo(vg, cx, cy + r * 0.12f);
      nvgLineTo(vg, cx, cy + r * 0.62f);
      nvgMoveTo(vg, cx - r * 0.26f, cy + r * 0.38f);
      nvgLineTo(vg, cx + r * 0.26f, cy + r * 0.38f);
      nvgStroke(vg);
    }
  }

 private:
  int gender_ = -1;
};

// Icone de pessoa do treinador original (spec 099): cabeca + ombros brancos,
// como na captura do HOME. Nanovg — a fonte nao tem o glifo.
class PersonIcon : public brls::View {
  void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style,
            brls::FrameContext*) override {
    const float cx = x + w / 2;
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgBeginPath(vg);
    nvgCircle(vg, cx, y + h * 0.30f, h * 0.22f);
    nvgFill(vg);
    // Ombros: meia-capsula fechada pela base.
    nvgBeginPath(vg);
    nvgArc(vg, cx, y + h * 0.98f, h * 0.38f, NVG_PI, 2 * NVG_PI, NVG_CW);
    nvgClosePath(vg);
    nvgFill(vg);
  }
};

// Os seis marcadores da caixa em contorno esmaecido (spec 099): circulo,
// triangulo, quadrado, coracao, estrela e losango. Desenhados em nanovg pelo
// mesmo motivo do GenderIcon. Continuam espaco reservado (spec 098).
class MarkStrip : public brls::View {
 public:
  // Um dos 6 marcadores (circulo, triangulo, quadrado, coracao, estrela,
  // losango), centrado em (cx, cy) com "raio" s. A cor e a do nvgFillColor
  // corrente. Compartilhado com a tela de summary (spec 103).
  static void DrawMark(NVGcontext* vg, int i, float cx, float cy, float s) {
    nvgBeginPath(vg);
    switch (i) {
      case 0:
        nvgCircle(vg, cx, cy, s);
        break;
      case 1:
        nvgMoveTo(vg, cx, cy - s);
        nvgLineTo(vg, cx + s, cy + s * 0.85f);
        nvgLineTo(vg, cx - s, cy + s * 0.85f);
        nvgClosePath(vg);
        break;
      case 2:
        nvgRect(vg, cx - s * 0.9f, cy - s * 0.9f, s * 1.8f, s * 1.8f);
        break;
      case 3:
        nvgMoveTo(vg, cx, cy + s * 0.9f);
        nvgBezierTo(vg, cx - s * 1.5f, cy - s * 0.15f, cx - s * 0.6f,
                    cy - s * 1.1f, cx, cy - s * 0.3f);
        nvgBezierTo(vg, cx + s * 0.6f, cy - s * 1.1f, cx + s * 1.5f,
                    cy - s * 0.15f, cx, cy + s * 0.9f);
        break;
      case 4:
        for (int p = 0; p < 10; ++p) {
          const float rr = (p % 2 == 0) ? s * 1.1f : s * 0.45f;
          const float a = -NVG_PI / 2 + p * NVG_PI / 5;
          const float px = cx + rr * std::cos(a), py = cy + rr * std::sin(a);
          if (p == 0) nvgMoveTo(vg, px, py);
          else nvgLineTo(vg, px, py);
        }
        nvgClosePath(vg);
        break;
      case 5:
        nvgMoveTo(vg, cx, cy - s);
        nvgLineTo(vg, cx + s * 0.8f, cy);
        nvgLineTo(vg, cx, cy + s);
        nvgLineTo(vg, cx - s * 0.8f, cy);
        nvgClosePath(vg);
        break;
    }
    nvgFill(vg);
  }

  void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style,
            brls::FrameContext*) override {
    // Cheios e esmaecidos, como na captura do HOME (passo ~42px).
    const float step = w / 6.0f, cy = y + h / 2, s = 8.0f;
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 210));
    for (int i = 0; i < 6; ++i) DrawMark(vg, i, x + step * i + step / 2, cy, s);
  }
};

// --- Identidade compartilhada (specs 098/103) ------------------------------
//
// Viviam como estaticos da BoxActivity; subiram para escopo de namespace na
// spec 103 porque a tela de summary mostra os mesmos dados.

// Tag de idioma como o HOME exibe (spec 098). A numeracao e a mesma do
// gen3 ao gen9: 1=JPN 2=ENG 3=FRE 4=ITA 5=GER 7=SPA 8=KOR 9=CHS 10=CHT.
const char* LanguageTag(std::uint8_t lang) {
  switch (lang) {
    case 1: return "JPN";
    case 2: return "ENG";
    case 3: return "FRE";
    case 4: return "ITA";
    case 5: return "GER";
    case 7: return "SPA";
    case 8: return "KOR";
    case 9: return "CHS";
    case 10: return "CHT";
    default: return "";
  }
}

// Sigla do jogo de origem, no lugar da insignia grafica (spec 098). Os
// codigos sao o GameVersion unificado que todos os formatos usam — o gen3
// grava 1-5/15 na word de origins, os modernos gravam o byte direto.
const char* GameSigla(std::uint8_t game) {
  switch (game) {
    case 1: return "S";    case 2: return "R";    case 3: return "E";
    case 4: return "FR";   case 5: return "LG";   case 7: return "HG";
    case 8: return "SS";   case 10: return "D";   case 11: return "P";
    case 12: return "PT";  case 15: return "CXD"; case 20: return "W";
    case 21: return "B";   case 22: return "W2";  case 23: return "B2";
    case 24: return "X";   case 25: return "Y";   case 26: return "AS";
    case 27: return "OR";  case 30: return "SN";  case 31: return "MN";
    case 32: return "US";  case 33: return "UM";  case 42: return "GP";
    case 43: return "GE";  case 44: return "SW";  case 45: return "SH";
    case 47: return "LA";  case 48: return "BD";  case 49: return "SP";
    case 50: return "SL";  case 51: return "VL";
    // 52 conferido no save real do Z-A do dono (sonda da spec 099).
    case 52: return "ZA";
    default: return "—";
  }
}

// Sprite da pokebola por id (numeracao gen3/moderna compartilhada).
// Fora da faixa = nullptr e a vetorial continua.
const char* BallSprite(std::uint8_t ball) {
  static const char* kBalls[] = {
      nullptr,        "master-ball",  "ultra-ball",  "great-ball",
      "poke-ball",    "safari-ball",  "net-ball",    "dive-ball",
      "nest-ball",    "repeat-ball",  "timer-ball",  "luxury-ball",
      "premier-ball", "dusk-ball",    "heal-ball",   "quick-ball",
      "cherish-ball", "fast-ball",    "level-ball",  "lure-ball",
      "heavy-ball",   "love-ball",    "friend-ball", "moon-ball",
      "sport-ball",   "dream-ball",   "beast-ball"};
  return ball < sizeof(kBalls) / sizeof(kBalls[0]) ? kBalls[ball] : nullptr;
}

// Nome do arquivo de icone de tipo (romfs/ui/types), numeracao do PokeAPI
// (1=normal ... 18=fairy). nullptr fora da faixa.
const char* TypeSpriteName(int t) {
  static const char* kNames[] = {
      "",       "normal",  "fighting", "flying", "poison",  "ground",
      "rock",   "bug",     "ghost",    "steel",  "fire",    "water",
      "grass",  "electric","psychic",  "ice",    "dragon",  "dark",
      "fairy"};
  return (t >= 1 && t <= 18) ? kNames[t] : nullptr;
}

// --- Check Summary 1:1 com o Pokemon HOME (spec 103) -----------------------
//
// A referencia e docs/referencia-summary-home.jpg, captura NATIVA 1280x720 do
// console — toda coordenada e cor abaixo foi medida nela, entao os numeros
// sao absolutos de proposito: a tela nao reflui. Texto usa a fonte do app (a
// do HOME e proprietaria da Nintendo), entao posicao e alinhamento batem; a
// metrica dos glifos nao — criterio registrado na spec.

// Cores medidas na captura (conta-gotas).
const NVGcolor kSumOrange = nvgRGB(0xF0, 0x82, 0x1E);    // titulo e cubo
const NVGcolor kSumText = nvgRGB(0x4E, 0x5A, 0x54);      // texto escuro geral
const NVGcolor kSumTeal = nvgRGB(0x35, 0xB3, 0xA8);      // header do cartao
const NVGcolor kSumTealBar = nvgRGB(0x2F, 0xA8, 0x9E);   // barras OT/ID
const NVGcolor kSumBand = nvgRGB(0xDE, 0xF3, 0xE8);      // faixa de navegacao
const NVGcolor kSumUnderOn = nvgRGB(0x2F, 0xB0, 0xA6);   // barra de posicao
const NVGcolor kSumUnderOff = nvgRGB(0xBF, 0xE3, 0xDE);
const NVGcolor kSumTag = nvgRGB(0x8F, 0xA2, 0x9A);       // ENG e chevrons
const NVGcolor kSumRadarBg = nvgRGBA(0xB0, 0xB0, 0xB0, 235);
const NVGcolor kSumRadarFill = nvgRGB(0x3D, 0x87, 0xE8);
const NVGcolor kSumDivider = nvgRGB(0xCB, 0xE9, 0xDA);
const NVGcolor kSumLink = nvgRGB(0x2E, 0x7F, 0xE0);      // "Pokemon HOME" azul
const NVGcolor kSumMarkOff = nvgRGB(0xB9, 0xC4, 0xBE);
const NVGcolor kSumMarkBlue = nvgRGB(0x57, 0xA8, 0xE8);
const NVGcolor kSumMarkPink = nvgRGB(0xF0, 0x8C, 0xA8);
const NVGcolor kSumFooterText = nvgRGB(0x3B, 0x4A, 0x43);

class SummaryContent : public brls::Box {
 public:
  SummaryContent(g3::BoxPokemon mon, BoxSource* source, std::size_t box,
                 std::size_t slot)
      : mon_(std::move(mon)), source_(source), box_(box), slot_(slot) {
    // Imagens sao filhos ABSOLUTE; todo o resto e desenhado no draw().
    auto make_img = [this](float left, float top, float size) {
      auto* img = new brls::Image();
      img->setPositionType(brls::PositionType::ABSOLUTE);
      img->setPositionLeft(left);
      img->setPositionTop(top);
      img->setSize(brls::Size(size, size));
      img->setScalingType(brls::ImageScalingType::FIT);
      addView(img);
      return img;
    };
    sprite_ = make_img(880.0f, 150.0f, 300.0f);
    prev_ = make_img(60.0f, 62.0f, 56.0f);
    next_ = make_img(1172.0f, 62.0f, 56.0f);
    ball_ = make_img(281.0f, 75.0f, 30.0f);
    head_type_ = make_img(796.0f, 178.0f, 32.0f);
    for (int i = 0; i < 4; ++i)
      move_type_[i] = make_img(560.0f, 242.0f + kMoveStep * i, 34.0f);

    gender_ = new GenderIcon();
    gender_->setPositionType(brls::PositionType::ABSOLUTE);
    gender_->setPositionLeft(541.0f);
    gender_->setPositionTop(75.0f);
    gender_->setSize(brls::Size(30.0f, 30.0f));
    addView(gender_);

    Load(slot_);
  }

  // L/R: Pokemon anterior/seguinte da caixa, pulando vazios (spec 103).
  bool Nav(int dir) {
    if (!source_) return true;  // aberta da lista: sem caixa em volta
    const std::size_t n = kSlotsPerBox;
    std::size_t s = slot_;
    for (std::size_t i = 1; i < n; ++i) {
      s = (s + n + static_cast<std::size_t>(dir)) % n;
      if (!source_->At(box_, s).empty()) {
        NLOG_NAV("summary: %s -> slot %zu", dir < 0 ? "L" : "R", s);
        Load(s);
        return true;
      }
    }
    return true;  // sozinho na caixa
  }

  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    nvgFontFaceId(vg, brls::Application::getDefaultFont());

    // --- Fundo: gradiente vertical verde-menta + faixa diagonal clara ------
    NVGpaint bg = nvgLinearGradient(vg, x, y + 122.0f, x, y + h,
                                    nvgRGB(0xEA, 0xF7, 0xEE),
                                    nvgRGB(0xA5, 0xE3, 0xC2));
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, bg);
    nvgFill(vg);
    // Faixa diagonal translucida no topo direito, como na captura.
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + w * 0.42f, y);
    nvgLineTo(vg, x + w, y);
    nvgLineTo(vg, x + w, y + 300.0f);
    nvgClosePath(vg);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 46));
    nvgFill(vg);

    // --- Header branco (0-58) ----------------------------------------------
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, 58.0f);
    nvgFillColor(vg, kWhite);
    nvgFill(vg);
    DrawHomeCube(vg, x + 41.0f, y + 29.0f, 16.0f);
    Text(vg, x + 74.0f, y + 30.0f, 27.0f, kSumOrange,
         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "CHECK SUMMARY");
    // Relogio real, como o console mostra.
    char clock[8] = "";
    const std::time_t now = std::time(nullptr);
    std::strftime(clock, sizeof(clock), "%H:%M", std::localtime(&now));
    Text(vg, x + 1204.0f, y + 30.0f, 25.0f, kSumText,
         NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, clock);
    // Avatar do treinador: nao temos esse dado — placeholder neutro
    // (decisao em aberto na spec 103).
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1222.0f, y + 7.0f, 44.0f, 44.0f, 6.0f);
    nvgFillColor(vg, nvgRGB(0xCF, 0xE3, 0xDA));
    nvgFill(vg);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 220));
    nvgBeginPath(vg);
    nvgCircle(vg, x + 1244.0f, y + 24.0f, 8.0f);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, x + 1244.0f, y + 46.0f, 13.0f);
    nvgFill(vg);

    // --- Faixa de navegacao (58-122) ---------------------------------------
    nvgBeginPath(vg);
    nvgRect(vg, x, y + 58.0f, w, 64.0f);
    nvgFillColor(vg, kSumBand);
    nvgFill(vg);
    Text(vg, x + 124.0f, y + 90.0f, 30.0f, kSumTag,
         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, kGlyphChevronLeft);
    Text(vg, x + 152.0f, y + 90.0f, 34.0f, kSumText,
         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, kGlyphL);
    Text(vg, x + 1156.0f, y + 90.0f, 30.0f, kSumTag,
         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, kGlyphChevronRight);
    Text(vg, x + 1126.0f, y + 90.0f, 34.0f, kSumText,
         NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, kGlyphR);

    // Nome, nivel e marca de origem no grupo central.
    Text(vg, x + 330.0f, y + 90.0f, 30.0f, kSumText,
         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
         name_ + (mon_.is_shiny() ? "  ★" : ""));
    Text(vg, x + 676.0f, y + 90.0f, 28.0f, kSumText,
         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "Lv. " + std::to_string(level_));
    DrawOriginMark(vg, x + 820.0f, y + 90.0f, 14.0f);

    // Barra de posicao na caixa, sob o grupo central.
    const float bar_x = x + 220.0f, bar_w = 838.0f, bar_y = y + 114.0f;
    nvgBeginPath(vg);
    nvgRect(vg, bar_x, bar_y, bar_w, 6.0f);
    nvgFillColor(vg, kSumUnderOff);
    nvgFill(vg);
    const float frac =
        source_ ? static_cast<float>(slot_ + 1) / kSlotsPerBox : 1.0f;
    nvgBeginPath(vg);
    nvgRect(vg, bar_x, bar_y, bar_w * frac, 6.0f);
    nvgFillColor(vg, kSumUnderOn);
    nvgFill(vg);

    // --- Tag de idioma ------------------------------------------------------
    if (!lang_.empty()) {
      nvgBeginPath(vg);
      nvgRoundedRect(vg, x + 30.0f, y + 124.0f, 84.0f, 34.0f, 6.0f);
      nvgFillColor(vg, nvgRGBA(255, 255, 255, 160));
      nvgFill(vg);
      nvgStrokeColor(vg, kSumTag);
      nvgStrokeWidth(vg, 3.0f);
      nvgStroke(vg);
      Text(vg, x + 72.0f, y + 141.0f, 23.0f, kSumTag,
           NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, lang_);
    }

    DrawRadar(vg, x + 258.0f, y + 341.0f, 118.0f);

    // --- Tabela Nature/Ability/Held Item ------------------------------------
    {
      const float tx = x + 48.0f, ty = y + 565.0f, tw = 418.0f, th = 111.0f;
      nvgBeginPath(vg);
      nvgRoundedRect(vg, tx, ty, tw, th, 10.0f);
      nvgFillColor(vg, nvgRGBA(255, 255, 255, 225));
      nvgFill(vg);
      nvgStrokeColor(vg, kSumDivider);
      nvgStrokeWidth(vg, 1.5f);
      for (int r = 1; r < 3; ++r) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, tx + 12.0f, ty + th / 3 * r);
        nvgLineTo(vg, tx + tw - 12.0f, ty + th / 3 * r);
        nvgStroke(vg);
      }
      nvgBeginPath(vg);
      nvgMoveTo(vg, tx + 148.0f, ty + 8.0f);
      nvgLineTo(vg, tx + 148.0f, ty + th - 8.0f);
      nvgStroke(vg);
      const char* labels[3] = {"Nature", "Ability", "Held Item"};
      const std::string* values[3] = {&nature_, &ability_, &item_};
      for (int r = 0; r < 3; ++r) {
        const float cy = ty + th / 6 + th / 3 * r;
        Text(vg, tx + 74.0f, cy, 23.0f, kSumText,
             NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, labels[r]);
        Text(vg, tx + 167.0f, cy, 23.0f, kSumText,
             NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, *values[r]);
      }
    }

    // --- Cartao de golpes ---------------------------------------------------
    {
      const float cx0 = x + 508.0f, cy0 = y + 175.0f, cw = 372.0f;
      DrawSoftShadow(vg, cx0, cy0, cw, 295.0f, 10.0f);
      nvgBeginPath(vg);
      nvgRoundedRectVarying(vg, cx0, cy0, cw, 38.0f, 10.0f, 10.0f, 0.0f, 0.0f);
      nvgFillColor(vg, kSumTeal);
      nvgFill(vg);
      nvgBeginPath(vg);
      nvgRoundedRectVarying(vg, cx0, cy0 + 38.0f, cw, 257.0f, 0.0f, 0.0f,
                            10.0f, 10.0f);
      nvgFillColor(vg, nvgRGBA(255, 255, 255, 240));
      nvgFill(vg);
      char dex_no[16];
      std::snprintf(dex_no, sizeof(dex_no), "No. %04d", dex_);
      Text(vg, cx0 + 34.0f, cy0 + 19.0f, 24.0f, kWhite,
           NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dex_no);
      Text(vg, cx0 + 217.0f, cy0 + 19.0f, 24.0f, kWhite,
           NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, species_);
      // Linhas de golpe: nome; o icone de tipo e um filho Image.
      int drawn = 0;
      for (int i = 0; i < 4; ++i) {
        if (mon_.moves[i] == 0) continue;
        const float cy = cy0 + 84.0f + kMoveStep * i;
        Text(vg, cx0 + 107.0f, cy, 24.0f, kSumText,
             NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, move_names_[i]);
        if (drawn > 0) {
          nvgBeginPath(vg);
          nvgMoveTo(vg, cx0 + 22.0f, cy - kMoveStep / 2 + 0.5f);
          nvgLineTo(vg, cx0 + cw - 22.0f, cy - kMoveStep / 2 + 0.5f);
          nvgStrokeColor(vg, kSumDivider);
          nvgStrokeWidth(vg, 1.5f);
          nvgStroke(vg);
        }
        ++drawn;
      }
    }

    // Brilho suave atras do sprite grande.
    NVGpaint glow = nvgRadialGradient(vg, x + 1030.0f, y + 300.0f, 40.0f,
                                      170.0f, nvgRGBA(255, 255, 230, 90),
                                      nvgRGBA(255, 255, 230, 0));
    nvgBeginPath(vg);
    nvgRect(vg, x + 850.0f, y + 120.0f, 380.0f, 370.0f);
    nvgFillPaint(vg, glow);
    nvgFill(vg);

    // --- Marcacoes ----------------------------------------------------------
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 952.0f, y + 463.0f, 316.0f, 42.0f, 8.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 140));
    nvgFill(vg);
    for (int i = 0; i < 6; ++i) {
      // 2 bits por marca nos formatos modernos: 0=apagada 1=azul 2=rosa.
      const unsigned v = (markings_ >> (2 * i)) & 3u;
      nvgFillColor(vg, v == 1 ? kSumMarkBlue
                              : v == 2 ? kSumMarkPink : kSumMarkOff);
      MarkStrip::DrawMark(vg, i, x + 985.0f + 52.0f * i, y + 484.0f, 11.0f);
    }

    // --- Barras OT / ID No. -------------------------------------------------
    DrawIdBar(vg, x + 508.0f, y + 516.0f, 339.0f, 112.0f, "OT", ot_);
    DrawIdBar(vg, x + 855.0f, y + 516.0f, 400.0f, 133.0f, "ID No.", id_);

    // --- Memo ---------------------------------------------------------------
    {
      const float mx = x + 508.0f, my = y + 565.0f;
      nvgBeginPath(vg);
      nvgRoundedRect(vg, mx, my, 747.0f, 120.0f, 8.0f);
      nvgFillColor(vg, nvgRGBA(255, 255, 255, 200));
      nvgFill(vg);
      // Linha 1: prefixo escuro + lugar em azul de link, como na captura.
      nvgFontSize(vg, 22.5f);
      nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
      nvgFillColor(vg, kSumText);
      const float adv = nvgText(vg, mx + 16.0f, my + 27.0f,
                                memo_prefix_.c_str(), nullptr);
      nvgFillColor(vg, kSumLink);
      nvgText(vg, adv, my + 27.0f, memo_place_.c_str(), nullptr);
      if (!memo_tail_.empty()) {
        Text(vg, mx + 16.0f, my + 57.0f, 22.5f, kSumText,
             NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, memo_tail_);
      }
      Text(vg, mx + 16.0f, my + 95.0f, 22.5f, kSumText,
           NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, characteristic_);
    }

    // --- Rodape -------------------------------------------------------------
    // Sem a barra branca de largura inteira: no app, o branco e SO a pilula
    // do Ajuda, como nas outras telas (dono, 2026-08-17). A legenda desenha
    // direto sobre o fundo.
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x - 24.0f, y + 686.0f, 168.0f, 60.0f, 20.0f);
    nvgFillColor(vg, kWhite);
    nvgFill(vg);
    // Legenda em portugues, como o resto do app (dono, 2026-08-17); so o
    // conteudo do Pokemon (golpes etc.) fica em ingles. So B/L/R funcionam
    // nesta spec; o resto e legenda visual (spec 103).
    const float fy = y + 704.0f;
    Text(vg, x + 28.0f, fy, 26.0f, kSumFooterText,
         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, kGlyphMinus);
    Text(vg, x + 60.0f, fy, 24.0f, kSumFooterText,
         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "Ajuda");
    Text(vg, x + 186.0f, fy, 26.0f, kSumFooterText,
         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, kGlyphB);
    Text(vg, x + 218.0f, fy, 24.0f, kSumFooterText,
         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "Voltar");
    // Lado direito ancorado na BORDA, de tras para a frente, com a largura
    // medida em runtime: a fonte do app e mais larga que a do HOME e as
    // ancoras fixas da captura estouravam a tela.
    struct { const char* glyph; const char* label; } right[3] = {
        {kGlyphPlus, "Jogos conectados"},
        {kGlyphY, "Pontos de base"},
        {kGlyphX, "Mudar marcacoes"}};
    float rx = x + 1256.0f;
    for (int i = 2; i >= 0; --i) {
      nvgFontSize(vg, 24.0f);
      float bounds[4];
      const float tw =
          nvgTextBounds(vg, 0, 0, right[i].label, nullptr, bounds);
      rx -= tw;
      Text(vg, rx, fy, 24.0f, kSumFooterText,
           NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, right[i].label);
      rx -= 34.0f;
      Text(vg, rx, fy, 26.0f, kSumFooterText,
           NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, right[i].glyph);
      rx -= 36.0f;
    }

    // Filhos (sprites e icones) por cima das formas.
    brls::Box::draw(vg, x, y, w, h, style, ctx);
  }

 private:
  // Passo vertical das linhas de golpe, medido na captura.
  static constexpr float kMoveStep = 57.6f;

  static void Text(NVGcontext* vg, float x, float y, float size,
                   NVGcolor color, int align, const std::string& s) {
    nvgFontSize(vg, size);
    nvgFillColor(vg, color);
    nvgTextAlign(vg, align);
    nvgText(vg, x, y, s.c_str(), nullptr);
  }

  // Rotulo do radar: branco com contorno escuro (a captura usa outline; o
  // nanovg nao tem stroke de texto — quatro copias deslocadas aproximam).
  static void OutlinedText(NVGcontext* vg, float x, float y, float size,
                           const std::string& s) {
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGB(0x5A, 0x6E, 0x62));
    for (int dx = -1; dx <= 1; ++dx)
      for (int dy = -1; dy <= 1; ++dy)
        if (dx || dy) nvgText(vg, x + dx * 1.6f, y + dy * 1.6f, s.c_str(),
                              nullptr);
    nvgFillColor(vg, kWhite);
    nvgText(vg, x, y, s.c_str(), nullptr);
  }

  // Cubo do logo do HOME: hexagono com o "Y" interno, so contorno laranja.
  static void DrawHomeCube(NVGcontext* vg, float cx, float cy, float r) {
    nvgStrokeColor(vg, kSumOrange);
    nvgStrokeWidth(vg, 3.4f);
    nvgLineJoin(vg, NVG_ROUND);
    nvgBeginPath(vg);
    float px[6], py[6];
    for (int i = 0; i < 6; ++i) {
      const float a = -NVG_PI / 2 + i * NVG_PI / 3;
      px[i] = cx + r * std::cos(a);
      py[i] = cy + r * std::sin(a);
      if (i == 0) nvgMoveTo(vg, px[i], py[i]);
      else nvgLineTo(vg, px[i], py[i]);
    }
    nvgClosePath(vg);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx, cy);
    nvgLineTo(vg, px[0], py[0]);
    nvgMoveTo(vg, cx, cy);
    nvgLineTo(vg, px[2], py[2]);
    nvgMoveTo(vg, cx, cy);
    nvgLineTo(vg, px[4], py[4]);
    nvgStroke(vg);
  }

  // Marca de origem (o "piao" do HOME na captura): espiral aproximada em dois
  // arcos. Vetor proprio — a arte da Nintendo nao entra no repo (spec 097).
  static void DrawOriginMark(NVGcontext* vg, float cx, float cy, float r) {
    nvgStrokeColor(vg, nvgRGB(0x9F, 0xB9, 0xB2));
    nvgStrokeWidth(vg, 3.4f);
    nvgLineCap(vg, NVG_ROUND);
    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, r, -NVG_PI * 0.35f, NVG_PI * 1.15f, NVG_CW);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, r * 0.45f, NVG_PI * 0.65f, NVG_PI * 1.9f, NVG_CW);
    nvgStroke(vg);
  }

  void DrawRadar(NVGcontext* vg, float cx, float cy, float r) const {
    // Hexagono cinza com raios brancos.
    auto vertex = [&](int i, float rr, float* vx, float* vy) {
      const float a = -NVG_PI / 2 + i * NVG_PI / 3;
      *vx = cx + rr * std::cos(a);
      *vy = cy + rr * std::sin(a);
    };
    nvgBeginPath(vg);
    for (int i = 0; i < 6; ++i) {
      float vx, vy;
      vertex(i, r, &vx, &vy);
      if (i == 0) nvgMoveTo(vg, vx, vy);
      else nvgLineTo(vg, vx, vy);
    }
    nvgClosePath(vg);
    nvgFillColor(vg, kSumRadarBg);
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 230));
    nvgStrokeWidth(vg, 2.5f);
    for (int i = 0; i < 6; ++i) {
      float vx, vy;
      vertex(i, r, &vx, &vy);
      nvgBeginPath(vg);
      nvgMoveTo(vg, cx, cy);
      nvgLineTo(vg, vx, vy);
      nvgStroke(vg);
    }

    // Poligono azul dos stats. Eixos em sentido horario a partir do topo:
    // HP, Atk, Def, Spe, SpD, SpA (indices do save 0,1,2,3,5,4).
    //
    // ponytail: a escala do HOME nao e publica; stat/(2*maximo teorico no
    // nivel) reproduz a captura do Pikachu lv5 — knob para a conferencia.
    const float denom =
        2.0f * ((604.0f * level_) / 100.0f + level_ + 10.0f);
    static constexpr int kAxis[6] = {0, 1, 2, 3, 5, 4};
    nvgBeginPath(vg);
    for (int i = 0; i < 6; ++i) {
      const float v = stats_.values[kAxis[i]] / denom;
      const float rr = r * std::min(1.0f, std::max(v, 0.045f));
      float vx, vy;
      vertex(i, rr, &vx, &vy);
      if (i == 0) nvgMoveTo(vg, vx, vy);
      else nvgLineTo(vg, vx, vy);
    }
    nvgClosePath(vg);
    nvgFillColor(vg, kSumRadarFill);
    nvgFill(vg);

    // Rotulos e valores, nas ancoras medidas na captura.
    struct { const char* label; float lx, ly, vx, vy; int axis; }
    kLabels[6] = {
        {"HP", 0.0f, -165.0f, 0.0f, -133.0f, 0},
        {"Attack", 161.0f, -84.0f, 161.0f, -51.0f, 1},
        {"Defense", 163.0f, 84.0f, 163.0f, 117.0f, 2},
        {"Speed", 0.0f, 167.0f, 0.0f, 132.0f, 3},
        {"Sp. Def", -163.0f, 84.0f, -163.0f, 117.0f, 5},
        {"Sp. Atk", -164.0f, -84.0f, -164.0f, -51.0f, 4},
    };
    for (const auto& l : kLabels) {
      OutlinedText(vg, cx + l.lx, cy + l.ly, 24.0f, l.label);
      Text(vg, cx + l.vx, cy + l.vy, 26.0f, kSumText,
           NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
           std::to_string(stats_.values[l.axis]));
    }
  }

  static void DrawIdBar(NVGcontext* vg, float bx, float by, float bw,
                        float split, const char* label,
                        const std::string& value) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, bx, by, bw, 40.0f, 8.0f);
    nvgFillColor(vg, kSumTealBar);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, bx + split, by + 7.0f);
    nvgLineTo(vg, bx + split, by + 33.0f);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 120));
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);
    Text(vg, bx + split / 2, by + 20.0f, 23.0f, kWhite,
         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, label);
    Text(vg, bx + split + (bw - split) / 2, by + 20.0f, 23.0f, kWhite,
         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, value);
  }

  // Recarrega todos os dados exibidos a partir do slot dado.
  void Load(std::size_t slot) {
    slot_ = slot;
    if (source_) mon_ = source_->At(box_, slot_);
    stats_ = g3::ComputeStats(mon_);
    level_ = mon_.display_level ? mon_.display_level : stats_.level;
    dex_ = mon_.national_dex ? mon_.national_dex
                             : g3::NationalDex(mon_.species);
    species_ = DisplaySpecies(mon_);
    name_ = mon_.nickname.empty() ? species_ : mon_.nickname;
    lang_ = LanguageTag(mon_.language);
    nature_ = g3::NatureName(mon_.nature());

    if (mon_.modern) {
      const std::uint16_t id = mon_.modern->ability;
      ability_ = id < pokehome::modern::kAbilityCount
                     ? pokehome::modern::kAbilityNames[id] : "";
    } else {
      const g3::PersonalInfo personal = g3::Personal(dex_);
      ability_ = g3::AbilityName(personal.ability(mon_.ability_bit));
    }
    if (ability_.empty()) ability_ = "—";

    if (mon_.held_item == 0) {
      item_ = "None";
    } else if (mon_.modern) {
      item_ = mon_.held_item < pokehome::modern::kItemCount
                  ? pokehome::modern::kItemNames[mon_.held_item] : "";
      if (item_.empty()) item_ = "???";
    } else {
      // ponytail: numeracao de item do gen3 difere da moderna e nao ha
      // tabela propria; gerar uma quando algum save real mostrar item.
      item_ = "Item #" + std::to_string(mon_.held_item);
    }

    gender_->Set(g3::Gender(mon_));
    ot_ = mon_.ot_name;
    char idbuf[16];
    if (mon_.modern) {
      // ID de 6 digitos dos jogos modernos (G7TID).
      const std::uint32_t full =
          (static_cast<std::uint32_t>(mon_.modern->sid) << 16) |
          mon_.modern->tid;
      std::snprintf(idbuf, sizeof(idbuf), "%06u", full % 1000000u);
    } else {
      std::snprintf(idbuf, sizeof(idbuf), "%05u",
                    static_cast<unsigned>(mon_.ot_id & 0xFFFF));
    }
    id_ = idbuf;

    markings_ = mon_.modern ? mon_.modern->markings : 0;

    // Memo (nucleo testado em summary_facts.h; aqui so a quebra de linha e o
    // acento de exibicao).
    const bool fateful = mon_.modern && mon_.modern->fateful_encounter;
    const char* place = pokehome::summary::OriginGameName(mon_.origin_game);
    memo_place_ = *place ? place : "Pokemon HOME";
    const std::size_t at = memo_place_.find("Pokemon");
    if (at != std::string::npos) memo_place_.replace(at, 7, "Pokémon");
    memo_prefix_ = fateful ? "Seems to have had a fateful encounter in "
                           : "Seems to have met in ";
    memo_tail_.clear();
    if (mon_.modern) {
      const auto& d = mon_.modern->met_date;
      if (d[0] || d[1] || d[2]) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "on %d/%d/%d.", d[1], d[2],
                      2000 + d[0]);
        memo_tail_ = buf;
      }
    }
    if (memo_tail_.empty()) memo_place_ += ".";

    const std::uint8_t* ivs =
        mon_.modern ? mon_.modern->ivs.data() : mon_.ivs;
    const std::uint32_t ec =
        mon_.modern ? mon_.modern->encryption_constant : mon_.personality;
    characteristic_ =
        std::string("Characteristic: ") +
        pokehome::summary::CharacteristicText(
            pokehome::summary::CharacteristicIndex(ivs, ec));

    // Imagens.
    sprite_->setImageFromFile(SpritePathBig(mon_));
    const char* ball = BallSprite(mon_.display_ball);
    if (!ball && !mon_.modern) ball = "poke-ball";  // gen3 sem bola parseada
    if (ball) {
      ball_->setImageFromFile(std::string(POKEHOME_UI_ASSETS) + "balls/" +
                              ball + ".png");
      ball_->setVisibility(brls::Visibility::VISIBLE);
    } else {
      ball_->setVisibility(brls::Visibility::INVISIBLE);
    }
    const std::uint8_t head_t =
        (dex_ >= 1 && dex_ <= 1025) ? pokehome::modern::kTypeIds[dex_][0] : 0;
    SetTypeIcon(head_type_, head_t);
    for (int i = 0; i < 4; ++i) {
      const std::uint16_t mv = mon_.moves[i];
      const std::uint8_t t =
          (mv > 0 && mv < pokehome::modern::kMoveTypeCount)
              ? pokehome::modern::kMoveTypeIds[mv] : 0;
      SetTypeIcon(move_type_[i], t);
      move_names_[i] = DisplayMove(mv);  // spec 106: alcanca a gen9
    }

    // Vizinhos da caixa para as pontas L/R.
    SetNeighbor(prev_, -1);
    SetNeighbor(next_, +1);
  }

  void SetTypeIcon(brls::Image* img, int type) {
    const char* name = TypeSpriteName(type);
    if (name) {
      img->setImageFromFile(std::string(POKEHOME_UI_ASSETS) + "types/" +
                            name + ".png");
      img->setVisibility(brls::Visibility::VISIBLE);
    } else {
      img->setVisibility(brls::Visibility::INVISIBLE);
    }
  }

  void SetNeighbor(brls::Image* img, int dir) {
    if (source_) {
      const std::size_t n = kSlotsPerBox;
      std::size_t s = slot_;
      for (std::size_t i = 1; i < n; ++i) {
        s = (s + n + static_cast<std::size_t>(dir)) % n;
        const g3::BoxPokemon m = source_->At(box_, s);
        if (!m.empty()) {
          img->setImageFromFile(SpritePath(m));
          img->setVisibility(brls::Visibility::VISIBLE);
          return;
        }
      }
    }
    img->setVisibility(brls::Visibility::INVISIBLE);
  }

  g3::BoxPokemon mon_;
  BoxSource* source_;
  std::size_t box_, slot_;
  g3::BattleStats stats_;
  int level_ = 0, dex_ = 0;
  unsigned markings_ = 0;
  std::string species_, name_, lang_, nature_, ability_, item_, ot_, id_;
  std::string memo_prefix_, memo_place_, memo_tail_, characteristic_;
  std::string move_names_[4];
  brls::Image* sprite_ = nullptr;
  brls::Image* prev_ = nullptr;
  brls::Image* next_ = nullptr;
  brls::Image* ball_ = nullptr;
  brls::Image* head_type_ = nullptr;
  brls::Image* move_type_[4] = {};
  GenderIcon* gender_ = nullptr;
};

class SummaryActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"SummaryActivity"};

  SummaryActivity(g3::BoxPokemon mon, BoxSource* source, std::size_t box,
                  std::size_t slot)
      : mon_(std::move(mon)), source_(source), box_(box), slot_(slot) {}

  brls::View* createContentView() override {
    auto* c = new SummaryContent(mon_, source_, box_, slot_);
    c->setFocusable(true);
    c->registerAction(
        "Voltar", brls::BUTTON_B,
        [](brls::View*) {
          brls::Application::popActivity();
          return true;
        },
        false);
    c->registerAction(
        "Anterior", brls::BUTTON_LB,
        [c](brls::View*) { return c->Nav(-1); }, false);
    c->registerAction(
        "Proximo", brls::BUTTON_RB,
        [c](brls::View*) { return c->Nav(+1); }, false);
    return c;
  }

 private:
  g3::BoxPokemon mon_;
  BoxSource* source_;
  std::size_t box_, slot_;
};

void ShowSummary(const g3::BoxPokemon& mon, BoxSource* source, std::size_t box,
                 std::size_t slot) {
  NLOG_NAV("abre summary de %s (caixa %zu slot %zu)",
           DisplaySpecies(mon).c_str(), box, slot);
  brls::Application::pushActivity(new SummaryActivity(mon, source, box, slot));
}

// Cartao da visao geral (spec 105): substitui a barra de status enquanto o
// painel ativo mostra caixas. Desenha o cartao central (faixa teal com o nome
// da caixa + corpo branco com a contagem) e, a esquerda, o tooltip com os
// mini-sprites do conteudo da caixa sob o cursor — como na referencia.
class BoxOverviewCard : public brls::View {
 public:
  void Set(const std::string& name, int count,
           std::vector<std::string> sprites, bool left_side) {
    name_ = name;
    count_ = count;
    sprites_ = std::move(sprites);
    left_side_ = left_side;
  }
  void Clear() { name_.clear(); sprites_.clear(); }

  void draw(NVGcontext* vg, float x, float y, float /*w*/, float /*h*/,
            brls::Style, brls::FrameContext*) override {
    if (name_.empty()) return;
    nvgFontFaceId(vg, brls::Application::getDefaultFont());

    // O overlay e 1x1 (desenha fora do proprio retangulo, como o
    // BlockOverflow): as medidas saem da tela logica de 1280x720.
    const float w = 1280.0f, h = 720.0f - 44.0f;  // 44 = faixa da legenda

    // Cartao central: teal em cima, branco embaixo (referencia).
    const float cw = 320.0f, cx = x + w / 2 - cw / 2;
    const float head_y = y + h - 76.0f;
    DrawSoftShadow(vg, cx, head_y, cw, 70.0f, 10.0f);
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, cx, head_y, cw, 34.0f, 10.0f, 10.0f, 0, 0);
    nvgFillColor(vg, kBoxBarOn);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, cx, head_y + 34.0f, cw, 36.0f, 0, 0, 10.0f,
                          10.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 240));
    nvgFill(vg);
    nvgFontSize(vg, 20.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, kWhite);
    nvgText(vg, cx + cw / 2, head_y + 17.0f, name_.c_str(), nullptr);
    nvgFillColor(vg, kTextPrimary);
    const std::string body =
        std::to_string(count_) + "/" + std::to_string(kSlotsPerBox);
    nvgText(vg, cx + cw / 2, head_y + 52.0f, body.c_str(), nullptr);

    // Tooltip de conteudo: grade 6x5 de mini-sprites, no canto do MESMO
    // lado do painel em visao geral (dono, 2026-08-17).
    if (sprites_.empty()) return;
    const float cell = 34.0f, pad = 10.0f;
    const float tw = kCols * cell + pad * 2;
    const float th = kRows * cell + pad * 2;
    const float tx = left_side_ ? x + 16.0f : x + w - tw - 16.0f;
    const float ty = y + h - th - 4.0f;
    DrawSoftShadow(vg, tx, ty, tw, th, 14.0f);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, tx, ty, tw, th, 14.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 240));
    nvgFill(vg);
    for (std::size_t i = 0; i < sprites_.size() && i < kSlotsPerBox; ++i) {
      if (sprites_[i].empty()) continue;
      const int tex = Handle(vg, sprites_[i]);
      if (tex <= 0) continue;
      const float sx = tx + pad + (i % kCols) * cell;
      const float sy = ty + pad + (i / kCols) * cell;
      NVGpaint p = nvgImagePattern(vg, sx, sy, cell, cell, 0, tex, 1.0f);
      nvgBeginPath(vg);
      nvgRect(vg, sx, sy, cell, cell);
      nvgFillPaint(vg, p);
      nvgFill(vg);
    }
  }

 private:
  // Cache de textura por caminho ABSOLUTO — o SlotIconHandle do SlotCell
  // prefixa o romfs de UI, e aqui o caminho e o do sprite de especie.
  static int Handle(NVGcontext* vg, const std::string& file) {
    struct Cached {
      std::string file;
      int handle;
    };
    static std::vector<Cached> cache;
    for (const auto& c : cache) {
      if (c.file == file) return c.handle;
    }
    const int handle = nvgCreateImage(vg, file.c_str(), 0);
    cache.push_back({file, handle});
    return handle;
  }

  std::string name_;
  int count_ = 0;
  bool left_side_ = true;
  std::vector<std::string> sprites_;
};

class BoxActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"BoxActivity"};

  BoxActivity(BoxSource* nest, BoxSource* save) : nest_(nest), save_(save) {}

  brls::View* createContentView() override {
    auto* root = new GradientBackground();
    root->setHeaderBand(58);
    root->setAxis(brls::Axis::COLUMN);
    // A raiz NAO e focavel: quem recebe foco sao as celulas. Se a raiz for
    // focavel ela captura o foco e as celulas nunca sao alcancadas.

    root->addView(MakeTopBar());

    // Area dos paineis. 22px abaixo da barra "CRIATURAS" (spec 048); os
    // paineis tem largura fixa, entao o conjunto e centrado na horizontal.
    auto* panels = new brls::Box(brls::Axis::ROW);
    panels->setGrow(1.0f);
    panels->setJustifyContent(brls::JustifyContent::CENTER);
    panels->setAlignItems(brls::AlignItems::FLEX_START);
    // As margens laterais do proprio painel (kPanelGap/2) ja contam para a
    // borda da tela — o padding aqui e so o que falta para kScreenSideMargin.
    panels->setPadding(kPanelTopGap, kScreenSideMargin - kPanelGap / 2, 28,
                       kScreenSideMargin - kPanelGap / 2);
    // Sobe a caixa colando na barra (spec 050 rodada 3). O vao que sobrava
    // era a folga INTERNA da barra (72px de altura para 52px de icone), e o
    // dono pediu para nao mexer no header — entao a area dos paineis avanca
    // por cima dessa folga em vez de a barra encolher.
    panels->setMarginTop(-10.0f);

    left_ = MakePanel(
        nest_, /*accent=*/true,
        [this](std::size_t i) {
          activeLeft_ = true;
          cursor_ = i;
          OnCursorMoved();
        },
        kNestId, &session_, [this] { ToggleOverview(left_); });
    right_ = MakePanel(
        save_, /*accent=*/false,
        [this](std::size_t i) {
          activeLeft_ = false;
          cursor_ = i;
          OnCursorMoved();
        },
        kSaveId, &session_, [this] { ToggleOverview(right_); });
    // Os avisos em repouso comparam contra o jogo do SAVE ABERTO (spec 104),
    // nos DOIS paineis — e o que faz um Pokemon parado no NestBox avisar que
    // nao entra no save antes de o jogador tentar move-lo.
    left_.ref_game = right_.ref_game = save_ ? save_->GameId() : cp::Game::kCount;
    // A borda da grade PARA (rodada 8). Trocar de caixa e exclusividade do
    // L/R: deixar a lateral virar pagina fazia o cursor "escapar" da caixa
    // sem o jogador pedir.
    panels->addView(left_.root);
    panels->addView(right_.root);

    // Overlay do bloco que vaza da grade (rodada 9). POSITION_ABSOLUTE e por
    // ULTIMO no root: precisa cobrir os dois paineis e o vao entre eles, que
    // e onde mora o "slot imaginario".
    overflowView_ = new BlockOverflow([this] { return OverflowItems(); });
    overflowView_->setPositionType(brls::PositionType::ABSOLUTE);
    overflowView_->setPositionTop(0);
    overflowView_->setPositionLeft(0);
    overflowView_->setWidth(1);   // nao ocupa espaco: desenha em coordenada
    overflowView_->setHeight(1);  // absoluta de tela, fora do proprio retangulo
    panels->addView(overflowView_);

    root->addView(panels);

    statsCard_ = MakeStatsBar();
    root->addView(statsCard_);
    // Legenda do rodape (spec 050): esta tela era a unica sem ela.
    root->addView(MakeLegendBar(/*back=*/true, &backLabel_,
                                std::string(kGlyphY) + "  Trocar"));

    // Cartao da visao geral (spec 105): overlay ABSOLUTE cobrindo a tela —
    // ele se desenha na regiao da barra de status, que some enquanto o
    // painel ativo mostra caixas.
    ovCard_ = new BoxOverviewCard();
    ovCard_->setPositionType(brls::PositionType::ABSOLUTE);
    ovCard_->setPositionTop(0);
    ovCard_->setPositionLeft(0);
    ovCard_->setWidth(1);   // desenha em coordenada absoluta, como o
    ovCard_->setHeight(1);  // BlockOverflow acima
    root->addView(ovCard_);

    RegisterActions(root);
    Refresh();

    // Foco inicial na primeira celula do save: sem isso o borealis nao escolhe
    // nenhuma e a grade fica sem cursor.
    if (!right_.cells.empty()) {
      brls::Application::giveFocus(right_.cells[0]);
    }
    return root;
  }

 private:
  // Fora de linha: RestoreActivity so fica completa depois desta classe.
  void OpenRestore();

  brls::Box* MakeTopBar() {
    auto* bar = new brls::Box(brls::Axis::ROW);
    bar->setHeight(72);   // 96 * 0.75
    bar->setAlignItems(brls::AlignItems::CENTER);
    bar->setPadding(0, 40, 0, 40);
    // Sem fundo proprio: a faixa diagonal do GradientBackground e o fundo
    // desta barra. Um retangulo branco aqui cobriria a diagonal.

    // Bloco da esquerda (icone + titulo) com largura FIXA, e um bloco vazio
    // igual a ele na direita (mais abaixo). E o que permite centrar a faixa
    // dos modos na tela: com o titulo de largura livre, os espacadores
    // grow(1) davam pesos diferentes aos dois lados e a faixa saia deslocada.
    auto* left_block = new brls::Box(brls::Axis::ROW);
    left_block->setWidth(kTopBarSideBlock);
    left_block->setAlignItems(brls::AlignItems::CENTER);

    // O quadrado escuro que segurava o lugar do icone saiu (dono,
    // 2026-08-17) — o titulo abre a barra sozinho.
    auto* title = new brls::Label();
    // Mesmo texto e tamanho da barra da tela anterior. Acento como no HOME
    // (spec 105) — a fonte padrao tem o glifo.
    title->setText("POKÉMON");
    title->setFontSize(24);
    title->setTextColor(kTextPrimary);
    left_block->addView(title);
    bar->addView(left_block);

    auto* spacer = new brls::Box(brls::Axis::ROW);
    spacer->setGrow(1.0f);
    bar->addView(spacer);

    // Marcador discreto: a tela de aviso ja explicou na abertura, aqui e so
    // lembrete de que o app esta limitado.
    if (IsAppletMode()) {
      auto* warn = new brls::Box(brls::Axis::ROW);
      warn->setHeight(30);
      warn->setCornerRadius(15);
      warn->setPadding(0, 16, 0, 16);
      warn->setMarginRight(20);
      warn->setJustifyContent(brls::JustifyContent::CENTER);
      warn->setAlignItems(brls::AlignItems::CENTER);
      warn->setBackgroundColor(nvgRGB(0xE8, 0x85, 0x3F));

      auto* wt = new brls::Label();
      wt->setText("MODO APPLET");
      wt->setFontSize(17);
      wt->setTextColor(kWhite);
      warn->addView(wt);
      bar->addView(warn);
    }

    // Indicador dos tres modos de cursor (spec 052): ZL, a faixa com os tres
    // icones e o seletor vermelho no ativo, e ZR — o formato do HOME. Antes
    // eram tres pilulas de texto.
    auto* modes = new brls::Box(brls::Axis::ROW);
    modes->setAlignItems(brls::AlignItems::CENTER);

    auto* zl = new brls::Label();
    zl->setText(kGlyphZL);
    zl->setFontSize(26);
    zl->setTextColor(kTextSecondary);
    zl->setMarginRight(10);
    modes->addView(zl);

    modeStrip_ = new ModeStrip();
    // 42 de altura e a REFERENCIA da spec 094 — todas as medidas da folha
    // foram escaladas por 42/72 a partir dela. A largura sai do passo dos
    // tres icones mais a folga que o degrade precisa para sumir nas pontas.
    modeStrip_->setSize(
        brls::Size(kModeCell * kModeCount + 46.0f, kModeStripH));
    modes->addView(modeStrip_);

    auto* zr = new brls::Label();
    zr->setText(kGlyphZR);
    zr->setFontSize(26);
    zr->setTextColor(kTextSecondary);
    zr->setMarginLeft(10);
    modes->addView(zr);

    bar->addView(modes);

    // O "Caixa 1 / 32" saiu do cabecalho (spec 052 rodada 2): a numeracao ja
    // aparece no rodape de cada painel. O espacador fica para equilibrar o
    // da esquerda e manter a faixa no centro da tela.
    auto* spacer_r = new brls::Box(brls::Axis::ROW);
    spacer_r->setGrow(1.0f);
    bar->addView(spacer_r);

    // Espelho do bloco da esquerda: mesma largura, vazio. Sem ele a faixa
    // ficaria a direita do centro, que foi o que o dono viu na tela.
    auto* right_block = new brls::Box(brls::Axis::ROW);
    right_block->setWidth(kTopBarSideBlock);
    bar->addView(right_block);

    return bar;
  }

  // Move o seletor vermelho para o modo ativo (spec 052).
  //
  // O enum e {kMove=0, kSwap=1, kSelect=2}, mas a faixa mostra na ordem
  // trocar, mover, selecionar — este mapa converte um no outro. Sem ele o
  // seletor apontaria o icone errado.
  void RefreshModePills() {
    static constexpr int kEnumToStrip[3] = {1, 0, 2};
    if (modeStrip_) {
      modeStrip_->SetActive(kEnumToStrip[static_cast<int>(mode_)]);
    }
  }

  // Barra de status (spec 053), no formato da referencia: uma linha superior
  // com nome, genero, nivel e treinador, e abaixo a grade de campos
  // rotulados — natureza/habilidade a esquerda, os seis stats no meio e os
  // quatro golpes a direita.
  //
  // Os rotulos ficam SEMPRE visiveis; so os valores esvaziam quando o slot
  // esta vazio (imagem 1 da referencia). Uma barra que some inteira faria a
  // tela pular de altura a cada movimento do cursor.
  // Geometria MEDIDA na captura do HOME em 1280x720 (2026-08-17, pedido
  // "minucioso" do dono): faixa teal de 26px comecando no topo da barra;
  // bola da captura centrada em x=47; nome em x=75; sexo centrado em x=237;
  // "Lv." em x=300 e o numero em x=340; badge reservado em x=420; OT
  // centrado na folga; [ENG] terminando em ~950; insignia em ~985; seis
  // marcadores de x~1032 a ~1240 (passo ~42). Grade: tarjas em x=30 (75 de
  // largura) e x=340/505/670 (108); golpes em x=845 e x=1050; logo
  // translucida encostada na direita.
  // O cartao da barra (dono, 2026-08-17): 10px de folga nas laterais,
  // cantos arredondados, sombra de elevacao, header #38B6AB e corpo
  // #EAF9F2. O desenho e do proprio cartao — header e grade ficam
  // transparentes para nao pintar por cima dos cantos.
  class StatsCard : public brls::Box {
    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
      const float r = 14.0f;
      DrawSoftShadow(vg, x, y, w, h, r);
      nvgBeginPath(vg);
      nvgRoundedRect(vg, x, y, w, h, r);
      nvgFillColor(vg, kStatusBodyBg);
      nvgFill(vg);
      nvgBeginPath(vg);
      nvgRoundedRectVarying(vg, x, y, w, 26.0f, r, r, 0.0f, 0.0f);
      nvgFillColor(vg, kStatusHeadBg);
      nvgFill(vg);
      brls::Box::draw(vg, x, y, w, h, style, ctx);
    }
  };

  brls::Box* MakeStatsBar() {
    auto* bar = new StatsCard();
    bar->setAxis(brls::Axis::COLUMN);
    bar->setHeight(104);
    // O cartao nao encosta nas bordas; 2px embaixo para desgrudar do rodape
    // "Ajuda" (pedido do dono, 2026-08-17).
    bar->setMargins(0, 10, 2, 10);

    // --- Linha 1: a faixa teal de identificacao, texto branco (referencia).
    // Coordenadas internas = as da captura menos os 10px da margem.
    auto* head = new StatusHead();
    head->setAxis(brls::Axis::ROW);
    head->setHeight(26);  // medido: y 577-603 na captura
    head->setAlignItems(brls::AlignItems::CENTER);
    head->setPadding(0, 22, 0, 65);
    statusHead_ = head;

    // Sprite da pokebola da captura (spec 099), no lugar da vetorial quando a
    // bola e conhecida. ABSOLUTE, centro em x=47 como na captura.
    statBall_ = new brls::Image();
    statBall_->setPositionType(brls::PositionType::ABSOLUTE);
    statBall_->setPositionLeft(27);
    statBall_->setPositionTop(3);
    statBall_->setSize(brls::Size(20, 20));
    statBall_->setScalingType(brls::ImageScalingType::FIT);
    statBall_->setVisibility(brls::Visibility::INVISIBLE);
    head->addView(statBall_);

    // Nome com largura FIXA (x 75-229): e o que poe o sexo sempre em x=237,
    // como na captura — campo de largura livre deslocaria tudo a direita.
    statName_ = new brls::Label();
    statName_->setFontSize(18);
    statName_->setTextColor(kWhite);
    statName_->setWidth(154);
    statName_->setSingleLine(true);
    statName_->setShrink(0.0f);
    head->addView(statName_);

    statGender_ = new GenderIcon();
    statGender_->setSize(brls::Size(17, 17));
    statGender_->setShrink(0.0f);
    head->addView(statGender_);

    // "Lv." menor e mais claro que o numero, como na captura (x=300 / x=340).
    lvTag_ = new brls::Label();
    lvTag_->setText("Lv.");
    lvTag_->setFontSize(15);
    lvTag_->setTextColor(nvgRGBA(255, 255, 255, 215));
    lvTag_->setMarginLeft(55);
    lvTag_->setShrink(0.0f);
    head->addView(lvTag_);

    statLevel_ = new brls::Label();
    statLevel_->setFontSize(18);
    statLevel_->setTextColor(kWhite);
    statLevel_->setWidth(60);  // x 340-400; numero de ate 3 digitos cabe
    statLevel_->setMarginLeft(12);
    statLevel_->setSingleLine(true);
    statLevel_->setShrink(0.0f);
    head->addView(statLevel_);

    // Icones de TIPO no lugar do badge da captura (x~410): e o que o HOME
    // mostra ali (o cinza do Rattata e o icone Normal). Ate dois — especies
    // com dois tipos mostram os dois (pedido do dono, spec 099). Arte:
    // partywhale/pokemon-type-icons, rasterizada em romfs/ui/types.
    for (int i = 0; i < 2; ++i) {
      typeIcons_[i] = new brls::Image();
      typeIcons_[i]->setSize(brls::Size(20, 20));
      typeIcons_[i]->setMarginLeft(i == 0 ? 10 : 8);
      typeIcons_[i]->setScalingType(brls::ImageScalingType::FIT);
      typeIcons_[i]->setShrink(0.0f);
      typeIcons_[i]->setVisibility(brls::Visibility::INVISIBLE);
      head->addView(typeIcons_[i]);
    }

    // O apelido saiu da faixa (a referencia nao tem o campo): statName_ ja
    // mostra o apelido quando existe. O Label fica GONE para os caminhos que
    // ainda escrevem nele nao quebrarem.
    statSub_ = new brls::Label();
    statSub_->setVisibility(brls::Visibility::GONE);
    head->addView(statSub_);

    // Da metade para a direita TUDO e ABSOLUTE, nas posicoes da captura.
    // A primeira versao usava dois espacadores grow(1) para centrar o OT, e
    // o relayout do borealis apos um setText colapsava os grow — o primeiro
    // desenho saia certo e o redesenho empilhava tudo a esquerda (relato do
    // dono, 2026-08-17). Posicao fixa nao re-mede nada.

    // Treinador original com o icone de pessoa (nanovg) — icone em x=620.
    otTag_ = new PersonIcon();
    otTag_->setPositionType(brls::PositionType::ABSOLUTE);
    otTag_->setPositionLeft(610);
    otTag_->setPositionTop(5);
    otTag_->setSize(brls::Size(16, 16));
    head->addView(otTag_);

    statOt_ = new brls::Label();
    statOt_->setPositionType(brls::PositionType::ABSOLUTE);
    statOt_->setPositionLeft(634);
    statOt_->setPositionTop(3);
    statOt_->setFontSize(17);
    statOt_->setTextColor(kWhite);
    statOt_->setSingleLine(true);
    head->addView(statOt_);

    // Tag de idioma [ENG] (x=915, termina em ~950): borda fina, texto 12.
    langChip_ = new brls::Box(brls::Axis::ROW);
    langChip_->setPositionType(brls::PositionType::ABSOLUTE);
    langChip_->setPositionLeft(905);
    langChip_->setPositionTop(5);
    langChip_->setHeight(16);
    langChip_->setCornerRadius(3);
    langChip_->setBorderColor(kWhite);
    langChip_->setBorderThickness(1.5f);
    langChip_->setPadding(0, 5, 0, 5);
    langChip_->setAlignItems(brls::AlignItems::CENTER);
    statLang_ = new brls::Label();
    statLang_->setFontSize(12);
    statLang_->setTextColor(kWhite);
    langChip_->addView(statLang_);
    head->addView(langChip_);

    // Insignia do jogo (circulo em x=976): sigla no lugar do icone
    // (decisao do dono, spec 098).
    gameChip_ = new brls::Box(brls::Axis::ROW);
    gameChip_->setPositionType(brls::PositionType::ABSOLUTE);
    gameChip_->setPositionLeft(966);
    gameChip_->setPositionTop(4);
    gameChip_->setHeight(17);
    gameChip_->setCornerRadius(8.5f);
    gameChip_->setBorderColor(kWhite);
    gameChip_->setBorderThickness(1.5f);
    gameChip_->setPadding(0, 4, 0, 4);
    gameChip_->setAlignItems(brls::AlignItems::CENTER);
    gameChip_->setJustifyContent(brls::JustifyContent::CENTER);
    statGame_ = new brls::Label();
    statGame_->setFontSize(11);
    statGame_->setTextColor(kWhite);
    gameChip_->addView(statGame_);
    head->addView(gameChip_);

    // Marcadores (x ~1032-1240, passo ~42): espaco reservado, cheios e
    // esmaecidos como na captura. Nanovg — a fonte nao tem os glifos.
    statMarks_ = new MarkStrip();
    statMarks_->setPositionType(brls::PositionType::ABSOLUTE);
    statMarks_->setPositionRight(22);
    statMarks_->setPositionTop(3);
    statMarks_->setSize(brls::Size(250, 20));
    head->addView(statMarks_);

    // Mensagem de contexto (segurando, bloqueado, aviso de golpe) na ponta
    // direita, no lugar dos marcadores — SetInfo() esconde os marcadores
    // enquanto ha mensagem, senao os dois se sobreporiam.
    statInfo_ = new brls::Label();
    statInfo_->setPositionType(brls::PositionType::ABSOLUTE);
    statInfo_->setPositionRight(22);
    statInfo_->setPositionTop(4);
    statInfo_->setFontSize(16);
    statInfo_->setTextColor(kWhite);
    statInfo_->setSingleLine(true);
    head->addView(statInfo_);
    bar->addView(head);

    // --- Linhas 2 e 3: a grade de campos, sobre a faixa clara ---
    // Medidas da captura: tarjas em x=30 (col1), x=340/505/670 (stats);
    // golpes em x=845 e x=1050; linhas com passo de 36px.
    auto* grid = new brls::Box(brls::Axis::ROW);
    grid->setGrow(1.0f);
    grid->setAlignItems(brls::AlignItems::CENTER);
    grid->setPadding(0, 8, 0, 20);

    // Coluna 1: natureza e habilidade (tarja de 75, valor em x=120).
    auto* col1 = new brls::Box(brls::Axis::COLUMN);
    col1->setWidth(310);
    col1->addView(MakeStatRow("Nature", &statNature_, 75));
    col1->addView(MakeStatRow("Ability", &statAbility_, 75));
    grid->addView(col1);

    // Colunas 2-4: os seis stats, dois por coluna (tarja de 108).
    static const char* kStatLabels[3][2] = {
        {"HP", "Speed"}, {"Attack", "Sp. Atk"}, {"Defense", "Sp. Def"}};
    for (int c = 0; c < 3; ++c) {
      auto* col = new brls::Box(brls::Axis::COLUMN);
      col->setWidth(165);
      col->addView(MakeStatRow(kStatLabels[c][0], &statValues_[c * 2], 108));
      col->addView(MakeStatRow(kStatLabels[c][1], &statValues_[c * 2 + 1], 108));
      grid->addView(col);
    }

    // Colunas 5-6: os quatro golpes, sem rotulo (o proprio nome ja diz).
    // Cada golpe e uma ROW [nome, icone]: o icone amarelo do aviso de golpe
    // ausente (spec 104) mora ao lado do nome, na altura do texto.
    for (int c = 0; c < 2; ++c) {
      auto* col = new brls::Box(brls::Axis::COLUMN);
      col->setWidth(205);
      if (c == 0) col->setMarginLeft(10);  // primeiro golpe em x=845
      for (int r = 0; r < 2; ++r) {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setHeight(36);
        row->setAlignItems(brls::AlignItems::CENTER);

        auto* lbl = new brls::Label();
        lbl->setFontSize(18);
        lbl->setTextColor(kTextPrimary);
        lbl->setSingleLine(true);
        statMoves_[c * 2 + r] = lbl;
        row->addView(lbl);

        auto* warn = new MoveWarnIcon();
        warn->setMarginLeft(8);
        statMoveWarn_[c * 2 + r] = warn;
        row->addView(warn);

        col->addView(row);
      }
      grid->addView(col);
    }

    bar->addView(grid);

    // Logo do jogo da caixa ativa, translucida na ponta direita da grade,
    // como a logo do HOME na captura (x 1165-1270, centro y~645). ABSOLUTE
    // para nao disputar largura com as colunas; some no slot vazio.
    statLogo_ = new brls::Image();
    statLogo_->setPositionType(brls::PositionType::ABSOLUTE);
    statLogo_->setPositionTop(39);
    statLogo_->setPositionRight(8);
    statLogo_->setSize(brls::Size(105, 58));
    statLogo_->setScalingType(brls::ImageScalingType::FIT);
    statLogo_->setAlpha(0.5f);
    statLogo_->setVisibility(brls::Visibility::INVISIBLE);
    bar->addView(statLogo_);
    return bar;
  }

  // Faixa de identificacao: fundo teal + a pokebola vetorial na ponta
  // esquerda, como na referencia. So o desenho — o conteudo e dos Labels.
  // A vetorial e o FALLBACK: com a bola da captura conhecida, o sprite real
  // (romfs/ui/balls, spec 099) entra no lugar e ela nao desenha.
  class StatusHead : public brls::Box {
   public:
    bool vector_ball = true;

   private:
    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
      brls::Box::draw(vg, x, y, w, h, style, ctx);
      if (vector_ball) DrawPokeball(vg, x + 37.0f, y + h / 2, 10.0f);
    }
  };

  // Uma dupla rotulo + valor. O rotulo e azulado e fixo; o valor muda com o
  // cursor. Devolve a linha e guarda o Label do valor em `out`. A largura da
  // tarja vem da captura: 75 na coluna 1, 108 nas de stats.
  brls::Box* MakeStatRow(const char* label, brls::Label** out, float chip_w) {
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setHeight(36);
    row->setAlignItems(brls::AlignItems::CENTER);

    // Tarja clara atras do rotulo, como na captura do HOME (spec 098): fundo
    // #DBF1EE arredondado, texto azul centrado.
    auto* chip = new brls::Box(brls::Axis::ROW);
    chip->setWidth(chip_w);
    chip->setHeight(26);
    chip->setCornerRadius(5);
    chip->setBackgroundColor(kStatusChipBg);
    chip->setJustifyContent(brls::JustifyContent::CENTER);
    chip->setAlignItems(brls::AlignItems::CENTER);
    chip->setMarginRight(15);
    chip->setShrink(0.0f);

    auto* lbl = new brls::Label();
    lbl->setText(label);
    lbl->setFontSize(17);
    lbl->setTextColor(kStatLabel);
    chip->addView(lbl);
    row->addView(chip);

    auto* val = new brls::Label();
    val->setFontSize(18);
    val->setTextColor(kTextPrimary);
    val->setSingleLine(true);
    row->addView(val);
    *out = val;
    return row;
  }

  void RegisterActions(brls::View* root) {
    // ZL/ZR movem o FOCO para o outro painel. Alternar so uma flag deixaria o
    // foco no painel antigo, e o onFocusGained da celula reverteria a flag.
    auto swap_panel = [this](brls::View*) {
      // As marcas valem so para a caixa corrente (spec 021): levar a selecao
      // para outro painel deixaria marcas invisiveis no painel de tras. O
      // bloco JA levantado atravessa — mover do save para o NestBox e o uso
      // principal dele (spec 088).
      if (selPhase_ != SelPhase::kSegurando &&
          (session_.SelectedCount() > 0 || selPhase_ != SelPhase::kOcioso)) {
        ResetSelection();
        Refresh();
      }
      BoxPanel& target = activeLeft_ ? right_ : left_;
      if (target.cells.empty()) return true;
      const std::size_t i = std::min(cursor_, target.cells.size() - 1);
      NLOG_NAV("LSB trocar painel: %s -> %s", activeLeft_ ? "save" : "nestbox",
               activeLeft_ ? "nestbox" : "save");
      brls::Application::giveFocus(target.cells[i]);
      return true;
    };
    // ZL/ZR ciclam os MODOS de cursor, como no HOME (§5). A troca de painel
    // migrou para o clique do stick esquerdo — decisao do dono, registrada no
    // TD-01 da spec 031, revertendo o TD-02 da 019 e o TD-01 da 021.
    root->registerAction("Trocar painel", brls::BUTTON_LSB, swap_panel, false);

    auto cycle = [this](bool forward) {
      // Sair de um modo limpa o estado dele: carregar Pokemon na mao ou marcas
      // para outro modo confundiria mais do que ajudaria.
      if (session_.Holding()) session_.Cancel();
      ResetSelection();  // a area do modo Selecao morre junto (spec 088)
      const bx::CursorMode from = mode_;
      mode_ = forward ? bx::NextMode(mode_) : bx::PrevMode(mode_);
      NLOG_NAV("%s trocar modo: %s -> %s", forward ? "ZR" : "ZL",
               bx::CursorModeName(from), bx::CursorModeName(mode_));
      Refresh();
      return true;
    };
    root->registerAction(
        "Modo anterior", brls::BUTTON_LT,
        [cycle](brls::View*) { return cycle(false); }, false);
    root->registerAction(
        "Proximo modo", brls::BUTTON_RT,
        [cycle](brls::View*) { return cycle(true); }, false);
    // Stick direito (clique) abre a lista: A, X e Y ja tem uso, e A sera
    // "pegar Pokemon" na Fase 4 — usa-lo agora criaria retrabalho.
    root->registerAction(
        "Lista", brls::BUTTON_RSB,
        [this](brls::View*) {
          NLOG_NAV("RSB -> ListActivity");
          brls::Application::pushActivity(new ListActivity(save_));
          return true;
        },
        false);
    // Restaurar backup fica no + porque e destino, nao atalho: um toque
    // acidental num botao de acao nao pode reverter horas de jogo (spec 037).
    // So aparece com um save de arquivo aberto — restaurar exige saber em qual
    // caminho gravar, e isso so o SaveSource sabe.
    root->registerAction(
        "Restaurar backup", brls::BUTTON_START,
        [this](brls::View*) {
          NLOG_NAV("+ -> restaurar backup");
          OpenRestore();
          return true;
        },
        false);
    root->registerAction(
        "Enciclopedia", brls::BUTTON_X,
        [this](brls::View*) {
          NLOG_NAV("X -> DexActivity (enciclopedia)");
          // As duas fontes e a sessao: o que esta no NestBox tambem conta, e a
          // movimentacao pendente e respeitada (spec 026).
          brls::Application::pushActivity(
              new DexActivity(save_, nest_, &session_));
          return true;
        },
        false);
    root->registerAction(
        "Caixa anterior", brls::BUTTON_LB,
        [this](brls::View*) {
          BoxPanel& p = Active();
          // Visao geral: L/R viram a PAGINA de caixas (spec 105).
          if (p.overview) {
            const std::size_t pg = p.OverviewPages();
            p.ov_page = (p.ov_page + pg - 1) % pg;
            NLOG_NAV("L pagina da visao geral: %zu/%zu", p.ov_page + 1, pg);
            Refresh();
            return true;
          }
          // Marcas e area valem so na caixa corrente (specs 021 e 088) — mas
          // um bloco JA levantado atravessa: carregar Pokemon de uma caixa
          // para outra e justamente para o que ele serve.
          if (selPhase_ != SelPhase::kSegurando) ResetSelection();
          const std::size_t n = p.source->BoxCount();
          const std::size_t from = p.box;
          p.box = (p.box + n - 1) % n;
          NLOG_NAV("L caixa: %zu -> %zu (painel %s, de %zu)", from, p.box,
                   activeLeft_ ? "esq" : "dir", n);
          Refresh();
          return true;
        },
        false);
    root->registerAction(
        "Proxima caixa", brls::BUTTON_RB,
        [this](brls::View*) {
          BoxPanel& p = Active();
          if (p.overview) {
            const std::size_t pg = p.OverviewPages();
            p.ov_page = (p.ov_page + 1) % pg;
            NLOG_NAV("R pagina da visao geral: %zu/%zu", p.ov_page + 1, pg);
            Refresh();
            return true;
          }
          if (selPhase_ != SelPhase::kSegurando) ResetSelection();
          const std::size_t from = p.box;
          p.box = (p.box + 1) % p.source->BoxCount();
          NLOG_NAV("R caixa: %zu -> %zu (painel %s, de %zu)", from, p.box,
                   activeLeft_ ? "esq" : "dir", p.source->BoxCount());
          Refresh();
          return true;
        },
        false);
    // A visao geral (spec 105) NAO tem atalho de botao: o acesso e o toque
    // na pilula do rodape do painel, como no HOME — decisao do dono
    // (2026-08-17). O − continua reservado ao Ajuda.
    // As setas NAO sao registradas: o borealis ja move o foco entre as celulas
    // focaveis (Box::getNextFocus). Registrar aqui competiria com ele — foi o
    // que travou o cursor na primeira tentativa.

    // A: pega e solta. A logica mora em box_move.h e e coberta por
    // tests/test_box_move.cpp — aqui so o gatilho.
    //
    // `swap` e o atalho do Y (spec 100): forca a semantica de TROCA e pula o
    // menu de contexto do modo Mover, sem alterar `mode_`. O A passa false e
    // se comporta como sempre.
    auto pick_or_drop = [this](bool swap) {
          BoxPanel& p = Active();
          // Visao geral (spec 105): A ABRE a caixa apontada — o painel volta
          // a vista normal ja nela. Nenhum outro gesto atravessa.
          if (p.overview) {
            const std::size_t b = p.ov_page * kSlotsPerBox + cursor_;
            if (b < p.source->BoxCount()) {
              NLOG_NAV("visao geral: A abriu a caixa %zu", b);
              p.box = b;
              p.overview = false;
              Refresh();
            }
            return true;
          }
          const bx::SlotRef ref{p.source_id, p.box, cursor_};
          const g3::BoxPokemon current = p.Effective(cursor_);

          // O que A faz depende do modo (spec 031). No Selecao, tambem da
          // FASE (spec 088): ancorar -> fechar a area -> soltar o bloco.
          if (mode_ == bx::CursorMode::kSelect) {
            if (selPhase_ == SelPhase::kOcioso) {
              // Ancora. Slot vazio nao ancora nada: o gesto comeca num
              // Pokemon, como no HOME.
              if (current.empty()) {
                NLOG_NAV("A em slot vazio no modo Selecao (caixa %zu slot %zu)",
                         p.box, cursor_);
                return true;
              }
              const std::string why = p.source->BlockedReason(p.box, cursor_);
              if (!why.empty()) {
                // Mesmo bloqueio do modo Mover (spec 082): marcar para o bloco
                // e o primeiro passo de uma transferencia.
                NLOG_ACT("BLOQUEADO ancorar %s (caixa %zu slot %zu): %s",
                         DisplaySpecies(current).c_str(), p.box, cursor_,
                         why.c_str());
                NoticeDialog("Este Pokemon nao pode ser transferido.\n" + why,
                             "Entendi");
                return true;
              }
              selAnchor_ = cursor_;
              selPhase_ = SelPhase::kAncorando;
              NLOG_ACT("ANCOROU a selecao em %s (caixa %zu slot %zu)",
                       DisplaySpecies(current).c_str(), p.box, cursor_);
              Refresh();
              return true;
            }

            if (selPhase_ == SelPhase::kAncorando) {
              FreezeSelectionShape();
              if (selShape_.empty()) {
                NLOG_NAV("area sem nenhum Pokemon — nada a pegar");
                return true;
              }
              // Os portoes de formato (spec 086) conferidos sobre o bloco
              // INTEIRO ANTES de levanta-lo: descobrir no destino que um deles
              // nao pode viajar jogaria fora o gesto todo.
              for (const auto& [from, _] : selShape_) {
                if (!p.source->BlockedReason(from.box, from.slot).empty()) {
                  NLOG_ACT("BLOQUEADO pegar bloco: %s reprovado pelo "
                           "verificador (caixa %zu slot %zu)",
                           DisplaySpecies(EffectiveOf(from)).c_str(), from.box,
                           from.slot);
                  NoticeDialog(
                      "Ha um Pokemon reprovado dentro da area.\n"
                      "Ele nao pode ser transferido.",
                      "Entendi");
                  return true;
                }
              }
              selPhase_ = SelPhase::kSegurando;
              NLOG_ACT("PEGOU BLOCO de %zu Pokemon (caixa %zu, canto slot %zu)",
                       selShape_.size(), p.box, selTopLeft_);
              // O cursor assume o CANTO da area, nao o slot onde a pintura
              // terminou: e ele que a formacao carrega, entao o que se ve sob
              // o cursor tem de ser o que vai pousar ali (rodada 4).
              if (selTopLeft_ < p.cells.size()) {
                brls::Application::giveFocus(p.cells[selTopLeft_]);
              }
              Refresh();
              return true;
            }

            // kSegurando: solta o bloco com o canto no cursor.
            const bool to_nest =
                dynamic_cast<NestBoxSource*>(p.source) != nullptr;
            const bool to_modern =
                dynamic_cast<ModernSaveSource*>(p.source) != nullptr;
            for (const auto& [from, _] : selShape_) {
              const g3::BoxPokemon m = EffectiveOf(from);
              const bool is_modern = m.modern != nullptr;
              if ((is_modern && to_nest) || (!is_modern && to_modern)) {
                NLOG_ACT("BLOQUEADO soltar bloco: %s cruza formato "
                         "(moderno=%d, destino %s)",
                         DisplaySpecies(m).c_str(), is_modern ? 1 : 0,
                         to_nest ? "nestbox" : "save moderno");
                NoticeDialog(to_nest
                                 ? "Guardar Pokemon de Switch no NestBox "
                                   "ainda nao e suportado."
                                 : "Transferir Pokemon de GBA para um save "
                                   "de Switch ainda nao e suportado.",
                             "Entendi");
                return true;
              }
            }
            // Ancorado no cursor CRU, o mesmo do desenho (rodada 9): o bloco
            // cai onde a tela o mostra. Se parte dele esta vazando para fora
            // da grade, o MoveBlock recusa — a mesma regra do tudo-ou-nada.
            const std::size_t moved = session_.MoveBlock(
                p.source_id, p.box, cursor_ / kCols, cursor_ % kCols, kRows,
                kCols, p.source->CanAccept(), selShape_,
                [this](const bx::SlotRef& r) { return EffectiveOf(r); });
            if (moved == 0) {
              // Tudo-ou-nada (TD-01 da spec 088): recusou inteiro, sem mover
              // nada. O aviso diz POR QUE, senao o A pareceria travado.
              NLOG_ACT("RECUSOU soltar bloco de %zu -> caixa %zu slot %zu: "
                       "nao cabe (ocupado ou fora da borda)",
                       selShape_.size(), p.box, cursor_);
              NoticeDialog(
                  "O bloco nao cabe aqui.\n"
                  "Ele precisa de espaco livre com a mesma forma.",
                  "Entendi");
              return true;
            }
            NLOG_ACT("SOLTOU BLOCO de %zu -> painel %s caixa %zu slot %zu",
                     moved, p.source_id == kNestId ? "nestbox" : "save", p.box,
                     cursor_);
            ResetSelection();
            Refresh();
            return true;
          }

          if (session_.Holding()) {
            // A especie precisa existir no jogo de destino (spec 034). O HOME
            // bloqueia o movimento, nao so avisa (§2 e §7 da pesquisa).
            // A grade ja esta marcada em vermelho e o rodape ja diz o motivo
            // desde que o Pokemon foi pego — nao precisa de aviso temporario.
            if (!p.FitsInPanel(session_.Held())) {
              NLOG_ACT("BLOQUEADO soltar %s: nao existe em %s",
                       DisplaySpecies(session_.Held()).c_str(),
                       cp::GameName(p.source->GameId()));
              return true;
            }
            // Verificador (spec 082, gatilho movido na spec 105): a reprovacao
            // bloqueia a TRANSFERENCIA. Reorganizar dentro da propria fonte e
            // livre; o erro aparece so ao soltar na OUTRA fonte. O julgamento
            // e da fonte de ORIGEM sobre o slot de onde o Pokemon saiu — o
            // overlay esvaziou o slot, mas a fonte continua respondendo.
            const bx::SlotRef& from = session_.HeldFrom();
            if (p.source_id != from.source) {
              BoxSource* origem = from.source == kNestId ? nest_ : save_;
              const std::string why =
                  origem ? origem->BlockedReason(from.box, from.slot) : "";
              if (!why.empty()) {
                NLOG_ACT("BLOQUEADO transferir %s (%s caixa %zu slot %zu): %s",
                         DisplaySpecies(session_.Held()).c_str(),
                         from.source == kNestId ? "nestbox" : "save", from.box,
                         from.slot, why.c_str());
                NoticeDialog("Este Pokemon nao pode ser transferido.\n" + why,
                             "Entendi");
                return true;
              }
            }
            // O mesmo gate na direcao oposta: numa TROCA cruzando fontes, o
            // ocupante reprovado do DESTINO viajaria para a origem — e
            // transferencia do mesmo jeito, so que do outro lado.
            if (p.source_id != from.source && !current.empty()) {
              const std::string why =
                  p.source->BlockedReason(p.box, cursor_);
              if (!why.empty()) {
                NLOG_ACT("BLOQUEADO trocar com %s reprovado (caixa %zu "
                         "slot %zu): %s",
                         DisplaySpecies(current).c_str(), p.box, cursor_,
                         why.c_str());
                NoticeDialog(
                    "O Pokemon deste espaco nao pode ser transferido.\n" + why,
                    "Entendi");
                return true;
              }
            }
            // Portoes de formato (spec 086): os dois cruzamentos que gravariam
            // dado mutilado. O NestBox armazena slots gen3 de 80 bytes — um
            // Pokemon moderno depositado ali perderia tudo que o raw nao
            // carrega. E um Pokemon gen3/NestBox num save moderno exigiria a
            // transferencia para cima (conversao de formato), que e spec
            // propria. Recusar AQUI, na hora do gesto, e o que evita o
            // trabalho perdido de descobrir so no commit.
            const bool held_modern = session_.Held().modern != nullptr;
            if (held_modern && dynamic_cast<NestBoxSource*>(p.source)) {
              NLOG_ACT("BLOQUEADO soltar %s no NestBox: Pokemon moderno, "
                       "deposito exige formato de slot proprio (spec futura)",
                       DisplaySpecies(session_.Held()).c_str());
              NoticeDialog(
                  "Guardar Pokemon de Switch no NestBox ainda nao e "
                  "suportado.\nEle pode ser movido dentro do proprio save.",
                  "Entendi");
              return true;
            }
            if (!held_modern && dynamic_cast<ModernSaveSource*>(p.source)) {
              NLOG_ACT("BLOQUEADO soltar %s no save moderno: origem gen3/"
                       "NestBox, transferencia para cima e spec propria",
                       DisplaySpecies(session_.Held()).c_str());
              NoticeDialog(
                  "Transferir Pokemon de GBA para um save de Switch ainda "
                  "nao e suportado.",
                  "Entendi");
              return true;
            }
            // Modo Mover recusa soltar sobre ocupado; e o que o diferencia do
            // modo Trocar (TD-02 da spec 031).
            const bool allow_swap = swap || mode_ == bx::CursorMode::kSwap;
            const std::string held = DisplaySpecies(session_.Held());
            // O RETORNO do Drop e quem diz se funcionou. Ate a spec 087 isto
            // olhava `Holding()`, que continuava true numa troca bem-sucedida
            // (o ocupante ia para a mao) — e o log gritava "RECUSOU" em cima
            // de uma troca que tinha dado certo, mandando o diagnostico para
            // o lado errado. Log que mente e pior que log nenhum.
            const bool dropped =
                session_.Drop(ref, current, p.source->CanAccept(), allow_swap);
            NLOG_ACT("%s %s -> painel %s caixa %zu slot %zu (modo %s, alvo %s)",
                     dropped ? "SOLTOU" : "RECUSOU soltar",
                     held.c_str(),
                     p.source_id == kNestId ? "nestbox" : "save", p.box, cursor_,
                     bx::CursorModeName(mode_),
                     current.empty() ? "vazio" : DisplaySpecies(current).c_str());
          } else {
            // Pokemon reprovado pelo verificador PODE ser pego (spec 105): o
            // bloqueio e da transferencia, nunca da leitura nem do gesto
            // (spec 082). A recusa que morava aqui impedia ate o menu de
            // contexto e os detalhes — ela desceu para o soltar, onde a
            // transferencia de fato acontece.
            if (current.empty()) {
              NLOG_NAV("A em slot vazio (caixa %zu slot %zu)", p.box, cursor_);
              session_.Pick(ref, current);  // no-op: Pick recusa slot vazio
              Refresh();
              return true;
            }

            // Modo MOVER: o A abre o MENU DE CONTEXTO em vez de pegar direto
            // (spec 095). E o que diferencia o Mover do Trocar — la o A pega
            // na hora, e foi assim que o dono validou.
            //
            // So dois itens por enquanto: os outros quatro da folha (resumo,
            // marcacoes, visual, liberar) ficam de fora ate terem para onde
            // ir. Item que nao faz nada e pior que item ausente.
            if (!swap && mode_ == bx::CursorMode::kMove) {
              NLOG_NAV("A -> menu de contexto de %s (caixa %zu slot %zu)",
                       DisplaySpecies(current).c_str(), p.box, cursor_);
              const std::string nome = DisplaySpecies(current);
              BoxSource* src = p.source;
              const std::size_t caixa = p.box, slot = cursor_;
              ShowContextMenu(
                  p.cells[cursor_]->getFrame(),
                  {{"Mover",
                    [this, ref, current, nome] {
                      NLOG_ACT("menu: PEGOU %s", nome.c_str());
                      session_.Pick(ref, current);
                      Refresh();
                    }},
                   // Check Summary do HOME (spec 103).
                   {"Ver detalhes",
                    [current, src, caixa, slot] {
                      NLOG_NAV("menu: summary de %s",
                               DisplaySpecies(current).c_str());
                      ShowSummary(current, src, caixa, slot);
                    }},
                   {"Sair", [] { NLOG_NAV("menu: fechou sem acao"); }}});
              return true;
            }

            NLOG_ACT("PEGOU %s \"%s\" do painel %s caixa %zu slot %zu",
                     DisplaySpecies(current).c_str(), current.nickname.c_str(),
                     p.source_id == kNestId ? "nestbox" : "save", p.box,
                     cursor_);
            session_.Pick(ref, current);
          }
          Refresh();
          return true;
    };
    root->registerAction(
        "Mover", brls::BUTTON_A,
        [pick_or_drop](brls::View*) { return pick_or_drop(false); }, false);

    // Y: atalho de troca (spec 100). Faz o mesmo gesto do A no modo Trocar —
    // levanta o Pokemon e solta trocando com o ocupante — mas NAO muda o modo
    // do cursor: terminado o gesto, a barra de modos continua onde estava.
    // Substituiu o "Detalhes" (TD-01 da spec 100).
    root->registerAction(
        "Trocar", brls::BUTTON_Y,
        [this, pick_or_drop](brls::View*) {
          // No modo Selecao o A tem gesto proprio de tres fases (spec 088); o
          // atalho nao o atravessa.
          if (mode_ == bx::CursorMode::kSelect) {
            NLOG_NAV("Y ignorado: modo Selecao tem gesto proprio");
            return true;
          }
          return pick_or_drop(true);
        },
        false);

    // + renomeia a caixa aberta do NestBox (spec 030). So o NestBox: renomear
    // caixa do save exigiria escrever no save, que e a v3 do produto.
    root->registerAction(
        "Renomear", brls::BUTTON_START,
        [this](brls::View*) {
          BoxPanel& p = Active();
          auto* nest = dynamic_cast<NestBoxSource*>(p.source);
          if (!nest) return true;  // painel do save: nada a fazer

          const std::size_t box = p.box;
          brls::Application::getPlatform()->getImeManager()->openForText(
              [this, nest, box](const std::string& text) {
                NLOG_ACT("RENOMEOU caixa %zu do NestBox: \"%s\" -> \"%s\"", box,
                         nest->data().BoxName(box).c_str(), text.c_str());
                nest->RenameBox(box, text);
                // Renomear tambem e alteracao a gravar. Sem isto, quem so
                // renomeasse sairia com Dirty() falso e perderia o nome em
                // silencio — a sessao so sabe de movimentacao (spec 030).
                renamed_ = true;
                Refresh();
              },
              "Nome da caixa", "", kBoxNameMax, nest->data().BoxName(box));
          return true;
        },
        false);


    // B com a mao cheia cancela o movimento em vez de sair — sair segurando
    // perderia o Pokemon de vista sem explicacao.
    root->registerAction(
        "Voltar", brls::BUTTON_B,
        [this](brls::View*) {
          // B sai da visao geral sem trocar de caixa (spec 105) — antes de
          // qualquer outro significado do botao.
          if (Active().overview) {
            NLOG_NAV("B saiu da visao geral");
            Active().overview = false;
            Refresh();
            return true;
          }
          // B cancela a selecao em qualquer fase, antes de tudo (spec 088):
          // enquanto se pinta a area ou se carrega o bloco, B desfaz o gesto
          // em vez de sair da tela.
          if (mode_ == bx::CursorMode::kSelect &&
              selPhase_ != SelPhase::kOcioso) {
            NLOG_ACT("B CANCELOU a selecao (fase %s)",
                     selPhase_ == SelPhase::kAncorando ? "ancorando"
                                                       : "segurando");
            ResetSelection();
            Refresh();
            return true;
          }
          if (session_.Holding()) {
            NLOG_ACT("B CANCELOU o movimento de %s",
                     DisplaySpecies(session_.Held()).c_str());
            session_.Cancel();
            Refresh();
            return true;
          }
          // Movimentos pendentes morrem ao sair — nada e gravado em disco
          // (spec 019). Sair em silencio perderia o trabalho sem aviso, entao
          // pergunta. Sem pendencia, sai direto: perguntar a toa e atrito.
          if (session_.Dirty() || renamed_) {
            NLOG_NAV("B com pendencia: %zu alteracoes, renomeado=%d — perguntou",
                     session_.changes().size(), renamed_ ? 1 : 0);
            // Tres opcoes, na ordem do menos destrutivo para o mais: o gesto
            // apressado (primeiro botao) continua sendo o inofensivo.
            //
            // "Salvar" grava so o NestBox — o painel do save nao e escrito, e
            // por isso o texto diz o que sobrevive e o que nao (spec 028).
            // O texto diz a verdade da spec 086: salvar grava OS DOIS lados
            // (NestBox e save aberto), com backup automatico do save antes.
            MessageBox::Show(
                "Salvar grava o NestBox e o save aberto no cartao.\n"
                "Um backup do save e criado antes de qualquer escrita.",
                {{"Continuar aqui", nullptr, kGlyphB},
                 {"Descartar e sair",
                  [] {
                    NLOG_ACT("DESCARTOU as alteracoes e saiu da caixa");
                    brls::Application::popActivity();
                  }, ""},
                 // O A fica no botao de acao — os tres nao cabem no par A/B.
                 {"Salvar e sair", [this] {
                    if (CommitNestBox()) {
                      brls::Application::popActivity();
                      return;
                    }
                    // Falhou: NAO sai. Sair aqui daria a entender que salvou, e
                    // o usuario perderia o trabalho achando que estava
                    // guardado.
                    NoticeDialog(
                        "Nao foi possivel salvar.\n"
                        "O save NAO foi alterado — o backup falhou ou o cartao "
                        "recusou a escrita.",
                        "Entendi");
                  }, kGlyphA}});
            return true;
          }
          // B volta ao menu, nao fecha o app. Fechar por botao ja causou saida
          // acidental — para sair o usuario usa o HOME, como num jogo.
          NLOG_NAV("B <- sai da BoxActivity (sem pendencia)");
          brls::Application::popActivity();
          return true;
        },
        false);
  }

  BoxPanel& Active() { return activeLeft_ ? left_ : right_; }

  // Entra/sai da visao geral das caixas (spec 105) num painel. Durante um
  // gesto (mao cheia ou selecao) o toque e ignorado — mover Pokemon sobre
  // uma caixa fica para spec propria.
  void ToggleOverview(BoxPanel& p) {
    if (session_.Holding() || selPhase_ != SelPhase::kOcioso) {
      NLOG_NAV("Espacos ignorado: gesto em curso");
      return;
    }
    p.overview = !p.overview;
    if (p.overview) p.ov_page = p.box / kSlotsPerBox;
    NLOG_NAV("Espacos: visao geral %s (painel %s)", p.overview ? "ON" : "OFF",
             &p == &left_ ? "esq" : "dir");
    Refresh();
  }

  // --- Area do modo Selecao (spec 088) --------------------------------------

  // O retangulo ancora<->cursor, em (linha, coluna). Existe so enquanto a
  // fase e kAncorando; nas outras devolve um retangulo vazio.
  struct SelRect {
    std::size_t r0 = 0, c0 = 0, r1 = 0, c1 = 0;
    bool valid = false;
  };

  SelRect SelectionRect() const {
    SelRect out;
    if (selPhase_ != SelPhase::kAncorando) return out;
    const std::size_t ar = selAnchor_ / kCols, ac = selAnchor_ % kCols;
    const std::size_t cr = cursor_ / kCols, cc = cursor_ % kCols;
    out.r0 = std::min(ar, cr);
    out.r1 = std::max(ar, cr);
    out.c0 = std::min(ac, cc);
    out.c1 = std::max(ac, cc);
    out.valid = true;
    return out;
  }

  // Congela a forma do retangulo: os slots OCUPADOS, com o deslocamento
  // relativo ao canto superior esquerdo. Os vazios ficam de fora da carga mas
  // continuam existindo como buraco — e o offset que preserva a forma.
  void FreezeSelectionShape() {
    const SelRect rc = SelectionRect();
    const BoxPanel& p = Active();
    selShape_.clear();
    if (!rc.valid) return;
    selTopLeft_ = rc.r0 * kCols + rc.c0;
    for (std::size_t r = rc.r0; r <= rc.r1; ++r) {
      for (std::size_t c = rc.c0; c <= rc.c1; ++c) {
        const std::size_t slot = r * kCols + c;
        if (p.Effective(slot).empty()) continue;
        selShape_.push_back({{p.source_id, p.box, slot},
                             {static_cast<int>(r - rc.r0),
                              static_cast<int>(c - rc.c0)}});
      }
    }
  }

  // Pinta a area nas celulas dos DOIS paineis (spec 088). O painel inativo
  // sempre limpa: a area vive numa caixa so, e deixar rastro no outro painel
  // sugeriria uma selecao que nao existe.
  //
  // Enquanto ancora, o retangulo segue o cursor. Com o bloco na mao
  // (kSegurando), a formacao inteira acompanha o cursor e as origens ficam
  // com o fantasma esmaecido.
  void RefreshSelectionArea() {
    const SelRect rc = SelectionRect();
    for (BoxPanel* panel : {&left_, &right_}) {
      const bool ativo = (panel == &Active());
      for (std::size_t i = 0; i < panel->cells.size(); ++i) {
        bool in_area = false, tl = false, tr = false, bl = false, br = false;
        if (ativo && rc.valid) {
          const std::size_t r = i / kCols, c = i % kCols;
          in_area = r >= rc.r0 && r <= rc.r1 && c >= rc.c0 && c <= rc.c1;
          tl = in_area && r == rc.r0 && c == rc.c0;
          tr = in_area && r == rc.r0 && c == rc.c1;
          bl = in_area && r == rc.r1 && c == rc.c0;
          br = in_area && r == rc.r1 && c == rc.c1;
        }
        panel->cells[i]->SetAreaCell(in_area, tl, tr, bl, br);
      }
    }

    // Bloco na mao: a formacao ACOMPANHA O CURSOR (rodada 3), nao fica parada
    // nas origens. O cursor carrega o Pokemon do canto SUPERIOR ESQUERDO da
    // area, e os outros o seguem no mesmo arranjo relativo.
    //
    // O canto TL, e nao o slot onde o A ancorou: e o mesmo referencial que o
    // MoveBlock usa para pousar o bloco, entao o que se ve e exatamente onde
    // ele cai. Ancorar de baixo para cima faz o cursor carregar outro que nao
    // o primeiro clicado — divergencia aceita para o desenho nao mentir sobre
    // o destino.
    //
    // Nas ORIGENS fica o fantasma esmaecido, o mesmo do Pokemon unico
    // (SetOrigin, spec 085): e o que mostra de onde o bloco saiu.
    // Limpa o rastro da formacao anterior ANTES de desenhar a nova. Mover o
    // cursor chama so OnCursorMoved, nao Refresh — sem isto cada passo
    // deixaria uma copia levantada para tras, espalhando o bloco pela caixa.
    for (SlotCell* cell : blockCells_) {
      cell->ClearHeld();
      cell->ClearOrigin();
    }
    blockCells_.clear();
    overflow_.clear();  // sem bloco na mao nao ha nada vazando

    if (selPhase_ == SelPhase::kSegurando) {
      BoxPanel& act = Active();

      // Cursor CRU, sem grampo (rodada 9). O bloco VAZA da grade: quem passa
      // da ultima coluna e desenhado no espaco entre os dois paineis — o
      // "slot imaginario" que o dono descreveu. O cursor continua parando na
      // borda (o L/R e que vira pagina, rodada 8); o que atravessa e so o
      // desenho do bloco.
      const long cur_r = static_cast<long>(cursor_ / kCols);
      const long cur_c = static_cast<long>(cursor_ % kCols);

      // DUAS passadas, e nao uma intercalada: quando a formacao ainda esta
      // por cima das proprias origens, o levantado (opaco) e o fantasma
      // (esmaecido) caem na mesma celula. Desenhando todos os fantasmas
      // primeiro, o levantado que vier depois fica por cima — que e a ordem
      // certa. Intercalado, um SetOrigin posterior apagava o levantado de
      // outro e a origem sumia da tela (rodada 4).
      for (const auto& [from, delta] : selShape_) {
        (void)delta;
        for (BoxPanel* panel : {&left_, &right_}) {
          if (panel->source_id != from.source || panel->box != from.box)
            continue;
          if (from.slot < panel->cells.size()) {
            panel->cells[from.slot]->SetOrigin(EffectiveOf(from));
            blockCells_.push_back(panel->cells[from.slot]);
          }
        }
      }

      // O Pokemon levantado, na posicao que a formacao ocupa AGORA: cursor +
      // o deslocamento dele dentro da forma.
      //
      // Quem cai FORA da grade nao some: vai para `overflow_`, desenhado pelo
      // BoxActivity por cima de tudo, no espaco entre os paineis. E o "slot
      // imaginario" — existe so enquanto se carrega um bloco.
      for (const auto& [from, delta] : selShape_) {
        const long r = cur_r + delta.first;
        const long c = cur_c + delta.second;
        if (r >= 0 && c >= 0 && r < kRows && c < kCols) {
          const std::size_t slot =
              static_cast<std::size_t>(r) * kCols + static_cast<std::size_t>(c);
          if (slot < act.cells.size()) {
            act.cells[slot]->SetHeld(EffectiveOf(from));
            blockCells_.push_back(act.cells[slot]);
          }
          continue;
        }
        // Fora da grade: guarda a posicao RELATIVA a celula (0,0) do painel,
        // em passo de slot. O desenho converte para pixel.
        const g3::BoxPokemon mon = EffectiveOf(from);
        if (!mon.empty()) overflow_.push_back({r, c, SpritePath(mon)});
      }
    }
  }

  // Converte o overflow (posicao em passo de slot) para pixel de tela.
  //
  // A referencia e a celula (0,0) do painel ATIVO: o passo entre slots e o
  // tamanho da celula mais o vao, os mesmos numeros que o layout usa. Assim o
  // que vaza fica exatamente na continuacao da grade, e nao num lugar
  // calculado por fora.
  // Nao e const: getX()/getY() do borealis nao sao const.
  std::vector<BlockOverflow::Item> OverflowItems() {
    std::vector<BlockOverflow::Item> out;
    if (overflow_.empty()) return out;
    BoxPanel& act = activeLeft_ ? left_ : right_;
    if (act.cells.empty()) return out;

    SlotCell* origin = act.cells[0];
    const float step_x = kSlotW + kSlotGapX;
    const float step_y = kSlotH + kSlotGapY;
    const float x0 = origin->getX() + (kSlotW - kSpriteSize) / 2;
    const float y0 = origin->getY() + (kSlotH - kSpriteSize) / 2;

    out.reserve(overflow_.size());
    for (const OverflowMon& m : overflow_) {
      out.push_back({m.sprite, x0 + m.col * step_x, y0 + m.row * step_y,
                     static_cast<float>(kSpriteSize)});
    }
    return out;
  }

  // Volta ao estado ocioso do modo Selecao. Um lugar so, porque B, trocar de
  // modo, trocar de caixa e o commit precisam todos do mesmo encerramento.
  void ResetSelection() {
    selPhase_ = SelPhase::kOcioso;
    selShape_.clear();
    session_.ClearSelection();
  }

  // Conteudo efetivo de qualquer slot, dos dois paineis. O bloco da
  // multissselecao move entre fontes, entao precisa resolver o SlotRef pelo id
  // em vez de assumir o painel ativo.
  g3::BoxPokemon EffectiveOf(const bx::SlotRef& ref) const {
    const BoxSource* src = ref.source == kNestId ? nest_ : save_;
    return session_.Get(ref, src->At(ref.box, ref.slot));
  }

  // Aplica ao NestBox o que a sessao mudou e grava no cartao (spec 028).
  //
  // So o lado do NestBox e gravado: escrever no save do jogo e a v3, com
  // backup obrigatorio, e continua fora de escopo. As alteracoes no painel do
  // save ficam onde sempre estiveram — no overlay, descartadas ao sair.
  bool CommitNestBox() {
    auto* nest = dynamic_cast<NestBoxSource*>(nest_);
    if (!nest) {
      NLOG_ACT("FALHA salvar: painel do NestBox nao e um NestBoxSource");
      return false;
    }
    NLOG_ACT("SALVAR: %zu alteracoes pendentes, renomeado=%d",
             session_.changes().size(), renamed_ ? 1 : 0);

    // Separa as alteracoes por lado ANTES de escrever qualquer coisa: se o
    // backup falhar, nada pode ter sido gravado.
    std::map<std::size_t, g3::BoxPokemon> save_changes;
    std::vector<vw::BoxChange> modern_changes;
    for (const auto& [ref, mon] : session_.changes()) {
      if (ref.source == kNestId) continue;
      save_changes[ref.box * g3::kSlotsPerBox + ref.slot] = mon;
      modern_changes.push_back({ref.box, ref.slot, mon});
    }

    // BACKUP OBRIGATORIO antes de tocar no save (spec 032). Falhou, nao
    // escreve — nao existe caminho que grave sem rede. Vale para as DUAS
    // fontes gravaveis: gen3 (spec 033) e moderna (spec 086).
    auto* save = dynamic_cast<SaveSource*>(save_);
    auto* modern = dynamic_cast<ModernSaveSource*>(save_);
    if (!save_changes.empty()) {
      if (save) {
        NLOG_ACT("  %zu alteracoes no save \"%s\" — backup obrigatorio primeiro",
                 save_changes.size(), save->path().c_str());
        if (!BackupSave(save->path(), save->bytes())) {
          NLOG_ACT("FALHA salvar: backup recusou. O save NAO foi tocado.");
          return false;
        }
        if (!save->WriteChanges(save_changes)) {
          NLOG_ACT("FALHA salvar: WriteChanges recusou em \"%s\" (%zu slots)",
                   save->path().c_str(), save_changes.size());
          return false;
        }
        NLOG_ACT("  save gravado: \"%s\"", save->path().c_str());
      } else if (modern) {
        NLOG_ACT("  %zu alteracoes no save moderno \"%s\" — backup primeiro",
                 modern_changes.size(), modern->backup_label().c_str());
        if (!BackupSave(modern->backup_label(), modern->bytes())) {
          NLOG_ACT("FALHA salvar: backup recusou. O save NAO foi tocado.");
          return false;
        }
        if (!modern->WriteChanges(modern_changes)) {
          NLOG_ACT("FALHA salvar: WriteChanges moderno recusou (%zu slots)",
                   modern_changes.size());
          return false;
        }
        NLOG_ACT("  save moderno gravado: \"%s\"",
                 modern->backup_label().c_str());
      } else {
        NLOG_ACT("FALHA salvar: %zu alteracoes no painel do save, mas a fonte "
                 "nao e gravavel",
                 save_changes.size());
        return false;
      }
    }

    for (const auto& [ref, mon] : session_.changes()) {
      if (ref.source != kNestId) continue;
      if (mon.empty()) {
        nest->Clear(ref.box, ref.slot);
      } else {
        nest->Put(ref.box, ref.slot, mon);
      }
    }
    if (!SaveNestBox(nest->data())) {
      NLOG_ACT("FALHA salvar: SaveNestBox recusou");
      return false;
    }
    NLOG_ACT("SALVO com sucesso");

    // Gravou: a sessao deixa de ter pendencia. Sem isto o overlay continuaria
    // por cima do save ja atualizado, mostrando o estado duas vezes.
    session_.Discard();
    renamed_ = false;
    return true;
  }

  // Redesenha as duas grades. Chamado ao trocar de caixa, nao a cada
  // movimento do cursor — o destaque do foco e responsabilidade da celula.
  void Refresh() {
    RefreshModePills();
    left_.Refresh();
    right_.Refresh();
    // Refresh repos os sprites normais em todas as celulas, inclusive a do
    // cursor: o "fantasma" anterior morreu junto. Zera o rastro para que
    // OnCursorMoved nao tente limpar uma celula ja limpa.
    lastHeldPanel_ = nullptr;
    // O ghost de origem (spec 085) e um overlay que Refresh() nao toca — se
    // nao zerar aqui, uma celula reciclada herdaria o ghost de outro slot.
    heldOriginCell_ = nullptr;
    OnCursorMoved();
  }

  // Rodape e, se houver algo na mao, o sprite esmaecido acompanhando o cursor.
  void OnCursorMoved() {
    // UMA linha por slot focado, nunca por frame: OnCursorMoved e chamado
    // tambem por Refresh(), e sem este guarda o log viraria a mesma linha
    // repetida centenas de vezes — o volume esconderia o rastro em vez de
    // revela-lo.
    if (cursor_ != logged_cursor_ || activeLeft_ != logged_left_) {
      logged_cursor_ = cursor_;
      logged_left_ = activeLeft_;
      const BoxPanel& lp = Active();
      const g3::BoxPokemon m = lp.Effective(cursor_);
      NLOG_NAV("cursor %s caixa %zu slot %zu: %s", activeLeft_ ? "esq" : "dir",
               lp.box, cursor_, m.empty() ? "vazio" : DisplaySpecies(m).c_str());
    }

    if (session_.Holding()) {
      // O cursor saiu de uma celula e entrou em outra: apaga o levantado da
      // antiga. Sem isso o "fantasma" ficaria para tras, aparecendo em dois
      // lugares. Nao precisa repor o sprite do ocupante — ele nunca foi
      // tocado, o levantado mora num Image proprio (rodada 8).
      if (lastHeldPanel_ && lastHeldCell_ < lastHeldPanel_->cells.size()) {
        lastHeldPanel_->cells[lastHeldCell_]->ClearHeld();
      }
      BoxPanel& act = Active();
      if (cursor_ < act.cells.size()) {
        act.cells[cursor_]->SetHeld(session_.Held());
        lastHeldPanel_ = &act;
        lastHeldCell_ = cursor_;
      }

      // Fantasma parado no slot de origem (spec 085): so existe se a origem
      // estiver visivel num dos dois paineis abertos agora — trocar de caixa
      // pode escondê-la, e nao ha celula pra marcar nesse caso.
      const bx::SlotRef& from = session_.HeldFrom();
      SlotCell* origin_cell = nullptr;
      for (BoxPanel* panel : {&left_, &right_}) {
        if (panel->source_id == from.source && panel->box == from.box &&
            from.slot < panel->cells.size()) {
          origin_cell = panel->cells[from.slot];
          break;
        }
      }
      if (origin_cell != heldOriginCell_) {
        if (heldOriginCell_) heldOriginCell_->ClearOrigin();
        heldOriginCell_ = origin_cell;
      }
      if (heldOriginCell_) heldOriginCell_->SetOrigin(session_.Held());
    } else if (heldOriginCell_) {
      heldOriginCell_->ClearOrigin();
      heldOriginCell_ = nullptr;
    }

    // Area do modo Selecao (spec 088). Recalculada a cada movimento do
    // cursor: e o que faz o retangulo crescer e encolher junto com ele.
    RefreshSelectionArea();

    // O B muda de significado durante um gesto em curso: desfaz, em vez de
    // sair da tela. O rodape acompanha, senao prometeria a acao errada.
    //
    // Vale para os DOIS gestos, nao so para o modo Selecao (spec 100): com um
    // Pokemon na mao — pelo A ou pelo atalho do Y — o B tambem cancela e
    // devolve o Pokemon ao slot de origem (`MoveSession::Cancel`). Ate aqui o
    // rodape so trocava o texto no modo Selecao, e prometia "Voltar" enquanto
    // o B na verdade cancelava o movimento.
    if (backLabel_) {
      const bool selecionando =
          session_.Holding() ||
          (mode_ == bx::CursorMode::kSelect && selPhase_ != SelPhase::kOcioso);
      backLabel_->setText(std::string(kGlyphB) +
                          (selecionando ? "  Cancelar" : "  Voltar"));
    }

    // Cartao em foco muda de cor, ganha os ombros L/R e o cursor de seta
    // (spec 046). Feito aqui porque este e o unico ponto por onde toda troca
    // de painel e de slot passa.
    left_.SetFocused(activeLeft_);
    right_.SetFocused(!activeLeft_);
    // Cor do cursor por modo (spec 089), como as tres cores do HOME: laranja
    // no Mover (o regular), azul no Trocar, verde no Selecionar.
    const NVGcolor cursor_color =
        mode_ == bx::CursorMode::kSwap     ? kSelectorBlue
        : mode_ == bx::CursorMode::kSelect ? kSelectorGreen
                                           : kSelectorOrange;
    // Segurando um Pokemon (mao cheia) ou um bloco (modo Selecao na fase de
    // carregar): o ponteiro vira MAO (spec 093).
    const bool segurando =
        session_.Holding() || selPhase_ == SelPhase::kSegurando;
    for (BoxPanel* panel : {&left_, &right_}) {
      if (auto* frame = dynamic_cast<BoxFrame*>(panel->root)) {
        frame->SetCursorColor(cursor_color);
        frame->SetCursorHolding(segurando);
      }
    }
    Active().ShowCursor(cursor_);

    const BoxPanel& p = Active();

    // Visao geral (spec 105): a barra de status do Pokemon SOME e da lugar
    // ao cartao da caixa (nome + contagem) e ao tooltip com o conteudo.
    if (p.overview) {
      statsCard_->setVisibility(brls::Visibility::INVISIBLE);
      const std::size_t b = p.ov_page * kSlotsPerBox + cursor_;
      if (b < p.source->BoxCount()) {
        std::vector<std::string> sprites(kSlotsPerBox);
        for (std::size_t s = 0; s < kSlotsPerBox; ++s) {
          const g3::BoxPokemon m = p.EffectiveAt(b, s);
          if (!m.empty()) sprites[s] = SpritePath(m);
        }
        ovCard_->Set(p.BoxName(b), p.CountIn(b), std::move(sprites),
                     activeLeft_);
      } else {
        ovCard_->Clear();
      }
      if (backLabel_) {
        backLabel_->setText(std::string(kGlyphB) + "  Voltar");
      }
      return;
    }
    statsCard_->setVisibility(brls::Visibility::VISIBLE);
    ovCard_->Clear();

    // Modo de selecao: o rodape mostra quantos estao marcados, que e a
    // informacao que importa enquanto se monta o bloco (spec 021).
    if (mode_ == bx::CursorMode::kSelect) {
      // O rodape acompanha a FASE (spec 088): cada uma tem um proximo passo
      // diferente, e dizer "A marca" enquanto se carrega um bloco mentiria.
      const SelRect rc = SelectionRect();
      statSub_->setText(" ");
      ClearStatGrid();
      if (selPhase_ == SelPhase::kAncorando) {
        const std::size_t larg = rc.c1 - rc.c0 + 1, alt = rc.r1 - rc.r0 + 1;
        statName_->setText("Area " + std::to_string(alt) + "x" +
                           std::to_string(larg));
        SetInfo("A pega o bloco — B cancela");
      } else if (selPhase_ == SelPhase::kSegurando) {
        statName_->setText(std::to_string(selShape_.size()) + " na mao");
        SetInfo("A solta o bloco — B cancela");
      } else {
        statName_->setText("Selecao");
        SetInfo("A comeca a area — ZL/ZR trocam de modo");
      }
      return;
    }

    // Segurando: o rodape mostra quem esta na mao, nao o slot sob o cursor —
    // e a informacao que importa enquanto se procura onde soltar. Espelha o
    // rotulo flutuante do HOME (ver 41.jpg na spec 019).
    if (session_.Holding()) {
      // Incompativel com o painel: explica POR QUE o A nao fez nada. Sem isto
      // o botao pareceria travado (spec 034).
      if (!p.FitsInPanel(session_.Held())) {
        const g3::BoxPokemon& held = session_.Held();
        statName_->setText(held.nickname.empty() ? DisplaySpecies(held)
                                                  : held.nickname);
        FillStatGrid(held);
        SetInfo(std::string("Nao existe em ") +
                           cp::GameName(p.source->GameId()));
        return;
      }
      const g3::BoxPokemon& held = session_.Held();
      statName_->setText(held.nickname.empty() ? DisplaySpecies(held)
                                                : held.nickname);
      FillStatGrid(held);

      // Aviso de golpe (spec 038): a especie cabe, mas um golpe nao existe no
      // destino. Diferente do vermelho, o A CONTINUA soltando — por isso o
      // texto avisa em vez de explicar uma recusa.
      const int missing = p.MissingMove(held);
      if (missing != 0) {
        // A tabela alcanca a gen9 (spec 106); o numero so aparece para id que
        // nem o PokeAPI conhece.
        const std::string name = DisplayMove(static_cast<std::uint16_t>(missing));
        const std::string what =
            name.empty() ? ("golpe #" + std::to_string(missing)) : name;
        SetInfo("Aviso: " + what + " nao existe em " +
                           cp::GameName(p.source->GameId()) + " — A solta");
        return;
      }

      SetInfo("Segurando — A solta, B cancela");
      return;
    }

    const g3::BoxPokemon mon = p.Effective(cursor_);
    if (mon.empty()) {
      statName_->setText("—");
      statSub_->setText("");
      const std::string warn = p.source->Warning();
      SetInfo(warn.empty() ? "" : warn);
      ClearStatGrid();
    } else {
      // Como o HOME: a faixa mostra o APELIDO quando existe; a especie e o
      // fallback. O campo separado de apelido saiu (captura nao tem).
      statName_->setText(mon.nickname.empty() ? DisplaySpecies(mon)
                                              : mon.nickname);
      FillStatGrid(mon);
      SetInfo("");
    }
  }

  // Preenche a identidade da faixa teal (sexo, OT, idioma, origem, logo) a
  // partir do Pokemon sob o cursor — e a parte nova da spec 098.
  // LanguageTag/GameSigla/BallSprite viviam aqui e subiram para escopo de
  // namespace na spec 103: a tela de summary usa os mesmos.
  void FillIdentity(const g3::BoxPokemon& mon) {
    identityShown_ = true;
    const std::uint8_t g = g3::Gender(mon);
    statGender_->Set(g);

    const char* ball = BallSprite(mon.display_ball);
    if (ball) {
      statBall_->setImageFromFile(std::string(POKEHOME_UI_ASSETS) + "balls/" +
                                  ball + ".png");
      statBall_->setVisibility(brls::Visibility::VISIBLE);
    } else {
      statBall_->setVisibility(brls::Visibility::INVISIBLE);
    }
    statusHead_->vector_ball = (ball == nullptr);
    lvTag_->setVisibility(brls::Visibility::VISIBLE);

    // Tipos da especie (1 ou 2), pela dex nacional.
    static const char* kTypeNames[] = {
        "",       "normal",  "fighting", "flying", "poison",  "ground",
        "rock",   "bug",     "ghost",    "steel",  "fire",    "water",
        "grass",  "electric","psychic",  "ice",    "dragon",  "dark",
        "fairy"};
    const int dex = mon.national_dex != 0
                        ? mon.national_dex
                        : g3::NationalDex(mon.species);
    for (int i = 0; i < 2; ++i) {
      const std::uint8_t t =
          (dex >= 1 && dex <= 1025) ? pokehome::modern::kTypeIds[dex][i] : 0;
      if (t >= 1 && t <= 18) {
        typeIcons_[i]->setImageFromFile(std::string(POKEHOME_UI_ASSETS) +
                                        "types/" + kTypeNames[t] + ".png");
        typeIcons_[i]->setVisibility(brls::Visibility::VISIBLE);
      } else {
        typeIcons_[i]->setVisibility(brls::Visibility::INVISIBLE);
      }
    }

    statOt_->setText(mon.ot_name);
    otTag_->setVisibility(mon.ot_name.empty() ? brls::Visibility::INVISIBLE
                                              : brls::Visibility::VISIBLE);

    const char* lang = LanguageTag(mon.language);
    statLang_->setText(lang);
    langChip_->setVisibility(*lang ? brls::Visibility::VISIBLE
                                   : brls::Visibility::INVISIBLE);
    statGame_->setText(GameSigla(mon.origin_game));
    gameChip_->setVisibility(brls::Visibility::VISIBLE);
    statMarks_->setVisibility(brls::Visibility::VISIBLE);

    // Mesma tabela do rodape do cartao (spec 101): PanelArtSlug e a fonte
    // unica, e `accent` marca o painel do NestBox.
    const BoxPanel& ap = Active();
    const char* slug = PanelArtSlug(ap.source, ap.source_id == kNestId);
    if (*slug) {
      // LogoPath() vive mais abaixo no arquivo (tela de saves); monta igual.
      statLogo_->setImageFromFile(std::string(POKEHOME_UI_ASSETS) + "games/" +
                                  slug + "_logo.png");
      statLogo_->setVisibility(brls::Visibility::VISIBLE);
    } else {
      statLogo_->setVisibility(brls::Visibility::INVISIBLE);
    }
  }

  // Mensagem de contexto na ponta direita. Ela divide o lugar com os
  // marcadores (ambos ABSOLUTE em right=32): com texto, os marcadores somem;
  // sem texto, voltam se houver um Pokemon exibido.
  void SetInfo(const std::string& text) {
    statInfo_->setText(text);
    statMarks_->setVisibility(text.empty() && identityShown_
                                  ? brls::Visibility::VISIBLE
                                  : brls::Visibility::INVISIBLE);
  }

  // Esconde tudo que so existe com um Pokemon sob o cursor (spec 098).
  void ClearIdentity() {
    identityShown_ = false;
    statGender_->Set(-1);
    statBall_->setVisibility(brls::Visibility::INVISIBLE);
    statusHead_->vector_ball = true;
    lvTag_->setVisibility(brls::Visibility::INVISIBLE);
    typeIcons_[0]->setVisibility(brls::Visibility::INVISIBLE);
    typeIcons_[1]->setVisibility(brls::Visibility::INVISIBLE);
    statOt_->setText("");
    otTag_->setVisibility(brls::Visibility::INVISIBLE);
    langChip_->setVisibility(brls::Visibility::INVISIBLE);
    gameChip_->setVisibility(brls::Visibility::INVISIBLE);
    statMarks_->setVisibility(brls::Visibility::INVISIBLE);
    statLogo_->setVisibility(brls::Visibility::INVISIBLE);
  }

  // Esvazia os VALORES da grade; os rotulos continuam na tela (spec 053).
  // O traco no lugar do vazio e o da referencia: mostra que o campo existe e
  // esta sem dado, em vez de sumir e deixar a linha desalinhada.
  void ClearStatGrid() {
    ClearIdentity();
    statLevel_->setText("");
    // Tracos como na captura do HOME (spec 098): longo nos campos de texto,
    // curto nos numericos.
    if (statNature_) statNature_->setText("———");
    if (statAbility_) statAbility_->setText("———");
    for (brls::Label* v : statValues_) {
      if (v) v->setText("—");
    }
    for (brls::Label* m : statMoves_) {
      if (m) {
        m->setText("———");
        m->setAlpha(1.0f);  // o esmaecimento do aviso morre com o dado
      }
    }
    for (MoveWarnIcon* w : statMoveWarn_) {
      if (w) w->setVisibility(brls::Visibility::GONE);
    }
  }

  void FillStatGrid(const g3::BoxPokemon& mon) {
    const g3::BattleStats bs = g3::ComputeStats(mon);
    // So o numero: o "Lv." e o lvTag_, menor e mais claro (captura).
    statLevel_->setText(std::to_string(bs.level));

    FillIdentity(mon);

    if (statNature_) statNature_->setText(g3::NatureName(mon.nature()));

    if (statAbility_) {
      std::string ability;
      if (mon.modern) {
        // Formato moderno traz o ID da habilidade; o nome sai da tabela
        // gerada do PokeAPI (spec 099).
        const std::uint16_t id = mon.modern->ability;
        if (id < pokehome::modern::kAbilityCount) {
          ability = pokehome::modern::kAbilityNames[id];
        }
      } else {
        const g3::PersonalInfo personal =
            g3::Personal(g3::NationalDex(mon.species));
        ability = g3::AbilityName(personal.ability(mon.ability_bit));
      }
      statAbility_->setText(ability.empty() ? "———" : ability);
    }

    // A ordem da barra e HP, Speed, Attack, Sp.Atk, Defense, Sp.Def — em
    // coluna. `bs.values` vem do save na ordem HP, Atk, Def, Spe, SpA, SpD.
    static constexpr int kOrder[6] = {0, 3, 1, 4, 2, 5};
    for (int i = 0; i < 6; ++i) {
      if (!statValues_[i]) continue;
      const std::uint16_t v = bs.values[kOrder[i]];
      statValues_[i]->setText(v == 0 ? "—" : std::to_string(v));
    }

    // Golpe ausente no jogo do SAVE ABERTO (spec 104): nome apagado e o
    // icone amarelo piscando ao lado. Sempre contra o save, nao contra o
    // painel do cursor — decisao do dono: um Pokemon parado no NestBox com
    // golpe que o save nao conhece avisa ANTES de o jogador tentar move-lo.
    const cp::Game save_game = save_ ? save_->GameId() : cp::Game::kCount;
    for (int i = 0; i < 4; ++i) {
      if (!statMoves_[i]) continue;
      // Slot de golpe vazio mostra o traco, como no HOME (spec 098).
      const std::string name = DisplayMove(mon.moves[i]);
      // A tabela alcanca a gen9 (spec 106); o numero so sobra para id que nem
      // o PokeAPI conhece — melhor que um rotulo em branco.
      statMoves_[i]->setText(mon.moves[i] == 0 ? "———"
                             : name.empty()
                                 ? ("golpe #" + std::to_string(mon.moves[i]))
                                 : name);
      const bool falta = mon.moves[i] != 0 && save_game != cp::Game::kCount &&
                         !cp::HasMove(save_game, mon.moves[i]);
      statMoves_[i]->setAlpha(falta ? 0.35f : 1.0f);
      if (statMoveWarn_[i]) {
        statMoveWarn_[i]->setVisibility(falta ? brls::Visibility::VISIBLE
                                              : brls::Visibility::GONE);
      }
    }
  }

  BoxSource* nest_;
  BoxSource* save_;
  BoxPanel left_;
  BoxPanel right_;
  // Visao geral das caixas (spec 105): a barra de status inteira para
  // esconder, e o cartao/tooltip que a substitui.
  brls::Box* statsCard_ = nullptr;
  BoxOverviewCard* ovCard_ = nullptr;
  brls::Label* statName_ = nullptr;
  GenderIcon* statGender_ = nullptr;
  brls::Label* statLevel_ = nullptr;
  brls::Label* statNature_ = nullptr;
  brls::Label* statAbility_ = nullptr;
  brls::Label* statValues_[6] = {};
  brls::Label* statMoves_[4] = {};
  MoveWarnIcon* statMoveWarn_[4] = {};  // icone do golpe ausente (spec 104)
  brls::Label* statSub_ = nullptr;
  brls::Label* statInfo_ = nullptr;
  // Identidade da faixa teal (spec 098).
  brls::Label* statOt_ = nullptr;
  PersonIcon* otTag_ = nullptr;
  brls::Label* lvTag_ = nullptr;
  brls::Image* typeIcons_[2] = {};
  brls::Box* langChip_ = nullptr;
  brls::Label* statLang_ = nullptr;
  brls::Box* gameChip_ = nullptr;
  brls::Label* statGame_ = nullptr;
  MarkStrip* statMarks_ = nullptr;
  brls::Image* statLogo_ = nullptr;
  StatusHead* statusHead_ = nullptr;
  brls::Image* statBall_ = nullptr;
  // Ha um Pokemon exibido na faixa agora (controla os marcadores em SetInfo).
  bool identityShown_ = false;
  std::size_t cursor_ = 0;
  bool activeLeft_ = false;  // comeca no save, que tem conteudo

  // Ultimo slot ja registrado no log (spec 083). Existe so para o log nao
  // repetir a mesma linha a cada Refresh().
  std::size_t logged_cursor_ = static_cast<std::size_t>(-1);
  bool logged_left_ = true;

  // Ids das duas fontes no overlay. Ver box_move.h.
  static constexpr int kNestId = 0;
  static constexpr int kSaveId = 1;

  // Limite do teclado. O formato guarda kBoxNameBytes com o ultimo byte sempre
  // zero, entao cabem 23 caracteres — pedir mais so seria truncado depois.
  static constexpr int kBoxNameMax =
      static_cast<int>(nest::kBoxNameBytes) - 1;

  // Sessao de movimentacao: vive enquanto a tela existe e morre com ela —
  // sair descarta tudo, como sair do HOME sem salvar. Nada e gravado em disco.
  bx::MoveSession session_;

  // Onde o sprite esmaecido foi desenhado por ultimo, para limpa-lo quando o
  // cursor anda. Ponteiro para um dos dois paineis desta tela; nao possui nada.
  BoxPanel* lastHeldPanel_ = nullptr;
  std::size_t lastHeldCell_ = 0;

  // Slot de onde o Pokemon na mao saiu, marcado com o "fantasma" parado
  // (SetOrigin, spec 085). So aponta pra algo quando a origem esta visivel
  // num dos dois paineis atuais — trocar de caixa pode tirá-la de vista.
  SlotCell* heldOriginCell_ = nullptr;

  // Modo de cursor ativo (spec 031). Decide o que A faz.
  bx::CursorMode mode_ = bx::CursorMode::kMove;
  ModeStrip* modeStrip_ = nullptr;

  // Fases do modo Selecao (spec 088), o cursor verde do HOME:
  //   kOcioso     — A ancora aqui e comeca a pintar a area.
  //   kAncorando  — mover o cursor expande/encolhe o retangulo; A fecha.
  //   kSegurando  — o bloco esta "na mao"; A solta no destino.
  // B volta para kOcioso em qualquer fase.
  enum class SelPhase { kOcioso, kAncorando, kSegurando };
  SelPhase selPhase_ = SelPhase::kOcioso;
  // Slot onde o A ancorou, e o canto superior esquerdo da area fechada.
  std::size_t selAnchor_ = 0;
  std::size_t selTopLeft_ = 0;
  // A forma congelada no segundo A: (origem, (dr, dc)) a partir do canto.
  std::vector<std::pair<bx::SlotRef, std::pair<int, int>>> selShape_;
  // Celulas que o bloco pintou no ultimo quadro (levantado ou fantasma), para
  // limpar antes de repintar — senao a formacao deixa rastro ao andar.
  std::vector<SlotCell*> blockCells_;

  // Pokemon do bloco que caem FORA da grade (spec 088, rodada 9): o cursor
  // anda livre e a formacao vaza para o espaco entre os paineis. Guardados em
  // passo de SLOT relativo a celula (0,0) do painel ativo; o BlockOverflow
  // converte para pixel.
  struct OverflowMon {
    long row, col;  // podem ser negativos ou >= kRows/kCols
    std::string sprite;
  };
  std::vector<OverflowMon> overflow_;
  BlockOverflow* overflowView_ = nullptr;

  // Rotulo do B no rodape. Vira "Cancelar" durante a selecao (spec 088): ali
  // o B desfaz o gesto em vez de sair da tela, e o rodape tem de dizer isso.
  brls::Label* backLabel_ = nullptr;

  // Alguma caixa foi renomeada nesta sessao (spec 030). Separado da
  // MoveSession, que so conhece movimentacao de Pokemon.
  bool renamed_ = false;
};

// --- Restauracao de backup (spec 037) --------------------------------------

// Grava os bytes de um backup por cima do save. E a UNICA escrita em save de
// jogo que nao passa pelo commit — e o proposito da tela.
//
// Confere ANTES de abrir o arquivo de destino: um backup ilegivel ou de
// tamanho diferente do save nao chega a tocar no save. Restaurar lixo por cima
// de um save bom e pior que nao restaurar (TD-02 da spec 037).
bool RestoreBackup(const std::string& backup_file, const std::string& save_path,
                   std::size_t expected_size) {
  NLOG_ACT("restaurar \"%s\" sobre \"%s\" (esperado %zu bytes)",
           backup_file.c_str(), save_path.c_str(), expected_size);
  const std::vector<std::uint8_t> bytes = ReadFile(BackupPath(backup_file));
  if (bytes.empty() || bytes.size() != expected_size) {
    NLOG_ACT("FALHA restaurar: backup tem %zu bytes, save espera %zu",
             bytes.size(), expected_size);
    return false;
  }

  std::FILE* f = std::fopen(save_path.c_str(), "wb");
  if (!f) {
    NLOG_ACT("FALHA restaurar: fopen(\"%s\",\"wb\") recusou", save_path.c_str());
    return false;
  }
  const std::size_t put = std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fflush(f);
  std::fclose(f);
  if (put != bytes.size()) {
    NLOG_ACT("FALHA restaurar \"%s\": esperado %zu bytes, gravado %zu",
             save_path.c_str(), bytes.size(), put);
    return false;
  }
  NLOG_ACT("save RESTAURADO: \"%s\" (%zu bytes)", save_path.c_str(),
           bytes.size());

#ifdef __SWITCH__
  FsFileSystem* sdmc = fsdevGetDeviceFileSystem("sdmc");
  if (sdmc) fsFsCommit(sdmc);
#endif
  return true;
}

// Uma linha da lista de backups: data legivel a esquerda, dica a direita.
class BackupRow : public brls::Box {
 public:
  BackupRow(const std::string& when, std::function<void()> on_select)
      : on_select_(std::move(on_select)) {
    setAxis(brls::Axis::ROW);
    setHeight(64);
    setCornerRadius(14);
    setMarginBottom(10);
    setPadding(0, 22, 0, 22);
    setAlignItems(brls::AlignItems::CENTER);
    setBackgroundColor(kWhite);
    setFocusable(true);
    setHideHighlight(true);

    label_ = new brls::Label();
    label_->setText(when);
    label_->setFontSize(24);
    label_->setTextColor(kTextPrimary);
    addView(label_);

    auto* spacer = new brls::Box();
    spacer->setGrow(1.0f);
    addView(spacer);

    hint_ = new brls::Label();
    hint_->setText("Restaurar");
    hint_->setFontSize(19);
    hint_->setTextColor(kTextTertiary);
    addView(hint_);

    registerClickAction([this](brls::View*) {
      if (on_select_) on_select_();
      return true;
    });
    addGestureRecognizer(new brls::TapGestureRecognizer(this));
  }

  // Em foco a linha inverte: fundo escuro e texto claro. O kMenuAmber do menu
  // so e declarado adiante, e o par kDarkBar/kWhite ja e o contraste usado nas
  // barras do app.
  void onFocusGained() override {
    brls::Box::onFocusGained();
    setBackgroundColor(kDarkBar);
    label_->setTextColor(kWhite);
    hint_->setTextColor(kWhite);
  }

  void onFocusLost() override {
    brls::Box::onFocusLost();
    setBackgroundColor(kWhite);
    label_->setTextColor(kTextPrimary);
    hint_->setTextColor(kTextTertiary);
  }

 private:
  std::function<void()> on_select_;
  brls::Label* label_ = nullptr;
  brls::Label* hint_ = nullptr;
};

// Lista os backups do save aberto e restaura o escolhido.
//
// So o save ABERTO: o caminho de destino sai do SaveSource, nao de um palpite
// sobre a qual arquivo o nome do backup pertencia — dois cartoes podem ter
// FireRed.sav em diretorios diferentes (TD-01 da spec 037).
class RestoreActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"RestoreActivity"};

  explicit RestoreActivity(SaveSource* save) : save_(save) {}

  brls::View* createContentView() override {
    auto* root = new GradientBackground();
    root->setHeaderBand(58);
    root->setAxis(brls::Axis::COLUMN);

    auto* bar = new brls::Box(brls::Axis::ROW);
    bar->setHeight(58);
    bar->setAlignItems(brls::AlignItems::CENTER);
    bar->setPadding(0, 40, 0, 40);
    auto* title = new brls::Label();
    title->setText("RESTAURAR BACKUP");
    title->setFontSize(24);
    title->setTextColor(kTextPrimary);
    bar->addView(title);
    root->addView(bar);

    auto* body = new brls::Box(brls::Axis::COLUMN);
    body->setGrow(1.0f);
    body->setPadding(20, 56, 24, 56);

    auto* sub = new brls::Label();
    sub->setText(save_->Title() + " — escolha o ponto para voltar");
    sub->setFontSize(20);
    sub->setTextColor(kTextSecondary);
    sub->setMarginBottom(18);
    body->addView(sub);

    entries_ = bk::NewestFirst(bk::ForSave(ListBackups(), save_->path()));

    if (entries_.empty()) {
      // Save sem backup nenhum precisa DIZER isso. Lista vazia sem explicacao
      // pareceria tela quebrada.
      auto* empty = new brls::Label();
      empty->setText(
          "Nenhum backup deste save ainda.\n"
          "O backup e criado sozinho antes da primeira gravacao.");
      empty->setFontSize(22);
      empty->setTextColor(kTextTertiary);
      body->addView(empty);
    } else {
      for (const bk::Entry& e : entries_) {
        auto* row = new BackupRow(bk::HumanStamp(e.stamp),
                                  [this, e] { AskRestore(e); });
        if (!firstRow_) firstRow_ = row;
        body->addView(row);
      }
    }
    root->addView(body);

    root->registerAction(
        "Voltar", brls::BUTTON_B,
        [](brls::View*) {
          brls::Application::popActivity();
          return true;
        },
        false);

    if (firstRow_) brls::Application::giveFocus(firstRow_);
    return root;
  }

 private:
  // Restaurar sobrescreve o save atual e nao tem desfazer, entao pergunta —
  // com o CANCELAR primeiro, para que o gesto apressado seja o inofensivo
  // (spec 020).
  void AskRestore(const bk::Entry& entry) {
    ConfirmDialog("Voltar o save para " + bk::HumanStamp(entry.stamp) +
                      "?\nO progresso posterior sera perdido.",
                  "Cancelar", "Restaurar", [this, entry] { DoRestore(entry); });
  }

  void DoRestore(const bk::Entry& entry) {
    if (!RestoreBackup(entry.filename, save_->path(), save_->bytes().size())) {
      // Backup reprovado ou escrita falhou. O save nao foi tocado — a
      // conferencia acontece antes de abrir o arquivo de destino.
      NoticeDialog("Backup invalido ou ilegivel.\nO save NAO foi alterado.",
                   "Entendi");
      return;
    }

    // O SaveSource em memoria guarda os bytes da abertura e agora esta velho.
    // Voltar ao menu forca reabrir o save, em vez de reconstruir o estado em
    // memoria e torcer para nao esquecer um campo (risco na spec 037).
    // Nao cancelavel: a tela de tras esta desatualizada, entao a saida tem de
    // ser a explicita — B nao serve.
    NoticeDialog("Save restaurado para " + bk::HumanStamp(entry.stamp) +
                     ".\nAbra o save de novo para ver o resultado.",
                 "Voltar ao menu",
                 [] {
                   // Empilhadas: restauracao, caixas e selecao de save.
                   //
                   // ENCADEADAS, nao tres chamadas seguidas: popActivity so
                   // tira da pilha no fim da animacao de saida
                   // (application.cpp:948-963), entao tres no mesmo frame
                   // mirariam a mesma Activity do topo e as de baixo ficariam
                   // (spec 085 rodada 7).
                   brls::Application::popActivity(
                       brls::TransitionAnimation::FADE, [] {
                         brls::Application::popActivity(
                             brls::TransitionAnimation::FADE,
                             [] { brls::Application::popActivity(); });
                       });
                 },
                 /*cancelable=*/false);
  }

  SaveSource* save_;
  std::vector<bk::Entry> entries_;
  BackupRow* firstRow_ = nullptr;
};

// Save que nao e arquivo gen3 (NestBox, Z-A) nao tem caminho para restaurar —
// o botao nao faz nada em vez de abrir uma tela que nao saberia onde gravar.
void BoxActivity::OpenRestore() {
  if (auto* save = dynamic_cast<SaveSource*>(save_)) {
    brls::Application::pushActivity(new RestoreActivity(save));
  }
}

// --- Menu principal --------------------------------------------------------

// Amarelo-ambar do botao em foco, tirado da referencia. O kAccent do resto do
// app e mais avermelhado e nao serve aqui.
const NVGcolor kMenuAmber = nvgRGB(0xF5, 0xB3, 0x2C);
// Verde-agua da pilula do contador nos botoes fora de foco.
const NVGcolor kPillTeal = nvgRGB(0x8C, 0xCF, 0xC4);

// Sombra suave sob um retangulo arredondado. A mesma usada na seta do menu:
// camadas translucidas empilhadas, recortadas para existirem so abaixo do
// centro. nanovg nao borra forma, entao esta e a aproximacao possivel sem
// render target intermediario.
//
// Chamar ANTES de desenhar o proprio componente — isto so pinta a sombra.
void DrawSoftShadow(NVGcontext* vg, float x, float y, float w, float h,
                    float radius) {
  // Sombra de elevacao: a forma fica ATRAS do componente e vaza um pouco por
  // todos os lados, mais para baixo — como se ele estivesse levantado.
  //
  // Sem recorte: cortar na base produzia uma faixa colada embaixo, e nao
  // elevacao. O componente e desenhado por cima e cobre o centro.
  //
  // nvgBoxGradient da a borda difusa; nanovg nao borra forma de verdade.
  const float blur = 13.0f;
  const float drop = 5.0f;  // deslocamento vertical
  const float grow = 2.0f;  // quanto a sombra passa das bordas laterais

  NVGpaint paint = nvgBoxGradient(
      vg, x - grow, y - grow + drop, w + grow * 2, h + grow * 2,
      radius + grow, blur, nvgRGBAf(0.20f, 0.32f, 0.27f, 0.30f),
      nvgRGBAf(0.20f, 0.32f, 0.27f, 0.0f));

  nvgBeginPath(vg);
  nvgRect(vg, x - grow - blur, y - grow - blur + drop, w + (grow + blur) * 2,
          h + (grow + blur) * 2);
  // Buraco no meio: sem isto a sombra escurece o interior do componente
  // translucido por cima dela.
  nvgRoundedRect(vg, x, y, w, h, radius);
  nvgPathWinding(vg, NVG_HOLE);
  nvgFillPaint(vg, paint);
  nvgFill(vg);
}

// Pilula do contador com uma pokebola desenhada a esquerda. Desenhada em vez
// de carregada: sao tres primitivas do nanovg, escalam em qualquer tamanho e
// evitam mais um asset no romfs.
class PillWithBall : public brls::Box {
 public:
  // O Label filho e a referencia: a bola se posiciona a esquerda dele.
  void setTextView(brls::View* v) { text_ = v; }

  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    brls::Box::draw(vg, x, y, w, h, style, ctx);

    // A pokebola acompanha o texto centralizado: fica a esquerda dele, nao
    // presa na borda da pilula.
    const float r = h * 0.34f;
    const float cx = text_ ? text_->getX() - r - 8 : x + 22 + r;
    const float cy = y + h / 2;

    DrawPokeball(vg, cx, cy, r);
  }

 private:
  brls::View* text_ = nullptr;
};

// Faixa do subtitulo: clara em cima, esmaecendo ate sumir no fundo.
class SubtitleBand : public brls::Box {
 public:
  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    // Quase opaca: na referencia a faixa nao se dissolve no fundo, ela desce
    // de um branco esverdeado para um verde claro e para ali.
    NVGpaint p = nvgLinearGradient(vg, x, y, x, y + h,
                                   nvgRGBA(0xF4, 0xF8, 0xEE, 250),
                                   nvgRGBA(0xD6, 0xE9, 0xC8, 235));
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, p);
    nvgFill(vg);

    // Sombra sob a aresta de baixo — a faixa tem elevacao sobre o fundo.
    NVGpaint shadow = nvgLinearGradient(vg, x, y + h, x, y + h + 12,
                                        nvgRGBAf(0.20f, 0.32f, 0.27f, 0.22f),
                                        nvgRGBAf(0.20f, 0.32f, 0.27f, 0.0f));
    nvgBeginPath(vg);
    nvgRect(vg, x, y + h, w, 12);
    nvgFillPaint(vg, shadow);
    nvgFill(vg);

    brls::Box::draw(vg, x, y, w, h, style, ctx);
  }
};

// Aba do rodape: cantos de cima arredondados, base reta encostando na tela.
// setCornerRadius do borealis e uniforme, entao o desenho e proprio.
class FooterTab : public brls::Box {
 public:
  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    nvgBeginPath(vg);
    // Ordem: topo-esq, topo-dir, base-dir, base-esq. So o topo direito
    // arredonda — a aba encosta na borda esquerda e na base da tela.
    nvgRoundedRectVarying(vg, x, y, w, h, 0, 16, 0, 0);
    nvgFillColor(vg, nvgRGBA(244, 244, 240, 235));
    nvgFill(vg);
    brls::Box::draw(vg, x, y, w, h, style, ctx);
  }
};

// Barra de legenda do rodape (spec 050). Estava duplicada em duas telas com o
// mesmo desenho, e a tela de caixas nao tinha nenhuma; agora e um componente
// so, usado por todas.
//
// `back = true` acrescenta o "B Voltar" ao lado da aba, ja ligado ao
// popActivity — o rotulo captura o toque para ele nao vazar para o que
// estiver atras.
constexpr float kFooterHeight = 35.2f;  // 44 - 20% (pedido do dono)

brls::Box* MakeLegendBar(bool back, brls::Label** out_back,
                         const std::string& right_hint) {
  auto* row = new brls::Box(brls::Axis::ROW);
  row->setHeight(kFooterHeight);
  // CENTER, nao FLEX_END: alinha "Ajuda" e "Voltar" na mesma linha. Antes o
  // "Voltar" descia com um marginBottom proprio e ficava desalinhado.
  row->setAlignItems(brls::AlignItems::CENTER);

  // Na referencia isto nao e uma barra cheia: e uma aba curta encostada no
  // canto inferior esquerdo, com os cantos de cima arredondados.
  auto* tab = new FooterTab();
  tab->setAxis(brls::Axis::ROW);
  tab->setHeight(kFooterHeight);
  tab->setAlignItems(brls::AlignItems::CENTER);
  tab->setPadding(0, 26, 0, 24);

  auto* help = new brls::Label();
  help->setText(std::string(kGlyphMinus) + "  Ajuda");
  help->setFontSize(21);
  help->setTextColor(kTextPrimary);
  tab->addView(help);
  row->addView(tab);

  if (back) {
    auto* label = new brls::Label();
    label->setText(std::string(kGlyphB) + "  Voltar");
    label->setFontSize(21);
    label->setTextColor(kTextPrimary);
    label->setMarginLeft(26);
    // O texto e so um rotulo do hint de B; sem isto o toque nele vaza para o
    // que estiver atras, confundindo o usuario.
    label->addGestureRecognizer(new brls::TapGestureRecognizer(
        label, []() { brls::Application::popActivity(); }));
    row->addView(label);
    // Devolvido para quem quiser trocar o texto conforme o estado da tela —
    // o modo Selecao troca "Voltar" por "Cancelar" (spec 088).
    if (out_back) *out_back = label;
  }

  // Lado direito da legenda (spec 100). O espacador grow(1) empurra a dica
  // para a borda; sem ele ela ficaria colada no "Voltar".
  if (!right_hint.empty()) {
    auto* spacer = new brls::Box(brls::Axis::ROW);
    spacer->setGrow(1.0f);
    row->addView(spacer);

    auto* hint = new brls::Label();
    hint->setText(right_hint);
    hint->setFontSize(21);
    hint->setTextColor(kTextPrimary);
    hint->setMarginRight(kScreenSideMargin);
    row->addView(hint);
  }

  return row;
}

// --- Caixa de mensagem (spec 044) ------------------------------------------

// Formato do Pokemon HOME, nao o modal de biblioteca: uma barra branca no
// rodape com o texto a esquerda, e os botoes flutuando sobre a tela, a direita.
// O fundo continua visivel e nao escurece — mas so os botoes recebem foco,
// porque a caixa e uma Activity empilhada por cima.
//
// Ordem dos botoes: o primeiro e o de baixo, e e o **seguro**. O gesto
// apressado (confirmar no primeiro que aparece) tem de ser o inofensivo — e a
// regra da spec 020, que esta caixa preserva.
//
// A classe foi declarada la em cima; aqui vem o corpo das partes visuais.

// Botao pilula. Em foco fica laranja (kAccent, o mesmo da selecao na primeira
// tela) com a seta apontando; fora de foco, branco.
class MessageBox::PillButton : public brls::Box {
   public:
    // `on_dismiss` recebe a acao do botao e a executa DEPOIS que esta caixa
    // saiu da pilha de verdade — ver Fire().
    using Dismiss = std::function<void(Callback)>;

    PillButton(const Button& spec, Dismiss on_dismiss)
        : on_click_(spec.on_click), on_dismiss_(std::move(on_dismiss)) {
      setAxis(brls::Axis::ROW);
      setHeight(56);
      setCornerRadius(28);  // metade da altura: pilula, nao elipse
      // Conteudo ancorado a esquerda, nao centralizado: como os botoes esticam
      // ate a largura do maior, centralizar poe o glifo de cada um numa coluna
      // diferente (o "Abrir" mais para dentro que o "Cancelar"). Ancorado, os
      // glifos empilham na mesma vertical e o texto comeca sempre no mesmo x —
      // que e como o Switch alinha as dicas de A/B.
      setJustifyContent(brls::JustifyContent::FLEX_START);
      setAlignItems(brls::AlignItems::CENTER);
      setPadding(0, 30, 0, 30);
      setMarginTop(10);
      setMinWidth(190);
      setBackgroundColor(kWhite);
      setFocusable(true);
      setHideHighlight(true);  // o realce e a cor de fundo, nao o contorno

      // Glifo do controle (A/B). Vem do switch_icons.ttf, que o borealis
      // carrega como fallback — nao ha imagem para carregar.
      //
      // A coluna existe mesmo quando o botao nao tem glifo (o "Descartar e
      // sair" da caixa de tres): sem ela o texto dele subiria para a coluna do
      // glifo e sairia desalinhado dos vizinhos. Largura fixa, entao o texto
      // comeca no mesmo x em todos.
      glyph_ = new brls::Label();
      glyph_->setText(spec.glyph);
      glyph_->setFontSize(24);
      glyph_->setTextColor(kTextPrimary);
      glyph_->setWidth(26);
      glyph_->setMarginRight(10);
      glyph_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
      addView(glyph_);

      label_ = new brls::Label();
      label_->setText(spec.label);
      label_->setFontSize(25);
      label_->setTextColor(kTextPrimary);
      addView(label_);

      // registerClickAction, nao registerAction(BUTTON_A): o borealis ja trata
      // A como clique na View focada, e registrar o botao direto nao dispara.
      registerClickAction([this](brls::View*) {
        Fire();
        return true;
      });
      addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    // Fecha a caixa e SO ENTAO roda a acao — entregue como callback pos-
    // animacao do popActivity, nao chamada logo depois dele.
    //
    // popActivity() nao remove a Activity na hora: ele bloqueia input, agenda
    // a animacao de saida e so tira da pilha no fim dela
    // (application.cpp:948-963). Chamar a acao imediatamente depois fazia o
    // popActivity() DELA mirar esta mesma caixa, que ainda estava no topo —
    // a tela de tras nunca saia e o token de input ficava presvo, travando o
    // app (spec 085 rodada 7: "descartar e sair" congelava).
    void Fire() {
      if (on_dismiss_) {
        on_dismiss_(on_click_);
      } else if (on_click_) {
        on_click_();
      }
    }

    void onFocusGained() override {
      brls::Box::onFocusGained();
      setBackgroundColor(kAccent);
      label_->setTextColor(kWhite);
      if (glyph_) glyph_->setTextColor(kWhite);
    }

    void onFocusLost() override {
      brls::Box::onFocusLost();
      setBackgroundColor(kWhite);
      label_->setTextColor(kTextPrimary);
      if (glyph_) glyph_->setTextColor(kTextPrimary);
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
      DrawSoftShadow(vg, x, y, w, h, h / 2);
      brls::Box::draw(vg, x, y, w, h, style, ctx);
      if (!isFocused()) return;
      ++tick_;
      // Seletor primary (spec 047) no lugar da seta nanovg.
      Selector::Draw(vg, Selector::Variant::kPrimary, x + 8, y + h / 2,
                     40.0f, tick_);
    }

    brls::View* getDefaultFocus() override { return this; }

   private:
    Callback on_click_;
    Dismiss on_dismiss_;
    brls::Label* label_ = nullptr;
    brls::Label* glyph_ = nullptr;
    mutable unsigned tick_ = 0;
  };

// Barra branca do rodape. Cantos de cima arredondados e sombra por cima:
// setCornerRadius do borealis e uniforme, entao o desenho e proprio.
class MessageBox::Bar : public brls::Box {
   public:
    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
      // Sem sombra: a barra e uma superficie assentada no rodape, nao um
      // elemento levantado. So os botoes flutuam.
      nvgBeginPath(vg);
      nvgRoundedRectVarying(vg, x, y, w, h, 18, 18, 0, 0);
      nvgFillColor(vg, nvgRGBA(0xF2, 0xF2, 0xEF, 250));
      nvgFill(vg);
      brls::Box::draw(vg, x, y, w, h, style, ctx);
    }
};

// A caixa e uma Activity para empilhar sobre a tela atual sem escurece-la e
// sem que o fundo receba foco.
class MessageBoxActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"MessageBoxActivity"};

  MessageBoxActivity(std::string text, std::vector<MessageBox::Button> buttons,
                     bool cancelable)
      : text_(std::move(text)),
        buttons_(std::move(buttons)),
        cancelable_(cancelable) {}

  brls::View* createContentView() override;

  // A tela de tras tem de continuar aparecendo. O borealis desenha a pilha de
  // Activities de cima para baixo e PARA na primeira nao-translucida
  // (application.cpp:778) — sem isto a caixa e a ultima desenhada e o fundo sai
  // branco. Ver evidence-log da spec 044.
  bool isTranslucent() override { return true; }

 private:
  std::string text_;
  std::vector<MessageBox::Button> buttons_;
  bool cancelable_;
};

brls::View* MessageBoxActivity::createContentView() {
  // A tela de tras continua visivel (ver isTranslucent abaixo), so escurece um
  // pouco: 10% de preto separa a caixa do que esta atras sem esconder o
  // contexto — o usuario ainda enxerga o save sobre o qual esta decidindo.
  auto* root = new brls::Box(brls::Axis::COLUMN);
  root->setJustifyContent(brls::JustifyContent::FLEX_END);
  root->setBackgroundColor(nvgRGBA(0, 0, 0, 26));  // 26/255 = 10%

  // A acao do botao vai como callback do popActivity: ele so a roda depois de
  // esta Activity ter saido da pilha, o que deixa um popActivity() dentro da
  // acao mirar a tela de tras, e nao esta caixa. Ver PillButton::Fire().
  auto dismiss = [](MessageBox::Callback action) {
    brls::Application::popActivity(brls::TransitionAnimation::FADE,
                                   action ? action : [] {});
  };

  // Coluna dos botoes, flutuando sobre a tela e alinhada a direita. Ordem
  // visual: o primeiro botao (o seguro) fica embaixo, encostado na barra, que e
  // onde o polegar cai e onde a referencia poe o "No".
  // O stack encolhe ate a largura do maior botao e os filhos esticam para
  // acompanha-lo (STRETCH) — assim todos saem do mesmo tamanho sem numero
  // fixo. Com FLEX_END cada botao ficava do tamanho do proprio texto.
  auto* stack = new brls::Box(brls::Axis::COLUMN);
  stack->setAlignItems(brls::AlignItems::STRETCH);
  stack->setJustifyContent(brls::JustifyContent::FLEX_END);
  stack->setAlignSelf(brls::AlignSelf::FLEX_END);
  stack->setPadding(0, 60, 16, 60);

  brls::View* first = nullptr;
  for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
    auto* pill = new MessageBox::PillButton(*it, dismiss);
    // B fecha, quando a caixa permite.
    //
    // Registrado em cada botao, e nao no root da caixa, porque
    // Application::handleAction nao para no primeiro handler: sobe de
    // currentFocus por getParent() e roda todos os que casarem com o botao.
    // Aqui em baixo o handler e o primeiro da cadeia.
    //
    // A caixa de "save restaurado" nao e cancelavel — a tela de tras esta
    // desatualizada e a saida tem de ser a escolha explicita. Devolver false
    // ali e seguro: a cadeia termina no root DESTA Activity, porque o root de
    // uma Activity tem parent nulo (ele se liga por setParentActivity, nao por
    // setParent). O B nao vaza para a tela de baixo.
    pill->registerAction(
        "Voltar", brls::BUTTON_B,
        [this](brls::View*) {
          if (cancelable_) brls::Application::popActivity();
          return cancelable_;
        },
        false, false, brls::SOUND_BACK);
    stack->addView(pill);
    first = pill;  // o ultimo adicionado e o primeiro da lista: o seguro
  }
  root->addView(stack);

  // Barra do rodape com o texto a esquerda.
  auto* bar = new MessageBox::Bar();
  bar->setAxis(brls::Axis::ROW);
  bar->setAlignItems(brls::AlignItems::CENTER);
  bar->setPadding(42, 60, 42, 60);
  bar->setMinHeight(154);  // 40% a mais que os 110 iniciais

  auto* label = new brls::Label();
  label->setText(text_);
  label->setFontSize(30);
  label->setTextColor(kTextPrimary);
  label->setSingleLine(false);
  label->setGrow(1.0f);
  bar->addView(label);
  root->addView(bar);

  // O foco nasce no botao seguro, nao no destrutivo. Mesmo mecanismo que o
  // brls::Dialog usa para focar o primeiro botao.
  if (first) stack->setLastFocusedView(first);

  return root;
}

void MessageBox::Show(const std::string& text, std::vector<Button> buttons,
                      bool cancelable) {
  brls::Application::pushActivity(
      new MessageBoxActivity(text, std::move(buttons), cancelable));
}

// --- Menu de contexto do modo Mover (spec 095) -----------------------------
//
// No modo Mover o A abre ESTE menu em vez de pegar o Pokemon direto — e o
// que diferencia o Mover do Trocar (que pega na hora).
//
// Nao reusa o MessageBox: aquele e uma caixa modal centrada de ate 3 botoes,
// com barra de texto no rodape. Aqui a lista e lateral, encostada no slot, e
// cresce com o numero de itens.
//
// As pecas e medidas sao as da folha de assets: pilula 234x45, passo 56,
// ambar #F5B32C no ativo, painel cinza claro OPACO atras da coluna.
class ContextMenuActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"ContextMenuActivity"};

  struct Item {
    std::string label;
    std::function<void()> on_pick;
    std::string glyph;  // vazio, ou o glifo de B em "Sair"
  };

  ContextMenuActivity(brls::Rect anchor, std::vector<Item> items)
      : anchor_(anchor), items_(std::move(items)) {}

  brls::View* createContentView() override;

  // A caixa atras continua visivel — mesma regra da MessageBoxActivity
  // (spec 044): sem isto o borealis para de desenhar na primeira Activity
  // nao-translucida e o fundo sai branco.
  bool isTranslucent() override { return true; }

 private:
  brls::Rect anchor_;  // retangulo do slot que abriu o menu, em coordenadas
                       // de tela (spec 096)
  std::vector<Item> items_;
};

// Pilula de um item. Em foco vira ambar com texto branco; fora dele, branca
// com texto escuro — o mesmo par do menu principal.
class ContextMenuRow : public brls::Box {
 public:
  // `on_dismiss` RECEBE a acao e a executa depois de fechar o menu — ver
  // Fire(). Por isso a assinatura leva um callback dentro.
  using Dismiss = std::function<void(std::function<void()>)>;

  ContextMenuRow(const ContextMenuActivity::Item& item, Dismiss on_dismiss)
      : on_pick_(item.on_pick), on_dismiss_(std::move(on_dismiss)) {
    setAxis(brls::Axis::ROW);
    setSize(brls::Size(234.0f, 45.0f));
    setCornerRadius(22.0f);
    setBackgroundColor(kWhite);
    setAlignItems(brls::AlignItems::CENTER);
    // Nomes alinhados a ESQUERDA com recuo fixo, como na referencia do HOME
    // (pedido do dono, 2026-08-17 — centrado era chute). O recuo de 30 deixa
    // o MN-GLIFO, desenhado antes do texto, caber dentro da pilula.
    setJustifyContent(brls::JustifyContent::FLEX_START);
    setPaddingLeft(30.0f);
    setMarginBottom(11.0f);  // passo de 56 = 45 + 11
    setFocusable(true);
    setHideHighlight(true);

    glyph_str_ = item.glyph;

    label_ = new brls::Label();
    label_->setText(item.label);
    label_->setFontSize(19);
    label_->setTextColor(kTextPrimary);
    addView(label_);

    registerClickAction([this](brls::View*) {
      Fire();
      return true;
    });
    addGestureRecognizer(new brls::TapGestureRecognizer(this));
  }

  // Mesma ordem do MessageBox (spec 085 rodada 7): fecha PRIMEIRO e so
  // depois roda a acao, senao o popActivity dela miraria este menu, que
  // ainda estaria no topo da pilha.
  void Fire() {
    if (on_dismiss_) on_dismiss_(on_pick_);
  }

  // MN-SETA: aponta o item em foco, encostada na borda esquerda dele
  // (spec 095). Desenhada aqui, e nao como filho, porque flutua FORA do
  // retangulo da pilula e anima — um filho em layout nao faria nem um nem
  // outro. Mesmo recurso do cursor da caixa.
  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    brls::Box::draw(vg, x, y, w, h, style, ctx);

    // MN-GLIFO desenhado a mao, FORA do layout: o texto de todos os itens e
    // centrado na pilula, e o B se encosta a esquerda do texto sem empurra-lo
    // (pedido do dono, 2026-08-17 — antes o glifo era filho do Box e o "Sair"
    // ficava desalinhado dos demais).
    if (!glyph_str_.empty()) {
      nvgFontFaceId(vg, brls::Application::getDefaultFont());
      nvgFontSize(vg, 19.0f);
      nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
      nvgFillColor(vg, focused_ ? kWhite : kTextPrimary);
      nvgText(vg, label_->getX() - 8.0f, y + h / 2, glyph_str_.c_str(),
              nullptr);
    }

    if (!focused_) return;
    ++tick_;
    // Aponta para a DIREITA, com o bico AVANCANDO por cima da borda esquerda
    // da pilula, como na referencia do HOME (pedido do dono, 2026-08-17 —
    // antes o bico parava encostado na borda, sem sobrepor).
    Selector::Draw(vg, Selector::Variant::kCustom, x + 14.0f, y + h / 2, h,
                   tick_, kSelectorTeal);
  }

  void onFocusGained() override {
    brls::Box::onFocusGained();
    focused_ = true;
    setBackgroundColor(kMenuAmber);
    label_->setTextColor(kWhite);
  }

  void onFocusLost() override {
    brls::Box::onFocusLost();
    focused_ = false;
    setBackgroundColor(kWhite);
    label_->setTextColor(kTextPrimary);
  }

  brls::View* getDefaultFocus() override { return this; }

 private:
  std::function<void()> on_pick_;
  Dismiss on_dismiss_;
  bool focused_ = false;
  // Contagem propria da flutuacao: duas setas em telas diferentes nao podem
  // pulsar em sincronia (regra da spec 043).
  mutable unsigned tick_ = 0;
  brls::Label* label_ = nullptr;
  std::string glyph_str_;
};

brls::View* ContextMenuActivity::createContentView() {
  auto* root = new brls::Box(brls::Axis::ROW);
  // MN-VEU: escurece a caixa atras sem esconde-la.
  root->setBackgroundColor(nvgRGBA(0x3C, 0x4C, 0x44, 60));

  // MN-PAINEL: cinza escuro TRANSLUCIDO, como nas fotos do HOME — os sprites
  // e o card de tras aparecem esmaecidos atraves dele. Substitui o opaco da
  // folha por pedido do dono (2026-08-17). Cor a olho da referencia; ajuste
  // fino fica para a conferencia visual.
  auto* panel = new brls::Box(brls::Axis::COLUMN);
  panel->setBackgroundColor(nvgRGBA(0x3C, 0x3C, 0x3C, 110));
  panel->setCornerRadius(10.0f);
  panel->setAlignItems(brls::AlignItems::CENTER);
  panel->setJustifyContent(brls::JustifyContent::CENTER);
  // 20 de padding em volta (pedido do dono), MENOS a esquerda: ali a MN-SETA
  // do item em foco fica fora da pilula e precisa de espaco para nao ser
  // cortada pela borda do painel.
  //
  // A seta de menu tem 60 de altura fixa (spec 093) contra os 45 da pilula,
  // entao ela transborda ~8 para cada lado — o padding vertical de 20 ja
  // cobre isso no primeiro e no ultimo item.
  panel->setPadding(20, 20, 20, 46);

  // Ancoragem no slot (spec 096), regra mapeada das fotos do HOME:
  // o painel abre colado a direita do slot e a PRIMEIRA pilula fica centrada
  // na fileira dele; o topo e clampado para o menu inteiro caber na tela —
  // em fileiras baixas o menu para e so a plaqueta segue o Pokemon.
  //
  // Medidas derivadas das constantes acima: largura = 46 + 234 + 20;
  // altura = 20 + n*56 + 20 (pilula 45 + margem 11 por item).
  const float menu_w = 46.0f + 234.0f + 20.0f;
  const float menu_h = 40.0f + static_cast<float>(items_.size()) * 56.0f;
  const float gap = 8.0f;
  const float margin = 16.0f;
  // O HOME nao tem o caso "nao cabe a direita" (uma caixa so); aqui, com
  // dois paineis, o menu espelha para a esquerda do slot (TD-01 da spec).
  float left = anchor_.getMaxX() + gap;
  if (left + menu_w > brls::Application::contentWidth - margin)
    left = anchor_.getMinX() - gap - menu_w;
  float top = anchor_.getMidY() - 20.0f - 45.0f / 2.0f;
  top = std::max(margin,
                 std::min(top, brls::Application::contentHeight - menu_h -
                                   margin));
  panel->setPositionType(brls::PositionType::ABSOLUTE);
  panel->setPositionLeft(left);
  panel->setPositionTop(top);

  brls::View* first = nullptr;
  auto dismiss = [](std::function<void()> action) {
    brls::Application::popActivity(brls::TransitionAnimation::FADE,
                                   action ? action : [] {});
  };
  for (const Item& it : items_) {
    auto* row = new ContextMenuRow(it, dismiss);
    // B fecha o menu, como o item "Sair". Registrado em cada linha porque
    // o handleAction sobe por getParent() e para no root desta Activity.
    row->registerAction(
        "Voltar", brls::BUTTON_B,
        [](brls::View*) {
          brls::Application::popActivity();
          return true;
        },
        false, false, brls::SOUND_BACK);
    panel->addView(row);
    if (!first) first = row;
  }

  root->addView(panel);
  if (first) panel->setLastFocusedView(first);
  return root;
}

// Abre o menu de contexto. O ultimo item recebe o glifo de B — e o "Sair",
// que faz o mesmo que apertar B.
void ShowContextMenu(
    brls::Rect anchor,
    std::vector<std::pair<std::string, std::function<void()>>> items) {
  std::vector<ContextMenuActivity::Item> out;
  out.reserve(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) {
    const bool last = i + 1 == items.size();
    out.push_back({std::move(items[i].first), std::move(items[i].second),
                   last ? kGlyphB : ""});
  }
  brls::Application::pushActivity(
      new ContextMenuActivity(anchor, std::move(out)));
}

// Declarados la em cima, perto do resto das confirmacoes.
void ConfirmDialog(const std::string& text, const std::string& cancel_label,
                   const std::string& confirm_label,
                   std::function<void()> on_confirm) {
  // Cancelar primeiro: fica embaixo, colado na barra, e recebe o foco inicial.
  // Os glifos anunciam o par A/B do console: A confirma, B cancela.
  MessageBox::Show(text, {{cancel_label, nullptr, kGlyphB},
                          {confirm_label, std::move(on_confirm), kGlyphA}});
}

void NoticeDialog(const std::string& text, const std::string& button_label,
                  std::function<void()> on_close, bool cancelable) {
  // Botao unico: e o de acao, entao leva o glifo de A.
  MessageBox::Show(text, {{button_label, std::move(on_close), kGlyphA}},
                   cancelable);
}

// Botao grande do menu: nome em cima, contador numa pilula embaixo. Em foco
// fica ambar com uma seta a esquerda, como no Home.
class MenuButton : public brls::Box {
 public:
  using Callback = std::function<void()>;

  MenuButton(const std::string& title, const std::string& counterLabel,
             std::size_t count, Callback on_select, Callback on_focus)
      : on_select_(std::move(on_select)), on_focus_(std::move(on_focus)) {
    setAxis(brls::Axis::COLUMN);
    setHeight(96);
    setCornerRadius(48);  // metade da altura: pilula, nao elipse
    setJustifyContent(brls::JustifyContent::CENTER);
    setAlignItems(brls::AlignItems::CENTER);
    setPadding(0, 32, 0, 32);
    setMarginBottom(14);
    setBackgroundColor(kWhite);
    setFocusable(true);
    setHideHighlight(true);  // o realce e a cor de fundo, nao o contorno

    title_ = new brls::Label();
    title_->setText(title);
    title_->setFontSize(27);
    title_->setTextColor(kTextPrimary);
    addView(title_);

    // Pilula do contador. Na referencia ela ocupa quase toda a largura do
    // botao e traz uma pokebola a esquerda do texto.
    counter_ = new PillWithBall();
    counter_->setAxis(brls::Axis::ROW);
    counter_->setHeight(38);
    counter_->setCornerRadius(19);
    counter_->setMarginTop(4);
    counter_->setWidthPercentage(84.0f);
    // Espaco a esquerda para a pokebola, que e desenhada no draw().
    counter_->setPadding(0, 20, 0, 20);
    counter_->setAlignItems(brls::AlignItems::CENTER);
    counter_->setJustifyContent(brls::JustifyContent::CENTER);
    counter_->setBackgroundColor(kPillTeal);

    counterText_ = new brls::Label();
    counterText_->setText(counterLabel + ": " + std::to_string(count));
    counterText_->setFontSize(20);
    counterText_->setTextColor(kWhite);
    // Desloca o texto para abrir espaco a esquerda para a pokebola; o
    // conjunto (bola + texto) fica centrado na pilula.
    counterText_->setMarginLeft(30);

    counter_->setTextView(counterText_);
    counter_->addView(counterText_);
    addView(counter_);

    // registerClickAction, nao registerAction(BUTTON_A): o borealis ja trata A
    // como clique na View focada, e registrar o botao direto nao dispara.
    registerClickAction([this](brls::View*) {
      if (on_select_) on_select_();
      return true;
    });
    // O construtor simples de TapGestureRecognizer da foco no tap e dispara a
    // acao BUTTON_A ja registrada acima — cobre toque sem duplicar logica.
    addGestureRecognizer(new brls::TapGestureRecognizer(this));
  }

  void onFocusGained() override {
    brls::Box::onFocusGained();
    setBackgroundColor(kMenuAmber);
    title_->setTextColor(kWhite);
    counter_->setBackgroundColor(kWhite);
    counterText_->setTextColor(kTextPrimary);
    if (on_focus_) on_focus_();
  }

  void onFocusLost() override {
    brls::Box::onFocusLost();
    setBackgroundColor(kWhite);
    title_->setTextColor(kTextPrimary);
    counter_->setBackgroundColor(kPillTeal);
    counterText_->setTextColor(kWhite);
    // Desloca o texto para abrir espaco a esquerda para a pokebola; o
    // conjunto (bola + texto) fica centrado na pilula.
    counterText_->setMarginLeft(30);
  }

  // A seta que aponta para o botao em foco fica fora dele, no container.
  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    DrawSoftShadow(vg, x, y, w, h, h / 2);
    brls::Box::draw(vg, x, y, w, h, style, ctx);
    if (!isFocused()) return;

    // Seletor primary (spec 047) no lugar da seta nanovg da spec 043.
    ++tick_;
    Selector::Draw(vg, Selector::Variant::kPrimary, x + 10, y + h / 2,
                   44.0f, tick_);
  }

  brls::View* getDefaultFocus() override { return this; }

 private:
  Callback on_select_, on_focus_;
  // mutable: draw() e const-ish no uso, mas a animacao precisa avancar.
  mutable unsigned tick_ = 0;
  brls::Label* title_ = nullptr;
  brls::Label* counterText_ = nullptr;
  PillWithBall* counter_ = nullptr;
};

// Botao redondo da grade inferior. Reservado nesta spec: desenhado, sem acao.
// Fica visivelmente inativo — botao que parece clicavel e nao faz nada engana.
// Card do preview: cabecalho verde-agua com cantos de cima arredondados e
// corpo mais claro. Como o cabecalho e o corpo tem raios diferentes, o desenho
// e proprio — setCornerRadius do borealis e uniforme.
class PreviewCard : public brls::Box {
 public:
  // Os tracos laterais do cabecalho sao do card do menu; na tela de selecao o
  // cabecalho e liso, como na referencia.
  void setShowDashes(bool show) { dashes_ = show; }

  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    const float radius = 18.0f;
    DrawSoftShadow(vg, x, y, w, h, radius);

    // Corpo.
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, radius);
    nvgFillColor(vg, nvgRGBA(0xBF, 0xE6, 0xDC, 235));
    nvgFill(vg);

    // Cabecalho: so os cantos de cima arredondam.
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x, y, w, kHeaderHeight, radius, radius, 0, 0);
    nvgFillColor(vg, nvgRGB(0x62, 0xC0, 0xB0));
    nvgFill(vg);

    // Tracos laterais ao redor do titulo, como na referencia.
    if (!dashes_) {
      brls::Box::draw(vg, x, y, w, h, style, ctx);
      return;
    }
    const float cy = y + kHeaderHeight / 2;
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 170));
    nvgStrokeWidth(vg, 2.5f);
    for (const float side : {-1.0f, 1.0f}) {
      const float from = x + w / 2 + side * 92;
      const float to = x + w / 2 + side * 128;
      nvgBeginPath(vg);
      nvgMoveTo(vg, from, cy);
      nvgLineTo(vg, to, cy);
      nvgStroke(vg);
    }

    brls::Box::draw(vg, x, y, w, h, style, ctx);
  }

  static constexpr float kHeaderHeight = 46.0f;

 private:
  bool dashes_ = true;
};

class ReservedCircle : public brls::Box {
 public:
  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    // Mesma elevacao dos botoes: os circulos flutuam sobre o fundo.
    DrawSoftShadow(vg, x, y, w, h, w / 2);
    brls::Box::draw(vg, x, y, w, h, style, ctx);
  }
};

brls::Box* MakeReservedCircle() {
  auto* circle = new ReservedCircle();
  circle->setAxis(brls::Axis::ROW);
  circle->setWidth(86);
  circle->setHeight(86);
  circle->setCornerRadius(43);
  circle->setMargins(0, 9, 12, 9);
  circle->setBackgroundColor(kWhite);
  return circle;
}

// --- Varredura dos saves -----------------------------------------------

// Tudo o que a tela de selecao precisa saber sobre um save, ja lido.
struct ScannedSave {
  nestbox::SaveEntry entry;
  std::string trainer, play_time, trainer_id, saved_at;
  // Nome do jogo quando o save e de um formato moderno (spec 082). Vazio no
  // gen3, onde o cartao ja diz de que jogo se trata.
  std::string format;
  std::size_t count = 0;
};

// Tela de varredura: le TODOS os saves antes de abrir a selecao.
//
// A alternativa — ler sob demanda, quando o cartao ganha foco — deixava a
// navegacao travando a cada movimento, porque montar o save data e parsear
// 3 MB nao cabe num frame. Fazendo tudo aqui, com barra de progresso, a tela
// seguinte fica fluida e os detalhes sao confiaveis.
//
// O trabalho e fatiado: um save por frame, como a leitura da LoadingActivity.
class ScanActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"ScanActivity"};

  using DoneCallback =
      std::function<void(std::vector<ScannedSave>, nestbox::User)>;

  explicit ScanActivity(DoneCallback on_done) : on_done_(std::move(on_done)) {}

  brls::View* createContentView() override {
    auto* root = new ScanRoot(this);
    root->setAxis(brls::Axis::COLUMN);
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setAlignItems(brls::AlignItems::CENTER);

    auto* logo = new brls::Image();
    logo->setImageFromFile(std::string(POKEHOME_UI_ASSETS) + "icon.png");
    logo->setWidth(128);
    logo->setHeight(128);
    logo->setCornerRadius(24);
    logo->setMarginBottom(28);
    root->addView(logo);

    status_ = new brls::Label();
    status_->setText("Procurando saves...");
    status_->setFontSize(24);
    status_->setTextColor(kTextSecondary);
    status_->setMarginBottom(20);
    root->addView(status_);

    progress_ = new ProgressBar(460.0f);
    root->addView(progress_);
    return root;
  }

 private:
  class ScanRoot : public GradientBackground {
   public:
    explicit ScanRoot(ScanActivity* owner) : owner_(owner) {}

    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
      owner_->Advance();
      GradientBackground::draw(vg, x, y, w, h, style, ctx);
    }

   private:
    ScanActivity* owner_;
  };

  // Uma etapa por frame: a barra anda de verdade e a UI nao congela.
  void Advance() {
    switch (step_) {
      case 0:  // usuarios do console
        users_ = nestbox::ListUsers();
        if (!users_.empty()) user_ = users_[0];
        SetStatus("Procurando saves...");
        Next(0.10f);
        break;

      case 1:  // saves em arquivo no cartao
        pending_ = nestbox::ListFileSaves();
        Next(0.25f);
        break;

      case 2: {  // jogos instalados
        if (!users_.empty()) {
          const auto installed = nestbox::ListInstalledSaves(users_[0]);
          pending_.insert(pending_.end(), installed.begin(), installed.end());
        }
        SetStatus("Lendo saves...");
        Next(0.35f);
        break;
      }

      case 3: {  // um save por frame
        if (index_ >= pending_.size()) {
          SetStatus("Pronto");
          progress_->setProgress(1.0f);
          step_ = 4;
          hold_ = 0;
          break;
        }

        Read(pending_[index_]);
        ++index_;
        progress_->setProgress(
            0.35f + 0.65f * float(index_) / float(pending_.size()));
        break;
      }

      case 4:  // segura um instante para a barra cheia ser vista
        if (++hold_ >= 10 && !finished_) {
          finished_ = true;
          brls::Application::popActivity();
          if (on_done_) on_done_(std::move(result_), user_);
        }
        break;
    }
  }

  void Read(const nestbox::SaveEntry& entry) {
    ScannedSave out;
    out.entry = entry;

    std::vector<std::uint8_t> file;
    if (entry.origin == nestbox::SaveOrigin::kFile) {
      file = ReadFile(entry.path);

      // O formato gen3 nao guarda data de save; o mtime do arquivo e a
      // aproximacao honesta disponivel.
      struct stat st {};
      if (stat(entry.path.c_str(), &st) == 0) {
        std::tm tm {};
        const std::time_t t = st.st_mtime;
#ifdef _WIN32
        const bool ok = localtime_s(&tm, &t) == 0;
#else
        const bool ok = localtime_r(&t, &tm) != nullptr;
#endif
        char buf[32];
        if (ok && std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &tm) > 0) {
          out.saved_at = buf;
        }
      }
    } else if (entry.origin == nestbox::SaveOrigin::kInstalled &&
               entry.supported) {
      file = nestbox::ReadInstalledSave(entry, user_);
    }

    if (!file.empty()) {
      if (const auto za = za::ParseZaSave(file)) {
        out.trainer = za->trainer.name;
        out.trainer_id = std::to_string(za->trainer.tid);
        const std::uint32_t secs = za->trainer.play_seconds;
        if (secs > 0) {
          char buf[16];
          std::snprintf(buf, sizeof(buf), "%u:%02u", secs / 3600,
                        (secs % 3600) / 60);
          out.play_time = buf;
        }
        out.count = za->count;
      } else if (const auto save = g3::ParseSave(file)) {
        if (const auto tr = g3::ReadTrainerInfo(file, *save)) {
          out.trainer = tr->name;
          out.trainer_id = std::to_string(tr->public_id);
          char buf[16];
          std::snprintf(buf, sizeof(buf), "%u:%02u", tr->hours, tr->minutes);
          out.play_time = buf;
        }
        const auto pc = g3::BuildPcBuffer(file, *save);
        for (std::size_t b = 0; b < g3::kBoxCount; ++b) {
          for (std::size_t sl = 0; sl < g3::kSlotsPerBox; ++sl) {
            const auto mon = g3::ReadBoxPokemonFrom(pc, b, sl);
            if (mon && !mon->empty()) ++out.count;
          }
        }
      } else if (const auto modern = savew::Load(file)) {
        // Os saves modernos (spec 082). Sem treinador: o savew le as caixas,
        // nao o bloco do jogador.
        out.format = savew::GameName(modern->game);
        out.count = modern->Count();
      }
    }

    result_.push_back(std::move(out));
  }

  void Next(float progress) {
    ++step_;
    if (progress_) progress_->setProgress(progress);
  }

  void SetStatus(const char* text) {
    if (status_) status_->setText(text);
  }

  DoneCallback on_done_;
  std::vector<nestbox::User> users_;
  nestbox::User user_;
  std::vector<nestbox::SaveEntry> pending_;
  std::vector<ScannedSave> result_;
  std::size_t index_ = 0;
  int step_ = 0, hold_ = 0;
  bool finished_ = false;
  brls::Label* status_ = nullptr;
  ProgressBar* progress_ = nullptr;
};

// --- Selecao de usuario e save ---------------------------------------------

// Qual jogo a arte deve mostrar, a partir do nome do save ou do titulo do
// jogo instalado. E heuristica: o conteudo do save nao distingue FireRed de
// LeafGreen (mesmo game code), entao o nome e o que temos.
//
// A ordem importa: chaves mais longas primeiro, senao "pokemon-red" casaria
// antes de "pokemon-firered".
std::string GameSlug(const std::string& title) {
  std::string t;
  for (char c : title) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      t.push_back(static_cast<char>(std::tolower(c)));
    }
  }
  if (t.empty()) return "";

  struct Entry {
    const char* needle;
    const char* slug;
  };
  // Chaves sem espacos nem pontuacao, para casar "Pokémon Legends: Z-A",
  // "pokemon-legends-z-a.sav" e "PokemonLegendsZA" do mesmo jeito.
  static constexpr Entry kTable[] = {
      {"legendsza", "pokemon-legends-z-a"},
      {"legendsarceus", "pokemon-legends-arceus"},
      {"brilliantdiamond", "pokemon-brilliant-diamond"},
      {"shiningpearl", "pokemon-shining-pearl"},
      {"letsgopikachu", "pokemon-lets-go-pikachu"},
      {"letsgoeevee", "pokemon-lets-go-eevee"},
      {"omegaruby", "pokemon-omega-ruby"},
      {"alphasapphire", "pokemon-alpha-sapphire"},
      {"ultrasun", "pokemon-ultra-sun"},
      {"ultramoon", "pokemon-ultra-moon"},
      {"heartgold", "pokemon-heartgold"},
      {"soulsilver", "pokemon-soulsilver"},
      {"firered", "pokemon-firered"},
      {"leafgreen", "pokemon-leafgreen"},
      {"black2", "pokemon-black-version-2"},
      {"white2", "pokemon-white-version-2"},
      {"blackversion2", "pokemon-black-version-2"},
      {"whiteversion2", "pokemon-white-version-2"},
      {"emerald", "pokemon-emerald"},
      {"sapphire", "pokemon-sapphire"},
      {"platinum", "pokemon-platinum"},
      {"scarlet", "pokemon-scarlet"},
      {"violet", "pokemon-violet"},
      {"crystal", "pokemon-crystal"},
      {"diamond", "pokemon-diamond"},
      {"shield", "pokemon-shield"},
      {"silver", "pokemon-silver"},
      {"yellow", "pokemon-yellow"},
      {"sword", "pokemon-sword"},
      {"pearl", "pokemon-pearl"},
      {"black", "pokemon-black-version"},
      {"white", "pokemon-white-version"},
      {"green", "pokemon-leafgreen"},
      {"ruby", "pokemon-ruby"},
      {"gold", "pokemon-gold"},
      {"blue", "pokemon-blue"},
      {"moon", "pokemon-moon"},
      {"home", "pokemon-home"},
      {"red", "pokemon-red"},
      {"sun", "pokemon-sun"},
      {"za", "pokemon-legends-z-a"},
      {"x", "pokemon-x"},
      {"y", "pokemon-y"},
  };
  for (const Entry& e : kTable) {
    if (t.find(e.needle) != std::string::npos) return e.slug;
  }
  return "";
}

std::string CoverPath(const std::string& slug) {
  return std::string(POKEHOME_UI_ASSETS) + "games/" + slug + "_cover.png";
}

std::string LogoPath(const std::string& slug) {
  return std::string(POKEHOME_UI_ASSETS) + "games/" + slug + "_logo.png";
}

// Cartao de um save, como no Home: SO o icone, numa moldura branca. Nome e
// detalhes nao ficam no cartao — aparecem no painel de informacoes a esquerda
// quando o cartao ganha foco.
class SaveCard : public brls::Box {
 public:
  SaveCard(const nestbox::SaveEntry& entry, std::function<void()> on_select,
           std::function<void()> on_focus)
      : entry_(entry),
        on_select_(std::move(on_select)),
        on_focus_(std::move(on_focus)) {
    setAxis(brls::Axis::COLUMN);
    setWidth(132);
    setHeight(132);
    setCornerRadius(20);
    setShrink(0.0f);
    setMargins(0, 12, 14, 12);
    setPadding(10, 10, 10, 10);
    setJustifyContent(brls::JustifyContent::CENTER);
    setAlignItems(brls::AlignItems::CENTER);
    setBackgroundColor(kWhite);
    setFocusable(true);
    setHideHighlight(true);

    std::string iconPath = entry.icon_path;
    if (entry.origin == nestbox::SaveOrigin::kNestBox) {
      iconPath = std::string(POKEHOME_UI_ASSETS) + "icon.png";
    } else {
      // A capa do jogo tem precedencia sobre o icone do console: e a mesma
      // arte nas duas plataformas, e o PC nao tem icone nenhum.
      const std::string slug = GameSlug(entry.title);
      if (!slug.empty()) iconPath = CoverPath(slug);
    }

    if (!iconPath.empty()) {
      auto* icon = new brls::Image();
      icon->setImageFromFile(iconPath);
      icon->setWidth(108);
      icon->setHeight(108);
      icon->setCornerRadius(14);
      addView(icon);
    } else {
      // Save em arquivo nao tem icone de jogo; o nome curto centralizado e o
      // que identifica o cartao ate existir uma arte propria.
      auto* label = new brls::Label();
      std::string title = entry_.title;
      if (title.size() > 20) title = title.substr(0, 19) + "…";
      label->setText(title);
      label->setFontSize(16);
      label->setTextColor(kTextPrimary);
      label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
      label->setWidth(104);
      addView(label);
    }

    registerClickAction([this](brls::View*) {
      if (!entry_.supported || !on_select_) return true;
      // Confirma antes de carregar: o cartao errado custa reabrir a tela, e a
      // leitura de um save grande demora. Mostra o nome para o usuario
      // perceber se escolheu o que queria (spec 020).
      ConfirmDialog("Abrir " + entry_.title + "?", "Cancelar", "Abrir",
                    [cb = on_select_] { cb(); });
      return true;
    });
    addGestureRecognizer(new brls::TapGestureRecognizer(this));
  }

  void draw(NVGcontext* vg, float x, float y, float w, float h,
            brls::Style style, brls::FrameContext* ctx) override {
    DrawSoftShadow(vg, x, y, w, h, 20);
    brls::Box::draw(vg, x, y, w, h, style, ctx);
    if (!isFocused()) return;

    // O mesmo seletor primary do menu, apontando para o cartao em foco.
    ++tick_;
    Selector::Draw(vg, Selector::Variant::kPrimary, x + 8, y + h / 2,
                   40.0f, tick_);
  }

  void onFocusGained() override {
    brls::Box::onFocusGained();
    setBackgroundColor(kMenuAmber);
    if (on_focus_) on_focus_();
    // A linha guarda a coluna para que a navegacao vertical a preserve.
    if (auto* row = dynamic_cast<GridRow*>(getParent())) {
      row->RememberColumn(this);
    }
  }

  void onFocusLost() override {
    brls::Box::onFocusLost();
    setBackgroundColor(kWhite);
  }

 private:
  nestbox::SaveEntry entry_;
  std::function<void()> on_select_, on_focus_;
  mutable unsigned tick_ = 0;
};

// Tela entre o menu e as caixas. A esquerda, as informacoes do save em foco;
// a direita, o usuario e a grade de cartoes.
class UserSelectActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"UserSelectActivity"};

  using PickCallback =
      std::function<void(const nestbox::SaveEntry&, const nestbox::User&)>;

  // Recebe a varredura pronta da ScanActivity: nenhuma leitura de save
  // acontece aqui, entao a navegacao nao trava.
  UserSelectActivity(std::vector<ScannedSave> scanned, nestbox::User user,
                     PickCallback on_pick)
      : on_pick_(std::move(on_pick)), scanned_(std::move(scanned)) {
    if (!user.name.empty()) users_.push_back(user);

    // A box do NestBox vem sempre, em primeiro — e o cofre do app, como o
    // cartao do HOME na tela equivalente do jogo oficial.
    ScannedSave home;
    home.entry.origin = nestbox::SaveOrigin::kNestBox;
    home.entry.title = "NestBox";
    home.entry.supported = true;
    scanned_.insert(scanned_.begin(), home);

    for (const auto& sc : scanned_) saves_.push_back(sc.entry);
  }

  brls::View* createContentView() override {
    auto* root = new GradientBackground();
    root->setHeaderBand(58);
    root->setAxis(brls::Axis::COLUMN);

    root->addView(MakeTopBar());
    root->addView(MakeSubtitle());

    auto* body = new brls::Box(brls::Axis::ROW);
    body->setGrow(1.0f);
    body->setPadding(20, 56, 24, 56);
    body->addView(MakeInfoPanel());
    body->addView(MakeRightColumn());
    root->addView(body);
    root->addView(MakeFooter());

    root->registerAction(
        "Voltar", brls::BUTTON_B,
        [](brls::View*) {
          brls::Application::popActivity();
          return true;
        },
        false);

    if (firstCard_) brls::Application::giveFocus(firstCard_);
    return root;
  }

 private:
  brls::Box* MakeFooter() { return MakeLegendBar(/*back=*/true); }

  brls::Box* MakeTopBar() {
    auto* bar = new brls::Box(brls::Axis::ROW);
    bar->setHeight(58);
    bar->setAlignItems(brls::AlignItems::CENTER);
    bar->setPadding(0, 40, 0, 40);

    auto* icon = new brls::Image();
    icon->setImageFromFile(std::string(POKEHOME_UI_ASSETS) + "icon.png");
    icon->setWidth(40);
    icon->setHeight(40);
    icon->setCornerRadius(9);
    icon->setMarginRight(14);
    bar->addView(icon);

    auto* title = new brls::Label();
    title->setText("POKÉMON");  // acento como no HOME (spec 105)
    title->setFontSize(24);
    title->setTextColor(kTextPrimary);
    bar->addView(title);
    return bar;
  }

  brls::Box* MakeSubtitle() {
    auto* row = new SubtitleBand();
    row->setAxis(brls::Axis::ROW);
    row->setHeight(60);
    row->setJustifyContent(brls::JustifyContent::CENTER);
    row->setAlignItems(brls::AlignItems::CENTER);

    auto* label = new brls::Label();
    label->setText("Escolha um save para abrir");
    label->setFontSize(26);
    label->setTextColor(kTextSecondary);
    row->addView(label);
    return row;
  }

  // Painel de informacoes do save em foco, como na referencia: cabecalho com
  // o nome, icone no meio e a tabela de dados embaixo.
  brls::Box* MakeInfoPanel() {
    auto* card = new PreviewCard();
    card->setShowDashes(false);
    card->setAxis(brls::Axis::COLUMN);
    // Mesma largura do painel do menu principal: as duas telas se seguem, e
    // paineis de larguras diferentes fazem a transicao "pular".
    card->setWidthPercentage(46.0f);
    card->setShrink(0.0f);
    card->setMarginRight(44);

    auto* header = new brls::Box(brls::Axis::ROW);
    header->setHeight(PreviewCard::kHeaderHeight);
    header->setJustifyContent(brls::JustifyContent::CENTER);
    header->setAlignItems(brls::AlignItems::CENTER);

    infoTitle_ = new brls::Label();
    infoTitle_->setText("NestBox");
    infoTitle_->setFontSize(21);
    infoTitle_->setTextColor(kWhite);
    header->addView(infoTitle_);
    card->addView(header);

    auto* body = new brls::Box(brls::Axis::COLUMN);
    body->setGrow(1.0f);
    body->setAlignItems(brls::AlignItems::CENTER);
    body->setPadding(18, 22, 18, 22);

    // Dois spacers iguais em volta do icone: a tabela continua presa no pe do
    // painel (o de baixo nao empurra ela) e o icone fica centrado na folga que
    // sobra entre o topo e a tabela. Com um spacer so, toda a folga ficava
    // acima e o icone descia colado na tabela.
    auto* spacerTop = new brls::Box();
    spacerTop->setGrow(1.0f);
    body->addView(spacerTop);

    // A largura fica por conta do SetInfo: depende de a fonte ter logo larga
    // ou icone quadrado, e e reescrita a cada troca de save.
    infoIcon_ = new brls::Image();
    body->addView(infoIcon_);

    auto* spacerBottom = new brls::Box();
    spacerBottom->setGrow(1.0f);
    spacerBottom->setMinHeight(32);  // nunca encosta na tabela
    body->addView(spacerBottom);

    // Tabela em duas cores, como na referencia: a chave num bloco verde-agua
    // com texto branco, o valor num bloco claro com texto escuro.
    for (int i = 0; i < kInfoRows; ++i) {
      auto* row = new brls::Box(brls::Axis::ROW);
      row->setHeight(36);
      row->setWidthPercentage(100.0f);
      row->setMarginBottom(3);

      auto* keyCell = new brls::Box(brls::Axis::ROW);
      keyCell->setWidth(170);
      keyCell->setShrink(0.0f);
      keyCell->setAlignItems(brls::AlignItems::CENTER);
      keyCell->setJustifyContent(brls::JustifyContent::CENTER);
      keyCell->setBackgroundColor(nvgRGB(0x57, 0xB2, 0xA4));

      auto* key = new brls::Label();
      key->setFontSize(16);
      key->setTextColor(kWhite);
      keyCell->addView(key);
      row->addView(keyCell);

      auto* valueCell = new brls::Box(brls::Axis::ROW);
      valueCell->setGrow(1.0f);
      valueCell->setAlignItems(brls::AlignItems::CENTER);
      valueCell->setBackgroundColor(nvgRGBA(255, 255, 255, 200));

      auto* value = new brls::Label();
      value->setFontSize(16);
      value->setTextColor(kTextPrimary);
      value->setMarginLeft(14);
      valueCell->addView(value);
      row->addView(valueCell);

      infoKeys_[i] = key;
      infoValues_[i] = value;
      infoRows_[i] = row;
      body->addView(row);
    }

    card->addView(body);
    return card;
  }

  brls::Box* MakeRightColumn() {
    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setGrow(1.0f);

    // Pilula do usuario, como o "Noga" da referencia.
    auto* pill = new brls::Box(brls::Axis::ROW);
    pill->setHeight(58);
    pill->setCornerRadius(29);
    pill->setBackgroundColor(kWhite);
    pill->setAlignItems(brls::AlignItems::CENTER);
    pill->setJustifyContent(brls::JustifyContent::CENTER);
    pill->setMarginBottom(20);

    auto* name = new brls::Label();
    name->setText(users_.empty() ? "Console" : users_[0].name);
    name->setFontSize(22);
    name->setTextColor(kTextPrimary);
    pill->addView(name);
    col->addView(pill);

    // Grade de 3 colunas. As linhas precisam ter LARGURA definida: sem isso o
    // borealis navegava por uma geometria diferente da desenhada — as setas
    // pulavam para cartoes que nao estavam ao lado do foco.
    constexpr int kPerRow = 3;
    constexpr int kCardSlot = 132 + 24;  // cartao + margens
    GridRow* line = nullptr;
    for (std::size_t i = 0; i < saves_.size(); ++i) {
      if (i % kPerRow == 0) {
        line = new GridRow(gridColumn_);
        line->setAxis(brls::Axis::ROW);
        line->setAlignItems(brls::AlignItems::FLEX_START);
        line->setJustifyContent(brls::JustifyContent::FLEX_START);
        line->setHeight(156);
        line->setWidth(kCardSlot * kPerRow);
        line->setShrink(0.0f);
        col->addView(line);
      }
      const nestbox::SaveEntry entry = saves_[i];
      auto* card = new SaveCard(
          entry,
          [this, entry] {
            if (on_pick_) {
              on_pick_(entry, users_.empty() ? nestbox::User{} : users_[0]);
            }
          },
          [this, entry] { ShowInfo(entry); });
      line->addView(card);
      if (!firstCard_ && entry.supported) firstCard_ = card;
    }
    return col;
  }

  // Preenche o painel esquerdo com o que se sabe do save em foco.
  void ShowInfo(const nestbox::SaveEntry& entry) {
    if (infoTitle_) infoTitle_->setText(entry.title);

    std::string iconPath = entry.icon_path;
    bool wide = false;  // logos sao largos; icones, quadrados
    if (entry.origin == nestbox::SaveOrigin::kNestBox) {
      iconPath = std::string(POKEHOME_UI_ASSETS) + "icon.png";
    } else {
      const std::string slug = GameSlug(entry.title);
      if (!slug.empty()) {
        iconPath = LogoPath(slug);
        wide = true;
      }
    }
    if (infoIcon_) {
      // O tamanho e definido aqui, nao no MakeInfoPanel: a cada troca de save
      // a logo pode ser larga ou quadrada, e o valor e reescrito.
      // 300x128 e a proporcao exata do canvas gerado pelo prepare-game-art.py:
      // com FIT, caixa de proporcao diferente sobraria margem inutil. O icone
      // do NestBox e quadrado, mas na mesma altura das logos — assim a troca
      // de save nao muda a altura do bloco.
      infoIcon_->setWidth(wide ? 300 : 128);
      infoIcon_->setHeight(128);
      infoIcon_->setCornerRadius(wide ? 0 : 19);
    }
    if (infoIcon_) {
      if (!iconPath.empty()) {
        infoIcon_->setImageFromFile(iconPath);
        infoIcon_->setVisibility(brls::Visibility::VISIBLE);
      } else {
        infoIcon_->setVisibility(brls::Visibility::INVISIBLE);
      }
    }

    switch (entry.origin) {
      case nestbox::SaveOrigin::kNestBox:
        SetRow(0, "Nome", "NestBox");
        SetRow(1, "Tempo de jogo", "—");
        SetRow(2, "Nº de ID", "—");
        SetRow(3, "Pokémon", "0 / " + std::to_string(kNestBoxCapacity));
        SetRow(4, "Salvo pela última vez em", "—");
        break;

      case nestbox::SaveOrigin::kFile: {
        const ScannedSave& info = ScannedFor(entry);
        // Mesma tabela para todo jogo. Campo que o formato nao guarda fica
        // com travessao — melhor que sumir a linha ou inventar valor.
        SetRow(0, "Nome", info.trainer.empty() ? "—" : info.trainer);
        SetRow(1, "Tempo de jogo",
               info.play_time.empty() ? "—" : info.play_time);
        SetRow(2, "Nº de ID", info.trainer_id.empty() ? "—" : info.trainer_id);
        SetRow(3, "Pokémon", std::to_string(info.count));
        SetRow(4, "Salvo pela última vez em",
               info.saved_at.empty() ? "—" : info.saved_at);
        break;
      }

      case nestbox::SaveOrigin::kInstalled: {
        const ScannedSave& info = ScannedFor(entry);
        SetRow(0, "Nome", info.trainer.empty() ? "—" : info.trainer);
        SetRow(1, "Tempo de jogo",
               info.play_time.empty() ? "—" : info.play_time);
        SetRow(2, "Nº de ID", info.trainer_id.empty() ? "—" : info.trainer_id);
        SetRow(3, "Pokémon",
               entry.supported ? std::to_string(info.count) : "não suportado");
        SetRow(4, "Salvo pela última vez em",
               info.saved_at.empty() ? "—" : info.saved_at);
        break;
      }
    }
  }

  void SetRow(int i, const std::string& key, const std::string& value) {
    if (i < 0 || i >= kInfoRows) return;
    if (infoKeys_[i]) infoKeys_[i]->setText(key);
    if (infoValues_[i]) infoValues_[i]->setText(value);
    // Linha sem chave some — um bloco verde vazio pareceria defeito.
    if (infoRows_[i]) {
      infoRows_[i]->setVisibility(key.empty() ? brls::Visibility::INVISIBLE
                                              : brls::Visibility::VISIBLE);
    }
  }

  // Dados de um save em arquivo. Parsear custa pouco, mas nao a cada mudanca
  // de foco — cache por caminho.
  struct FileInfo {
    std::string trainer, play_time, trainer_id;
    // Vazio para gen3; preenchido quando o save e de outra geracao.
    std::string format;
    // Data de modificacao do ARQUIVO: o save gen3 nao guarda quando foi
    // salvo, entao esta e a melhor aproximacao honesta que existe.
    std::string saved_at;
    std::size_t count = 0;
  };

  // O dado ja veio pronto da varredura — so localizar.
  const ScannedSave& ScannedFor(const nestbox::SaveEntry& entry) {
    static const ScannedSave kEmpty;
    for (const auto& sc : scanned_) {
      if (sc.entry.title == entry.title &&
          sc.entry.origin == entry.origin) {
        return sc;
      }
    }
    return kEmpty;
  }

  const FileInfo& InstalledInfoFor(const nestbox::SaveEntry& entry) {
    auto it = fileInfo_.find(entry.title);
    if (it != fileInfo_.end()) return it->second;

    FileInfo info;
    if (entry.supported) {
      const auto file = nestbox::ReadInstalledSave(
          entry, users_.empty() ? nestbox::User{} : users_[0]);
      if (!file.empty()) {
        if (const auto za = za::ParseZaSave(file)) {
          info.trainer = za->trainer.name;
          info.trainer_id = std::to_string(za->trainer.tid);
          const std::uint32_t secs = za->trainer.play_seconds;
          if (secs > 0) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%u:%02u", secs / 3600,
                          (secs % 3600) / 60);
            info.play_time = buf;
          }
          info.count = za->count;
        } else if (const auto save = g3::ParseSave(file)) {
          if (const auto tr = g3::ReadTrainerInfo(file, *save)) {
            info.trainer = tr->name;
            info.trainer_id = std::to_string(tr->public_id);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%u:%02u", tr->hours, tr->minutes);
            info.play_time = buf;
          }
          const auto pc = g3::BuildPcBuffer(file, *save);
          for (std::size_t b = 0; b < g3::kBoxCount; ++b) {
            for (std::size_t sl = 0; sl < g3::kSlotsPerBox; ++sl) {
              const auto mon = g3::ReadBoxPokemonFrom(pc, b, sl);
              if (mon && !mon->empty()) ++info.count;
            }
          }
        } else if (const auto modern = savew::Load(file)) {
          // Os cinco saves modernos (spec 082). O treinador nao entra: o
          // `savew` le as CAIXAS, e nao o bloco do jogador — inventar um nome
          // aqui seria pior que deixar em branco.
          info.format = savew::GameName(modern->game);
          info.count = modern->Count();
        }
      }
    }
    return fileInfo_.emplace(entry.title, std::move(info)).first->second;
  }

  const FileInfo& FileInfoFor(const std::string& path) {
    auto it = fileInfo_.find(path);
    if (it != fileInfo_.end()) return it->second;

    FileInfo info;

    // mtime do arquivo — o formato gen3 nao tem campo de data.
    struct stat st {};
    if (stat(path.c_str(), &st) == 0) {
      std::tm tm {};
      const std::time_t t = st.st_mtime;
#ifdef _WIN32
      const bool ok = localtime_s(&tm, &t) == 0;
#else
      const bool ok = localtime_r(&t, &tm) != nullptr;
#endif
      if (ok) {
        char buf[32];
        if (std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &tm) > 0) {
          info.saved_at = buf;
        }
      }
    }

    const auto file = ReadFile(path);
    if (!file.empty()) {
      if (const auto za_save = za::ParseZaSave(file)) {
        info.format = "Legends Z-A";
        for (std::size_t b = 0; b < za::kBoxCount; ++b) {
          for (std::size_t sl = 0; sl < za::kSlotsPerBox; ++sl) {
            if (za::ReadZaBoxPokemon(*za_save, b, sl)) ++info.count;
          }
        }
      } else if (const auto save = g3::ParseSave(file)) {
        if (const auto trainer = g3::ReadTrainerInfo(file, *save)) {
          info.trainer = trainer->name;
          char buf[16];
          std::snprintf(buf, sizeof(buf), "%u:%02u", trainer->hours,
                        trainer->minutes);
          info.play_time = buf;
          info.trainer_id = std::to_string(trainer->public_id);
        }
        const auto pc = g3::BuildPcBuffer(file, *save);
        for (std::size_t b = 0; b < g3::kBoxCount; ++b) {
          for (std::size_t sl = 0; sl < g3::kSlotsPerBox; ++sl) {
            const auto mon = g3::ReadBoxPokemonFrom(pc, b, sl);
            if (mon && !mon->empty()) ++info.count;
          }
        }
      } else if (const auto modern = savew::Load(file)) {
        info.format = savew::GameName(modern->game);
        info.count = modern->Count();
      }
    }
    return fileInfo_.emplace(path, std::move(info)).first->second;
  }

  static constexpr int kInfoRows = 5;

  PickCallback on_pick_;
  std::vector<nestbox::User> users_;
  std::vector<nestbox::SaveEntry> saves_;
  std::vector<ScannedSave> scanned_;
  // Coluna do foco na grade de cartoes.
  std::shared_ptr<std::size_t> gridColumn_ = std::make_shared<std::size_t>(0);
  std::map<std::string, FileInfo> fileInfo_;
  brls::View* firstCard_ = nullptr;
  brls::Label* infoTitle_ = nullptr;
  brls::Image* infoIcon_ = nullptr;
  brls::Label* infoKeys_[kInfoRows] = {};
  brls::Label* infoValues_[kInfoRows] = {};
  brls::Box* infoRows_[kInfoRows] = {};
};

// Tela inicial. Layout 01: barra superior, subtitulo que reage ao foco, painel
// de preview a esquerda e coluna de botoes a direita.
class MenuActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"MenuActivity"};

  MenuActivity(BoxSource* nest, BoxSource* save) : nest_(nest), save_(save) {
    // Especies distintas no save — o mesmo criterio da DexActivity.
    std::vector<bool> seen(387, false);
    for (std::size_t b = 0; b < g3::kBoxCount; ++b) {
      for (std::size_t s = 0; s < g3::kSlotsPerBox; ++s) {
        const g3::BoxPokemon mon = save_->At(b, s);
        if (mon.empty()) continue;
        const int dex = g3::NationalDex(mon.species);
        if (dex > 0 && dex < static_cast<int>(seen.size()) && !seen[dex]) {
          seen[dex] = true;
          ++registered_;
        }
      }
    }
  }

  brls::View* createContentView() override {
    auto* root = new GradientBackground();
    root->setHeaderBand(58);
    root->setAxis(brls::Axis::COLUMN);

    root->addView(MakeTopBar());
    root->addView(MakeSubtitle());

    auto* body = new brls::Box(brls::Axis::ROW);
    body->setGrow(1.0f);
    body->setPadding(20, 56, 24, 56);
    body->addView(MakePreviewPanel());
    body->addView(MakeButtonColumn());
    root->addView(body);

    root->addView(MakeFooter());

    if (pokemonButton_) brls::Application::giveFocus(pokemonButton_);
    return root;
  }

 private:
  brls::Box* MakeTopBar() {
    auto* bar = new brls::Box(brls::Axis::ROW);
    bar->setHeight(58);
    bar->setAlignItems(brls::AlignItems::CENTER);
    bar->setPadding(0, 40, 0, 40);
    // Sem fundo proprio: a faixa diagonal do GradientBackground e o fundo.

    auto* icon = new brls::Image();
    icon->setImageFromFile(std::string(POKEHOME_UI_ASSETS) + "icon.png");
    icon->setWidth(40);
    icon->setHeight(40);
    icon->setCornerRadius(9);
    icon->setMarginRight(14);
    bar->addView(icon);

    auto* title = new brls::Label();
    title->setText("MENU PRINCIPAL");
    title->setFontSize(24);
    title->setTextColor(kTextPrimary);
    bar->addView(title);

    auto* spacer = new brls::Box();
    spacer->setGrow(1.0f);
    bar->addView(spacer);

    // Hora do sistema. Se o relogio nao responder, o texto fica vazio em vez
    // de mostrar valor errado.
    auto* clock = new brls::Label();
    clock->setText(CurrentTime());
    clock->setFontSize(28);
    clock->setTextColor(kTextSecondary);
    clock->setMarginRight(18);
    bar->addView(clock);

    // Avatar: forma neutra. Ler o perfil do console e dado pessoal, fora do
    // escopo desta spec.
    auto* avatar = new brls::Box();
    avatar->setWidth(44);
    avatar->setHeight(44);
    avatar->setCornerRadius(10);
    avatar->setBackgroundColor(nvgRGBA(255, 255, 255, 190));
    bar->addView(avatar);

    return bar;
  }

  static std::string CurrentTime() {
    const std::time_t now = std::time(nullptr);
    std::tm tm {};
#ifdef _WIN32
    if (localtime_s(&tm, &now) != 0) return "";
#else
    if (!localtime_r(&now, &tm)) return "";
#endif
    char buf[8];
    if (std::strftime(buf, sizeof(buf), "%H:%M", &tm) == 0) return "";
    return buf;
  }

  brls::Box* MakeSubtitle() {
    auto* row = new SubtitleBand();
    row->setAxis(brls::Axis::ROW);
    row->setHeight(60);
    row->setJustifyContent(brls::JustifyContent::CENTER);
    row->setAlignItems(brls::AlignItems::CENTER);
    // Faixa propria com gradiente: na referencia ela nao e chapada, esmaece
    // de cima para baixo ate se dissolver no fundo.

    subtitle_ = new brls::Label();
    subtitle_->setText(kSubtitlePokemon);
    subtitle_->setFontSize(26);
    subtitle_->setTextColor(kTextSecondary);
    row->addView(subtitle_);
    return row;
  }

  // Painel de preview. A imagem da funcionalidade entra numa spec futura; por
  // ora fica o container com o titulo, que e o que o dono pediu.
  brls::Box* MakePreviewPanel() {
    auto* panel = new PreviewCard();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setWidthPercentage(46.0f);
    panel->setShrink(0.0f);
    panel->setMarginRight(44);

    // Cabecalho: so o texto: o fundo vem do draw() do card.
    auto* header = new brls::Box(brls::Axis::ROW);
    header->setHeight(PreviewCard::kHeaderHeight);
    header->setJustifyContent(brls::JustifyContent::CENTER);
    header->setAlignItems(brls::AlignItems::CENTER);

    previewTitle_ = new brls::Label();
    previewTitle_->setText("POKÉMON");
    previewTitle_->setFontSize(23);
    previewTitle_->setTextColor(kWhite);
    header->addView(previewTitle_);
    panel->addView(header);

    // A imagem da funcionalidade entra numa spec futura. Sem moldura por
    // dentro: o corpo do card ja e a area reservada.

    return panel;
  }

  brls::Box* MakeButtonColumn() {
    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setGrow(1.0f);
    col->setShrink(1.0f);

    pokemonButton_ = new MenuButton(
        "POKÉMON", "Depositados", nest_->Count(),
        [this] { OpenBoxes(); }, [this] {
          SetSubtitle(kSubtitlePokemon);
          SetPreviewTitle("POKÉMON");
        });
    col->addView(pokemonButton_);

    dexButton_ = new MenuButton(
        "POKÉDEX", "Registrados", registered_,
        [this] { OpenDex(); }, [this] {
          SetSubtitle(kSubtitleDex);
          SetPreviewTitle("POKÉDEX");
        });
    col->addView(dexButton_);

    // Grade reservada de botoes redondos.
    auto* grid = new brls::Box(brls::Axis::COLUMN);
    grid->setMarginTop(26);
    // 3 em cima, 2 embaixo centralizados — como na referencia.
    for (const int count : {3, 2}) {
      auto* line = new brls::Box(brls::Axis::ROW);
      line->setJustifyContent(brls::JustifyContent::CENTER);
      for (int i = 0; i < count; ++i) line->addView(MakeReservedCircle());
      grid->addView(line);
    }
    col->addView(grid);

    return col;
  }

  // Rodape com as dicas de botao, usando o glifo real do Switch.
  brls::Box* MakeFooter() { return MakeLegendBar(/*back=*/false); }

  void SetSubtitle(const char* text) {
    if (subtitle_) subtitle_->setText(text);
  }

  void SetPreviewTitle(const char* text) {
    if (previewTitle_) previewTitle_->setText(text);
  }

  void OpenBoxes() {
    // Passa pela selecao de save. O save carregado na abertura vira o padrao
    // quando o jogador escolhe outro, ele e lido ali.
    // Varredura primeiro: le todos os saves com barra de progresso, e so
    // entao abre a selecao — que fica fluida porque nao le mais nada.
    brls::Application::pushActivity(new ScanActivity(
        [this](std::vector<ScannedSave> scanned, nestbox::User user) {
          brls::Application::pushActivity(new UserSelectActivity(
              std::move(scanned), user,
              [this](const nestbox::SaveEntry& entry,
                     const nestbox::User& user2) {
          pickedUser_ = user2;
          if (entry.origin == nestbox::SaveOrigin::kNestBox) {
            // A box exclusiva: painel do save vazio, foco no cofre do app.
            brls::Application::pushActivity(
                new BoxActivity(nest_, new EmptyBoxSource()));
            return;
          }

          // Arquivo no SD ou save data de jogo instalado — os jogos NSO de
          // GBA guardam um .sav gen3 cru, que o parser le igual.
          std::vector<std::uint8_t> file;
          std::string label = entry.title;
          ModernSaveSource::Writer writer;
          if (entry.origin == nestbox::SaveOrigin::kFile) {
            file = ReadFile(entry.path);
            label = entry.path;
            writer = [path = entry.path](const std::vector<std::uint8_t>& b) {
              return WriteWholeFile(path, b);
            };
          } else {
            file = nestbox::ReadInstalledSave(entry, pickedUser_);
            // Save data montado para escrita no commit — TD-01 da spec 086,
            // autorizado pelo dono em 2026-08-16.
            writer = [entry, user = pickedUser_](
                         const std::vector<std::uint8_t>& b) {
              return nestbox::WriteInstalledSave(entry, user, b);
            };
          }

          BoxSource* source =
              OpenBoxSource(file, label, entry.title, std::move(writer));
          brls::Application::pushActivity(new BoxActivity(nest_, source));
              }));
        }));
  }

  void OpenDex() {
    // Sem sessao: a movimentacao vive na tela de caixas, e o menu e o ponto de
    // partida dela. As duas fontes, sim — o NestBox conta (spec 026).
    brls::Application::pushActivity(new DexActivity(save_, nest_));
  }

  static constexpr const char* kSubtitlePokemon = "Deposite seus Pokémon";
  static constexpr const char* kSubtitleDex = "Consulte informações";

  BoxSource* nest_;
  BoxSource* save_;
  nestbox::User pickedUser_;
  std::size_t registered_ = 0;
  brls::Label* subtitle_ = nullptr;
  brls::Label* previewTitle_ = nullptr;
  MenuButton* pokemonButton_ = nullptr;
  MenuButton* dexButton_ = nullptr;
};

// --- Tela de carregamento --------------------------------------------------

std::string FindSaveOnDevice() {
#ifdef __SWITCH__
  static const char* kDirs[] = {
      "sdmc:/pokehome/",
      "sdmc:/retroarch/saves/",
      "sdmc:/mgba/",
  };
  static const char* kExts[] = {".sav", ".srm", ".sps"};

  for (const char* dir : kDirs) {
    DIR* d = opendir(dir);
    if (!d) continue;
    std::string found;
    while (dirent* e = readdir(d)) {
      const std::string name = e->d_name;
      for (const char* ext : kExts) {
        const std::size_t n = std::strlen(ext);
        if (name.size() > n && name.compare(name.size() - n, n, ext) == 0) {
          found = std::string(dir) + name;
          break;
        }
      }
      if (!found.empty()) break;
    }
    closedir(d);
    if (!found.empty()) return found;
  }
#endif
  return "";
}

// Fundo verde com o logo e uma barra no centro. E a primeira tela do app e
// cobre os dois momentos de espera: abrir (varrer o SD, ler o save) e
// atualizar (baixar e instalar).
//
// O trabalho e fatiado em etapas, uma por frame, em vez de rodar numa thread:
// o borealis nao e thread-safe para tocar em Views, e sincronizar o resultado
// com o loop de UI custaria mais do que a leitura de 14 caixas (TD-01).
class LoadingActivity : public brls::Activity {
 public:
  LogScreen log_screen_{"LoadingActivity"};

  // Executada quando o carregamento termina; recebe a fonte pronta.
  using DoneCallback = std::function<void(BoxSource*)>;

  LoadingActivity(std::string save_path, DoneCallback on_done)
      : save_path_(std::move(save_path)), on_done_(std::move(on_done)) {}

  brls::View* createContentView() override {
    root_ = new LoadingRoot(this);
    root_->setAxis(brls::Axis::COLUMN);
    root_->setJustifyContent(brls::JustifyContent::CENTER);
    root_->setAlignItems(brls::AlignItems::CENTER);

    auto* logo = new brls::Image();
    logo->setImageFromFile(std::string(POKEHOME_UI_ASSETS) + "icon.png");
    logo->setWidth(148);
    logo->setHeight(148);
    logo->setCornerRadius(28);
    logo->setMarginBottom(30);
    root_->addView(logo);

    status_ = new brls::Label();
    status_->setText("Iniciando...");
    status_->setFontSize(24);
    status_->setTextColor(kTextSecondary);
    status_->setMarginBottom(22);
    root_->addView(status_);

    progress_ = new ProgressBar(460.0f);
    root_->addView(progress_);

    return root_;
  }

 private:
  // Etapas do carregamento. Uma por frame, para a barra andar de verdade.
  enum class Step { kFadeIn, kLookingForSave, kReading, kParsing, kBuilding,
                    kDone, kFadeOut };

  // A raiz precisa de draw() proprio: e onde o fade acontece e onde as etapas
  // avancam, ja que o borealis nao expoe um "tick" por Activity.
  class LoadingRoot : public GradientBackground {
   public:
    explicit LoadingRoot(LoadingActivity* owner) : owner_(owner) {}

    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
      owner_->Advance();

      // nvgGlobalAlpha multiplica tudo que for desenhado a seguir, entao o
      // fade cobre a tela inteira sem tocar em cada View.
      nvgGlobalAlpha(vg, owner_->alpha_);
      GradientBackground::draw(vg, x, y, w, h, style, ctx);
      nvgGlobalAlpha(vg, 1.0f);
    }

   private:
    LoadingActivity* owner_;
  };

  // Chamada a cada frame pelo draw() da raiz.
  void Advance() {
    ++frame_;

    switch (step_) {
      case Step::kFadeIn:
        alpha_ = std::min(1.0f, alpha_ + kFadeStep);
        if (alpha_ >= 1.0f) Next(Step::kLookingForSave, "Procurando saves...");
        break;

      case Step::kLookingForSave:
        if (save_path_.empty()) save_path_ = FindSaveOnDevice();
        SetProgress(0.15f);
        Next(Step::kReading, "Lendo save...");
        break;

      case Step::kReading:
        if (!save_path_.empty()) file_ = ReadFile(save_path_);
        SetProgress(0.45f);
        Next(Step::kParsing, "Conferindo checksums...");
        break;

      case Step::kParsing:
        // O parse mora no OpenBoxSource desde a spec 082 — este passo virou
        // so o tempo de tela da barra. Parsear duas vezes seria trabalho
        // jogado fora e um segundo lugar decidindo o formato.
        SetProgress(0.70f);
        Next(Step::kBuilding, "Montando caixas...");
        break;

      case Step::kBuilding:
        source_ = OpenBoxSource(
            std::move(file_), save_path_, save_path_,
            [path = save_path_](const std::vector<std::uint8_t>& b) {
              return WriteWholeFile(path, b);
            });
        SetProgress(1.0f);
        Next(Step::kDone, "Pronto");
        break;

      case Step::kDone:
        // Segura um instante em tela cheia: sumir no frame seguinte ao 100%
        // faz a barra parecer que nunca encheu.
        if (frame_ - step_frame_ >= kHoldFrames) step_ = Step::kFadeOut;
        break;

      case Step::kFadeOut:
        alpha_ = std::max(0.0f, alpha_ - kFadeStep);
        if (alpha_ <= 0.0f && !finished_) {
          finished_ = true;
          brls::Application::popActivity();
          if (on_done_) on_done_(source_);
        }
        break;
    }
  }

  void Next(Step step, const char* text) {
    step_ = step;
    step_frame_ = frame_;
    if (status_) status_->setText(text);
  }

  void SetProgress(float ratio) {
    if (progress_) progress_->setProgress(ratio);
  }

  // ~0,25 s de fade e ~0,3 s de espera a 60 FPS.
  static constexpr float kFadeStep = 1.0f / 15.0f;
  static constexpr unsigned kHoldFrames = 18;

  std::string save_path_;
  DoneCallback on_done_;

  std::vector<std::uint8_t> file_;
  BoxSource* source_ = nullptr;

  Step step_ = Step::kFadeIn;
  float alpha_ = 0.0f;
  unsigned frame_ = 0, step_frame_ = 0;
  bool finished_ = false;

  LoadingRoot* root_ = nullptr;
  brls::Label* status_ = nullptr;
  ProgressBar* progress_ = nullptr;
};

}  // namespace


int main(int argc, char* argv[]) {
  // Antes de qualquer coisa: se a execucao anterior baixou uma versao nova, o
  // .nro em disco e trocado agora — o hbloader ja carregou o binario atual na
  // memoria, entao substituir o arquivo aqui e seguro.
  // argv[0] e o caminho de onde o hbloader carregou este .nro.
  nestbox::SetRunningNroPath(argc > 0 ? argv[0] : nullptr);
  nestbox::ApplyPendingUpdate();

  std::string save_path;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-d") == 0) {
      brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    } else if (save_path.empty()) {
      save_path = argv[i];
    }
  }

  // Antes do borealis: se a inicializacao falhar, o log tem de ter registrado
  // a tentativa.
  OpenLog();
  NLOG_ACT("=== sessao NestBox %s === save_arg=\"%s\" applet=%d",
           NESTBOX_VERSION, save_path.c_str(), IsAppletMode() ? 1 : 0);

  if (!brls::Application::init()) {
    NLOG_ACT("FALHA: brls::Application::init() devolveu false");
    brls::Logger::error("Nao foi possivel iniciar o borealis");
    return EXIT_FAILURE;
  }

  brls::Application::createWindow("NestBox");
  brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::LIGHT);
  brls::Application::setGlobalQuit(false);

  // A leitura do save deixou de acontecer aqui: agora e a tela de loading que
  // a executa, fatiada entre frames, para a barra andar de verdade. O menu so
  // sobe quando ela entrega a fonte pronta.
  brls::Application::pushActivity(
      new LoadingActivity(save_path, [](BoxSource* save_source) {
        if (!save_source) save_source = new EmptyBoxSource();

        // Carrega o banco do cartao. Arquivo ausente ou invalido devolve banco
        // vazio — primeira execucao nao e erro (spec 028).
        auto* nest = new NestBoxSource();
        nest->Load(LoadNestBox());

        brls::Application::pushActivity(new MenuActivity(nest, save_source));

        // Empilhado por cima: ao pressionar A o aviso sai e a tela principal
        // ja esta pronta atras, sem recarregar nada.
        if (IsAppletMode()) {
          brls::Application::pushActivity(new AppletWarningActivity());
        }

        const nestbox::UpdateInfo update = nestbox::CheckForUpdate();
        if (update.available) {
          brls::Application::pushActivity(new UpdateActivity(update));
        }
      }));

  // O carimbo do log conta FRAMES, nao relogio: armGetSystemTick() (CNTVCT_EL0)
  // derruba o app no Ryujinx e toda API de tempo do borealis desce por ela.
  // Este e o unico ponto por onde todo frame passa. Ver TD-02 da spec 083.
  while (brls::Application::mainLoop()) {
    pokehome::nlog::Tick();
  }

  NLOG_ACT("=== fim da sessao (%.1f s) ===", pokehome::nlog::Seconds());
  return EXIT_SUCCESS;
}
