// Parser de save Pokémon gen3 (Ruby/Sapphire/Emerald/FireRed/LeafGreen).
//
// Este header e sua implementacao NAO dependem de libnx nem de nenhuma API de
// plataforma: recebem bytes, devolvem dados. E o que permite testar no PC antes
// de tocar no Switch (ver TD-02 no evidence-log da spec 001).
//
// Formato documentado a partir do PKHeX (ver CREDITS.md) e verificado contra um
// save real de LeafGreen.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// So o nome: quem carrega o payload moderno (spec 086) inclui pkm_model.h;
// este header continua sem depender do modelo moderno.
namespace pkm {
struct Pokemon;
}

namespace pokehome::gen3 {

// --- Constantes do formato -------------------------------------------------

inline constexpr std::size_t kSectionSize = 0x1000;  // 4096
inline constexpr std::size_t kSectionData = 0x0F80;  // 3968 bytes uteis
inline constexpr std::size_t kSectionCount = 14;
inline constexpr std::size_t kSlotSize = kSectionSize * kSectionCount;  // 57344
inline constexpr std::size_t kSaveSize = kSlotSize * 2;                 // 114688

// Offsets dentro do footer de cada secao, conforme o PKHeX (SAV3.cs) e
// validados contra um save cru de LeafGreen: 14/14 checksums batem nos dois
// slots.
inline constexpr std::size_t kOffSectionId = 0x0FF4;
inline constexpr std::size_t kOffChecksum = 0x0FF6;
inline constexpr std::size_t kOffSignature = 0x0FF8;
inline constexpr std::size_t kOffSaveIndex = 0x0FFC;

inline constexpr std::uint32_t kSignature = 0x08012025;

// Uma entrada de Pokemon guardado no PC: 80 bytes (sem os stats calculados,
// que so existem no formato de party).
inline constexpr std::size_t kBoxPokemonSize = 80;
inline constexpr std::size_t kBoxCount = 14;
inline constexpr std::size_t kSlotsPerBox = 30;

// --- Tipos -----------------------------------------------------------------

struct Section {
  std::uint16_t id = 0;
  std::uint16_t stored_checksum = 0;
  std::uint16_t computed_checksum = 0;
  std::uint32_t save_index = 0;
  std::size_t file_offset = 0;

  bool checksum_ok() const { return stored_checksum == computed_checksum; }
};

struct Slot {
  std::vector<Section> sections;  // na ordem fisica em que aparecem
  std::uint32_t save_index = 0;
  bool complete = false;  // todos os 14 ids presentes exatamente uma vez
};

// Um Pokemon lido de uma caixa. Esta spec le apenas a especie; os demais campos
// entram em specs futuras.
struct BoxPokemon {
  std::uint32_t personality = 0;
  std::uint32_t ot_id = 0;
  std::uint16_t species = 0;  // 0 = slot vazio
  std::string nickname;

  // Nivel em que foi CAPTURADO (bits 0-6 de 0x46), nao o nivel atual. O gen3
  // nao armazena o nivel atual: ele deriva da experiencia pela curva de
  // crescimento da especie, tabela que este parser ainda nao tem.
  std::uint8_t met_level = 0;
  std::uint16_t held_item = 0;
  std::uint32_t experience = 0;
  std::uint8_t friendship = 0;
  std::uint8_t ability_bit = 0;  // 0 ou 1; qual das duas habilidades

  // Ordem: HP, Atk, Def, Spe, SpA, SpD — a mesma do save, que difere da
  // ordem de exibicao usual (HP, Atk, Def, SpA, SpD, Spe).
  std::uint8_t ivs[6] = {0, 0, 0, 0, 0, 0};
  std::uint8_t evs[6] = {0, 0, 0, 0, 0, 0};

  std::uint16_t moves[4] = {0, 0, 0, 0};  // 0 = slot vazio
  std::uint8_t pp[4] = {0, 0, 0, 0};

  bool is_egg = false;

  // Campos de exibicao para fontes NAO-gen3 (ex.: Legends Z-A). O parser gen3
  // nao os preenche: quando national_dex e 0, a UI deriva o dex e o nome da
  // especie pelas tabelas gen3, como sempre fez.
  std::uint16_t national_dex = 0;
  std::string species_name;

  // Nivel e shiny JA RESOLVIDOS pela fonte (spec 082). O gen3 deriva os dois
  // do proprio save — nivel pela curva de crescimento, shiny com limiar 8 —
  // e continua fazendo isso quando estes campos ficam zerados.
  //
  // Fontes modernas nao podem derivar aqui: a tabela `Personal` para na dex
  // 386, entao `ComputeStats` devolveria nivel 0 para tudo do gen8/gen9; e o
  // limiar do shiny virou 16 a partir do gen6, entao `is_shiny()` classificaria
  // errado. Quem sabe a resposta e a fonte, e ela a entrega pronta.
  std::uint8_t display_level = 0;
  bool display_shiny = false;

  // Identidade para a barra de status (spec 098). O parser gen3 preenche
  // OT/idioma/origem do proprio slot; o sexo do gen3 deriva do personality
  // contra o gender_ratio da especie, entao fica 0xFF e a UI resolve. Fontes
  // modernas entregam os quatro prontos (ToBoxPokemon).
  std::string ot_name;
  std::uint8_t language = 0;     // 1=JPN 2=ENG 3=FRE 4=ITA 5=GER 7=SPA ...
  std::uint8_t origin_game = 0;  // codigo do formato de origem (gen3 ou moderno)
  std::uint8_t display_gender = 0xFF;  // 0=M 1=F 2=sem sexo 0xFF=derivar (gen3)
  std::uint8_t display_ball = 0;       // id da pokebola (spec 099); 0=desconhecida

  // Os 80 bytes CRUS do slot, como estao no save.
  //
  // Existem porque o parser nao decodifica tudo: a word `origins` vira so
  // met_level, descartando Poke Ball, jogo de origem e idioma, e ribbons nunca
  // sao lidas. A §7 da pesquisa exige que esses campos sobrevivam ao
  // armazenamento — guardar os bytes e o unico jeito de nao perde-los.
  //
  // Sao redundantes com os campos parseados (saem daqui), e essa e a troca
  // aceita na spec 028: 80 bytes a mais no struct em vez de mudar BoxSource,
  // MoveSession, as celulas e as duas telas.
  //
  // Zerado quando o slot esta vazio.
  std::uint8_t raw[kBoxPokemonSize] = {};

  // O Pokemon MODERNO original (PK8/PK9/PA8/PB8/PB7), quando esta entrada veio
  // de um save de Switch (spec 086). Mesma regra do `raw` acima, um formato
  // acima: mover pela tela move o registro de exibicao, e sem os bytes
  // originais o commit gravaria um Pokemon mutilado — os campos que a tela
  // nao mostra (tracker, ribbons, memorias) nao existem aqui.
  //
  // shared_ptr CONST e imutavel de proposito: as copias que a MoveSession faz
  // do BoxPokemon compartilham o mesmo payload congelado na abertura do save.
  // nullptr = origem gen3/NestBox (o `raw` e a verdade) ou slot vazio.
  std::shared_ptr<const ::pkm::Pokemon> modern;

  bool empty() const { return species == 0; }

  // Shiny: TID ^ SID ^ PID_alto ^ PID_baixo < 8.
  //
  // O limiar e 8 no gen3 — virou 16 so a partir do gen6. Como este parser le
  // saves de gen3, 8 e o valor certo (spec 025).
  //
  // ot_id guarda os dois ids: TID nos 16 bits baixos, SID nos altos.
  bool is_shiny() const {
    if (empty()) return false;
    // Fonte moderna ja resolveu (limiar 16, spec 082).
    if (display_shiny) return true;
    const std::uint16_t tid = static_cast<std::uint16_t>(ot_id & 0xFFFF);
    const std::uint16_t sid = static_cast<std::uint16_t>(ot_id >> 16);
    const std::uint16_t pid_lo = static_cast<std::uint16_t>(personality & 0xFFFF);
    const std::uint16_t pid_hi = static_cast<std::uint16_t>(personality >> 16);
    return (tid ^ sid ^ pid_lo ^ pid_hi) < 8;
  }

  // Natureza nao e armazenada: deriva da personality, o mesmo byte usado para
  // permutar as substruturas.
  std::uint8_t nature() const {
    return static_cast<std::uint8_t>(personality % 25);
  }
};

struct SaveFile {
  std::size_t base_offset = 0;  // 0 para .sav cru, 100 para SharkPortSave
  Slot slot_a;
  Slot slot_b;
  int active_slot = 0;  // 0 = A, 1 = B
};

// Dados do treinador, lidos da secao 0 do slot ativo. E o que a tela de
// selecao mostra sem abrir as caixas.
struct TrainerInfo {
  std::string name;
  std::uint16_t public_id = 0;
  std::uint16_t hours = 0;
  std::uint8_t minutes = 0;
};

// --- API -------------------------------------------------------------------

// Checksum gen3: soma os bytes uteis em palavras de 32 bits little-endian,
// depois dobra os 16 bits altos sobre os baixos.
std::uint16_t ComputeChecksum(const std::uint8_t* data, std::size_t size);

// Localiza o inicio do save dentro do arquivo procurando a signature
// 0x08012025 no footer da primeira secao. Suporta .sav cru (offset 0) e
// containers como SharkPortSave (.sps, offset 100).
// Devolve nullopt se nenhuma posicao plausivel for encontrada.
std::optional<std::size_t> FindSaveOffset(const std::vector<std::uint8_t>& file);

// Faz o parsing completo: detecta offset, le os dois slots, escolhe o ativo.
// Devolve nullopt se o arquivo nao contiver um save gen3 reconhecivel.
std::optional<SaveFile> ParseSave(const std::vector<std::uint8_t>& file);

// Nome, ID publico e tempo de jogo, da secao 0 do slot ativo.
std::optional<TrainerInfo> ReadTrainerInfo(
    const std::vector<std::uint8_t>& file, const SaveFile& save);

// Devolve o slot ativo (o de maior save index entre os completos).
const Slot& ActiveSlot(const SaveFile& save);

// Le um Pokemon de uma caixa. box e slot sao 0-indexed.
// Devolve nullopt se os indices estiverem fora do intervalo ou se os dados do
// PC nao puderem ser remontados.
//
// ATENCAO: remonta o PC buffer (~35 KB) a cada chamada. Para ler varios slots,
// use BuildPcBuffer + ReadBoxPokemonFrom, que reaproveitam o buffer.
std::optional<BoxPokemon> ReadBoxPokemon(const std::vector<std::uint8_t>& file,
                                         const SaveFile& save, std::size_t box,
                                         std::size_t slot);

// Remonta o PC buffer do slot ativo, concatenando as secoes na ordem logica.
// Vazio se alguma secao estiver faltando.
std::vector<std::uint8_t> BuildPcBuffer(const std::vector<std::uint8_t>& file,
                                        const SaveFile& save);

// Le um Pokemon de um PC buffer ja montado. Preferir esta versao ao percorrer
// uma caixa inteira: evita 30 remontagens de 35 KB por tela.
std::optional<BoxPokemon> ReadBoxPokemonFrom(
    const std::vector<std::uint8_t>& pc_buffer, std::size_t box,
    std::size_t slot);

// Le um Pokemon dos seus 80 bytes crus, fora de qualquer save. E o que permite
// ao NestBox guardar bytes e reconstruir o Pokemon depois (spec 028).
//
// `rec` precisa apontar para kBoxPokemonSize bytes validos.
BoxPokemon ParseBoxPokemonRecord(const std::uint8_t* rec);

// --- Escrita (spec 033) ----------------------------------------------------
//
// ATENCAO: estas funcoes alteram save de jogo. Nada aqui deve ser chamado sem
// backup verificado antes — ver spec 032 e o guardrail do CLAUDE.md.

// Escreve os 80 bytes crus de um Pokemon num slot do PC buffer. `rec` nulo
// esvazia o slot (zera os 80 bytes).
//
// Devolve false se a caixa/slot estiver fora da faixa ou o buffer for pequeno
// demais — sem tocar em nada.
bool WriteBoxPokemonTo(std::vector<std::uint8_t>& pc_buffer, std::size_t box,
                       std::size_t slot, const std::uint8_t* rec);

// Aplica um PC buffer alterado de volta sobre os bytes do arquivo, na ordem
// inversa de BuildPcBuffer, e recalcula o checksum das secoes tocadas.
//
// So mexe nas secoes de PC (5..13); trainer info, party e itens ficam
// intocados (TD-01 da spec 033).
//
// Devolve false — sem alterar `file` — se o buffer tiver tamanho errado ou
// faltar alguma secao de PC no slot ativo.
bool ApplyPcBuffer(std::vector<std::uint8_t>& file, const SaveFile& save,
                   const std::vector<std::uint8_t>& pc_buffer);

// --- Registro completo de 80 bytes (spec 108) ------------------------------
//
// O parser de tela (ParseBoxPokemonRecord) descarta o que a tela nao usa:
// palavra de origins inteira, ribbons, contest, pokerus, met_location, flags.
// O construtor da transferencia entre geracoes precisa de TUDO — dai o par
// DecodeFullRecord/EncodeFullRecord, cujo criterio e roundtrip byte-identico.
//
// Os nomes ficam CRUS (charset gen3) de proposito: o roundtrip nao pode
// depender de uma conversao de charset ser bijetora. Quem cria nome novo na
// descida usa EncodeGen3String.
struct FullRecord {
  std::uint32_t personality = 0;
  std::uint32_t ot_id = 0;
  std::uint8_t nickname_raw[10] = {};
  std::uint8_t language = 0;
  std::uint8_t flags = 0;  // 0x13: bad egg / has species / uses egg name
  std::uint8_t ot_name_raw[7] = {};
  std::uint8_t markings = 0;
  std::uint16_t unused_1e = 0;  // 0x1E, preservado

  // Growth
  std::uint16_t species = 0;  // indice INTERNO gen3
  std::uint16_t held_item = 0;
  std::uint32_t experience = 0;
  std::uint8_t pp_bonuses = 0;
  std::uint8_t friendship = 0;
  std::uint16_t growth_unknown = 0;
  // Attacks
  std::uint16_t moves[4] = {};
  std::uint8_t pp[4] = {};
  // EVs & Condition
  std::uint8_t evs[6] = {};
  std::uint8_t contest[6] = {};
  // Misc
  std::uint8_t pokerus = 0;
  std::uint8_t met_location = 0;
  std::uint16_t origins = 0;  // met_level | origem<<7 | bola<<11 | sexo_ot<<15
  std::uint32_t iv32 = 0;     // 6x5 bits de IV | egg<<30 | ability<<31
  std::uint32_t ribbons = 0;
};

// Decodifica os 80 bytes por completo. nullopt para slot vazio.
std::optional<FullRecord> DecodeFullRecord(const std::uint8_t* rec);

// O inverso exato: substruturas na ordem do personality, cifra PID^OTID e o
// checksum do registro (0x1C) DERIVADO dos 48 bytes em claro — e ele que o
// jogo confere; errado vira bad egg.
void EncodeFullRecord(const FullRecord& r, std::uint8_t out[80]);

// UTF-8 -> charset gen3 (para nomes novos na descida, spec 110). Trunca em
// max_len e completa com o terminador 0xFF. Caracter sem mapa vira espaco.
void EncodeGen3String(const std::string& utf8, std::uint8_t* out,
                      std::size_t max_len);

// Nome da especie no indice interno do gen3 (National Dex reordenada).
// Devolve "???" para indices desconhecidos.
std::string SpeciesName(std::uint16_t species);

// Maior indice de especie valido no gen3 + 1. Especie fora dessa faixa indica
// leitura incorreta (tipicamente permutacao errada das substruturas).
std::uint16_t SpeciesTableSize();

// National Dex correspondente ao indice interno do gen3. Devolve 0 se o indice
// for invalido. Os indices 1-251 coincidem; 277+ sao deslocados (277 -> 252).
// Necessario porque os arquivos de sprite sao nomeados por National Dex.
int NationalDex(std::uint16_t species);

// O inverso: indice interno gen3 de uma National Dex. 0 se a especie nao
// existe no gen3 (dex > 386 ou buraco da tabela) — e o portao da descida
// (spec 110).
std::uint16_t InternalFromDex(int national_dex);

// Nome da natureza (0-24). "???" se fora da faixa.
std::string NatureName(std::uint8_t nature);

// Nome do golpe. Vazio se o slot estiver vazio (move 0).
std::string MoveName(std::uint16_t move);

// Nome da habilidade (ids 0-76). Vazio se fora da faixa — dado corrompido nao
// deve estourar a tela de detalhes. O id vem de PersonalInfo::ability().
std::string AbilityName(std::uint8_t id);

// Nome por numero da DEX NACIONAL, cobrindo 1..1025 (spec 035).
//
// Diferente de SpeciesName(), que recebe o indice INTERNO do gen3 e so alcanca
// as 386 do gen3. Esta e a que serve para exibir Pokemon de qualquer geracao —
// a tabela por tras e a mesma que o parser do Legends Z-A ja usava, so que
// agora acessivel a todo o app.
//
// Vazio para dex fora da faixa.
std::string SpeciesNameByDex(int dex);

// Maior numero de dex conhecido pelas tabelas de nome.
int MaxKnownDex();

// Ultima especie do gen3. Acima disto o projeto tem nome e sprite, mas NAO tem
// personal table — tipos e stats nao existem (spec 035).
inline constexpr int kMaxGen3Dex = 386;

// --- Personal data ---------------------------------------------------------
//
// Tipos e base stats vem da tabela do jogo, nao do save. Indexados por
// NATIONAL DEX (use NationalDex() para converter do indice interno).

struct PersonalInfo {
  std::uint8_t base_stats[6] = {0, 0, 0, 0, 0, 0};  // HP, Atk, Def, Spe, SpA, SpD
  std::uint8_t type1 = 0;
  std::uint8_t type2 = 0;
  std::uint8_t growth_rate = 0;  // byte 19; 0=MedFast, 3=MedSlow, 5=Slow
  // Bytes 22-23: as duas habilidades da especie. Como no caso do tipo, uma
  // especie com habilidade unica repete o mesmo id nos dois.
  std::uint8_t ability1 = 0;
  std::uint8_t ability2 = 0;
  // Byte 16: limiar de sexo. (personality & 0xFF) < ratio => femea.
  // 0 = sempre macho, 254 = sempre femea, 255 = sem sexo. Conferido na spec
  // 098 contra Bulbasaur (31), Nidoran-F (254) e Magnemite (255).
  std::uint8_t gender_ratio = 0;

  // Tipo unico e representado com type1 == type2 na tabela do jogo.
  bool single_type() const { return type1 == type2; }

  // Qual das duas habilidades este Pokemon tem. `bit` e o BoxPokemon::
  // ability_bit (bit 31 da word de IVs).
  std::uint8_t ability(std::uint8_t bit) const {
    return bit ? ability2 : ability1;
  }
};

// Sexo para exibicao (spec 098): 0=M 1=F 2=sem sexo. Fontes modernas ja
// entregam em display_gender; no gen3 deriva do personality contra o
// gender_ratio da especie.
std::uint8_t Gender(const BoxPokemon& mon);

// Nivel atual a partir da experiencia. O gen3 NAO armazena o nivel: ele deriva
// da curva de crescimento da especie.
std::uint8_t LevelFromExp(std::uint32_t exp, std::uint8_t growth_rate);

// Stats de batalha calculados. Pokemon guardado em caixa nao os armazena — o
// bloco de stats so existe no formato de party (100 bytes). Aqui aplicamos a
// formula do jogo sobre base stats, IVs, EVs, nivel e natureza.
//
// Ordem: HP, Atk, Def, Spe, SpA, SpD — a mesma do save.
struct BattleStats {
  std::uint16_t values[6] = {0, 0, 0, 0, 0, 0};
  std::uint8_t level = 0;
};

BattleStats ComputeStats(const BoxPokemon& mon);

// Devolve os dados da especie por National Dex. Entrada zerada se fora da
// faixa (1-386).
PersonalInfo Personal(int national_dex);

// Nome do tipo na numeracao do gen3. "???" se fora da faixa.
std::string TypeName(std::uint8_t type);

}  // namespace pokehome::gen3
