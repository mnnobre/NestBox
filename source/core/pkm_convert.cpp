#include "pkm_convert.h"

#include "body_size.h"
#include "gen9_species_id.h"
#include "species_facts.h"

namespace pkm {
namespace {

// Quais campos do modelo cada formato realmente representa. Derivado do que o
// Parse de cada parser LE — se o parser nao le, o Write nao escreve, e mandar
// o campo para la e jogar dado fora com aparencia de preservacao.
//
// A tabela existe para que o zeramento seja explicito e conferivel, em vez de
// espalhado em ifs pelo corpo do Convert.
struct Caps {
  bool memories;       // memorias OT/HT (gen6+): PK8, PB8, PA8, PK9
  bool ribbons;        // bitfield de ribbons/marks: idem
  bool contest;        // contest stats + sheen
  bool ht_extended;    // ht_language, ht_id (o PB7 so tem nome/genero/amizade)
  bool stat_nature;    // mint (gen8+) — o PB7 nao tem
  bool home_tracker;   // o PB7 e anterior ao HOME
  bool pokerus;
  bool camping;        // fullness / enjoyment
  bool sociability;    // PK8 e PB8
  bool move_records;   // TR/TM flags: PK8 (14 bytes) e PK9 (26)
  bool scale;          // so PK9
  bool dynamax;        // dynamax_level + can_gigantamax: so PK8
  bool palma;          // so PK8
  bool alpha;          // is_alpha, is_noble, effort_levels, alpha_move: so PA8
  bool tera;           // tera type + obedience: so PK9
  bool awakening;      // AVs: so PB7
  int name_chars;      // apelido: 12 no PB7, 13 nos gen8+
  int trainer_chars;   // OT/HT: 11 no PB7, 13 nos gen8+
  bool ability_u16;    // no PB7 a habilidade e u8
};

constexpr Caps kCaps[] = {
    // kNone — nada. Nunca usado: Convert rejeita destino kNone.
    {},
    // kPB7 — Let's Go
    {false, false, false, false, false, false, true, true, false, false,
     false, false, false, false, false, true, 12, 11, false},
    // kPK8 — Sword/Shield
    {true, true, true, true, true, true, true, true, true, true,
     false, true, true, false, false, false, 13, 13, true},
    // kPB8 — BDSP
    {true, true, true, true, true, true, true, true, true, false,
     false, false, false, false, false, false, 13, 13, true},
    // kPA8 — Legends: Arceus
    {true, true, true, true, true, true, true, false, false, false,
     true, false, false, true, false, false, 13, 13, true},
    // kPK9 — Scarlet/Violet e Legends Z-A
    {true, true, true, true, true, true, false, false, false, true,
     true, false, false, false, true, false, 13, 13, true},
};

const Caps& CapsOf(Format f) {
  const auto i = static_cast<std::size_t>(f);
  return kCaps[i < std::size(kCaps) ? i : 0];
}

// Trunca uma string UTF-8 a `max_chars` code units UTF-16. Os campos de texto
// do PB7 sao menores que os dos gen8+ (12/11 contra 13/13): um apelido de 13
// caracteres NAO cabe. Truncar aqui e melhor que deixar o WriteUtf16 cortar no
// meio de uma sequencia multibyte.
std::string TruncUtf8(const std::string& s, int max_chars) {
  int units = 0;
  std::size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    std::size_t len = 1;
    if ((c & 0xF8) == 0xF0) len = 4;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xE0) == 0xC0) len = 2;
    // Fora do BMP ocupa 2 code units UTF-16 (par substituto).
    const int cost = len == 4 ? 2 : 1;
    if (units + cost > max_chars) break;
    units += cost;
    i += len;
  }
  return s.substr(0, i);
}

}  // namespace

std::uint16_t NationalDex(const Pokemon& p) {
  if (p.format == Format::kPK9) {
    return pokehome::gen9::ToNational(p.species);
  }
  return p.species;
}

std::uint16_t SpeciesForFormat(std::uint16_t national_dex, Format fmt) {
  if (fmt == Format::kPK9) {
    return pokehome::gen9::ToInternal(national_dex);
  }
  return national_dex;
}

std::optional<Pokemon> Convert(const Pokemon& origem, Format destino) {
  if (destino == Format::kNone || origem.format == Format::kNone) {
    return std::nullopt;
  }

  // Mesmo formato: copia integral, `raw` INCLUSIVE. Mover um Pokemon dentro do
  // mesmo jogo continua sob a regra conservadora da spec 063 — este atalho
  // existe para que Convert nao vire porta de fuga dela.
  if (destino == origem.format) return origem;

  // A grandeza comum. Sem esta normalizacao, PB8 -> PK9 gravaria o National
  // Dex onde o binario espera indice interno, e o Pokemon vira outra especie.
  const std::uint16_t dex = NationalDex(origem);
  if (dex == 0) return std::nullopt;

  const std::uint16_t alvo = SpeciesForFormat(dex, destino);
  if (alvo == 0) return std::nullopt;  // TD-03: nao representavel no destino

  Pokemon p = origem;
  p.format = destino;
  p.species = alvo;

  // TD-01: o `raw` da origem esta num layout que nao e o do destino.
  // Reaproveita-lo escreveria os campos novos por cima de bytes que, no layout
  // de destino, significam outra coisa. O Write do formato cai no seu proprio
  // caminho de buffer zerado.
  p.raw.clear();
  p.checksum = 0;  // recalculado pelo Write do destino

  const Caps& c = CapsOf(destino);

  // --- Campos que o destino nao representa ------------------------------
  if (!c.memories) {
    p.ot_memory = {};
    p.ht_memory = {};
  }
  if (!c.ribbons) {
    p.ribbon_bytes = {};
    p.ribbon_count_memory = 0;
    p.ribbon_count_battle = 0;
    p.affixed_ribbon = 0;
  }
  if (!c.contest) {
    p.contest_stats = {};
    p.contest_sheen = 0;
  }
  if (!c.ht_extended) {
    p.ht_language = 0;
    p.ht_id = 0;
  }
  if (!c.home_tracker) p.home_tracker = 0;
  if (!c.pokerus) p.pokerus = 0;
  if (!c.camping) {
    p.fullness = 0;
    p.enjoyment = 0;
  }
  if (!c.sociability) p.sociability = 0;
  if (!c.move_records) p.move_record_flags.clear();
  // SAINDO do gen9: o oficial grava o SCALE no HeightScalar do destino —
  // PK9(H102 W115 S128) vira H128 W115 nos TRES formatos, PA8 inclusive
  // (sonda P13). E o espelho da entrada (`Scale = HeightScalar`, 069/078).
  // Sem isto, um capturado em Tera Raid perde a correlacao de seed que o
  // PkHeX confere pela altura e cai em "Unable to match an encounter" — 108
  // no SwSh e 101 no BDSP. So quando a ORIGEM e PK9: nos outros formatos
  // p.scale e 0 e copiar zeraria a altura verdadeira.
  if (origem.format == Format::kPK9 && p.scale != 0)
    p.height_scalar = p.scale;
  if (!c.scale) p.scale = 0;
  if (!c.dynamax) {
    // §7: BDSP e PLA bloqueiam Gigantamax. Aqui o campo simplesmente nao
    // existe no binario de destino — o BLOQUEIO (recusar a transferencia) e
    // regra da G09, nao desta funcao.
    p.dynamax_level = 0;
    p.can_gigantamax = false;
  }
  if (!c.palma) p.palma = 0;
  if (!c.alpha) {
    // L3 (spec 138): o status do alpha NAO evapora ao sair do PA8. O destino
    // que tem o bitfield de marks recebe a **Alpha Mark**, que e onde o
    // "Former Alpha" do SV mora. Antes de zerar o bit, grava a mark.
    //
    // Medido na sonda `tools/pkhex-138` contra o EntityConverter do PkHeX:
    //   ConvertToType(PA8 com IsAlpha -> PK9) liga [RibbonMarkAlpha] e SO ela.
    //   RibbonMarkAlpha vive no byte 69 bit 3 = ribbon_bytes[13] & 0x08.
    //
    // A pesquisa dizia "Alpha Mark + Jumbo Mark"; a sonda MEDIU que o
    // converter NAO liga a Jumbo (nem com scale 255, bloco A4). A Jumbo e
    // uma mark que o JOGO concede pelo tamanho — nao a inventamos aqui.
    if (p.is_alpha) {
      if (c.ribbons) p.ribbon_bytes[13] |= 0x08;
      // O alpha e sempre do tamanho maximo. O converter do PkHeX FORCA
      // Scale=255 na saida de um alpha, medido (sonda 138, bloco A4):
      //   PA8 ALPHA scale=  0 -> PK9 scale=255
      //   PA8 ALPHA scale=128 -> PK9 scale=255
      // Sem isto o "Former Alpha" chegaria ao SV com o tamanho errado, e a
      // Jumbo Mark (que depende de scale==255) ficaria inalcancavel.
      if (c.scale) p.scale = 255;
    }
    p.is_alpha = false;
    p.is_noble = false;
    p.alpha_move = 0;
    // effort_levels: ver o bloco de effort levels abaixo.
  }
  if (!c.tera) {
    p.tera_type_original = 0;
    p.tera_type_override = 0;
    p.obedience_level = 0;
  }
  if (!c.awakening) p.awakening_values = {};

  // --- Effort levels do PLA ---------------------------------------------
  // A pesquisa §7 diz que a HOME "converte effort levels <-> EVs/IVs", sem dar
  // a formula. A sonda contra o PKHeX.Core (spec 069) MEDIU o EntityConverter
  // nas duas direcoes e ele NAO converte:
  //   PA8 -> PK9 com GV 10/8/7/5/4/2  ->  EVs saem 0/0/0
  //   PK9 -> PA8 com EV 252/100       ->  GVs saem 0/0
  // Os GV sao grandeza armazenada a parte, com setter proprio; setar GV nao
  // move IV nem EV.
  //
  // Inventar uma formula aqui produziria exatamente o falso-verde da spec 066.
  // Entao: fora do PA8 o campo e ZERADO, e a conversao propriamente dita fica
  // como PENDENCIA declarada na spec.
  if (!c.alpha) p.effort_levels = {};

  // --- Campos que o destino exige e a origem nao tem --------------------
  // §7: "ao entrar em Scarlet/Violet, recebe Tera Type derivado do tipo
  // primario". A spec 069 conferiu isso em 10 especies e fechou a regra como
  // `== Type1` — mas nenhuma das 10 era Normal com segundo tipo.
  //
  // Medido em TODAS as 733 do SV (sonda P15, 2026-08-20): 707 recebem o
  // Type1 e 26 recebem o Type2 — e os 26 sao exatamente os de Type1 = Normal.
  // A regra e "o tipo que NAO e Normal". Starly (Normal/Flying) chegava com
  // tera Normal e o PkHeX acusava "Tera Type does not match the expected
  // value" — 26 dos nossos, o numero exato.
  if (c.tera && origem.format != Format::kPK9) {
    p.tera_type_original = pokehome::species::TeraOnEntry(dex);
    p.tera_type_override = p.tera_type_original;
  }

  // --- Ajustes de largura ------------------------------------------------
  // O PB7 tem campos de texto menores. Nome que nao cabe e truncado — perda
  // real, coberta pelo teste de ida-e-volta.
  p.nickname = TruncUtf8(p.nickname, c.name_chars);
  p.ot_name = TruncUtf8(p.ot_name, c.trainer_chars);
  p.ht_name = TruncUtf8(p.ht_name, c.trainer_chars);
  // O PB7 tem campos de 24 bytes: os 26 crus nao lhe servem, e o Write dele
  // e textual mesmo. Zera para o proximo formato de 26 nao ressuscitar um
  // buffer que ja nao corresponde ao texto truncado.
  if (c.name_chars < 13) {
    p.nickname_raw = {};
    p.ot_name_raw = {};
    p.ht_name_raw = {};
  }

  // No PB7 a habilidade e u8: acima de 255 nao ha byte onde caiba. Zerar o
  // campo produziria um Pokemon com "habilidade nenhuma" em silencio — o
  // Glastrier (Chilling Neigh = 1013) e o caso que o teste pegou. Mesma
  // logica do TD-03: o que o formato nao sabe endereçar, ele nao recebe.
  if (!c.ability_u16 && p.ability > 0xFF) return std::nullopt;

  // Sem mint no PB7: o parser copia nature para stat_nature na leitura, entao
  // manter os dois divergentes produziria um modelo que o roundtrip desmente.
  if (!c.stat_nature) p.stat_nature = p.nature;

  // A forma do PB7 mora em 5 bits (byte 29). Forma que nao cabe viraria outra
  // forma em silencio — melhor recusar do que gravar Pokemon errado.
  if (destino == Format::kPB7 && p.form > 0x1F) return std::nullopt;

  return p;
}

void AjustesDeEntrada(Pokemon& mon, Format origem_fmt) {
  const Format destino = mon.format;

  // Mesmo formato: nada a ajustar. Mover dentro do proprio jogo continua sob
  // a regra conservadora da spec 063.
  if (destino == origem_fmt) return;

  // Ao entrar no PK9 o PkHeX acusava `Invalid Obedience Level` e `Height does
  // not match the expected value` — motivos NOVOS, que nao existiam na
  // origem. Medido na sonda tools/pkhex-entry (spec 069/078):
  //
  //   ObedienceLevel = MetLevel        (met 5 -> obediencia 5)
  //   Scale          = HeightScalar    (255 -> 255, e nao 0)
  if (destino == Format::kPK9) {
    mon.obedience_level = mon.met_level;
    mon.scale = mon.height_scalar;
  }
  // Spec 143: o PA8 tem o MESMO campo (pa8.cpp:53 — "no PA8 o Scale do PkHeX
  // repete o height scalar"), e ficou de fora quando a 069/078 corrigiu o
  // PK9. O PkHeX acusa `Copy Height does not match the original value`.
  // `obedience_level` NAO entra aqui: e conceito de gen 9, sem campo no PA8.
  if (destino == Format::kPA8) mon.scale = mon.height_scalar;

  // O SENTINELA de "nao veio de ovo" muda de formato para formato. Nao
  // traduzi-lo produz `Egg Met Date is not a valid calendar date`, e foi o
  // que chegou ao console do dono na spec 143: um Bidoof do BDSP entrou no
  // Legends: Arceus com egg_location = 0xFFFF, enquanto os nativos tem 0.
  //
  // Medido nas duas direcoes (tools/pkhex-egg, e antes tools/pkhex-entry):
  //   qualquer -> PB8      : EggLocation 0      vira 0xFFFF
  //   PB8      -> qualquer : EggLocation 0xFFFF vira 0
  //
  // Vale so para quem NAO e ovo e nao nasceu de ovo (egg_date zerada). Um
  // Pokemon com procedencia real de ovo tem local de verdade, e sobrescrever
  // isso destruiria dado.
  {
    constexpr std::uint16_t kNone = 0;
    constexpr std::uint16_t kBdspNone = 0xFFFF;
    const bool sem_ovo = !mon.is_egg && mon.egg_date[0] == 0 &&
                         mon.egg_date[1] == 0 && mon.egg_date[2] == 0;
    if (sem_ovo) {
      if (destino == Format::kPB8) {
        if (mon.egg_location == kNone) mon.egg_location = kBdspNone;
      } else if (mon.egg_location == kBdspNone) {
        mon.egg_location = kNone;
      }
    }
  }

  // Altura/peso ABSOLUTOS (spec 143): o PA8 e o PB7 guardam floats derivados
  // dos escalares; PB8/PK8/PK9 nao os tem. Sem recalcular, eles entram
  // zerados e o PkHeX acusa "Calculated Height does not match stored value"
  // — e o PLA renderiza registro ilegal como OVO.
  //
  // Mesma formula ja usada na subida gen3 (gen3_transfer.cpp:297), aqui
  // reaproveitada em vez de reescrita: uma segunda copia divergiria na
  // primeira correcao.
  if (destino == Format::kPA8 || destino == Format::kPB7) {
    namespace body = pokehome::body;
    const bool pla = destino == Format::kPA8;
    const body::Entry* tab = pla ? body::kLa : body::kGg;
    const std::size_t n = pla ? sizeof(body::kLa) / sizeof(body::kLa[0])
                              : sizeof(body::kGg) / sizeof(body::kGg[0]);
    std::uint16_t base_h = 0, base_w = 0;
    if (body::Lookup(tab, n, NationalDex(mon), mon.form, &base_h, &base_w) &&
        base_h && base_w) {
      body::Absolutes(pla ? body::kRatioPla : body::kRatioLgpe, base_h, base_w,
                      mon.height_scalar, mon.weight_scalar,
                      &mon.height_absolute, &mon.weight_absolute);
    }
  }

  // PLA: docs/pesquisa-effort-levels-pla.md fechou a P-01 da spec 070. NAO
  // existe conversao de effort levels — ao entrar, GV = 0 em tudo e os EVs
  // passam intactos. Fonte: GameDataPA8.TryCreate do PkHeX, que inicializa
  // ball/locations/moveset e nao toca em nenhum GV_*.
  if (destino == Format::kPA8) mon.effort_levels = {};
}

}  // namespace pkm
