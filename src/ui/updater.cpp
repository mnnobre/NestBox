#include "updater.h"

#include <cerrno>
#include <sys/stat.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__SWITCH__) && defined(NESTBOX_HAS_CURL)
#include <curl/curl.h>
#include <switch.h>

#include "nlog.h"
#define NESTBOX_UPDATE_ENABLED 1
#endif

namespace nestbox {
namespace {

#ifdef NESTBOX_UPDATE_ENABLED

// O .nro em execucao fica travado pelo hbloader. O download vai para .new e a
// troca acontece na abertura seguinte.
//
// O caminho vem de argv[0] (SetRunningNroPath): supor "sdmc:/switch/..." erra
// se o usuario renomeou o arquivo ou o pos noutra pasta, e ai o rename cria um
// .nro que o hbmenu nem lista enquanto o antigo continua abrindo.
std::string g_nro_path = "sdmc:/switch/nestbox.nro";
// true quando o app foi carregado a partir do .new — reinicio pos-update.
bool g_running_from_pending = false;

std::string NroPath() { return g_nro_path; }
std::string PendingPath() { return g_nro_path + ".new"; }

bool RunningFromPending() { return g_running_from_pending; }
constexpr const char* kLogPath = "sdmc:/switch/nestbox-update.log";

// Diagnostico do update. O app fecha entre baixar e aplicar, entao um erro so
// e observavel se ficar gravado.
void Log(const char* fmt, ...) {
  std::FILE* f = std::fopen(kLogPath, "a");
  if (!f) return;
  va_list args;
  va_start(args, fmt);
  std::vfprintf(f, fmt, args);
  va_end(args);
  std::fputc('\n', f);
  std::fclose(f);
}

std::size_t AppendToString(void* data, std::size_t size, std::size_t n,
                           void* user) {
  auto* out = static_cast<std::string*>(user);
  out->append(static_cast<char*>(data), size * n);
  return size * n;
}

std::size_t WriteToFile(void* data, std::size_t size, std::size_t n,
                        void* user) {
  return std::fwrite(data, size, n, static_cast<std::FILE*>(user));
}

int ReportProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t,
                   curl_off_t) {
  if (!user || total <= 0) return 0;
  auto* cb = static_cast<const std::function<void(float)>*>(user);
  (*cb)(static_cast<float>(now) / static_cast<float>(total));
  return 0;
}

// Extrai o valor de uma chave JSON por busca de substring. A resposta da API
// e pequena e previsivel; um parser completo seria mais robusto, mas traria
// uma dependencia nova para ler dois campos.
std::string JsonField(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\":";
  const auto k = json.find(needle);
  if (k == std::string::npos) return "";

  const auto open = json.find('"', k + needle.size());
  if (open == std::string::npos) return "";
  const auto close = json.find('"', open + 1);
  if (close == std::string::npos) return "";

  return json.substr(open + 1, close - open - 1);
}

// Compara "1.2.3" numericamente, campo a campo. Comparar como texto erraria:
// "0.10.0" < "0.9.0" em ordem lexicografica.
bool IsNewer(const std::string& candidate, const std::string& current) {
  int a[3] = {0, 0, 0};
  int b[3] = {0, 0, 0};
  std::sscanf(candidate.c_str(), "%d.%d.%d", &a[0], &a[1], &a[2]);
  std::sscanf(current.c_str(), "%d.%d.%d", &b[0], &b[1], &b[2]);
  for (int i = 0; i < 3; ++i) {
    if (a[i] != b[i]) return a[i] > b[i];
  }
  return false;
}

bool HasNetwork() {
  NifmInternetConnectionType type;
  std::uint32_t strength = 0;
  NifmInternetConnectionStatus status;
  if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status))) {
    return false;
  }
  return status == NifmInternetConnectionStatus_Connected;
}

#endif  // NESTBOX_UPDATE_ENABLED

}  // namespace

UpdateInfo CheckForUpdate() {
  UpdateInfo info;
#ifdef NESTBOX_UPDATE_ENABLED
  // Cada saida silenciosa vira uma linha de log (spec 111): "o update nao
  // apareceu" precisa ser diagnosticavel pelo nestbox.log.
  if (!HasNetwork()) {
    NLOG_ACT("update: sem internet na abertura — checagem pulada");
    return info;
  }

  CURL* curl = curl_easy_init();
  if (!curl) return info;

  const std::string url =
      "https://api.github.com/repos/" NESTBOX_REPO "/releases/latest";
  std::string body;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AppendToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  // A API do GitHub exige User-Agent.
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "NestBox");
  // Timeout curto: uma consulta lenta nao pode atrasar a abertura do app.
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

  const CURLcode rc = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  if (rc != CURLE_OK || body.empty()) {
    NLOG_ACT("update: consulta ao GitHub falhou (curl rc=%d, %zu bytes)",
             static_cast<int>(rc), body.size());
    return info;
  }

  std::string tag = JsonField(body, "tag_name");
  if (tag.empty()) {
    NLOG_ACT("update: resposta sem tag_name (rate limit?)");
    return info;
  }
  if (tag[0] == 'v') tag.erase(0, 1);

  if (!IsNewer(tag, CurrentVersion())) {
    NLOG_ACT("update: ja na versao mais recente (%s local, %s remota)",
             CurrentVersion(), tag.c_str());
    return info;
  }
  NLOG_ACT("update: %s disponivel (local %s)", tag.c_str(), CurrentVersion());

  info.latest_version = tag;
  info.download_url = JsonField(body, "browser_download_url");
  info.available = !info.download_url.empty();
#endif
  return info;
}

#ifdef NESTBOX_UPDATE_ENABLED
// Um .nro valido tem a magic "NRO0" em 0x10.
bool LooksLikeNro(const char* path) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return false;
  char magic[4] = {};
  const bool ok = std::fseek(f, 0x10, SEEK_SET) == 0 &&
                  std::fread(magic, 1, 4, f) == 4 &&
                  std::memcmp(magic, "NRO0", 4) == 0;
  std::fclose(f);
  return ok;
}
#endif

bool DownloadUpdate(const std::string& url,
                    const std::function<void(float)>& on_progress) {
#ifdef NESTBOX_UPDATE_ENABLED
  std::FILE* out = std::fopen(PendingPath().c_str(), "wb");
  if (!out) return false;

  CURL* curl = curl_easy_init();
  if (!curl) {
    std::fclose(out);
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "NestBox");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ReportProgress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &on_progress);

  const CURLcode rc = curl_easy_perform(curl);
  long http = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
  curl_easy_cleanup(curl);
  std::fclose(out);

  if (rc != CURLE_OK || http != 200) {
    // Download parcial nao pode virar o .nro da proxima abertura.
    Log("download falhou: curl=%d http=%ld", static_cast<int>(rc), http);
    std::remove(PendingPath().c_str());
    return false;
  }

  // curl devolve CURLE_OK quando a transferencia funciona, mesmo que o corpo
  // seja uma pagina de erro. Sem esta checagem o .new fica com lixo, e o app
  // volta a oferecer a atualizacao na abertura seguinte.
  if (!LooksLikeNro(PendingPath().c_str())) {
    Log("download nao e um NRO valido; descartado");
    std::remove(PendingPath().c_str());
    return false;
  }

  Log("download concluido: %s", PendingPath().c_str());
  return true;
#else
  (void)url;
  (void)on_progress;
  return false;
#endif
}

void SetRunningNroPath(const char* argv0) {
#ifdef NESTBOX_UPDATE_ENABLED
  if (!argv0 || !*argv0) {
    Log("argv[0] vazio; mantendo %s", g_nro_path.c_str());
    return;
  }
  std::string path = argv0;
  if (path.size() <= 4 || path.compare(path.size() - 4, 4, ".nro") != 0) {
    Log("argv[0] nao e .nro: \"%s\"", argv0);
    return;
  }

  // O hbloader entrega o caminho sem o prefixo do dispositivo ("/switch/...").
  // Sem normalizar, o app grava o .new num caminho e tenta trocar em outro —
  // foi o que produziu ENOENT no remove e EEXIST no rename.
  if (path.compare(0, 5, "sdmc:") != 0) {
    if (path[0] != '/') path.insert(0, "/");
    path.insert(0, "sdmc:");
  }

  // Rodando a partir do proprio .new (reinicio pos-update): o destino final e
  // o caminho sem o sufixo, e o binario em execucao e que deve ser promovido.
  const std::string kSuffix = ".nro.new";
  if (path.size() > kSuffix.size() &&
      path.compare(path.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
    g_running_from_pending = true;
    path.erase(path.size() - 4);  // tira ".new"
  }

  g_nro_path = path;
  Log("nro em execucao: %s%s (argv0=\"%s\")", g_nro_path.c_str(),
      g_running_from_pending ? " [rodando do .new]" : "", argv0);
#else
  (void)argv0;
#endif
}

#ifdef NESTBOX_UPDATE_ENABLED
// Estado do download fatiado. Um de cada vez — a tela so oferece um.
CURLM* g_multi = nullptr;
CURL* g_easy = nullptr;
std::FILE* g_out = nullptr;
double g_total = 0.0, g_now = 0.0;
#endif

bool BeginDownload(const std::string& url) {
#ifdef NESTBOX_UPDATE_ENABLED
  if (g_multi) return false;  // ja em andamento

  g_out = std::fopen(PendingPath().c_str(), "wb");
  if (!g_out) return false;

  g_easy = curl_easy_init();
  g_multi = curl_multi_init();
  if (!g_easy || !g_multi) {
    EndDownload();
    return false;
  }

  curl_easy_setopt(g_easy, CURLOPT_URL, url.c_str());
  curl_easy_setopt(g_easy, CURLOPT_WRITEFUNCTION, WriteToFile);
  curl_easy_setopt(g_easy, CURLOPT_WRITEDATA, g_out);
  curl_easy_setopt(g_easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(g_easy, CURLOPT_USERAGENT, "NestBox");
  curl_easy_setopt(g_easy, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_multi_add_handle(g_multi, g_easy);
  g_total = g_now = 0.0;
  return true;
#else
  (void)url;
  return false;
#endif
}

bool PumpDownload(float* progress) {
#ifdef NESTBOX_UPDATE_ENABLED
  if (!g_multi) return false;

  int running = 0;
  // Uma fatia curta: o suficiente para andar sem segurar o frame.
  curl_multi_perform(g_multi, &running);
  curl_multi_wait(g_multi, nullptr, 0, 16, nullptr);

  curl_easy_getinfo(g_easy, CURLINFO_SIZE_DOWNLOAD, &g_now);
  curl_easy_getinfo(g_easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &g_total);
  if (progress) {
    *progress = g_total > 0.0 ? float(g_now / g_total) : 0.0f;
  }
  return running != 0;
#else
  (void)progress;
  return false;
#endif
}

bool EndDownload() {
#ifdef NESTBOX_UPDATE_ENABLED
  bool ok = false;
  long http = 0;
  if (g_easy) {
    CURLcode rc = CURLE_OK;
    int msgs = 0;
    while (CURLMsg* m = curl_multi_info_read(g_multi, &msgs)) {
      if (m->msg == CURLMSG_DONE) rc = m->data.result;
    }
    curl_easy_getinfo(g_easy, CURLINFO_RESPONSE_CODE, &http);
    curl_multi_remove_handle(g_multi, g_easy);
    curl_easy_cleanup(g_easy);
    ok = rc == CURLE_OK && http == 200;
  }
  if (g_multi) curl_multi_cleanup(g_multi);
  if (g_out) std::fclose(g_out);
  g_easy = nullptr;
  g_multi = nullptr;
  g_out = nullptr;

  if (ok && !LooksLikeNro(PendingPath().c_str())) {
    Log("download nao e um NRO valido; descartado");
    ok = false;
  }
  if (!ok) {
    Log("download falhou: http=%ld", http);
    std::remove(PendingPath().c_str());
  } else {
    Log("download concluido: %s", PendingPath().c_str());
  }
  return ok;
#else
  return false;
#endif
}

// Copia um arquivo inteiro pela API fs do libnx, com commit no fim.
//
// Arquitetura copiada do Sphaira (sphaira/source/fs.cpp), que resolve o mesmo
// problema — um homebrew que substitui o proprio binario. Duas licoes que
// custaram quatro tentativas aqui:
//
//   1. NUNCA usar rename(). O comentario deles e explicito: entre o delete e
//      o rename existe uma janela em que outro processo pode abrir o arquivo.
//      Copiar abre o handle uma vez; se abriu, a escrita inteira funciona.
//   2. stdio (fopen/remove) nao basta: e preciso fsFsCommit, senao a escrita
//      pode nao persistir no cartao.
#ifdef NESTBOX_UPDATE_ENABLED
Result CopyEntireFile(const char* dst, const char* src) {
  // Le tudo primeiro: o destino so e tocado quando ha conteudo garantido.
  std::FILE* in = std::fopen(src, "rb");
  if (!in) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

  std::fseek(in, 0, SEEK_END);
  const long size = std::ftell(in);
  std::fseek(in, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(in);
    return MAKERESULT(Module_Libnx, LibnxError_IoError);
  }

  std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
  const std::size_t got = std::fread(data.data(), 1, data.size(), in);
  std::fclose(in);
  if (got != data.size()) return MAKERESULT(Module_Libnx, LibnxError_IoError);

  std::FILE* out = std::fopen(dst, "wb");
  if (!out) return MAKERESULT(Module_Libnx, LibnxError_IoError);
  const std::size_t put = std::fwrite(data.data(), 1, data.size(), out);
  std::fflush(out);
  std::fclose(out);
  if (put != data.size()) return MAKERESULT(Module_Libnx, LibnxError_IoError);

  // O commit e o que faltava: sem ele a escrita pode nao chegar ao cartao.
  FsFileSystem* sdmc = fsdevGetDeviceFileSystem("sdmc");
  if (sdmc) fsFsCommit(sdmc);
  return 0;
}
#endif

bool ApplyPendingUpdate() {
#ifdef NESTBOX_UPDATE_ENABLED
  Log("--- abertura: versao %s, alvo %s", CurrentVersion(), NroPath().c_str());

  struct stat sp {};
  if (stat(PendingPath().c_str(), &sp) != 0) {
    return false;  // nada baixado: caso normal
  }
  Log("pendente presente: %lld bytes", (long long)sp.st_size);

  if (!LooksLikeNro(PendingPath().c_str())) {
    Log("pendente invalido (sem magic NRO0); descartado");
    std::remove(PendingPath().c_str());
    return false;
  }

  // Copiar, nunca renomear (ver CopyEntireFile). Funciona nos dois sentidos:
  // rodando do .nro (o pendente vira o definitivo) ou rodando do proprio .new
  // apos o reinicio encadeado.
  const Result rc = CopyEntireFile(NroPath().c_str(), PendingPath().c_str());
  if (R_FAILED(rc)) {
    Log("copia falhou (rc=0x%x): %s -> %s", rc, PendingPath().c_str(),
        NroPath().c_str());
    return false;
  }

  // So apaga o pendente depois da copia confirmada.
  if (std::remove(PendingPath().c_str()) != 0) {
    Log("copia ok, mas nao consegui apagar o pendente (errno=%d)", errno);
  }
  Log("atualizacao aplicada: %s", NroPath().c_str());
  return true;
#else
  return false;
#endif
}

bool RestartIntoUpdate() {
#ifdef NESTBOX_UPDATE_ENABLED
  if (!envHasNextLoad()) {
    Log("loader nao suporta encadeamento; sera preciso reabrir na mao");
    return false;
  }

  // Rodar o .nro recem-baixado diretamente: o arquivo em uso continua travado
  // pelo hbloader, mas o proximo carregamento pode apontar para o .new. A
  // troca de nomes acontece na abertura seguinte, ja com o antigo liberado.
  const std::string next = PendingPath();
  const Result rc = envSetNextLoad(next.c_str(), next.c_str());
  if (R_FAILED(rc)) {
    Log("envSetNextLoad falhou (rc=0x%x) para %s", rc, next.c_str());
    return false;
  }
  Log("reiniciando em %s", next.c_str());
  return true;
#else
  return false;
#endif
}

}  // namespace nestbox
