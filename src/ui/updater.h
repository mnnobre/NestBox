// Auto-update pela API de releases do GitHub.
//
// Só faz sentido no Switch: no PC o app é recompilado, não atualizado. Fora do
// Switch todas as funções devolvem "sem atualização" sem tocar na rede.

#pragma once

#include <functional>
#include <string>

namespace nestbox {

struct UpdateInfo {
  bool available = false;
  std::string latest_version;  // sem o "v" da tag
  std::string download_url;
};

// Consulta a release mais recente. Falha de rede devolve available=false —
// nunca impede o app de abrir.
UpdateInfo CheckForUpdate();

// Baixa para um arquivo temporário ao lado do .nro. O progresso vai de 0 a 1.
// Não substitui o .nro em uso: o hbloader o mantém aberto, então a troca é
// feita na próxima abertura por ApplyPendingUpdate().
//
// Bloqueia até terminar — use as funções abaixo se a tela precisa desenhar
// durante o download.
bool DownloadUpdate(const std::string& url,
                    const std::function<void(float)>& on_progress);

// Download fatiado, para a barra de progresso andar de verdade: o curl roda
// em modo multi e devolve o controle a cada fatia, entre frames.
//
//   BeginDownload(url) -> PumpDownload() a cada frame -> EndDownload()
//
// PumpDownload devolve false quando termina (com sucesso ou falha); o
// resultado sai em EndDownload.
bool BeginDownload(const std::string& url);
bool PumpDownload(float* progress);
bool EndDownload();

// Registra o caminho do .nro em execução, vindo de argv[0]. Sem isto o updater
// assume "sdmc:/switch/pokehome.nro", e um app instalado com outro nome ou em
// outra pasta seria substituído no lugar errado.
void SetRunningNroPath(const char* argv0);

// Se houver um download concluído da execução anterior, substitui o .nro.
// Chamado antes de qualquer UI subir. Devolve true se algo foi aplicado.
bool ApplyPendingUpdate();

// Reinicia o app na versão recém-baixada, sem o usuário precisar fechar e
// abrir. Só funciona sob um loader que suporte encadeamento (hbmenu, Sphaira);
// devolve false quando não há suporte, e aí resta pedir para reabrir.
bool RestartIntoUpdate();

// Versão embutida no build.
inline const char* CurrentVersion() {
#ifdef NESTBOX_VERSION
  return NESTBOX_VERSION;
#else
  return "dev";
#endif
}

}  // namespace nestbox
