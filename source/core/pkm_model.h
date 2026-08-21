// Modelo de dados unico dos Pokemon modernos (spec 057, F01 da descoberta
// parsers-switch-completos). Os parsers PB7/PK8/PB8/PA8/PK9 preenchem isto;
// a ligacao com a tela (F16) consome.
//
// Anotacao por campo:
//   [HOME]  — usado pela Pokemon HOME (pesquisa-pokemon-home.md, secao citada)
//   [extra] — existe no formato e fica guardado; pedido do dono: "o que
//             usamos e o que NAO usamos, para nada ficar de fora".
//
// O modelo guarda o que o BINARIO armazena. Derivadas (shiny, stats de
// batalha, level) sao funcao de quem consome — dado derivado guardado e dado
// que diverge do primario.
//
// Regra da spec 028 preservada: `raw` carrega os bytes originais do slot;
// regravar nunca destroi o que nao parseamos.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pkm {

// De qual formato binario este Pokemon veio. Diz quais campos sao
// significativos (TD-02: os demais ficam zerados).
enum class Format : std::uint8_t {
  kNone = 0,
  kPB7,  // Let's Go (260 bytes)
  kPK8,  // Sword/Shield (328/344)
  kPB8,  // BDSP (328/344)
  kPA8,  // Legends Arceus (360/376)
  kPK9,  // Scarlet/Violet e Legends Z-A (328/344)
  kPK4,  // Diamond/Pearl/Platinum/HGSS (136/236) — spec 126
};

const char* FormatName(Format f);

struct Pokemon {
  Format format = Format::kNone;

  // Bytes crus do slot como estavam no save (cifrados nao — ja decifrados,
  // mas completos). Regra da spec 028.
  std::vector<std::uint8_t> raw;

  // --- Cabecalho do formato -------------------------------------------
  std::uint32_t encryption_constant = 0;  // [extra] semente da cifra
  std::uint16_t sanity = 0;               // [extra] flags internas
  std::uint16_t checksum = 0;             // [extra] conferido na leitura

  // --- Identidade ------------------------------------------------------
  std::uint16_t species = 0;      // [HOME §6] National Dex nos formatos modernos
  std::uint8_t form = 0;          // [HOME §6/§7] forma (alt forms bloqueiam descida)
  std::uint8_t gender = 0;        // [HOME §6] 0=M 1=F 2=sem genero
  std::uint8_t nature = 0;        // [HOME §6]
  std::uint8_t stat_nature = 0;   // [extra] mint (gen8+): a natureza que vale nos stats
  std::uint16_t ability = 0;      // [HOME §6] id da habilidade
  std::uint8_t ability_number = 0;  // [extra] 1/2/4 (hidden)
  bool is_egg = false;            // [HOME §7] ovo nao deposita
  bool is_nicknamed = false;      // [extra]
  std::string nickname;           // [HOME §6] UTF-8

// Os 26 bytes CRUS dos campos de texto (spec 145). O jogo escreve o nome por
// cima de um buffer usado, e o lixo depois do terminador FAZ PARTE do
// registro: o PkHeX exige o trash do OT nos capturados do Max Lair
// ("[Trainer] TrashBytesExpected", 21 casos), e reconstruir a string limpa o
// destroi. Todo-zero = "sem bytes crus" — o Write cai no caminho textual.
// So os formatos de campo 26 (pk8/pb8/pk9/pa8); o PB7 (24) fica no textual.
std::array<std::uint8_t, 26> nickname_raw{};
std::array<std::uint8_t, 26> ot_name_raw{};
std::array<std::uint8_t, 26> ht_name_raw{};
  std::uint8_t language = 0;      // [HOME §6] tag de idioma (ENG/POR/...)
  std::uint32_t pid = 0;          // [HOME §6] personality (shiny deriva daqui)
  bool fateful_encounter = false; // [extra]

  // --- Treinador -------------------------------------------------------
  std::string ot_name;            // [HOME §6] OT no topo do summary
  std::uint16_t tid = 0;          // [HOME §6] visivel
  std::uint16_t sid = 0;          // [HOME §7] parte do shiny check
  std::uint8_t ot_gender = 0;     // [extra]
  std::uint8_t ot_friendship = 0; // [extra]
  std::string ht_name;            // [extra] handling trainer
  std::uint8_t ht_gender = 0;     // [extra]
  std::uint8_t ht_language = 0;   // [extra]
  std::uint16_t ht_id = 0;        // [extra]
  std::uint8_t ht_friendship = 0; // [extra]
  std::uint8_t current_handler = 0;  // [extra] 0=OT 1=HT

  // Memorias (gen6+): "lembra de X com intensidade Y sentindo Z". [extra]
  struct Memory {
    std::uint8_t memory = 0;
    std::uint8_t intensity = 0;
    std::uint8_t feeling = 0;
    std::uint16_t text_var = 0;
  };
  Memory ot_memory;               // [extra]
  Memory ht_memory;               // [extra]

  // --- Progresso -------------------------------------------------------
  std::uint32_t exp = 0;          // [HOME §6] nivel deriva daqui pela curva
  std::uint16_t held_item = 0;    // [HOME §7] HOME devolve o item a bag
  std::uint32_t markings = 0;     // [extra] circulos/triangulos da box (2 bits cada)
  std::uint8_t pokerus = 0;       // [extra] strain+dias
  bool favorite = false;          // [extra] coracao de favorito (LGPE/SwSh)
  std::uint8_t fullness = 0;      // [extra] camping/curry
  std::uint8_t enjoyment = 0;     // [extra]
  std::uint32_t status_condition = 0;  // [extra] dorme/queimado ao guardar
  // HP ATUAL. Mora no NUCLEO (0x8A no pk8/pb8/pk9, 0x92 no pa8, 0xF0 no
  // pb7) — NAO na cauda de party, apesar de ser estado de batalha. Medido
  // pela sonda tools/pkhex-za2 contra o PkHeX (spec 119): sem preencher, o
  // Pokemon depositado aparece com 0 de vida (morto) no jogo.
  std::uint16_t hp_current = 0;        // [extra]

  // Golpes cujo bit de "plus flag" do Legends Z-A deve estar ligado (spec
  // 120). O bitmap mora no binario do PK9 e o app so LIGA bits — modelar os
  // ~99 bytes seria carregar estado que ninguem le. O writer do pk9 aplica
  // esta lista sobre o buffer; o resto do bloco vem do `raw` (escrita
  // conservadora da spec 063).
  //
  // NOTA da sonda (spec 120): um nativo LEGAL do Z-A tem ZERO plus flags, e
  // ligar a flag NAO torna legal um golpe fora do learnset da especie. O
  // campo existe porque o PkHeX cobra a flag para golpes que o Pokemon
  // legitimamente conhece; nao e a causa do "Invalid Move".
  std::vector<std::uint16_t> za_plus_moves;  // [extra]
  // Especie cuja lista PlusMoveIndexes indexa os bits acima (spec 122).
  std::uint16_t za_plus_dex = 0;             // [extra]
  std::uint32_t palma = 0;        // [extra] PK8
  std::uint32_t form_argument = 0;     // [extra] Furfrou/Yamask/etc
  // Flags de TR/TM aprendidos (tamanho varia por formato: 14 no PK8).
  std::vector<std::uint8_t> move_record_flags;  // [extra]

  // --- IVs / EVs / treino ---------------------------------------------
  // Ordem fisica do save: HP, Atk, Def, Spe, SpA, SpD.
  std::array<std::uint8_t, 6> ivs{};             // [HOME §6] stats derivam
  std::array<std::uint8_t, 6> evs{};             // [HOME §6]
  std::array<bool, 6> hyper_trained{};           // [HOME §7] BDSP restringe
  std::array<std::uint8_t, 6> contest_stats{};   // [extra] cool/beauty/...
  std::uint8_t contest_sheen = 0;                // [extra]

  // --- Golpes ----------------------------------------------------------
  std::array<std::uint16_t, 4> moves{};        // [HOME §6/§7]
  std::array<std::uint8_t, 4> pp{};            // [extra]
  std::array<std::uint8_t, 4> pp_ups{};        // [extra]
  std::array<std::uint16_t, 4> relearn_moves{};  // [HOME §7] moveset por jogo

  // --- Origem ----------------------------------------------------------
  std::uint8_t origin_game = 0;    // [HOME §6] marca de origem no summary
  std::uint16_t met_location = 0;  // [HOME §6] local de origem
  std::uint8_t met_level = 0;      // [extra]
  std::uint16_t egg_location = 0;  // [extra]
  // Datas como veio do save (ano-2000, mes, dia); 0 = ausente.
  std::array<std::uint8_t, 3> met_date{};  // [HOME §6] data de origem
  std::array<std::uint8_t, 3> egg_date{};  // [extra]
  std::uint8_t ball = 0;           // [HOME §6/§7] preservada sempre
  std::uint64_t home_tracker = 0;  // [HOME §7] tracker de 64 bits do servidor
  // Gen4 (spec 126): D/P usam um par de campos de local e Platinum/HGSS
  // outro (estendido). O par DP fica aqui para o roundtrip ser fiel;
  // met_location/egg_location carregam o valor EFETIVO (ext || dp).
  std::uint16_t met_location_dp = 0;   // [extra] so pk4
  std::uint16_t egg_location_dp = 0;   // [extra] so pk4

  // --- Cosmetica / flags em lote --------------------------------------
  // Ribbons e marks sao ~16 bytes de bitfield com ~120 nomes padronizados
  // pelo PkHeX. Guardados crus (TD-01): a F16 mapeia os que exibir.
  // [HOME §7] "ribbons preservadas sempre; marks (SwSh+)".
  std::array<std::uint8_t, 16> ribbon_bytes{};
  std::uint8_t ribbon_count_memory = 0;   // [extra] Contest Memory (byte 60)
  std::uint8_t ribbon_count_battle = 0;   // [extra] Battle Memory (byte 61, spec 136)
  // 0xFF = NENHUMA fita afixada. O default NAO pode ser 0: esse e o codigo da
  // Kalos Champion, e um Pokemon que nasce sem passar por um parser sai com
  // ela pendurada — 117 de 117 numa volta LGPE -> SwSh, reprovados com
  // "Invalid Affixed Ribbon: Kalos Champion". O PB7 nao guarda este campo,
  // entao quem sai do Let's Go depende inteiramente deste default.
  std::uint8_t affixed_ribbon = 0xFF;     // [extra]

  // --- Corpo (gen8+) ---------------------------------------------------
  std::uint8_t height_scalar = 0;  // [extra]
  std::uint8_t weight_scalar = 0;  // [extra]
  // Altura/peso ABSOLUTOS (spec 121): so PA8 e PB7 os guardam, em float.
  // O verificador do PkHeX recalcula e compara — zerado, acusa. Offsets
  // medidos: PA8 0xAC/0xB0, PB7 0x2C/0xE4.
  float height_absolute = 0.0f;  // [extra]
  float weight_absolute = 0.0f;  // [extra]
  std::uint8_t scale = 0;          // [extra] PK9

  // --- Especificos de SwSh (PK8/PB8) ----------------------------------
  std::uint8_t dynamax_level = 0;  // [extra]
  bool can_gigantamax = false;     // [HOME §7] BDSP/PLA bloqueiam Gmax
  std::uint32_t sociability = 0;   // [extra]

  // --- Especificos de PLA (PA8) ----------------------------------------
  bool is_alpha = false;           // [extra]
  bool is_noble = false;           // [extra]
  std::array<std::uint8_t, 6> effort_levels{};  // [HOME §7] convertidos ↔ EV/IV
  std::uint16_t alpha_move = 0;    // [extra]

  // --- Especificos de SV / Z-A (PK9) -----------------------------------
  std::uint8_t tera_type_original = 0;  // [HOME §7] derivado do tipo na entrada
  std::uint8_t tera_type_override = 0;  // [extra]
  std::uint8_t obedience_level = 0;     // [extra]

  // --- Especificos de LGPE (PB7) ---------------------------------------
  std::array<std::uint8_t, 6> awakening_values{};  // [extra] AVs
  std::uint16_t received_flags = 0;                // [extra]

  bool empty() const { return species == 0; }
};

// O shiny e derivado, nao armazenado: xor das metades de PID contra TID/SID.
// Gen6+ usa limiar 16.
inline bool IsShiny(const Pokemon& p) {
  const std::uint32_t x = (p.pid >> 16) ^ (p.pid & 0xFFFF) ^ p.tid ^ p.sid;
  return x < 16;
}

}  // namespace pkm
