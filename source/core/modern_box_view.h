// Ponte entre o motor de saves modernos (specs 063-081) e a TELA (spec 082).
//
// A tela inteira consome `g3::BoxPokemon` — uma decisao que vem da spec 001 e
// que atravessa `BoxSource`, `MoveSession`, as celulas e as duas telas. Trocar
// o modelo de exibicao custaria reescrever tudo isso; converter na entrada
// custa esta funcao.
//
// A ARMADILHA que este arquivo existe para nao repetir: o `pk9::Parse` guarda
// o INDICE INTERNO do gen9, nao a National Dex (Pawmo e 955 no binario e 922
// na dex). Copiar `p.species` direto troca a especie em 106 casos e nenhum
// checksum acusa — foi o bug real corrigido na spec 076/069. A unica leitura
// correta e `pkm::NationalDex(p)`, e e a que esta aqui.
//
// Vive em source/core/ e nao em src/ui/ porque e LOGICA: sem tela, testavel
// por ctest (regra do CLAUDE.md).
#pragma once

#include <vector>

#include "gen3_save.h"
#include "pkm_model.h"
#include "save_writer.h"

namespace pokehome::view {

// Converte para o registro que a tela desenha. Preenche so o que o formato
// moderno de fato guarda; o resto fica zerado (nunca inventado).
//
// `species` recebe a National Dex, e nao o indice interno, porque o `empty()`
// e o sprite da tela leem esse campo — e um Pokemon com dex 0 sumiria.
//
// Desde a spec 086 o registro tambem carrega `modern`: uma copia congelada do
// pkm::Pokemon original, para o caminho de volta (ApplyBoxChanges) gravar o
// Pokemon intacto em vez de reconstrui-lo dos campos de exibicao.
gen3::BoxPokemon ToBoxPokemon(const pkm::Pokemon& p);

// Uma alteracao de slot vinda da MoveSession, enderecada como a fonte enxerga
// (caixa/slot do proprio save, nao o stride do gen3).
struct BoxChange {
  std::size_t box = 0;
  std::size_t slot = 0;
  gen3::BoxPokemon mon;  // vazio = limpar o slot
};

// Aplica as alteracoes da sessao a um SaveData (spec 086). O caminho de volta
// da ToBoxPokemon: cada slot recebe o `modern` INTACTO do registro — nunca uma
// reconstrucao a partir dos campos de exibicao.
//
// Devolve false SEM aplicar nada se alguma alteracao nao-vazia vier sem
// payload moderno (Pokemon de origem gen3/NestBox): gravar isso mutilaria o
// dado, e transferir para cima e uma spec propria. Tudo-ou-nada de proposito —
// um commit parcial deixaria o save e o NestBox contando historias diferentes.
bool ApplyBoxChanges(savew::SaveData& sd, const std::vector<BoxChange>& changes);

}  // namespace pokehome::view
