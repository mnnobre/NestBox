// Descoberta de usuários e saves no console.
//
// Duas fontes bem diferentes, deliberadamente separadas:
//
//   - **Arquivo no SD**: saves de emulador ou dumps. Não pertencem a nenhuma
//     conta do Switch, e o parser gen3 os lê hoje.
//   - **Save data de jogo instalado**: pertence a um `AccountUid`, vem por
//     `fsOpenSaveDataInfoReader`. Exige permissão que um NRO pode não ter.
//
// Fora do Switch tudo devolve lista vazia — o PC não tem contas nem save data.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nestbox {

struct User {
  std::uint64_t id_lo = 0, id_hi = 0;  // AccountUid, sem depender de libnx aqui
  std::string name;
};

// De onde o save veio. Muda o que dá para fazer com ele.
enum class SaveOrigin {
  kFile,        // arquivo no SD: leitura e escrita diretas
  kInstalled,   // save data de jogo instalado
  kNestBox,     // a box exclusiva do NestBox — não é um save, é o cofre do app
};

struct SaveEntry {
  SaveOrigin origin = SaveOrigin::kFile;
  std::string path;      // caminho, quando vem de arquivo
  std::string title;     // nome exibido
  std::uint64_t app_id = 0;  // ApplicationId, quando é jogo instalado
  std::string icon_path;     // ícone do jogo, extraído do console (JPEG)

  // Nome do arquivo dentro do save data do jogo, quando ele é legível
  // (jogos NSO de GBA guardam um .sav gen3 cru, ex.: "FireRed_e.sav").
  std::string inner_file;

  // O parser do projeto é gen3. Um save de Switch é listado, mas não abre —
  // marcar isso é melhor que oferecer um cartão que falha ao ser tocado.
  bool supported = false;
};

// Usuários do console. Vazio se a API não responder.
std::vector<User> ListUsers();

// Todos os saves em arquivo no SD. Substitui a busca que parava no primeiro.
std::vector<SaveEntry> ListFileSaves();

// Save data dos jogos instalados do usuário. Vazio se a permissão faltar —
// nesse caso a seção some da tela e os saves em arquivo seguem funcionando.
std::vector<SaveEntry> ListInstalledSaves(const User& user);

// Lê o .sav de GBA de dentro do save data de um jogo instalado. Vazio se o
// jogo não tiver um, ou se a montagem falhar.
std::vector<std::uint8_t> ReadInstalledSave(const SaveEntry& entry,
                                            const User& user);

// Grava de volta no save data de um jogo instalado (spec 086, TD-01).
//
// No Switch monta o save data para ESCRITA e faz commit; no PC escreve o
// arquivo dentro da raiz do simulador. Devolve false sem ter tocado o destino
// se a montagem falhar ou a escrita não fechar.
//
// SÓ o save data do jogo que está aberto é montado, e só a região das caixas
// chega aqui — quem monta o buffer é o commit da tela, que exige backup antes.
bool WriteInstalledSave(const SaveEntry& entry, const User& user,
                        const std::vector<std::uint8_t>& data);

}  // namespace nestbox
