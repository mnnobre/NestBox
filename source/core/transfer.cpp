// Transferencia end-to-end (spec 075, G13).
#include "transfer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "pkm_convert.h"

namespace fs = std::filesystem;

namespace transfer {
namespace {

// O formato de PKM que cada save guarda. E o mesmo mapeamento que o
// save_writer usa ao parsear; aqui ele serve para o sentido inverso (para qual
// formato converter).
pkm::Format FormatOf(savew::Game g) {
  switch (g) {
    case savew::Game::kSwSh: return pkm::Format::kPK8;
    case savew::Game::kSV:   return pkm::Format::kPK9;
    case savew::Game::kPLA:  return pkm::Format::kPA8;
    case savew::Game::kBDSP: return pkm::Format::kPB8;
    case savew::Game::kLGPE: return pkm::Format::kPB7;
  }
  return pkm::Format::kNone;
}

// std::filesystem::path e obrigatorio: ifstream/ofstream com std::string nao
// abre nome com caractere especial no Windows.
std::vector<std::uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(fs::path(path), std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
}

bool WriteFile(const std::string& path, const std::vector<std::uint8_t>& data) {
  std::ofstream f(fs::path(path), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(data.data()),
          static_cast<std::streamsize>(data.size()));
  f.close();
  return !f.fail();
}

// Slot vazio: o Set() do writer trata `mon.empty()` (species 0) como remocao.
pkm::Pokemon EmptyMon() { return pkm::Pokemon{}; }

}  // namespace

const char* StatusName(Status s) {
  switch (s) {
    case Status::kOk:             return "ok";
    case Status::kBlocked:        return "bloqueado";
    case Status::kInvalidSlot:    return "slot invalido";
    case Status::kNoRoom:         return "destino sem espaco";
    case Status::kUnconvertible:  return "especie nao cabe no destino";
    case Status::kWriteFailed:    return "gravacao falhou (revertida)";
    case Status::kRollbackFailed: return "gravacao E reversao falharam";
  }
  return "?";
}

cp::Game ToCompatGame(savew::Game g) {
  switch (g) {
    case savew::Game::kSwSh: return cp::Game::kSwordShield;
    case savew::Game::kSV:   return cp::Game::kScarletViolet;
    case savew::Game::kPLA:  return cp::Game::kLegendsArceus;
    case savew::Game::kBDSP: return cp::Game::kBdsp;
    case savew::Game::kLGPE: return cp::Game::kLetsGo;
  }
  return cp::Game::kCount;
}

bool MemorizesMoveset(savew::Game g, ms::Game* out) {
  if (g == savew::Game::kPLA) {
    if (out) *out = ms::Game::kLegendsArceus;
    return true;
  }
  if (g == savew::Game::kBDSP) {
    if (out) *out = ms::Game::kBdsp;
    return true;
  }
  return false;
}

Plan Prepare(const savew::SaveData& src_in, const savew::SaveData& dst_in,
             const std::string& src_path, const std::string& dst_path,
             cp::Game dest_game, const Request& req,
             const ms::Memory& memory) {
  Plan plan;
  plan.src = src_in;
  plan.dst = dst_in;
  plan.src_path = src_path;
  plan.dst_path = dst_path;
  plan.memory = memory;
  Result& r = plan.result;

  // --- 1. Ler o Pokemon da ORIGEM ----------------------------------------
  if (req.src_box >= plan.src.box_count || req.src_slot >= plan.src.slots_per_box ||
      !plan.src.At(req.src_box, req.src_slot).present) {
    r.status = Status::kInvalidSlot;
    r.message = "o slot de origem esta vazio";
    return plan;
  }
  pkm::Pokemon mon = plan.src.At(req.src_box, req.src_slot).mon;

  // --- 2. As REGRAS antes de qualquer alteracao --------------------------
  // Um kBlocked tem de sair daqui sem nada alterado nos dois saves. Como
  // ainda nao mexemos em `plan.src`/`plan.dst`, isso vale por construcao.
  rules::SaveContext ctx;
  ctx.level = req.level;
  for (std::size_t b = 0; b < plan.dst.box_count; ++b)
    for (std::size_t s = 0; s < plan.dst.slots_per_box; ++s)
      if (plan.dst.At(b, s).present)
        ctx.species_present.push_back(pkm::NationalDex(plan.dst.At(b, s).mon));

  const rules::RuleResult verdict = rules::CanTransfer(mon, dest_game, ctx);
  if (verdict.verdict == rules::Verdict::kBlocked) {
    r.status = Status::kBlocked;
    r.message = verdict.reason;
    return plan;
  }
  if (verdict.verdict == rules::Verdict::kWarning) {
    r.warning = true;
    r.warning_reason = verdict.reason;
  }

  // --- 3. Onde ele vai cair no destino ------------------------------------
  std::size_t db = req.dst_box, ds = req.dst_slot;
  if (req.dst_auto) {
    bool found = false;
    for (std::size_t b = 0; b < plan.dst.box_count && !found; ++b)
      for (std::size_t s = 0; s < plan.dst.slots_per_box && !found; ++s)
        if (!plan.dst.At(b, s).present) { db = b; ds = s; found = true; }
    if (!found) {
      r.status = Status::kNoRoom;
      r.message = "todas as caixas do destino estao cheias";
      return plan;
    }
  } else if (db >= plan.dst.box_count || ds >= plan.dst.slots_per_box ||
             plan.dst.At(db, ds).present) {
    r.status = Status::kInvalidSlot;
    r.message = "o slot de destino nao esta livre";
    return plan;
  }

  // --- 4. ITEM — a escrita e na ORIGEM (§7) ------------------------------
  // "o HOME nao armazena itens — ao depositar, o item volta automaticamente
  //  para a Bag do jogo de ORIGEM. Nunca viaja, nunca se perde"
  //
  // Feito ANTES da conversao porque a operacao e sobre o save de origem, e o
  // WithdrawHeldItem le o Pokemon direto do slot de la.
  if (mon.held_item != 0) {
    r.item_returned = mon.held_item;
    if (bagw::Supported(plan.src.game) &&
        bagw::WithdrawHeldItem(plan.src, req.src_box, req.src_slot)) {
      // O Withdraw ja zerou o held_item no slot da origem; a copia local
      // precisa acompanhar, senao o item viajaria junto.
      mon.held_item = 0;
    } else {
      // TD-03: bag nao suportada (SV/BDSP/PLA — pendencia declarada da spec
      // 072). O item some, que e a regra, mas quem chama FICA SABENDO. Sumir
      // em silencio seria contrabandear a limitacao para dentro do produto.
      mon.held_item = 0;
      r.item_lost = true;
    }
  }

  // --- 5. TRACKER — atribui se 0, NUNCA sobrescreve (spec 071) -----------
  r.tracker_assigned = ms::AssignTracker(mon);

  // --- 6. MOVESET: snapshot do jogo de ORIGEM ----------------------------
  // So os jogos de engine propria (PLA, BDSP) memorizam. Sair de SwSh/SV/LGPE
  // nao gera snapshot porque os tres compartilham a engine — nao ha o que
  // restaurar depois (TD-02).
  ms::Game src_ms{};
  if (MemorizesMoveset(plan.src.game, &src_ms))
    plan.memory.Remember(mon, src_ms);

  // --- 7. CONVERSAO de formato -------------------------------------------
  const pkm::Format origem_fmt = mon.format;
  const pkm::Format dst_fmt = FormatOf(plan.dst.game);
  auto converted = pkm::Convert(mon, dst_fmt);
  if (!converted) {
    r.status = Status::kUnconvertible;
    r.message = "a especie nao e representavel no formato de destino";
    return plan;
  }
  mon = std::move(*converted);

  // --- Ajustes de ENTRADA -------------------------------------------------
  //
  // Obedience/scale no PK9, sentinela de egg location, effort levels do PLA.
  // Desde a spec 143 as tres regras vivem em `pkm::AjustesDeEntrada`, e nao
  // aqui: elas existiam SO neste caminho, e o deposito pela tela — que e o
  // que o dono usa — as pulava inteiras. Um Pokemon do BDSP chegava ao
  // Legends: Arceus com o sentinela do BDSP e virava ovo no jogo.
  //
  // O tera type do SV o proprio Convert deriva (§7, spec 069).
  pkm::AjustesDeEntrada(mon, origem_fmt);

  // O HANDLING TRAINER. Achado pela bateria da spec 078 (G14), e so ela
  // poderia te-lo achado: nos saves de terceiros da spec 075 os Pokemon ja
  // eram 100% ilegais, entao `Current handler cannot be the OT` se perdia no
  // meio dos outros motivos. Com os saves LIMPOS (077) e o criterio ABSOLUTO,
  // ele apareceu em 18 dos 20 pares.
  //
  // O valor NAO foi deduzido. Sonda `tools/pkhex-handler`, que roda o
  // `SaveFile.AdaptToSaveFile` — a operacao que o proprio PkHeX executa ao
  // largar um Pokemon na caixa de outro save:
  //
  //   ANTES   OT='NESTBOX' HT=''        handler=0 HTfriend=0
  //   DEPOIS  OT='NESTBOX' HT='NESTBOX' handler=1 HTfriend=50
  //
  // Ou seja: quem RECEBE vira o handler, mesmo quando o nome do treinador do
  // destino coincide com o do OT. E a semantica do jogo — o Pokemon foi
  // negociado, e quem o segura agora nao e mais o treinador original.
  //
  // O nome do treinador de destino sai do PROPRIO save de destino: o
  // `savew::SaveData` nao expoe o OT do save (spec 068 modela caixas), entao
  // ele e lido do primeiro Pokemon ja presente nas caixas do destino. Se o
  // destino estiver vazio nao ha de onde tirar, e o campo fica como estava —
  // declarado em `r.handler_unknown` em vez de inventar um nome.
  if (dst_fmt != pkm::Format::kPB7) {  // o PB7 nao tem HT (formato pre-HOME)
    std::string dst_ot;
    std::uint8_t dst_ot_gender = 0, dst_ot_lang = 0;
    for (std::size_t bi = 0; bi < plan.dst.box_count && dst_ot.empty(); ++bi)
      for (std::size_t si = 0; si < plan.dst.slots_per_box && dst_ot.empty(); ++si) {
        const auto& s = plan.dst.At(bi, si);
        if (!s.present || s.mon.ot_name.empty()) continue;
        dst_ot = s.mon.ot_name;
        dst_ot_gender = s.mon.ot_gender;
        dst_ot_lang = s.mon.language;
      }
    if (dst_ot.empty()) {
      r.handler_unknown = true;
    } else {
      mon.ht_name = dst_ot;
      mon.ht_name_raw = {};  // quem muda o texto zera o raw (spec 145)
      mon.ht_gender = dst_ot_gender;
      mon.ht_language = dst_ot_lang;
      mon.current_handler = 1;
      // 50 e a amizade base que o AdaptToSaveFile grava. Nao e chute: e o
      // valor medido na sonda, para as tres especies testadas.
      mon.ht_friendship = 50;
    }
  }

  // --- 8. MOVESET: o que ele deve ter ao ENTRAR --------------------------
  //   ja esteve la  -> restaura o memorizado         (G11)
  //   primeira vez  -> reseta por nivel via learnset (G12)
  ms::Game dst_ms{};
  if (MemorizesMoveset(plan.dst.game, &dst_ms) && req.level > 0) {
    if (plan.memory.ApplyOnEntry(mon, dst_ms, req.level))
      r.moveset_restored = true;
    else
      r.moveset_reset = true;
  }

  // --- 9. Escreve no DESTINO e remove da ORIGEM ---------------------------
  // Os dois `Set` sao EM MEMORIA. Nada foi gravado ainda — esse e o ponto do
  // TD-01.
  if (!plan.dst.Set(db, ds, mon) ||
      !plan.src.Set(req.src_box, req.src_slot, EmptyMon())) {
    r.status = Status::kInvalidSlot;
    r.message = "falha ao gravar o slot em memoria";
    return plan;
  }

  r.status = Status::kOk;
  return plan;
}

Result Commit(const Plan& plan) {
  Result r = plan.result;
  if (!r.ok()) return r;  // plano recusado nao commita

  // O UNICO ponto de escrita. Os DOIS saves sao serializados ANTES de
  // qualquer arquivo ser aberto: se o Save() de um deles falhar/estourar, nada
  // foi tocado no disco.
  const auto src_bytes = savew::Save(plan.src);
  const auto dst_bytes = savew::Save(plan.dst);
  if (src_bytes.empty() || dst_bytes.empty()) {
    r.status = Status::kWriteFailed;
    r.message = "a serializacao de um dos saves falhou; nada foi gravado";
    return r;
  }

  // O conteudo ORIGINAL do primeiro arquivo, para o rollback. Lido do DISCO,
  // e nao de `plan.src.file`, porque o que precisa voltar e o que esta la —
  // nao o que achamos que esta.
  const auto src_backup = ReadFile(plan.src_path);
  if (src_backup.empty()) {
    r.status = Status::kWriteFailed;
    r.message = "nao foi possivel ler o save de origem para o rollback";
    return r;
  }

  if (!WriteFile(plan.src_path, src_bytes)) {
    // Falhou o primeiro: nada foi alterado, nao ha o que reverter.
    r.status = Status::kWriteFailed;
    r.message = "a gravacao do save de origem falhou; nada foi alterado";
    return r;
  }

  if (!WriteFile(plan.dst_path, dst_bytes)) {
    // O SEGUNDO falhou. O primeiro JA foi gravado — reverte.
    r.status = WriteFile(plan.src_path, src_backup) ? Status::kWriteFailed
                                                    : Status::kRollbackFailed;
    r.message = r.status == Status::kWriteFailed
                    ? "a gravacao do destino falhou; a origem foi revertida"
                    : "a gravacao do destino falhou E a reversao da origem "
                      "tambem — o estado no disco e INCERTO";
    return r;
  }

  return r;
}

Result Run(const savew::SaveData& src, const savew::SaveData& dst,
           const std::string& src_path, const std::string& dst_path,
           cp::Game dest_game, const Request& req, ms::Memory* memory) {
  Plan plan = Prepare(src, dst, src_path, dst_path, dest_game, req,
                      memory ? *memory : ms::Memory{});
  Result r = Commit(plan);
  // A memoria so e publicada se a gravacao deu certo — memorizar um moveset de
  // uma transferencia que nao aconteceu corromperia o indice por tracker.
  if (memory && r.ok()) *memory = plan.memory;
  return r;
}

}  // namespace transfer
