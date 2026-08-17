#include "device_saves.h"

#include "switch_sim.h"
#include "switch_titles.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>
#include <cstring>

#include <sys/stat.h>

#include <dirent.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace nestbox {

namespace sim = pokehome::sim;

namespace {

// Diretorios onde emuladores e dumps costumam deixar saves de GBA.
//
// No PC esta lista e VAZIA de proposito: a fonte de verdade e o simulador
// (spec 039/041). Ver o comentario dentro do #else.
constexpr const char* kSaveDirs[] = {
#ifdef __SWITCH__
    "sdmc:/pokehome/",
    "sdmc:/nestbox/",
    "sdmc:/retroarch/saves/",
    "sdmc:/mgba/",
    "sdmc:/switch/nestbox/saves/",
#else
    // No PC a fonte de verdade e o simulador (spec 039/041), nao pastas
    // avulsas: o console nao tem save de jogo nativo solto num diretorio, e
    // listar o sdcard do Ryujinx aqui mostrava os mesmos saves duas vezes —
    // uma como arquivo, outra como jogo instalado.
    //
    // Saves de emulador voltam quando houver emulador em uso; a lista fica
    // vazia de proposito ate la.
#endif
};

constexpr const char* kSaveExts[] = {".sav", ".srm", ".sps"};

#ifdef __SWITCH__
// Diagnostico da descoberta. Sem isto so da para supor por que um jogo nao
// apareceu na tela — e supor ja custou duas rodadas.
void LogDiscovery(const char* fmt, ...) {
  std::FILE* f = std::fopen("sdmc:/switch/nestbox-saves.log", "a");
  if (!f) return;
  va_list args;
  va_start(args, fmt);
  std::vfprintf(f, fmt, args);
  va_end(args);
  std::fputc('\n', f);
  std::fclose(f);
}
#endif
// (usado nas duas plataformas)

bool HasSaveExtension(const std::string& name) {
  for (const char* ext : kSaveExts) {
    const std::size_t n = std::strlen(ext);
    if (name.size() > n && name.compare(name.size() - n, n, ext) == 0) {
      return true;
    }
  }
  return false;
}

// Busca do titulo pelo ApplicationId. Usada nos DOIS caminhos: no console,
// sobre o que o fsSaveDataInfoReader devolve; no PC, sobre o nome da pasta do
// simulador (spec 039). E o que garante que os dois concordam sobre o que e um
// jogo conhecido.
const SwitchTitle* FindTitle(std::uint64_t app_id) {
  for (const SwitchTitle& t : kSwitchTitles) {
    if (t.app_id == app_id) return &t;
  }
  return nullptr;
}

#ifdef __SWITCH__

// Identificacao por ApplicationId, conferida contra blawar/titledb e contra o
// console do dono. O nome do NACP nao serve: 223 dos 225 saves enumerados no
// console vieram sem nome nenhum.
using KnownTitle = SwitchTitle;
constexpr auto& kPokemonTitles = kSwitchTitles;

// Nome e ícone do jogo pelo ApplicationId, numa consulta só. O ícone (JPEG)
// vem no mesmo buffer do control data e é gravado num cache no SD para o
// brls::Image carregar por caminho.
struct ControlInfo {
  std::string name;
  std::string icon_path;
};

ControlInfo QueryControl(std::uint64_t app_id) {
  ControlInfo out;
  for (const KnownTitle& t : kPokemonTitles) {
    if (t.app_id == app_id) out.name = t.name;
  }

  // O buffer do control data é grande (~144 KB), então é alocado e liberado
  // por consulta, não mantido.
  auto* data = new (std::nothrow) NsApplicationControlData();
  if (!data) return out;

  std::uint64_t out_size = 0;
  if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                              app_id, data, sizeof(*data),
                                              &out_size))) {
    if (out.name.empty()) {
      NacpLanguageEntry* entry = nullptr;
      if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &entry)) && entry) {
        out.name = entry->name;
      }
    }

    // Icone: o que sobra do buffer depois do NACP.
    if (out_size > sizeof(NacpStruct)) {
      const std::size_t icon_size = out_size - sizeof(NacpStruct);
      mkdir("sdmc:/switch/nestbox", 0777);
      mkdir("sdmc:/switch/nestbox/cache", 0777);
      char path[96];
      std::snprintf(path, sizeof(path),
                    "sdmc:/switch/nestbox/cache/%016llX.jpg",
                    static_cast<unsigned long long>(app_id));
      std::FILE* f = std::fopen(path, "wb");
      if (f) {
        std::fwrite(data->icon, 1, icon_size, f);
        std::fclose(f);
        out.icon_path = path;
      }
    }
  }
  delete data;
  return out;
}

// Procura um "main" no save data — o formato SwishCrypto (SwSh, Arceus, Z-A).
// So o tamanho decide aqui; quem valida de verdade e o hash no ParseZaSave.
std::string FindMainSave(std::uint64_t app_id, AccountUid uid) {
  if (R_FAILED(fsdevMountSaveData("nbmain", app_id, uid))) {
    LogDiscovery("  montagem falhou (main) app=%016llX",
                 (unsigned long long)app_id);
    return "";
  }

  std::string found;
  // Percorre os nomes conhecidos, nao so "main": BDSP usa SaveData.bin e
  // Let's Go usa savedata.bin. A lista mora no core e e a MESMA que o
  // simulador consulta (spec 042).
  for (const char* name : sim::kSaveNames) {
    struct stat st {};
    const std::string path = std::string("nbmain:/") + name;
    if (stat(path.c_str(), &st) == 0 &&
        sim::IsMainSave(name, static_cast<std::size_t>(st.st_size))) {
      found = name;
      LogDiscovery("  %s encontrado: %lld bytes", name, (long long)st.st_size);
      break;
    }
  }
  if (found.empty()) {
    // Nenhum nome conhecido bateu; registrar o que existe ajuda a mapear o
    // proximo jogo.
    if (DIR* d = opendir("nbmain:/")) {
      while (dirent* e = readdir(d)) {
        LogDiscovery("  conteudo: %s", e->d_name);
      }
      closedir(d);
    }
  }
  fsdevUnmountDevice("nbmain");
  return found;
}

// Monta o save data do jogo e procura um .sav de GBA dentro. Devolve o nome
// do arquivo, ou vazio se nao houver (ou se a montagem falhar).
std::string FindGbaSave(std::uint64_t app_id, AccountUid uid) {
  if (R_FAILED(fsdevMountSaveData("nbsave", app_id, uid))) {
    LogDiscovery("  montagem falhou (gba) app=%016llX",
                 (unsigned long long)app_id);
    return "";
  }

  std::string found;
  if (DIR* d = opendir("nbsave:/")) {
    while (dirent* e = readdir(d)) {
      const std::string name = e->d_name;
      if (!HasSaveExtension(name)) continue;
      // Tamanho tem que bater com um save gen3.
      const std::string path = "nbsave:/" + name;
      struct stat st {};
      if (stat(path.c_str(), &st) == 0 && st.st_size == 131072) {
        found = name;
        break;
      }
    }
    closedir(d);
  }
  fsdevUnmountDevice("nbsave");
  return found;
}

#endif  // __SWITCH__

}  // namespace

#ifndef __SWITCH__
// Raiz do simulador de save data (spec 039). Vazia = sem simulacao, e o app
// volta a nao listar jogo instalado nenhum, como antes.
//
// A variavel de ambiente ganha do caminho padrao para o dono poder apontar
// para outro lugar sem recompilar.
std::string SimRoot() {
  if (const char* env = std::getenv("NESTBOX_SWITCH_SIM")) {
    if (*env) return env;
  }
  const std::string padrao = std::string(NESTBOX_SIM_DEFAULT);
  struct stat st {};
  if (stat(padrao.c_str(), &st) == 0) return padrao;
  return "";
}

// Procura, dentro de um save data "montado", o arquivo que serve. As regras
// (tamanho exato de GBA, `main` grande) moram no core e sao as mesmas do
// console — ver switch_sim.h.
std::string FindInnerSave(const std::string& dir) {
  DIR* d = opendir(dir.c_str());
  if (!d) return "";

  std::string found;
  while (dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    struct stat st {};
    if (stat((dir + "/" + name).c_str(), &st) != 0) continue;
    const std::size_t size = static_cast<std::size_t>(st.st_size);

    if (sim::IsGbaSave(name, size) || sim::IsMainSave(name, size)) {
      found = name;
      break;
    }
  }
  closedir(d);
  return found;
}
#endif

std::vector<User> ListUsers() {
  std::vector<User> users;
#ifdef __SWITCH__
  // accountInitialize pode já ter sido chamado; o serviço conta referências.
  if (R_FAILED(accountInitialize(AccountServiceType_Application))) return users;

  AccountUid uids[8] = {};
  std::int32_t total = 0;
  if (R_SUCCEEDED(accountListAllUsers(uids, 8, &total))) {
    for (std::int32_t i = 0; i < total; ++i) {
      User u;
      u.id_lo = uids[i].uid[0];
      u.id_hi = uids[i].uid[1];

      AccountProfile profile;
      if (R_SUCCEEDED(accountGetProfile(&profile, uids[i]))) {
        AccountProfileBase base = {};
        AccountUserData data = {};
        if (R_SUCCEEDED(accountProfileGet(&profile, &data, &base))) {
          u.name = base.nickname;
        }
        accountProfileClose(&profile);
      }
      if (u.name.empty()) u.name = "Usuário";
      users.push_back(std::move(u));
    }
  }
  accountExit();
#else
  // Cada pasta em users/ e um usuario (spec 039). O AccountUid real tem 128
  // bits; aqui o id vem do nome, so para os dois lados nao se confundirem.
  const std::string root = SimRoot();
  if (!root.empty()) {
    if (DIR* d = opendir((root + "/users").c_str())) {
      while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        User u;
        u.name = name;
        u.id_lo = std::hash<std::string>{}(name);
        u.id_hi = 0;
        users.push_back(std::move(u));
      }
      closedir(d);
    }
  }
#endif
  return users;
}

std::vector<SaveEntry> ListFileSaves() {
  std::vector<SaveEntry> saves;
#ifdef __SWITCH__
  for (const char* dir : kSaveDirs) {
    DIR* d = opendir(dir);
    if (!d) continue;

    while (dirent* e = readdir(d)) {
      const std::string name = e->d_name;
      if (!HasSaveExtension(name)) continue;

      SaveEntry entry;
      entry.origin = SaveOrigin::kFile;
      entry.path = std::string(dir) + name;

      // Nome sem extensão, que é o que o jogador reconhece.
      const auto dot = name.find_last_of('.');
      entry.title = dot == std::string::npos ? name : name.substr(0, dot);

      // Arquivo de save de GBA: o parser gen3 tenta abrir. Se o conteúdo não
      // for gen3, a falha aparece ao abrir — aqui só o tamanho é conhecido.
      entry.supported = true;
      saves.push_back(std::move(entry));
    }
    closedir(d);
  }

  std::sort(saves.begin(), saves.end(),
            [](const SaveEntry& a, const SaveEntry& b) {
              return a.title < b.title;
            });
#endif  // __SWITCH__
  return saves;
}

std::vector<SaveEntry> ListInstalledSaves(const User& user) {
  std::vector<SaveEntry> saves;
#ifdef __SWITCH__
  FsSaveDataInfoReader reader;
  // Pode falhar por falta de permissão — em applet mode, provavelmente falha.
  // Nesse caso a lista volta vazia e a tela simplesmente não mostra a seção.
  if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) {
    return saves;
  }

  FsSaveDataInfo info = {};
  std::int64_t read = 0;
  while (R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, &info, 1, &read)) &&
         read > 0) {
    if (info.save_data_type != FsSaveDataType_Account) continue;
    if (info.uid.uid[0] != user.id_lo || info.uid.uid[1] != user.id_hi) {
      continue;
    }

    // Filtro por ApplicationId: identidade estavel do jogo. Montar o save
    // data de todos os 225 titulos para "adivinhar" pelo conteudo seria lento
    // e frageis — o id decide.
    const SwitchTitle* title = FindTitle(info.application_id);
    if (!title) continue;

    SaveEntry entry;
    entry.origin = SaveOrigin::kInstalled;
    entry.app_id = info.application_id;
    entry.title = title->name;

    // O icone do console e um extra: se o NACP nao responder, a arte local
    // (romfs/ui/games) cobre pelo GameSlug.
    const ControlInfo info_ctrl = QueryControl(info.application_id);
    if (!info_ctrl.name.empty()) entry.title = info_ctrl.name;
    entry.icon_path = info_ctrl.icon_path;

    // Jogos NSO de GBA guardam um .sav gen3 cru; gen8/gen9 guardam "main"
    // com SwishCrypto. Quem valida de verdade e o parser na abertura.
    entry.inner_file = FindGbaSave(info.application_id, info.uid);
    if (entry.inner_file.empty()) {
      entry.inner_file = FindMainSave(info.application_id, info.uid);
    }
    entry.supported = !entry.inner_file.empty();

    LogDiscovery("%016llX \"%s\" inner=\"%s\" suportado=%d",
                 (unsigned long long)info.application_id, entry.title.c_str(),
                 entry.inner_file.c_str(), entry.supported ? 1 : 0);
    LogDiscovery("  -> inner=\"%s\" suportado=%d", entry.inner_file.c_str(),
                 entry.supported ? 1 : 0);
    saves.push_back(std::move(entry));
  }
  fsSaveDataInfoReaderClose(&reader);
#else
  // No PC, a "montagem" e um diretorio na raiz do simulador (spec 039). O
  // resto — achar o titulo pelo id, escolher o arquivo interno — e o MESMO
  // codigo que roda no console, e e isso que faz o teste no PC dizer algo.
  //
  // O save e POR USUARIO: no console,  filtra; aqui, o nome do
  // usuario e um nivel da hierarquia. Conferido no Switch do dono, onde
  // Let's Go tem saves separados de "Pedro" e "Amaral" (spec 041).
  const std::string root = SimRoot();
  if (!root.empty()) {
    const std::string saves_dir = root + "/saves";
    if (DIR* d = opendir(saves_dir.c_str())) {
      while (dirent* e = readdir(d)) {
        std::uint64_t app_id = 0;
        if (!sim::ParseAppId(e->d_name, &app_id)) continue;

        const SwitchTitle* title = FindTitle(app_id);
        if (!title) continue;  // id valido mas nao e jogo Pokemon

        // <raiz>/saves/<AppId>/<usuario>/ — sem o nivel do usuario, o app
        // mostraria o save de um perfil ao abrir outro.
        const std::string dir =
            saves_dir + "/" + e->d_name + "/" + user.name;
        struct stat st {};
        if (stat(dir.c_str(), &st) != 0) continue;  // este usuario nao joga

        SaveEntry entry;
        entry.origin = SaveOrigin::kInstalled;
        entry.app_id = app_id;
        entry.title = title->name;
        entry.inner_file = FindInnerSave(dir);

        // Pasta sem save nao vira cartao na tela. No console isso nao acontece
        // porque o fsSaveDataInfoReader so devolve jogos que TEM save data —
        // pasta vazia e artefato do simulador, que cria as 28 de uma vez.
        if (entry.inner_file.empty()) continue;

        entry.supported = true;
        saves.push_back(std::move(entry));
      }
      closedir(d);
    }
  }
#endif

  std::sort(saves.begin(), saves.end(),
            [](const SaveEntry& a, const SaveEntry& b) {
              return a.title < b.title;
            });
  return saves;
}

std::vector<std::uint8_t> ReadInstalledSave(const SaveEntry& entry,
                                            const User& user) {
  std::vector<std::uint8_t> data;
#ifdef __SWITCH__
  if (entry.inner_file.empty()) return data;

  AccountUid uid = {};
  uid.uid[0] = user.id_lo;
  uid.uid[1] = user.id_hi;
  if (R_FAILED(fsdevMountSaveData("nbread", entry.app_id, uid))) return data;

  const std::string path = "nbread:/" + entry.inner_file;
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 0) {
      data.resize(static_cast<std::size_t>(size));
      if (std::fread(data.data(), 1, data.size(), f) != data.size()) {
        data.clear();
      }
    }
    std::fclose(f);
  }
  fsdevUnmountDevice("nbread");
#else
  // O arquivo interno vive dentro da pasta do jogo E do usuario (spec 041).
  if (entry.inner_file.empty()) return data;
  const std::string root = SimRoot();
  if (root.empty()) return data;

  const std::string path = root + "/saves/" + sim::AppIdToDir(entry.app_id) +
                           "/" + user.name + "/" + entry.inner_file;
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 0) {
      data.resize(static_cast<std::size_t>(size));
      if (std::fread(data.data(), 1, data.size(), f) != data.size()) {
        data.clear();
      }
    }
    std::fclose(f);
  }
#endif
  return data;
}

bool WriteInstalledSave(const SaveEntry& entry, const User& user,
                        const std::vector<std::uint8_t>& data) {
  if (entry.inner_file.empty() || data.empty()) return false;

#ifdef __SWITCH__
  AccountUid uid = {};
  uid.uid[0] = user.id_lo;
  uid.uid[1] = user.id_hi;
  // "nbwrite", nao o "nbread" da leitura: montar com nome proprio deixa claro
  // no codigo e no log qual caminho abriu o save data para escrita.
  if (R_FAILED(fsdevMountSaveData("nbwrite", entry.app_id, uid))) return false;

  bool ok = false;
  const std::string path = "nbwrite:/" + entry.inner_file;
  if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
    ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    // fclose ANTES do commit: o commit publica o que ja foi descarregado, e um
    // buffer ainda aberto nao entraria nele.
    if (std::fclose(f) != 0) ok = false;
  }

  // Sem commit o save data nao persiste — a escrita fica num overlay que o
  // desmonte descarta. E o passo que diferencia gravar de "parecer que gravou".
  if (ok && R_FAILED(fsdevCommitDevice("nbwrite"))) ok = false;

  fsdevUnmountDevice("nbwrite");
  return ok;
#else
  const std::string root = SimRoot();
  if (root.empty()) return false;

  const std::string path = root + "/saves/" + sim::AppIdToDir(entry.app_id) +
                           "/" + user.name + "/" + entry.inner_file;
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool wrote = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  return std::fclose(f) == 0 && wrote;
#endif
}

}  // namespace nestbox
