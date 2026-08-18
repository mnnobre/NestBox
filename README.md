# NestBox

<p align="center">
  <b>Um "Pokémon HOME" que roda dentro do próprio Nintendo Switch</b><br/>
  Homebrew, sem PC, sem nuvem — as caixas na tela do console.
</p>

<p align="center">
  <a href="../../releases/latest"><img src="https://img.shields.io/github/v/release/mnnobre/NestBox?label=vers%C3%A3o&color=orange" alt="Última release" /></a>
  <img src="https://img.shields.io/badge/plataforma-Nintendo%20Switch%20(homebrew)-e60012" alt="Plataforma" />
  <img src="https://img.shields.io/badge/status-em%20desenvolvimento%20ativo-brightgreen" alt="Status" />
</p>

---

## O Charmander que você pegou no FireRed pode viajar

Aquele Charmander que você escolheu no seu FireRed original — o mesmo, com o
mesmo nickname, o mesmo OT, os mesmos IVs — sai da caixa, atravessa Let's Go,
Scarlet, Legends Arceus, qualquer jogo compatível, e **volta pro FireRed**
depois. Sem passar pelo PC, sem sair do Switch: as caixas ficam na tela do
console, do jeito que sempre deveriam ter ficado.

## Por que existe

As ferramentas atuais forçam uma escolha ruim:

- **PKHeX, OpenHome, PKVault** rodam no PC. Exigem dumpar o save, levar pro
  computador, mexer e devolver — quebra o fluxo de estar jogando no console.
- **PKSM** roda no Switch e é o mais próximo, mas com interface de homebrew
  antigo.

O buraco é um app **dentro do console**, com visual que não pareça homebrew —
que se pareça com o Pokémon HOME oficial.

## Telas

|  |  |
| --- | --- |
| ![Menu principal](screenshots/menu-principal.png) | ![Seleção de save](screenshots/selecao-save.png) |
| Menu no estilo do HOME, com NestBox e Pokédex | Escolha do save por capas, como o app oficial |
| ![Caixas com bloqueio](screenshots/caixas-bloqueio.png) | ![Pegar Pokémon](screenshots/pegar-pokemon.png) |
| Dois painéis lado a lado; ícone vermelho bloqueia incompatíveis | Pegar e mover, com feedback visual |
| ![Enciclopédia](screenshots/enciclopedia.png) | |
| Pokédex — 1025 espécies registráveis | |

## O que já funciona

- **Leitura de saves de gen 3 a gen 9** — FireRed/LeafGreen, Let's Go,
  Sword/Shield, BDSP, Legends Arceus, Scarlet/Violet e Legends Z-A, tanto de
  emulador quanto de jogos instalados no console.
- **Caixas com sprites**, navegação por controle, dois painéis lado a lado
  como o HOME.
- **NestBox** — o banco central do app: persiste no cartão SD, caixas
  renomeáveis, Pokédex global própria.
- **Transferência real** — mover Pokémon entre o save do jogo e o NestBox.
  Sai de um lado, entra no outro. Escrita no save com backup automático antes.
- **Bloqueio de incompatibilidade** — como o HOME, um Pokémon só se move para
  um jogo se a espécie existir naquele título.
- **Tela de Summary** no formato do HOME oficial, barra de status com dados
  modernos, commit explícito sem janela de perda.
- **Backup e restauração de saves** antes de qualquer escrita.

## Roadmap — a caminho do Pokémon HOME completo

### Feito

- [x] Parser multi-geração (gen 3 → gen 9 / Legends Z-A)
- [x] Interface de caixas, detalhes, enciclopédia e modo lista
- [x] NestBox: banco central persistente, separado por save de origem
- [x] Movimentação real entre save e banco (não cópia)
- [x] Backup automático antes de qualquer escrita
- [x] Bloqueio por incompatibilidade de espécie/jogo
- [x] Commit atômico no fechamento, sem gravação a cada movimento
- [x] Tela de Summary no formato do HOME
- [x] Três modos de cursor com ZL/ZR (vermelho/azul/verde)
- [x] Aviso amarelo de golpe incompatível
- [x] Memória automática de moveset por jogo (restaura o golpe ao voltar a
      um jogo onde o Pokémon já esteve)

### Não replicado ainda

- [ ] Troca manual de golpes ao transferir (escolher entre golpes já
      aprendidos — o app hoje só restaura automaticamente pela memória)
- [ ] Pokédex por jogo (hoje só a global)
- [ ] Judge (avaliação de IVs)

### Fora de escopo (por design)

- Trocas online / GTS / Wonder Box / Room Trade
- Legalidade de Pokémon gerados — o app move o que já existe, não fabrica
- Sincronização em nuvem — tudo vive no cartão SD do console

## Instalação

1. Baixe o `nestbox.nro` da [última release](../../releases/latest)
2. Copie para `sdmc:/switch/`
3. Abra pelo homebrew launcher

> **Abra em application mode**: no menu HOME, segure `R` e abra um jogo
> qualquer. Pelo álbum, o homebrew recebe só ~448 MB e não alcança os saves
> dos jogos instalados. O app avisa quando está no modo limitado.

## Controles

| Botão | Ação |
| --- | --- |
| D-pad | mover o cursor |
| `L` / `R` | trocar de caixa |
| `ZL` / `ZR` | alternar entre NestBox e save |
| `Y` | pegar / soltar Pokémon |
| `X` | enciclopédia |
| `R3` | modo lista |
| `B` | voltar / sair |

## Sobre este repositório

Este repositório é um **espelho de leitura do código-fonte**, publicado a cada
release para quem quiser inspecionar como o app funciona — em especial como ele
lê e escreve os saves, já que é o tipo de coisa que ninguém deveria rodar no
seu save sem poder auditar.

**Ele não compila sozinho.** Os assets de jogo (sprites, arte da interface)
não são distribuídos aqui: são propriedade da The Pokémon Company e existem
apenas embutidos no `.nro` da release. As screenshots acima são do próprio
app em uso, mantidas à parte. O desenvolvimento acontece em um repositório
privado.

| Diretório | Conteúdo |
| --- | --- |
| `source/core/` | Parsers e regras — C++ puro, sem dependência de plataforma |
| `src/ui/` | Interface (borealis) |
| `src/cli/` | Ferramenta de linha de comando para inspecionar saves |
| `tests/` | Suíte de testes do core |

O core não depende de libnx: compila no PC e no Switch sem alteração. É o que
permite testar os parsers sem hardware.

## Créditos

O formato dos saves e dos Pokémon foi documentado a partir da pesquisa do
[PKHeX](https://github.com/kwsch/PKHeX) e do
[Project Pokémon](https://projectpokemon.org/). O PKHeX foi usado como
**referência de leitura**: os formatos foram lidos e reimplementados do zero
em C++. Nenhum código do PKHeX é copiado ou linkado.

Ver [CREDITS.md](CREDITS.md) para o detalhamento.

## Aviso

Ferramenta para uso pessoal com seus próprios jogos e saves. **Faça backup
dos seus saves.** Este projeto não é afiliado à Nintendo, Game Freak ou The
Pokémon Company.
