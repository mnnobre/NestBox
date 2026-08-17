// Verificador de legalidade — nivel 2, coerencia interna (spec 079).
//
// A PROMESSA, dita em voz alta para ninguem confiar demais depois:
//
//   Isto detecta ADULTERACAO CASUAL, nao "detecta hack".
//
// Quem edita um save mexe em especie, IVs e golpes e esquece dos campos
// derivados — e e essa incoerencia que pegamos. Um Pokemon gerado por uma
// ferramenta que se importa com legalidade (AutoLegality) passa liso por
// tudo o que ha aqui, porque ele NAO e internamente incoerente. Provar
// procedencia exigiria o encounter database do PkHeX, que a pesquisa
// docs/pesquisa-verificador-legalidade.md concluiu ser inviavel portar.
//
// Medido nos saves reais do dono (spec 079, tools/pkhex-probe079 com o PkHeX
// como oraculo): deteccao 90,98% dos ilegais, falso positivo 0 em 280 legais.
//
// O QUE ESTA FUNCAO NAO DECIDE: se o Pokemon aparece na tela. A decisao do
// dono (spec/discovery/escrita-e-transferencia.md) e que o reprovado CONTINUA
// LISTADO na caixa e so fica desabilitado para TRANSFERIR. O bloqueio e da
// transferencia, nunca da leitura.
#pragma once

#include <string>
#include <vector>

#include "pkm_model.h"

namespace legality {

// Um motivo de suspeita. `code` e estavel para teste e log; `reason` e texto
// legivel que a tela pode mostrar ao jogador.
struct LegalityIssue {
  std::string code;
  std::string reason;
};

struct LegalityResult {
  bool suspect = false;
  std::vector<LegalityIssue> issues;
};

// Roda todas as verificacoes de coerencia interna. Nunca lanca; um Pokemon
// vazio (species 0) devolve resultado limpo — slot vazio nao e adulteracao.
LegalityResult CheckLegality(const pkm::Pokemon& p);

}  // namespace legality
