// Transferencia end-to-end (spec 075, G13 da descoberta
// escrita-e-transferencia).
//
// A peca que junta 063..072: mover um Pokemon de um save para outro aplicando
// as regras da Pokemon HOME. Nada aqui parseia binario, converte formato ou
// calcula learnset — tudo isso ja existe e foi provado nas specs anteriores.
// O que existe aqui e a ORQUESTRACAO, e a garantia de que ela nao deixa os
// dois saves em estados incoerentes.
//
// --- Commit atomico (pesquisa §4) -----------------------------------------
//
//     "nada e gravado durante a movimentacao; o unico ponto de escrita e o
//      Save changes and exit"
//
// Dai as DUAS fases, que sao o motivo de o modulo existir:
//
//   1. Prepare() — altera os dois SaveData EM MEMORIA. Nenhum arquivo tocado.
//      Um veredito kBlocked para aqui, e nada foi alterado em lugar nenhum.
//   2. Commit()  — serializa os DOIS antes de abrir qualquer arquivo, grava o
//      primeiro, grava o segundo. Se o segundo falhar, restaura o primeiro a
//      partir do conteudo original que guardou.
//
// TD-01 da spec: nao existe transacao real de sistema de arquivos aqui. O
// rollback tambem pode falhar (disco cheio, arquivo travado), e nesse caso o
// resultado e kRollbackFailed — um erro DISTINTO, em vez de fingir que
// reverteu.
//
// --- Por que a transferencia altera os DOIS saves --------------------------
//
// Mesmo "so" movendo um Pokemon: o item segurado volta para a bag do save de
// ORIGEM (§7, spec 072) e o Pokemon sai da caixa de origem. E exatamente por
// isso que a atomicidade importa — sem ela, uma falha no meio duplica ou
// evapora o Pokemon.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bag_writer.h"
#include "moveset_memory.h"
#include "pkm_model.h"
#include "save_writer.h"
#include "transfer_rules.h"

namespace transfer {

namespace rules = pokehome::rules;
namespace ms = pokehome::moveset;
namespace cp = pokehome::compat;

enum class Status : std::uint8_t {
  kOk,
  kBlocked,          // as regras recusaram; nada foi alterado
  kInvalidSlot,      // origem vazia, ou indices fora da caixa
  kNoRoom,           // o destino nao tem slot livre
  kUnconvertible,    // a especie nao e representavel no formato de destino
  kWriteFailed,      // a gravacao falhou, e o rollback devolveu o estado
  kRollbackFailed,   // a gravacao falhou E o rollback tambem — estado incerto
};

const char* StatusName(Status s);

struct Result {
  Status status = Status::kOk;
  // Legivel, para a tela mostrar direto. Vem do `reason` da spec 070 quando
  // kBlocked.
  std::string message;

  // O veredito kWarning da 070 nao impede a transferencia, mas nao pode se
  // perder no caminho (TD-03 da 070: o motivo mais grave vence, mas a tela
  // ainda precisa dele).
  bool warning = false;
  std::string warning_reason;

  // Item devolvido a bag da ORIGEM (0 = o Pokemon nao segurava nada).
  std::uint16_t item_returned = 0;
  // O Pokemon segurava item mas a bag do jogo de ORIGEM nao e suportada pelo
  // bag_writer (SV/BDSP/PLA — pendencia declarada da spec 072). O item some,
  // que e a regra, mas quem chama fica sabendo. Contrabandear a limitacao em
  // silencio e o que esta spec se proibe.
  bool item_lost = false;

  bool tracker_assigned = false;
  // O handling trainer NAO pode ser preenchido: o save de destino nao tem
  // nenhum Pokemon de onde ler o nome do treinador (caixas vazias), e o
  // savew::SaveData nao expoe o OT do save. O Pokemon entra com o handler
  // como estava. Declarado em vez de inventar um nome — spec 078.
  bool handler_unknown = false;
  // O moveset do destino veio da memoria (true) ou foi resetado por nivel
  // (false). So faz sentido quando o destino e PLA ou BDSP.
  bool moveset_restored = false;
  bool moveset_reset = false;

  bool ok() const { return status == Status::kOk; }
};

// O que a operacao precisa saber e nao consegue derivar sozinha.
struct Request {
  std::size_t src_box = 0;
  std::size_t src_slot = 0;
  // Slot de destino. Se `dst_auto` for true, procura o primeiro livre e estes
  // dois sao ignorados.
  std::size_t dst_box = 0;
  std::size_t dst_slot = 0;
  bool dst_auto = true;

  // Nivel do Pokemon. NAO deriva do modelo: o nivel sai da experiencia pela
  // curva de crescimento da especie, e a tabela de curva so existe para o
  // gen3 (gen3_personal.h). Mesma decisao da spec 070 (SaveContext::level).
  // 0 = desconhecido; nesse caso a regra do Hyper Training nao bloqueia e o
  // reset por nivel nao acontece.
  std::uint8_t level = 0;
};

// O plano: os dois saves ja ALTERADOS, em memoria, e o caminho de cada um.
// Nenhum arquivo foi tocado ainda. Um plano com `result.ok() == false` nao
// deve ser commitado — o Commit() recusa.
struct Plan {
  Result result;
  savew::SaveData src;
  savew::SaveData dst;
  std::string src_path;
  std::string dst_path;
  // A memoria de moveset, ja com o snapshot da origem gravado. Quem chama
  // persiste no arquivo do NestBox — este modulo nao faz I/O de banco.
  ms::Memory memory;
};

// Fase 1. `src` e `dst` sao copias de trabalho: a funcao as altera em memoria
// e as devolve dentro do Plan. Nada e gravado.
//
// `dest_game` e o jogo de destino no vocabulario das REGRAS (compat::Game), que
// e mais amplo que o do writer — a 070 sabe responder por 23 jogos.
Plan Prepare(const savew::SaveData& src, const savew::SaveData& dst,
             const std::string& src_path, const std::string& dst_path,
             cp::Game dest_game, const Request& req, const ms::Memory& memory);

// Fase 2 — o UNICO ponto de escrita. Serializa os dois ANTES de abrir qualquer
// arquivo; grava o primeiro; grava o segundo; e se o segundo falhar, restaura
// o primeiro byte a byte.
//
// Devolve o Plan.result com o status atualizado. kOk so quando os DOIS
// arquivos foram gravados.
Result Commit(const Plan& plan);

// Conveniencia: Prepare + Commit. E o que a tela chama.
Result Run(const savew::SaveData& src, const savew::SaveData& dst,
           const std::string& src_path, const std::string& dst_path,
           cp::Game dest_game, const Request& req, ms::Memory* memory);

// --- Auxiliares expostos para o teste --------------------------------------

// O jogo do writer no vocabulario das regras. Existe porque savew::Game (5
// jogos, um layout de save cada) e compat::Game (23 jogos) sao grandezas
// diferentes de proposito.
cp::Game ToCompatGame(savew::Game g);

// O jogo tem engine de golpes propria, e portanto memoriza moveset? Sao PLA e
// BDSP — os mesmos que a §7 cita e que learnset.h cobre. `out` recebe o enum
// da memoria quando devolve true.
bool MemorizesMoveset(savew::Game g, ms::Game* out);

}  // namespace transfer
