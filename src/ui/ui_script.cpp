// Controle remoto da UI (spec 134). Ver ui_script.h para o desenho.
#include "ui_script.h"

#ifndef __SWITCH__

#include <borealis.hpp>
#include <borealis/platforms/glfw/glfw_input.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace nestbox::script {
namespace {

// --- Roteiro ---------------------------------------------------------------

struct Command {
  std::string verb;
  std::vector<std::string> args;
  int line = 0;  // linha no arquivo, para a mensagem de erro apontar o lugar
};

std::vector<Command> g_script;
std::size_t g_pc = 0;      // proximo comando
bool g_active = false;
int g_exit = 0;

// Pilha de telas ativas (ver ClearStateProvider). O topo e quem responde.
struct Screen {
  std::string name;
  StateProvider provider;
};
std::vector<Screen> g_stack;

// --- Relogio do roteiro ----------------------------------------------------

// Frames restantes do `wait` corrente.
int g_wait = 0;

// O botao injetado precisa de uma TRANSICAO solto->pressionado para o borealis
// registrar um press (application.cpp: `!oldControllerState.buttons[i]`).
// Segurar por varios frames dispararia o auto-repeat e a acao aconteceria
// duas vezes. Entao: um frame pressionado, um frame solto, e so depois o
// roteiro anda.
int g_key_hold = 0;
int g_key_index = -1;

// --- Gesto de toque (spec 133) ---------------------------------------------
//
// O dedo tambem precisa de transicoes: o borealis deriva START/STAY/END de
// `pressed` mudando entre frames, entao um toque instantaneo (encostar e
// soltar no mesmo frame) nao produz gesto nenhum.
//
// O arrasto anda em PASSOS. Saltar da origem ao destino num frame daria um
// deslocamento unico e gigante — o PanGesture leria isso como um pulo, nao
// como o movimento continuo que ele mede para decidir aceitar o gesto.
struct TouchGesture {
  bool active = false;
  float x0 = 0, y0 = 0;  // origem
  float x1 = 0, y1 = 0;  // destino (igual a origem num `tap`)
  int step = 0;          // passo corrente
  int steps = 0;         // total de passos do arrasto
  int hold = 0;          // frames parado no fim, antes de soltar
};
TouchGesture g_touch;

// Passos de um `drag`. 12 e o suficiente para o PanGesture ver movimento
// continuo sem alongar demais o roteiro.
constexpr int kDragSteps = 12;
// Frames com o dedo parado no destino antes de levantar. Sem essa pausa o
// END chega junto com o ultimo movimento e a UI nao alcanca desenhar o
// estado de "segurando sobre o destino".
constexpr int kDragHold = 6;

// Teto de frames do roteiro inteiro. Um fluxo que nunca alcanca o estado
// esperado tem de FALHAR, nao pendurar o ctest ate o timeout do CI.
constexpr long kMaxFrames = 60 * 60 * 5;  // ~5 min a 60fps
long g_frames = 0;

// --- Erro ------------------------------------------------------------------

// Toda falha passa por aqui: marca o codigo de saida e imprime no stderr, que
// e o que o ctest mostra em --output-on-failure.
void Fail(int line, const std::string& msg) {
  std::fprintf(stderr, "[script] linha %d: %s\n", line, msg.c_str());
  g_exit = 1;
  g_active = false;
}

void Info(const std::string& msg) {
  std::fprintf(stderr, "[script] %s\n", msg.c_str());
}

// --- Botoes ----------------------------------------------------------------

// Nome do roteiro -> indice do botao no ControllerState do borealis.
// So os botoes que a UI do app realmente usa; um nome fora desta lista e erro
// de sintaxe, nao um no-op silencioso.
int ButtonIndex(const std::string& name) {
  if (name == "A") return brls::BUTTON_A;
  if (name == "B") return brls::BUTTON_B;
  if (name == "X") return brls::BUTTON_X;
  if (name == "Y") return brls::BUTTON_Y;
  if (name == "L") return brls::BUTTON_LB;
  if (name == "R") return brls::BUTTON_RB;
  if (name == "ZL") return brls::BUTTON_LT;
  if (name == "ZR") return brls::BUTTON_RT;
  if (name == "UP") return brls::BUTTON_UP;
  if (name == "DOWN") return brls::BUTTON_DOWN;
  if (name == "LEFT") return brls::BUTTON_LEFT;
  if (name == "RIGHT") return brls::BUTTON_RIGHT;
  if (name == "PLUS") return brls::BUTTON_START;
  if (name == "MINUS") return brls::BUTTON_BACK;
  // Cliques de stick: LSB troca de painel, RSB abre a lista (spec 031).
  if (name == "LSB") return brls::BUTTON_LSB;
  if (name == "RSB") return brls::BUTTON_RSB;
  return -1;
}

// --- Estado ----------------------------------------------------------------

// O JSON do dump. A tela ativa responde pelo proprio estado; sem provedor
// registrado sai so a identificacao, que ja permite `assert activity <nome>`.
std::string DumpJson() {
  if (g_stack.empty()) return "{\"activity\":\"\"}";
  const Screen& top = g_stack.back();
  const std::string body = top.provider ? top.provider() : std::string();
  std::string out = "{\"activity\":\"" + top.name + "\"";
  if (!body.empty()) out += "," + body;
  out += "}";
  return out;
}

// Le uma chave do JSON do dump. Busca textual mesmo: o dump e gerado aqui ao
// lado e um parser de JSON completo seria peso morto — o roteiro compara
// valores escalares.
//
// Aceita `"chave":valor` e `"chave":"valor"`, e casa a chave inteira para
// `box` nao casar dentro de `box_count`.
bool ReadKey(const std::string& json, const std::string& key,
             std::string* out) {
  const std::string needle = "\"" + key + "\":";
  std::size_t at = json.find(needle);
  if (at == std::string::npos) return false;
  std::size_t p = at + needle.size();
  if (p >= json.size()) return false;

  if (json[p] == '"') {
    const std::size_t end = json.find('"', p + 1);
    if (end == std::string::npos) return false;
    *out = json.substr(p + 1, end - p - 1);
    return true;
  }
  const std::size_t end = json.find_first_of(",}", p);
  if (end == std::string::npos) return false;
  *out = json.substr(p, end - p);
  return true;
}

// --- Comandos --------------------------------------------------------------

// Executa um comando. Devolve false se o roteiro deve parar agora (falha).
// Comandos que consomem tempo (`key`, `wait`) apenas ARMAM o relogio; quem
// os deixa acontecer e o Step() dos frames seguintes.
bool Run(const Command& c) {
  const int line = c.line;

  if (c.verb == "key") {
    if (c.args.size() != 1) {
      Fail(line, "key exige 1 argumento");
      return false;
    }
    const int idx = ButtonIndex(c.args[0]);
    if (idx < 0) {
      Fail(line, "botao desconhecido: " + c.args[0]);
      return false;
    }
    // Arma a transicao: pressionado neste frame, solto no proximo.
    g_key_index = idx;
    g_key_hold = 1;
    return true;
  }

  if (c.verb == "tap" || c.verb == "drag") {
    const bool drag = (c.verb == "drag");
    const std::size_t need = drag ? 4u : 2u;
    if (c.args.size() != need) {
      Fail(line, c.verb + " exige " + std::to_string(need) + " argumentos");
      return false;
    }
    g_touch = TouchGesture{};
    g_touch.active = true;
    g_touch.x0 = static_cast<float>(std::atof(c.args[0].c_str()));
    g_touch.y0 = static_cast<float>(std::atof(c.args[1].c_str()));
    g_touch.x1 = drag ? static_cast<float>(std::atof(c.args[2].c_str())) : g_touch.x0;
    g_touch.y1 = drag ? static_cast<float>(std::atof(c.args[3].c_str())) : g_touch.y0;
    g_touch.steps = drag ? kDragSteps : 1;
    g_touch.hold = drag ? kDragHold : 2;
    g_touch.step = 0;
    return true;
  }

  if (c.verb == "wait") {
    if (c.args.size() != 1) {
      Fail(line, "wait exige 1 argumento");
      return false;
    }
    g_wait = std::atoi(c.args[0].c_str());
    if (g_wait < 0) {
      Fail(line, "wait negativo");
      return false;
    }
    return true;
  }

  if (c.verb == "screenshot") {
    if (c.args.size() != 1) {
      Fail(line, "screenshot exige 1 argumento");
      return false;
    }
    // Arma a captura; quem escreve o PNG e o fim do quadro seguinte, em
    // glfw_video.cpp — la o framebuffer tem o desenho pronto.
    std::snprintf(brls::nestboxShotPath, sizeof(brls::nestboxShotPath), "%s",
                  c.args[0].c_str());
    brls::nestboxShotPending = true;
    // Dois frames de folga: um para o quadro ser desenhado e capturado,
    // outro para o arquivo fechar antes de o roteiro seguir.
    g_wait = (g_wait > 2) ? g_wait : 2;
    return true;
  }

  if (c.verb == "dump") {
    if (c.args.size() != 1) {
      Fail(line, "dump exige 1 argumento");
      return false;
    }
    std::ofstream f(c.args[0], std::ios::binary);
    if (!f) {
      Fail(line, "dump: nao foi possivel escrever " + c.args[0]);
      return false;
    }
    f << DumpJson();
    return true;
  }

  if (c.verb == "assert") {
    if (c.args.size() < 2) {
      Fail(line, "assert exige 2 argumentos");
      return false;
    }
    // O valor pode ter espacos ("Salvar e sair"): tudo depois da chave e o
    // valor esperado, remontado com um espaco entre as partes. Sem isto um
    // rotulo de botao com espaco seria impossivel de afirmar.
    std::string want = c.args[1];
    for (std::size_t i = 2; i < c.args.size(); ++i) want += " " + c.args[i];

    const std::string json = DumpJson();
    std::string got;
    if (!ReadKey(json, c.args[0], &got)) {
      Fail(line, "assert: chave ausente no estado: " + c.args[0] +
                     "\n  estado: " + json);
      return false;
    }
    if (got != want) {
      Fail(line, "assert " + c.args[0] + ": esperado \"" + want +
                     "\", obtido \"" + got + "\"");
      return false;
    }
    return true;
  }

  if (c.verb == "echo") {
    std::string msg;
    for (const std::string& a : c.args) {
      if (!msg.empty()) msg += " ";
      msg += a;
    }
    Info(msg);
    return true;
  }

  Fail(line, "comando desconhecido: " + c.verb);
  return false;
}

}  // namespace

// --- API -------------------------------------------------------------------

void SetStateProvider(const char* name, StateProvider provider) {
  const std::string key = name ? name : "";
  // A MESMA tela registrando de novo ATUALIZA em vez de empilhar: a
  // BoxActivity registra duas vezes (o LogScreen poe a identificacao, o
  // construtor poe o provedor rico) e sao a mesma tela.
  if (!g_stack.empty() && g_stack.back().name == key) {
    g_stack.back().provider = std::move(provider);
    return;
  }
  g_stack.push_back({key, std::move(provider)});
}

void ClearStateProvider(const char* name) {
  const std::string key = name ? name : "";
  // Pilha, e nao um slot unico: fechar um dialogo tem de REVELAR a tela de
  // baixo, que e a que o usuario volta a ver. Com um slot so, sair de uma
  // MessageBox deixava o dump cego.
  //
  // Remove a ocorrencia mais recente deste nome — telas empilhadas saem fora
  // de ordem, entao apagar sempre o topo perderia o registro errado.
  for (auto it = g_stack.rbegin(); it != g_stack.rend(); ++it) {
    if (it->name == key) {
      g_stack.erase(std::next(it).base());
      return;
    }
  }
}

bool Load(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    std::fprintf(stderr, "[script] nao foi possivel abrir o roteiro: %s\n",
                 path.c_str());
    g_exit = 1;
    return false;
  }

  std::string raw;
  int line = 0;
  bool first = true;
  while (std::getline(f, raw)) {
    ++line;
    // BOM UTF-8: editor de texto do Windows o adiciona sem avisar, e ele
    // grudaria no primeiro verbo — um roteiro valido seria recusado.
    if (first) {
      first = false;
      if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
          static_cast<unsigned char>(raw[1]) == 0xBB &&
          static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
      }
    }
    // Fim de linha CRLF: o CR sobreviveria ao getline e entraria no ultimo
    // argumento, fazendo `assert` comparar contra o valor com CR no fim.
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    // Comentario e linha em branco somem aqui: o roteiro e escrito a mao.
    const std::size_t hash = raw.find('#');
    if (hash != std::string::npos) raw.erase(hash);

    std::istringstream parts(raw);
    Command c;
    c.line = line;
    if (!(parts >> c.verb)) continue;

    std::string arg;
    while (parts >> arg) c.args.push_back(arg);
    g_script.push_back(std::move(c));
  }

  if (g_script.empty()) {
    std::fprintf(stderr, "[script] roteiro vazio: %s\n", path.c_str());
    g_exit = 1;
    return false;
  }

  // Sintaxe conferida ANTES de a UI subir: um verbo errado na ultima linha
  // deve falhar agora, nao depois de o roteiro ja ter mexido no save.
  for (const Command& c : g_script) {
    if (c.verb != "key" && c.verb != "wait" && c.verb != "dump" &&
        c.verb != "assert" && c.verb != "echo" && c.verb != "tap" &&
        c.verb != "drag" && c.verb != "screenshot") {
      Fail(c.line, "comando desconhecido: " + c.verb);
      return false;
    }
    if (c.verb == "key" && (c.args.size() != 1 || ButtonIndex(c.args[0]) < 0)) {
      Fail(c.line, "key: botao invalido");
      return false;
    }
  }

  g_active = true;
  brls::nestboxScriptActive = true;
  Info("roteiro carregado: " + path + " (" +
       std::to_string(g_script.size()) + " comandos)");
  return true;
}

bool Active() { return g_active; }

int ExitCode() { return g_exit; }

bool Step() {
  if (!g_active) return false;

  if (++g_frames > kMaxFrames) {
    Fail(g_pc < g_script.size() ? g_script[g_pc].line : 0,
         "roteiro estourou o limite de frames");
    return false;
  }

  // 1) Solta o botao do frame anterior. Precisa acontecer ANTES de qualquer
  //    coisa: e a borda de descida que fecha o press.
  if (g_key_index >= 0 && g_key_hold == 0) {
    brls::nestboxScriptButtons[g_key_index] = false;
    g_key_index = -1;
    // Um frame de folga com o botao ja solto, para a acao disparada por ele
    // rodar antes de o proximo comando ler o estado.
    g_wait = (g_wait > 1) ? g_wait : 1;
    return true;
  }

  // 2) Mantem o botao pressionado pelo frame armado.
  if (g_key_index >= 0) {
    brls::nestboxScriptButtons[g_key_index] = true;
    --g_key_hold;
    return true;
  }

  // 3) Gesto de toque em andamento (spec 133): um passo por frame, para o
  //    borealis derivar START/STAY/END como faria com um dedo real.
  if (g_touch.active) {
    if (g_touch.step < g_touch.steps) {
      // Interpola da origem ao destino. O primeiro passo ja encosta na
      // origem (t=0 quando ha um passo so, que e o caso do `tap`).
      const float t = (g_touch.steps <= 1)
                          ? 0.0f
                          : static_cast<float>(g_touch.step) /
                                static_cast<float>(g_touch.steps - 1);
      brls::nestboxScriptTouchX = g_touch.x0 + (g_touch.x1 - g_touch.x0) * t;
      brls::nestboxScriptTouchY = g_touch.y0 + (g_touch.y1 - g_touch.y0) * t;
      brls::nestboxScriptTouching = true;
      ++g_touch.step;
      return true;
    }
    if (g_touch.hold > 0) {
      // Parado no destino, dedo ainda encostado.
      --g_touch.hold;
      return true;
    }
    // Levanta o dedo: e esta borda que vira o END do gesto.
    brls::nestboxScriptTouching = false;
    g_touch.active = false;
    // Um frame de folga com o dedo ja fora, para a acao disparada pelo gesto
    // rodar antes de o proximo comando ler o estado.
    g_wait = (g_wait > 1) ? g_wait : 1;
    return true;
  }

  // 4) Deixa a UI respirar.
  if (g_wait > 0) {
    --g_wait;
    return true;
  }

  // 5) Fim do roteiro.
  if (g_pc >= g_script.size()) {
    Info(g_exit == 0 ? "roteiro concluido" : "roteiro terminou com falha");
    g_active = false;
    return false;
  }

  // 6) Proximo comando. Um comando por frame: assim `key` seguido de `assert`
  //    nao le o estado antes de a acao do botao ter acontecido.
  const Command& c = g_script[g_pc++];
  return Run(c);
}

}  // namespace nestbox::script

#endif  // __SWITCH__
