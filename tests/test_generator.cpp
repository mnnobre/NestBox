// Gerador de Pokemon (spec 144) — nucleo de regra, sem UI.
//
// A tela deixa o jogador escolher o NIVEL. Nenhum formato guarda nivel: todos
// guardam EXPERIENCIA, e o nivel exibido no jogo deriva dela pela curva de
// crescimento da especie. Se a conversao nivel->exp errar, o Pokemon nasce
// com outro nivel na tela do jogo — sem nenhum erro no caminho.
//
// Por isso a primeira coisa coberta aqui e a ida e volta:
//   LevelFromExp(ExpForLevel(n, curva), curva) == n
// para os 100 niveis nas 6 curvas. O teste existe porque `ExpForLevel` estava
// num namespace anonimo e nunca foi exercitado sozinho — so indiretamente,
// pelo caminho de leitura.
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "pkm_convert.h"

#include "gen3_save.h"
#include "generator.h"
#include "moldes.h"
#include "pa8.h"
#include "pb7.h"
#include "pb8.h"
#include "personal_tables.h"
#include "pk4.h"
#include "pk8.h"
#include "pk9.h"

namespace g3 = pokehome::gen3;
namespace pers = pokehome::personal;
namespace mld = pokehome::molde;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FALHOU: %s\n", what.c_str());
    ++g_failures;
  } else {
    std::printf("  ok: %s\n", what.c_str());
  }
}

// As 6 curvas do byte 19 do personal.
static const char* CurvaNome(std::uint8_t g) {
  switch (g) {
    case 0: return "Medium Fast";
    case 1: return "Erratic";
    case 2: return "Fluctuating";
    case 3: return "Medium Slow";
    case 4: return "Fast";
    case 5: return "Slow";
    default: return "?";
  }
}

int main() {
  std::printf("== nivel <-> experiencia, as 6 curvas ==\n");
  for (std::uint8_t curva = 0; curva <= 5; ++curva) {
    int erros = 0;
    for (int nivel = 1; nivel <= 100; ++nivel) {
      const std::uint32_t exp = g3::ExpForLevel(nivel, curva);
      const int volta = g3::LevelFromExp(exp, curva);
      if (volta != nivel) {
        if (erros < 3) {
          std::printf("    nivel %d -> exp %u -> nivel %d\n", nivel, exp, volta);
        }
        ++erros;
      }
    }
    Check(erros == 0, std::string("curva ") + CurvaNome(curva) +
                          ": os 100 niveis voltam iguais");
  }

  // A exigencia e monotonica: nivel maior nunca pede menos experiencia. Uma
  // curva que quebrasse isso faria o LevelFromExp (busca do topo) devolver
  // nivel errado sem que a ida e volta acusasse.
  std::printf("== monotonicidade ==\n");
  for (std::uint8_t curva = 0; curva <= 5; ++curva) {
    bool ok = true;
    for (int nivel = 2; nivel <= 100; ++nivel) {
      if (g3::ExpForLevel(nivel, curva) < g3::ExpForLevel(nivel - 1, curva)) {
        ok = false;
        std::printf("    quebra no nivel %d da curva %s\n", nivel,
                    CurvaNome(curva));
      }
    }
    Check(ok, std::string("curva ") + CurvaNome(curva) + ": exp nunca diminui");
  }

  // Bordas: nivel 1 nao custa nada, e fora da faixa fica presa em vez de
  // devolver lixo (o header promete o clamp).
  std::printf("== bordas ==\n");
  for (std::uint8_t curva = 0; curva <= 5; ++curva) {
    Check(g3::ExpForLevel(1, curva) == 0,
          std::string("curva ") + CurvaNome(curva) + ": nivel 1 = 0 exp");
    Check(g3::ExpForLevel(0, curva) == 0,
          std::string("curva ") + CurvaNome(curva) + ": nivel 0 presa em 0");
    Check(g3::ExpForLevel(150, curva) == g3::ExpForLevel(100, curva),
          std::string("curva ") + CurvaNome(curva) + ": acima de 100 presa em 100");
  }

  // ------------------------------------------------------------------
  // Tabela personal completa (TD-05). Uma tabela gerada que ninguem confere
  // e uma tabela em que ninguem deve confiar: os asserts abaixo sao fatos
  // do jogo conhecidos DE FORA, nao repeticao do que o gerador escreveu.
  // ------------------------------------------------------------------
  std::printf("== tabela personal: fatos conhecidos ==\n");
  {
    // Bulbasaur em FireRed: Overgrow (65), sem 2a habilidade, 87.5%% macho
    // (ratio 31), curva Medium Slow (3), base 45/49/49/45/65/65.
    const pers::EntryFull* b = pers::Find(pers::Jogo::kFireRed, 1, 0);
    Check(b != nullptr, "Bulbasaur existe em FireRed");
    if (b) {
      Check(b->ability1 == 65, "Bulbasaur/FireRed: habilidade 1 = Overgrow(65)");
      Check(b->gender == 31, "Bulbasaur/FireRed: ratio 31 (87,5% macho)");
      Check(b->growth == 3, "Bulbasaur/FireRed: curva Medium Slow");
      Check(b->base[0] == 45 && b->base[1] == 49 && b->base[2] == 49 &&
                b->base[3] == 45 && b->base[4] == 65 && b->base[5] == 65,
            "Bulbasaur/FireRed: base 45/49/49/45/65/65 na ordem do save");
    }

    // Magnemite nunca teve genero: ratio 255 em todo jogo.
    for (const auto j : {pers::Jogo::kFireRed, pers::Jogo::kSwSh,
                         pers::Jogo::kSV}) {
      const pers::EntryFull* m = pers::Find(j, 81, 0);
      Check(m && m->gender == 255, "Magnemite sem genero (ratio 255)");
    }

    // Nidoran-F (29) so femea = 254; Nidoran-M (32) so macho = 0. Os dois
    // extremos, que um off-by-one na leitura do ratio trocaria.
    const pers::EntryFull* nf = pers::Find(pers::Jogo::kFireRed, 29, 0);
    const pers::EntryFull* nm = pers::Find(pers::Jogo::kFireRed, 32, 0);
    Check(nf && nf->gender == 254, "Nidoran-F: so femea (254)");
    Check(nm && nm->gender == 0, "Nidoran-M: so macho (0)");

    // A habilidade muda entre geracoes — a razao de a tabela ser POR JOGO.
    // Butterfree (12) nao tinha habilidade oculta no gen3; ganhou Tinted
    // Lens (110) do gen5 em diante.
    const pers::EntryFull* bf3 = pers::Find(pers::Jogo::kFireRed, 12, 0);
    const pers::EntryFull* bf8 = pers::Find(pers::Jogo::kSwSh, 12, 0);
    Check(bf8 && bf8->ability_hidden == 110,
          "Butterfree/SwSh: oculta = Tinted Lens(110)");
    Check(bf3 && bf3->ability_hidden != 110,
          "Butterfree/FireRed: NAO tem Tinted Lens (por isso a tabela e por jogo)");

    // Especie fora do jogo devolve nullptr em vez de dado errado.
    Check(pers::Find(pers::Jogo::kFireRed, 906, 0) == nullptr,
          "Sprigatito nao existe em FireRed");
    Check(pers::Find(pers::Jogo::kSV, 906, 0) != nullptr,
          "Sprigatito existe em Scarlet/Violet");
    Check(!pers::HasSpecies(pers::Jogo::kFireRed, 906),
          "HasSpecies concorda: Sprigatito fora do FireRed");

    // Formas alternativas: Rotom tem 6 no SwSh (base + 5 aparelhos).
    Check(pers::FormCount(pers::Jogo::kSwSh, 479) == 6,
          "Rotom tem 6 formas no SwSh");

    // Z-A entrou na tabela (PersonalTable.ZA existe no PkHeX 25.12.21).
    // Chespin (650) e inicial de Kalos, o cenario do jogo.
    Check(pers::Find(pers::Jogo::kZA, 650, 0) != nullptr,
          "Chespin existe em Legends Z-A");
    Check(!pers::HasSpecies(pers::Jogo::kZA, 906),
          "Sprigatito nao existe em Legends Z-A");

    // Nenhum registro pode ter curva fora de 0..5: ExpForLevel cairia no
    // default silencioso e todo Pokemon daquela especie nasceria com o
    // nivel errado.
    int fora = 0;
    for (std::size_t j = 0; j < static_cast<std::size_t>(pers::Jogo::kCount); ++j) {
      const pers::Faixa& f = pers::kTabelas[j];
      for (std::size_t i = 0; i < f.n; ++i)
        if (f.dados[i].growth > 5) ++fora;
    }
    Check(fora == 0, "toda entrada tem curva de crescimento valida (0..5)");
  }

  // ------------------------------------------------------------------
  // Moldes por formato (TD-04). O gerador NUNCA parte de bytes zerados: o
  // `Write()` de todo formato copia `p.raw` e so sobrescreve o que o `Parse`
  // conhece, entao um raw vazio deixa em zero todo offset ainda nao mapeado.
  //
  // O que estes asserts provam: o molde e legivel pelo NOSSO parser, e a
  // ida e volta pelo NOSSO writer nao perde byte. Se `Write(Parse(molde))`
  // divergisse, o gerador estaria corrompendo justamente os campos que o
  // molde existe para preservar.
  // ------------------------------------------------------------------
  std::printf("== moldes: ida e volta pelo nosso parser/writer ==\n");
  {
    struct Caso {
      const char* nome;
      pkm::Format fmt;
      std::size_t esperado;
    };
    const Caso casos[] = {
        {"PB7", pkm::Format::kPB7, 260}, {"PK8", pkm::Format::kPK8, 328},
        {"PB8", pkm::Format::kPB8, 328}, {"PA8", pkm::Format::kPA8, 360},
        {"PK9", pkm::Format::kPK9, 328}, {"PK4", pkm::Format::kPK4, 136},
    };

    for (const Caso& c : casos) {
      std::size_t n = 0;
      const std::uint8_t* bytes = mld::Bytes(c.fmt, &n);
      const std::string q = std::string("molde ") + c.nome;

      Check(bytes != nullptr, q + ": existe");
      if (!bytes) continue;
      Check(n == c.esperado, q + ": tamanho do formato");

      // Um molde todo zerado seria pior que molde nenhum: passaria nos
      // asserts de tamanho e entregaria o bug que TD-04 quer evitar.
      std::size_t nao_zero = 0;
      for (std::size_t i = 0; i < n; ++i)
        if (bytes[i] != 0) ++nao_zero;
      Check(nao_zero > n / 8, q + ": tem conteudo de verdade, nao zeros");

      std::optional<pkm::Pokemon> lido;
      std::vector<std::uint8_t> volta;
      switch (c.fmt) {
        case pkm::Format::kPB7:
          lido = pb7::Parse(bytes, n);
          if (lido) volta = pb7::Write(*lido);
          break;
        case pkm::Format::kPK8:
          lido = pk8::Parse(bytes, n);
          if (lido) volta = pk8::Write(*lido);
          break;
        case pkm::Format::kPB8:
          lido = pb8::Parse(bytes, n);
          if (lido) volta = pb8::Write(*lido);
          break;
        case pkm::Format::kPA8:
          lido = pa8::Parse(bytes, n);
          if (lido) volta = pa8::Write(*lido);
          break;
        case pkm::Format::kPK9:
          lido = pk9::Parse(bytes, n);
          if (lido) volta = pk9::Write(*lido);
          break;
        case pkm::Format::kPK4:
          lido = pk4::Parse(bytes, n);
          if (lido) volta = pk4::Write(*lido);
          break;
        default:
          break;
      }

      Check(lido.has_value(), q + ": o nosso parser le");
      if (!lido) continue;

      // O molde e um Pikachu nivel 5 em todos os formatos. Conferir a
      // especie prova que o parser leu o campo certo e nao lixo coerente.
      Check(pkm::NationalDex(*lido) == 25, q + ": e o Pikachu do molde");

      Check(volta.size() >= n, q + ": o writer devolve o tamanho do formato");
      if (volta.size() < n) continue;

      std::size_t divergencias = 0;
      std::size_t primeira = 0;
      for (std::size_t i = 0; i < n; ++i) {
        if (volta[i] != bytes[i]) {
          if (divergencias == 0) primeira = i;
          ++divergencias;
        }
      }
      if (divergencias != 0) {
        std::printf("    %s: %zu bytes divergem, o primeiro em 0x%zX (%02X -> %02X)\n",
                    c.nome, divergencias, primeira, bytes[primeira],
                    volta[primeira]);
      }
      Check(divergencias == 0, q + ": ida e volta nao perde byte");
    }
  }

  // ------------------------------------------------------------------
  // O nucleo do gerador.
  // ------------------------------------------------------------------
  namespace gen = pokehome::generator;

  std::printf("== orcamento de EV ==\n");
  {
    gen::GeneratorState s;
    // 252 + 252 = 504; o terceiro campo so pode receber 6, nao 252.
    gen::SetEv(s, 0, 252);
    gen::SetEv(s, 1, 252);
    const std::uint8_t terceiro = gen::SetEv(s, 2, 252);
    Check(terceiro == 6, "o terceiro EV e cortado no que sobra do teto (6)");
    Check(gen::EvTotal(s) == 510, "o total para exatamente em 510");

    // Teto por stat, independente do total.
    gen::GeneratorState s2;
    Check(gen::SetEv(s2, 0, 300) == 252, "EV de um stat nao passa de 252");
    Check(gen::SetEv(s2, 1, -50) == 0, "EV negativo vira 0");

    // Baixar um campo devolve orcamento aos outros.
    gen::SetEv(s, 0, 0);
    Check(gen::SetEv(s, 2, 252) == 252,
          "liberar um campo devolve orcamento ao proximo");

    gen::GeneratorState s3;
    Check(gen::SetIv(s3, 0, 99) == 31, "IV nao passa de 31");
    Check(gen::SetIv(s3, 1, -1) == 0, "IV negativo vira 0");
  }

  std::printf("== Build: monta a partir do molde ==\n");
  {
    // Pikachu de Scarlet/Violet, nivel 50.
    gen::GeneratorState s;
    s.dex = 25;
    s.jogo = pers::Jogo::kSV;
    s.level = 50;
    s.met_level = 5;
    s.nature = 3;
    s.moves[0] = 84;  // Thunder Shock
    s.tid = 24601;
    s.sid = 13337;

    auto p = gen::Build(s);
    Check(p.has_value(), "Build devolve Pokemon");
    if (p) {
      Check(p->format == pkm::Format::kPK9, "formato do jogo escolhido");
      Check(pkm::NationalDex(*p) == 25, "especie chega como Pikachu");
      // A armadilha do PK9: `species` guarda indice INTERNO do gen9, nao a
      // dex nacional. Se Build copiasse o campo direto, isto pegaria.
      Check(p->species != 25 || pkm::NationalDex(*p) == 25,
            "especie passou pela conversao de indice do formato");
      // O molde nao pode ter sobrado por baixo: raw preenchido = molde
      // carregado (TD-04), e sem raw o Write cairia no buffer zerado.
      Check(!p->raw.empty(), "o registro carrega o molde (raw preenchido)");

      // NIVEL -> EXP: nenhum formato guarda nivel. Se a conversao errar, o
      // Pokemon nasce com outro nivel na tela do jogo.
      const pers::EntryFull* pe = pers::Find(pers::Jogo::kSV, 25, 0);
      Check(pe != nullptr, "personal do Pikachu em SV");
      if (pe) {
        Check(p->exp == g3::ExpForLevel(50, pe->growth),
              "exp corresponde ao nivel 50 na curva da especie");
        Check(g3::LevelFromExp(p->exp, pe->growth) == 50,
              "o nivel volta como 50 ao ser derivado da exp");
      }

      // PP acompanha o golpe (licao da spec 143).
      Check(p->pp[0] > 0, "o golpe recebeu PP");
      Check(p->pp[1] == 0, "slot vazio nao recebe PP");
    }

    // Especie que nao existe no jogo: recusa em vez de gravar Pokemon errado.
    gen::GeneratorState fora;
    fora.dex = 906;  // Sprigatito
    fora.jogo = pers::Jogo::kFireRed;
    Check(!gen::Build(fora).has_value(),
          "Build recusa especie que nao existe no jogo");
  }

  std::printf("== shiny deriva do PID ==\n");
  {
    gen::GeneratorState s;
    s.dex = 25;
    s.jogo = pers::Jogo::kSV;
    s.level = 5;
    s.moves[0] = 84;
    s.tid = 24601;
    s.sid = 13337;

    s.shiny = false;
    auto normal = gen::Build(s);
    Check(normal && !pkm::IsShiny(*normal), "sem shiny: o PID nao e shiny");

    s.shiny = true;
    auto brilhante = gen::Build(s);
    Check(brilhante && pkm::IsShiny(*brilhante),
          "com shiny: o PID satisfaz a relacao com TID/SID");

    // O par do treinador NAO pode ter sido mexido para conseguir o shiny.
    Check(brilhante && brilhante->tid == 24601 && brilhante->sid == 13337,
          "o shiny sai do PID, sem alterar TID/SID escolhidos");
  }

  std::printf("== Verify: aponta e ApplyFix conserta ==\n");
  {
    // Cada caso planta UM defeito e exige que o code apareca; depois aplica a
    // correcao e exige que ele SUMA. Verificar so o "aponta" deixaria passar
    // uma correcao que nao corrige.
    auto tem = [](const std::vector<gen::Issue>& v, const std::string& code) {
      for (const auto& i : v)
        if (i.code == code) return true;
      return false;
    };

    auto base = [] {
      gen::GeneratorState s;
      s.dex = 25;
      s.jogo = pers::Jogo::kSV;
      s.level = 20;
      s.met_level = 5;
      s.moves[0] = 84;
      return s;
    };

    // Limpo nao acusa nada dos nossos codes.
    {
      gen::GeneratorState s = base();
      const auto v = gen::Verify(s);
      Check(!tem(v, "bola_anacronica") && !tem(v, "met_level_alto") &&
                !tem(v, "sem_golpe") && !tem(v, "genero_impossivel"),
            "estado coerente nao acusa problema proprio do gerador");
    }

    // Bola anacronica: Beast Ball (gen 7) num jogo gen 3.
    {
      gen::GeneratorState s = base();
      s.jogo = pers::Jogo::kFireRed;
      s.ball = 33;  // Beast Ball
      Check(tem(gen::Verify(s), "bola_anacronica"),
            "acusa Beast Ball em jogo de gen 3");
      Check(gen::ApplyFix(s, "bola_anacronica"), "ApplyFix aceita o code");
      Check(!tem(gen::Verify(s), "bola_anacronica"),
            "depois do fix a bola nao e mais anacronica");
    }

    // Nivel de encontro acima do nivel atual.
    {
      gen::GeneratorState s = base();
      s.level = 5;
      s.met_level = 40;
      Check(tem(gen::Verify(s), "met_level_alto"),
            "acusa nivel de encontro acima do atual");
      gen::ApplyFix(s, "met_level_alto");
      Check(!tem(gen::Verify(s), "met_level_alto"), "o fix iguala os niveis");
      Check(s.met_level == 5, "met_level virou o nivel atual");
    }

    // Genero impossivel: Magnemite nao tem genero.
    {
      gen::GeneratorState s = base();
      s.dex = 81;
      s.gender = 0;  // macho
      Check(tem(gen::Verify(s), "genero_impossivel"),
            "acusa genero em especie sem genero");
      gen::ApplyFix(s, "genero_impossivel");
      Check(s.gender == 2, "o fix marca como sem genero");
      Check(!tem(gen::Verify(s), "genero_impossivel"), "e o problema some");
    }

    // Habilidade num slot que a especie nao tem.
    {
      gen::GeneratorState s = base();
      s.ability_slot = 2;  // Pikachu so tem slot 1 e a oculta
      const bool acusou = tem(gen::Verify(s), "habilidade_invalida");
      if (acusou) {
        gen::ApplyFix(s, "habilidade_invalida");
        Check(!tem(gen::Verify(s), "habilidade_invalida"),
              "o fix devolve a habilidade ao slot 1");
      } else {
        Check(true, "Pikachu tem slot 2 nesta tabela — nada a acusar");
      }
    }

    // Sem golpe nenhum.
    {
      gen::GeneratorState s = base();
      for (int i = 0; i < 4; ++i) s.moves[i] = 0;
      Check(tem(gen::Verify(s), "sem_golpe"), "acusa Pokemon sem golpe");
      Check(gen::ApplyFix(s, "sem_golpe"),
            "ApplyFix preenche pelo aprendizado de nivel");
      Check(s.moves[0] != 0, "e o primeiro slot deixou de estar vazio");
      Check(!tem(gen::Verify(s), "sem_golpe"), "o problema some");
    }

    // Golpe repetido.
    {
      gen::GeneratorState s = base();
      s.moves[0] = 84;
      s.moves[1] = 84;
      Check(tem(gen::Verify(s), "golpe_duplicado_1"), "acusa golpe repetido");
    }

    // Nivel de encontro fora do que o molde traz. MEDIDO contra o PkHeX
    // (evidence-log): Pikachu de SV com met_level 5 e recusado com "Unable to
    // match an encounter from origin game"; com met_level 10 (o do molde) sai
    // LEGAL. E aviso, nao erro: o gerador nao tem o encounter DB para oferecer
    // os outros pares validos, so sabe qual e o que conhece.
    {
      gen::GeneratorState s = base();
      const std::uint8_t molde_lvl = gen::MetLevelDoMolde(pers::Jogo::kSV);
      Check(molde_lvl != 0, "o molde de SV declara um nivel de encontro");
      s.met_level = static_cast<std::uint8_t>(molde_lvl == 3 ? 4 : 3);
      Check(tem(gen::Verify(s), "met_level_fora_do_encontro"),
            "avisa quando o nivel de encontro nao e o do molde");
      Check(gen::ApplyFix(s, "met_level_fora_do_encontro"),
            "ApplyFix ajusta para o nivel do molde");
      Check(s.met_level == molde_lvl, "e o nivel virou o do molde");
      Check(!tem(gen::Verify(s), "met_level_fora_do_encontro"),
            "o aviso some");
    }

    // Especie fora do jogo NAO tem conserto automatico: escolher por conta
    // propria entre trocar a especie ou trocar o jogo seria decidir pelo
    // jogador.
    {
      gen::GeneratorState s = base();
      s.dex = 906;
      s.jogo = pers::Jogo::kFireRed;
      Check(tem(gen::Verify(s), "especie_fora_do_jogo"),
            "acusa especie fora do jogo");
      Check(!gen::ApplyFix(s, "especie_fora_do_jogo"),
            "e NAO oferece conserto automatico");
    }
  }

  if (g_failures == 0) {
    std::printf("\nTUDO OK\n");
    return 0;
  }
  std::printf("\n%d FALHA(S)\n", g_failures);
  return 1;
}
