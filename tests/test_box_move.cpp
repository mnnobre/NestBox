// Teste da movimentacao de Pokemon entre caixas (spec 019).
//
// Mesmo estilo do test_gen3: sem framework, assert e um main.
//
// Estes testes existem porque o criterio de aceite da 019 nao pode depender so
// de olhar a tela: a logica de pegar/soltar/trocar e estado puro e da para
// provar aqui, sem abrir janela. A UI fica sendo so o gatilho.

#include <cstdio>
#include <map>
#include <vector>

#include "box_move.h"

namespace bx = pokehome::box;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
  if (cond) {
    std::printf("  ok   %s\n", what);
  } else {
    std::printf("  FAIL %s\n", what);
    ++g_failures;
  }
}

// Fonte falsa: um mapa de slots. Representa o que uma BoxSource devolveria,
// sem depender da UI nem de um save real.
class FakeSource {
 public:
  FakeSource(int id, bool accepts) : id_(id), accepts_(accepts) {}

  void Put(std::size_t box, std::size_t slot, std::uint16_t species) {
    bx::Pokemon p;
    p.species = species;
    data_[{id_, box, slot}] = p;
  }

  // Devolve por VALOR, como o `BoxSource::At()` de verdade (main.cpp:1058).
  // Enquanto isto devolvia referencia, a suite nao reproduzia a condicao do
  // crash da spec 151: o temporario que morre e a razao do use-after-free.
  bx::Pokemon At(const bx::SlotRef& ref) const {
    auto it = data_.find(ref);
    return it == data_.end() ? empty_ : it->second;
  }

  std::size_t Count() const {
    std::size_t n = 0;
    for (const auto& [ref, p] : data_) {
      if (!bx::IsEmpty(p)) ++n;
    }
    return n;
  }

  int id() const { return id_; }
  bool accepts() const { return accepts_; }

 private:
  int id_;
  bool accepts_;
  std::map<bx::SlotRef, bx::Pokemon> data_;
  bx::Pokemon empty_;
};

// Atalho: conteudo efetivo de um slot (fonte + overlay).
bx::Pokemon Eff(const bx::MoveSession& s, const FakeSource& src,
                const bx::SlotRef& ref) {
  return s.Get(ref, src.At(ref));
}

// O crash do console (2026-08-21): `Get` ligado ao temporario de `At()`.
//
// `auto mon = session.Get(ref, fonte->At(...))` — se `Get` devolvesse
// referencia, o `original` morreria no fim da expressao e o chamador leria
// memoria liberada. So acontece com slot SEM alteracao pendente: com
// alteracao, a referencia aponta para `changes_`, que sobrevive. Foi por isso
// que "evoluir e abrir o detalhe" quebrava e "fechar e reabrir" resolvia.
//
// AVISO ao ler este teste: ele NAO falha com o defeito plantado de volta —
// medido. `auto mon = ...` copia dentro da mesma expressao completa, e o UB
// nao se manifesta neste caso simples. O teste documenta o CONTRATO (Get
// devolve valor utilizavel mesmo sem alteracao pendente); a prova do bug foi
// o relato do console mais a leitura do codigo, nao o vermelho daqui.
void TestGetNaoPrendeTemporario() {
  std::printf("get nao prende temporario:\n");
  FakeSource save(1, true);
  save.Put(0, 0, 25);
  bx::MoveSession s;
  const bx::SlotRef ref{1, 0, 0};   // id da fonte = 1, como no FakeSource

  // sem alteracao pendente: o caminho que quebrava
  auto mon = s.Get(ref, save.At(ref));
  Check(mon.species == 25, "sem alteracao: le a fonte (25)");

  // com alteracao pendente: continua valendo. Pegar e soltar noutro slot
  // deixa `changes_` preenchido nos dois.
  const bx::SlotRef destino{1, 0, 1};
  s.Pick(ref, save.At(ref));
  s.Drop(destino, save.At(destino), true);
  auto movido = s.Get(destino, save.At(destino));
  Check(movido.species == 25, "com alteracao: le o overlay (25 no destino)");
  auto vazio = s.Get(ref, save.At(ref));
  Check(vazio.species == 0, "a origem ficou vazia no overlay");
}

void TestPick() {
  std::printf("pegar:\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);  // Pikachu no slot 0
  bx::MoveSession s;

  const bx::SlotRef from{1, 0, 0};
  Check(!s.Holding(), "comeca com a mao vazia");

  Check(s.Pick(from, Eff(s, save, from)), "pega o Pokemon do slot ocupado");
  Check(s.Holding(), "a mao fica cheia");
  Check(s.Held().species == 25, "a mao tem o Pikachu");
  Check(bx::IsEmpty(Eff(s, save, from)), "o slot de origem fica vazio");

  // Com a mao cheia nao pega outro.
  const bx::SlotRef other{1, 0, 1};
  save.Put(0, 1, 1);
  Check(!s.Pick(other, Eff(s, save, other)), "nao pega com a mao ja cheia");

  // Slot vazio nao da nada.
  bx::MoveSession s2;
  const bx::SlotRef empty{1, 0, 9};
  Check(!s2.Pick(empty, Eff(s2, save, empty)), "nao pega de slot vazio");
}

void TestDropEmpty() {
  std::printf("soltar em slot vazio:\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);
  bx::MoveSession s;

  const bx::SlotRef from{1, 0, 0};
  const bx::SlotRef to{1, 0, 5};
  s.Pick(from, Eff(s, save, from));

  Check(s.Drop(to, Eff(s, save, to), true), "solta no slot vazio");
  Check(!s.Holding(), "a mao esvazia");
  Check(Eff(s, save, to).species == 25, "o Pokemon aparece no destino");
  Check(bx::IsEmpty(Eff(s, save, from)), "a origem continua vazia");
}

void TestDropSwap() {
  std::printf("soltar em slot ocupado (troca):\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);  // Pikachu
  save.Put(0, 1, 6);   // Charizard
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 0};
  const bx::SlotRef b{1, 0, 1};
  s.Pick(a, Eff(s, save, a));

  // A troca fecha NUM GESTO (spec 087): os dois trocam de lugar e a mao
  // esvazia. Antes o ocupante ia para a mao e exigia um segundo gesto.
  Check(s.Drop(b, Eff(s, save, b), true), "solta sobre o ocupado");
  Check(Eff(s, save, b).species == 25, "o destino agora tem o Pikachu");
  Check(Eff(s, save, a).species == 6, "a origem recebeu o Charizard");
  Check(!s.Holding(), "a mao esvazia: a troca fechou num gesto so");
}

// Soltar no proprio slot de origem desfaz o gesto — nao pode duplicar nem
// sumir com ninguem.
void TestDropNaPropriaOrigem() {
  std::printf("soltar de volta na origem:\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 0};
  s.Pick(a, Eff(s, save, a));
  Check(s.Drop(a, Eff(s, save, a), true), "solta de volta onde pegou");
  Check(Eff(s, save, a).species == 25, "o Pokemon continua no lugar");
  Check(!s.Holding(), "a mao esvazia");
}

// Mover um bloco DENTRO da mesma caixa, para tras: o destino de um dos
// marcados cai na origem de outro que ainda nao foi processado.
//
// O teste existente move entre fontes diferentes (save -> NestBox), onde
// origem e destino nunca colidem — este caso ficava fora da rede.
void TestMoveSelectionMesmaCaixa() {
  std::printf("mover bloco dentro da mesma caixa (spec 088):\n");

  FakeSource save(1, true);
  save.Put(0, 3, 25);   // Pikachu
  save.Put(0, 4, 6);    // Charizard
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 3};
  const bx::SlotRef b{1, 0, 4};
  s.ToggleSelect(a, Eff(s, save, a));
  s.ToggleSelect(b, Eff(s, save, b));

  auto eff = [&](const bx::SlotRef& r) -> bx::Pokemon {
    return s.Get(r, save.At(r));
  };

  // Destino slot 2: o Pikachu (origem 3) vai para 2, e o Charizard (origem 4)
  // vai para 3 — que e a origem do Pikachu.
  const std::size_t moved = s.MoveSelection(1, 0, 2, 30, true, eff);
  Check(moved == 2, "move os dois marcados");
  Check(eff({1, 0, 2}).species == 25, "o Pikachu foi para o slot 2");
  Check(eff({1, 0, 3}).species == 6,
        "o Charizard foi para o slot 3 (origem do Pikachu) sem some-lo");
  Check(bx::IsEmpty(eff({1, 0, 4})), "o slot 4 ficou vazio");

  // O que mais importa: ninguem desapareceu.
  std::size_t total = 0;
  for (std::size_t i = 0; i < 30; ++i) {
    if (!bx::IsEmpty(eff({1, 0, i}))) ++total;
  }
  Check(total == 2, "os dois Pokemon continuam existindo (nenhum sumiu)");

  // Bloco ESPARSO movido para tras: o destino de um marcado cai exatamente na
  // origem de outro que JA foi processado (e nao no de um pendente). E a
  // ordem em que o `changes_[from] = vazio` pode apagar quem acabou de
  // chegar, se o esvaziamento nao respeitar quem ocupa o slot agora.
  FakeSource save2(1, true);
  save2.Put(0, 0, 25);   // Pikachu — sera movido para 1
  save2.Put(0, 5, 6);    // Charizard — sera movido para 0 (origem do Pikachu)
  bx::MoveSession s2;

  const bx::SlotRef x{1, 0, 0};
  const bx::SlotRef y{1, 0, 5};
  s2.ToggleSelect(x, s2.Get(x, save2.At(x)));
  s2.ToggleSelect(y, s2.Get(y, save2.At(y)));

  auto eff2 = [&](const bx::SlotRef& r) -> bx::Pokemon {
    return s2.Get(r, save2.At(r));
  };

  // Destino a partir do slot 0: o proprio slot 0 esta ocupado pelo Pikachu,
  // entao o primeiro livre e o 1.
  const std::size_t moved2 = s2.MoveSelection(1, 0, 0, 30, true, eff2);
  std::size_t total2 = 0;
  for (std::size_t i = 0; i < 30; ++i) {
    if (!bx::IsEmpty(eff2({1, 0, i}))) ++total2;
  }
  Check(moved2 == 2, "bloco esparso: move os dois");
  Check(total2 == 2, "bloco esparso: nenhum Pokemon sumiu");
}

// Bloco que PRESERVA A FORMA e e tudo-ou-nada (spec 088). Substitui as duas
// regras do MoveSelection (compactar nos livres, mover parcialmente).
void TestMoveBlock() {
  std::printf("mover bloco preservando a forma (spec 088):\n");

  // Grade 6 colunas. Bloco em L: (0,0), (0,1), (1,0) — com BURACO em (1,1).
  FakeSource save(1, true);
  save.Put(0, 0, 25);   // linha 0, col 0 — Pikachu
  save.Put(0, 1, 6);    // linha 0, col 1 — Charizard
  save.Put(0, 6, 150);  // linha 1, col 0 — Mewtwo
  bx::MoveSession s;

  auto eff = [&](const bx::SlotRef& r) -> bx::Pokemon {
    return s.Get(r, save.At(r));
  };

  // (SlotRef de origem, deslocamento (dr, dc) a partir do canto do bloco)
  const std::vector<std::pair<bx::SlotRef, std::pair<int, int>>> forma = {
      {{1, 0, 0}, {0, 0}},
      {{1, 0, 1}, {0, 1}},
      {{1, 0, 6}, {1, 0}},
  };

  // Desce o bloco duas linhas: canto vai para (2,0) = slot 12.
  const std::size_t moved =
      s.MoveBlock(1, 0, 2, 0, 5, 6, true, forma, eff);
  Check(moved == 3, "move os tres do bloco");
  Check(eff({1, 0, 12}).species == 25, "Pikachu no canto (2,0)");
  Check(eff({1, 0, 13}).species == 6, "Charizard em (2,1)");
  Check(eff({1, 0, 18}).species == 150, "Mewtwo em (3,0)");
  Check(bx::IsEmpty(eff({1, 0, 19})),
        "o BURACO da forma continua buraco em (3,1)");
  Check(bx::IsEmpty(eff({1, 0, 0})) && bx::IsEmpty(eff({1, 0, 1})) &&
            bx::IsEmpty(eff({1, 0, 6})),
        "as origens ficaram vazias");

  std::size_t total = 0;
  for (std::size_t i = 0; i < 30; ++i) {
    if (!bx::IsEmpty(eff({1, 0, i}))) ++total;
  }
  Check(total == 3, "ninguem sumiu nem foi duplicado");
}

// Tudo-ou-nada: um unico ocupante no caminho recusa o bloco inteiro.
void TestMoveBlockRecusa() {
  std::printf("bloco recusa quando nao cabe (spec 088):\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);
  save.Put(0, 1, 6);
  save.Put(0, 13, 9);  // intruso em (2,1): no caminho do destino
  bx::MoveSession s;

  auto eff = [&](const bx::SlotRef& r) -> bx::Pokemon {
    return s.Get(r, save.At(r));
  };

  const std::vector<std::pair<bx::SlotRef, std::pair<int, int>>> forma = {
      {{1, 0, 0}, {0, 0}},
      {{1, 0, 1}, {0, 1}},
  };

  Check(s.MoveBlock(1, 0, 2, 0, 5, 6, true, forma, eff) == 0,
        "recusa: (2,1) esta ocupado");
  Check(eff({1, 0, 0}).species == 25 && eff({1, 0, 1}).species == 6,
        "as origens NAO foram tocadas pela recusa");
  Check(!s.Dirty(), "a recusa nao deixou pendencia nenhuma");

  // Fora da borda: o bloco de 2 colunas nao cabe comecando na ultima coluna.
  Check(s.MoveBlock(1, 0, 2, 5, 5, 6, true, forma, eff) == 0,
        "recusa: a forma passaria da borda direita");
  Check(!s.Dirty(), "a recusa por borda tambem nao deixou rastro");

  // Onde CABE, move — a recusa acima nao pode ter travado o resto.
  Check(s.MoveBlock(1, 0, 3, 0, 5, 6, true, forma, eff) == 2,
        "move onde ha espaco livre");
}

// Mover o bloco para uma area que se sobrepoe a ele mesmo: as proprias
// origens nao podem contar como "ocupado".
void TestMoveBlockSobrepoe() {
  std::printf("bloco que se sobrepoe a si mesmo (spec 088):\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);
  save.Put(0, 1, 6);
  bx::MoveSession s;

  auto eff = [&](const bx::SlotRef& r) -> bx::Pokemon {
    return s.Get(r, save.At(r));
  };

  const std::vector<std::pair<bx::SlotRef, std::pair<int, int>>> forma = {
      {{1, 0, 0}, {0, 0}},
      {{1, 0, 1}, {0, 1}},
  };

  // Uma coluna para a direita: o destino (0,1) e origem do proprio bloco.
  Check(s.MoveBlock(1, 0, 0, 1, 5, 6, true, forma, eff) == 2,
        "aceita: o slot ocupado no caminho e do proprio bloco");
  Check(bx::IsEmpty(eff({1, 0, 0})), "o slot 0 esvaziou");
  Check(eff({1, 0, 1}).species == 25, "Pikachu andou para o slot 1");
  Check(eff({1, 0, 2}).species == 6, "Charizard andou para o slot 2");
}

// Bloco levantado numa caixa e solto em OUTRA (spec 088): as origens ficam
// para tras e nao podem "proteger" slots do destino.
void TestMoveBlockOutraCaixa() {
  std::printf("bloco atravessa caixas (spec 088):\n");

  FakeSource save(1, true);   // id da fonte = 1 (o painel do save)
  save.Put(0, 0, 25);         // caixa 0, slot 0
  save.Put(0, 1, 6);          // caixa 0, slot 1
  save.Put(1, 1, 9);          // caixa 1, slot 1 — ocupa o caminho do destino
  bx::MoveSession s;

  auto eff = [&](const bx::SlotRef& r) -> bx::Pokemon {
    return s.Get(r, save.At(r));
  };

  const std::vector<std::pair<bx::SlotRef, std::pair<int, int>>> forma = {
      {{1, 0, 0}, {0, 0}},
      {{1, 0, 1}, {0, 1}},
  };

  // Caixa 1, canto em (0,0): o slot 1 da caixa 1 esta ocupado. As origens sao
  // da caixa 0 — nao valem como excecao aqui.
  Check(s.MoveBlock(1, 1, 0, 0, 5, 6, true, forma, eff) == 0,
        "recusa: o destino na outra caixa esta ocupado");
  Check(eff({1, 0, 0}).species == 25, "as origens continuam intactas");

  // Onde cabe na outra caixa, move.
  Check(s.MoveBlock(1, 1, 2, 0, 5, 6, true, forma, eff) == 2,
        "move para a outra caixa onde ha espaco");
  Check(eff({1, 1, 12}).species == 25, "Pikachu chegou na caixa 1");
  Check(eff({1, 1, 13}).species == 6, "Charizard chegou na caixa 1");
  Check(bx::IsEmpty(eff({1, 0, 0})) && bx::IsEmpty(eff({1, 0, 1})),
        "as origens na caixa 0 ficaram vazias");
}

void TestCancel() {
  std::printf("cancelar:\n");

  FakeSource save(1, true);
  save.Put(0, 3, 150);
  bx::MoveSession s;

  const bx::SlotRef from{1, 0, 3};
  s.Pick(from, Eff(s, save, from));
  Check(bx::IsEmpty(Eff(s, save, from)), "origem vazia enquanto segura");

  Check(s.Cancel(), "cancela");
  Check(!s.Holding(), "a mao esvazia");
  Check(Eff(s, save, from).species == 150,
        "o Pokemon volta ao slot de origem");

  Check(!s.Cancel(), "cancelar com a mao vazia nao faz nada");
}

void TestCrossSource() {
  std::printf("entre fontes e entre caixas:\n");

  FakeSource save(1, true);
  FakeSource nest(0, true);
  save.Put(0, 0, 25);
  bx::MoveSession s;

  const bx::SlotRef from{1, 0, 0};   // save, caixa 0
  const bx::SlotRef to{0, 2, 7};     // nestbox, caixa 2
  s.Pick(from, Eff(s, save, from));
  Check(s.Drop(to, Eff(s, nest, to), nest.accepts()),
        "move do save para o NestBox, em outra caixa");
  Check(Eff(s, nest, to).species == 25, "chega na caixa 2 do NestBox");
  Check(bx::IsEmpty(Eff(s, save, from)), "sai do save");

  // E de volta.
  s.Pick(to, Eff(s, nest, to));
  const bx::SlotRef back{1, 5, 2};  // save, caixa 5
  Check(s.Drop(back, Eff(s, save, back), save.accepts()),
        "volta do NestBox para outra caixa do save");
  Check(Eff(s, save, back).species == 25, "chega na caixa 5 do save");
}

void TestRejects() {
  std::printf("fonte que recusa:\n");

  FakeSource save(1, true);
  FakeSource readonly(0, false);  // CanAccept() == false
  save.Put(0, 0, 25);
  bx::MoveSession s;

  const bx::SlotRef from{1, 0, 0};
  const bx::SlotRef to{0, 0, 0};
  s.Pick(from, Eff(s, save, from));

  Check(!s.Drop(to, Eff(s, readonly, to), readonly.accepts()),
        "nao solta em fonte que recusa");
  Check(s.Holding(), "a mao continua cheia");
  Check(bx::IsEmpty(Eff(s, readonly, to)), "o destino continua vazio");
}

void TestCount() {
  std::printf("contador:\n");

  FakeSource save(1, true);
  FakeSource nest(0, true);
  save.Put(0, 0, 25);
  save.Put(0, 1, 6);
  bx::MoveSession s;

  // Por VALOR: `At()` devolve temporario, e devolver referencia a ele deixa
  // uma pendurada (o mesmo defeito da spec 151, aqui no proprio teste).
  auto save_at = [&save](const bx::SlotRef& r) { return save.At(r); };
  auto nest_at = [&nest](const bx::SlotRef& r) { return nest.At(r); };

  Check(s.Count(1, save.Count(), save_at) == 2, "save comeca com 2");
  Check(s.Count(0, nest.Count(), nest_at) == 0, "nestbox comeca com 0");

  const bx::SlotRef from{1, 0, 0};
  const bx::SlotRef to{0, 0, 0};
  s.Pick(from, Eff(s, save, from));
  s.Drop(to, Eff(s, nest, to), true);

  Check(s.Count(1, save.Count(), save_at) == 1, "save perde um");
  Check(s.Count(0, nest.Count(), nest_at) == 1, "nestbox ganha um");
}

void TestDiscard() {
  std::printf("descartar a sessao:\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);
  bx::MoveSession s;

  const bx::SlotRef from{1, 0, 0};
  const bx::SlotRef to{1, 0, 4};
  Check(!s.Dirty(), "sessao nova nao esta suja");

  s.Pick(from, Eff(s, save, from));
  s.Drop(to, Eff(s, save, to), true);
  Check(s.Dirty(), "depois de mover, esta suja");

  s.Discard();
  Check(!s.Dirty(), "descartar limpa a sessao");
  Check(Eff(s, save, from).species == 25, "o Pokemon volta ao lugar original");
  Check(bx::IsEmpty(Eff(s, save, to)), "o destino volta a ficar vazio");
}

// O ponto mais importante da spec: o dado de origem nunca e tocado. Toda a
// movimentacao vive no overlay.
void TestOriginalUntouched() {
  std::printf("a fonte original nao e modificada:\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);
  save.Put(0, 1, 6);
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 0};
  const bx::SlotRef b{1, 0, 1};
  const bx::SlotRef c{1, 3, 9};

  s.Pick(a, Eff(s, save, a));
  s.Drop(b, Eff(s, save, b), true);
  s.Drop(c, Eff(s, save, c), true);

  // A fonte continua exatamente como comecou.
  Check(save.At(a).species == 25, "slot de origem intacto na fonte");
  Check(save.At(b).species == 6, "slot de destino intacto na fonte");
  Check(bx::IsEmpty(save.At(c)), "slot distante intacto na fonte");
  Check(save.Count() == 2, "contagem da fonte inalterada");
}

// Spec 020: Dirty() decide se sair da tela precisa perguntar. Um falso
// negativo aqui perde trabalho do usuario em silencio.
void TestDirty() {
  std::printf("pendencia (spec 020):\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);
  bx::MoveSession s;

  const bx::SlotRef from{1, 0, 0};
  const bx::SlotRef to{1, 0, 4};

  Check(!s.Dirty(), "sessao nova nao tem pendencia");

  // So segurar ja e pendencia: sair agora perderia o movimento.
  s.Pick(from, Eff(s, save, from));
  Check(s.Dirty(), "segurando ja conta como pendencia");

  s.Drop(to, Eff(s, save, to), true);
  Check(s.Dirty(), "depois de soltar continua pendente");

  s.Discard();
  Check(!s.Dirty(), "descartar limpa a pendencia");

  // Pegar e cancelar deixa o overlay com uma entrada que reverte ao original.
  // Dirty() continua verdadeiro — e conservador de proposito: perguntar a mais
  // custa um clique, perguntar a menos custa o trabalho do usuario.
  bx::MoveSession s2;
  s2.Pick(from, Eff(s2, save, from));
  s2.Cancel();
  Check(!s2.Holding(), "cancelou, nada na mao");
}

// --- Multissselecao (spec 021) ---------------------------------------------

void TestSelectToggle() {
  std::printf("marcar e desmarcar (spec 021):\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);
  save.Put(0, 1, 6);
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 0};
  const bx::SlotRef b{1, 0, 1};
  const bx::SlotRef vazio{1, 0, 9};

  Check(s.SelectedCount() == 0, "comeca sem nada marcado");

  s.ToggleSelect(a, Eff(s, save, a));
  Check(s.IsSelected(a) && s.SelectedCount() == 1, "marca um slot");

  s.ToggleSelect(b, Eff(s, save, b));
  Check(s.SelectedCount() == 2, "marca o segundo");

  s.ToggleSelect(a, Eff(s, save, a));
  Check(!s.IsSelected(a) && s.SelectedCount() == 1, "desmarca o primeiro");

  s.ToggleSelect(vazio, Eff(s, save, vazio));
  Check(s.SelectedCount() == 1, "slot vazio nao entra na selecao");

  s.ClearSelection();
  Check(s.SelectedCount() == 0, "limpar zera a selecao");
}

void TestMoveSelection() {
  std::printf("mover em bloco (spec 021):\n");

  FakeSource save(1, true);
  FakeSource nest(0, true);
  save.Put(0, 0, 25);
  save.Put(0, 1, 6);
  save.Put(0, 2, 150);
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 0};
  const bx::SlotRef b{1, 0, 1};
  const bx::SlotRef c{1, 0, 2};
  s.ToggleSelect(a, Eff(s, save, a));
  s.ToggleSelect(b, Eff(s, save, b));
  s.ToggleSelect(c, Eff(s, save, c));

  // effective_at precisa cobrir as duas fontes: origens no save, destino no
  // NestBox. O id do SlotRef diz qual.
  auto eff = [&](const bx::SlotRef& r) -> bx::Pokemon {
    return s.Get(r, r.source == 1 ? save.At(r) : nest.At(r));
  };

  const std::size_t moved = s.MoveSelection(0, 0, 0, 30, true, eff);
  Check(moved == 3, "move os tres marcados");
  Check(s.SelectedCount() == 0, "a selecao esvazia depois de mover tudo");

  Check(bx::IsEmpty(eff(a)) && bx::IsEmpty(eff(b)) && bx::IsEmpty(eff(c)),
        "as origens ficam vazias");

  Check(!bx::IsEmpty(eff({0, 0, 0})) && !bx::IsEmpty(eff({0, 0, 1})) &&
            !bx::IsEmpty(eff({0, 0, 2})),
        "os tres aparecem no destino");
  Check(s.Dirty(), "movimento em bloco deixa pendencia");

  // A fonte original nao foi tocada.
  Check(save.At(a).species == 25 && save.Count() == 3,
        "a fonte original continua intacta");
}

void TestMoveSelectionPartial() {
  std::printf("bloco maior que o espaco (spec 021):\n");

  FakeSource save(1, true);
  FakeSource nest(0, true);
  save.Put(0, 0, 25);
  save.Put(0, 1, 6);
  save.Put(0, 2, 150);
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 0};
  const bx::SlotRef b{1, 0, 1};
  const bx::SlotRef c{1, 0, 2};
  s.ToggleSelect(a, Eff(s, save, a));
  s.ToggleSelect(b, Eff(s, save, b));
  s.ToggleSelect(c, Eff(s, save, c));

  auto eff = [&](const bx::SlotRef& r) -> bx::Pokemon {
    return s.Get(r, r.source == 1 ? save.At(r) : nest.At(r));
  };

  // Caixa de destino com so 2 slots: cabe 2 dos 3.
  const std::size_t moved = s.MoveSelection(0, 0, 0, 2, true, eff);
  Check(moved == 2, "move so o que cabe");
  Check(s.SelectedCount() == 1, "o que sobrou continua marcado");
}

void TestMoveSelectionSkipsOccupied() {
  std::printf("bloco pula slots ocupados (spec 021):\n");

  FakeSource save(1, true);
  FakeSource nest(0, true);
  save.Put(0, 0, 25);
  save.Put(0, 1, 6);
  nest.Put(0, 0, 133);  // destino ja tem alguem no slot 0
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 0};
  const bx::SlotRef b{1, 0, 1};
  s.ToggleSelect(a, Eff(s, save, a));
  s.ToggleSelect(b, Eff(s, save, b));

  auto eff = [&](const bx::SlotRef& r) -> bx::Pokemon {
    return s.Get(r, r.source == 1 ? save.At(r) : nest.At(r));
  };

  const std::size_t moved = s.MoveSelection(0, 0, 0, 30, true, eff);
  Check(moved == 2, "move os dois");
  Check(eff({0, 0, 0}).species == 133, "o ocupante do slot 0 nao foi trocado");
  Check(!bx::IsEmpty(eff({0, 0, 1})) && !bx::IsEmpty(eff({0, 0, 2})),
        "os dois foram para os slots livres seguintes");
}

void TestMoveSelectionRejects() {
  std::printf("bloco em fonte que recusa (spec 021):\n");

  FakeSource save(1, true);
  FakeSource readonly(0, false);
  save.Put(0, 0, 25);
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 0};
  s.ToggleSelect(a, Eff(s, save, a));

  auto eff = [&](const bx::SlotRef& r) -> bx::Pokemon {
    return s.Get(r, r.source == 1 ? save.At(r) : readonly.At(r));
  };

  const std::size_t moved = s.MoveSelection(0, 0, 0, 30, false, eff);
  Check(moved == 0, "nao move nada");
  Check(s.SelectedCount() == 1, "a selecao continua intacta");
  Check(!bx::IsEmpty(eff(a)), "a origem nao foi esvaziada");
}

// --- Modos de cursor (spec 031) --------------------------------------------

void TestCicloDeModos() {
  std::printf("ciclo de modos (spec 031):\n");

  bx::CursorMode m = bx::CursorMode::kMove;
  m = bx::NextMode(m);
  Check(m == bx::CursorMode::kSwap, "Mover -> Trocar");
  m = bx::NextMode(m);
  Check(m == bx::CursorMode::kSelect, "Trocar -> Selecionar");
  m = bx::NextMode(m);
  Check(m == bx::CursorMode::kMove, "Selecionar -> Mover (fecha o ciclo)");

  // Para tras (ZL).
  m = bx::PrevMode(m);
  Check(m == bx::CursorMode::kSelect, "para tras: Mover -> Selecionar");
  m = bx::PrevMode(m);
  Check(m == bx::CursorMode::kSwap, "para tras: Selecionar -> Trocar");
  m = bx::PrevMode(m);
  Check(m == bx::CursorMode::kMove, "para tras: Trocar -> Mover");

  Check(std::string(bx::CursorModeName(bx::CursorMode::kSwap)) == "Trocar",
        "cada modo tem nome para o indicador");
}

void TestModoMoverNaoTroca() {
  std::printf("modo Mover recusa ocupado (spec 031):\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);  // Pikachu
  save.Put(0, 1, 6);   // Charizard
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 0};
  const bx::SlotRef b{1, 0, 1};
  const bx::SlotRef vazio{1, 0, 5};

  s.Pick(a, Eff(s, save, a));

  // allow_swap = false: e o modo Mover.
  Check(!s.Drop(b, Eff(s, save, b), true, /*allow_swap=*/false),
        "nao solta sobre ocupado");
  Check(s.Holding(), "a mao continua cheia");
  Check(Eff(s, save, b).species == 6, "o ocupante nao foi tocado");

  // Em slot vazio funciona igual nos dois modos.
  Check(s.Drop(vazio, Eff(s, save, vazio), true, /*allow_swap=*/false),
        "solta em vazio normalmente");
  Check(!s.Holding(), "a mao esvazia");
  Check(Eff(s, save, vazio).species == 25, "o Pokemon chegou ao destino");
}

void TestModoTrocarTroca() {
  std::printf("modo Trocar troca (spec 031):\n");

  FakeSource save(1, true);
  save.Put(0, 0, 25);
  save.Put(0, 1, 6);
  bx::MoveSession s;

  const bx::SlotRef a{1, 0, 0};
  const bx::SlotRef b{1, 0, 1};

  s.Pick(a, Eff(s, save, a));
  // allow_swap = true (o padrao): modo Trocar.
  Check(s.Drop(b, Eff(s, save, b), true, /*allow_swap=*/true),
        "solta sobre ocupado");
  Check(Eff(s, save, b).species == 25, "o destino recebeu o Pikachu");
  Check(Eff(s, save, a).species == 6, "e o Charizard foi para a origem");
  Check(!s.Holding(), "a mao esvazia: um gesto, uma troca (spec 087)");
}

}  // namespace

int main() {
  TestPick();
  TestDropEmpty();
  TestDropSwap();
  TestDropNaPropriaOrigem();
  TestCancel();
  TestCrossSource();
  TestRejects();
  TestCount();
  TestDiscard();
  TestOriginalUntouched();
  TestDirty();
  TestSelectToggle();
  TestMoveSelection();
  TestMoveSelectionMesmaCaixa();
  TestMoveBlock();
  TestMoveBlockRecusa();
  TestMoveBlockSobrepoe();
  TestMoveBlockOutraCaixa();
  TestMoveSelectionPartial();
  TestMoveSelectionSkipsOccupied();
  TestMoveSelectionRejects();
  TestCicloDeModos();
  TestModoMoverNaoTroca();
  TestModoTrocarTroca();
  TestGetNaoPrendeTemporario();

  if (g_failures > 0) {
    std::printf("\n%d teste(s) falharam.\n", g_failures);
    return 1;
  }
  std::printf("\nTodos os testes passaram.\n");
  return 0;
}
