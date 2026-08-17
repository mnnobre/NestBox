# NestBox

Um gerenciador de Pokémon que roda **dentro do Nintendo Switch**, como
homebrew. Lê os saves dos seus jogos e mostra as caixas com sprites, detalhes
e enciclopédia — sem precisar de PC.

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
| `Y` | detalhes do Pokémon |
| `X` | enciclopédia |
| `R3` | modo lista |
| `B` | voltar / sair |

## Sobre este repositório

Este repositório é um **espelho de leitura do código-fonte**, publicado a cada
release para quem quiser inspecionar como o app funciona — em especial como ele
lê e escreve os saves, já que é o tipo de coisa que ninguém deveria rodar no
seu save sem poder auditar.

**Ele não compila sozinho.** Os assets (sprites e arte da interface) não são
distribuídos aqui: são propriedade da The Pokémon Company e existem apenas
embutidos no `.nro` da release. O desenvolvimento acontece em um repositório
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

A interface usa [borealis](https://github.com/xfangfang/borealis) (Apache 2.0).

## Licença

Código sob [Apache 2.0](LICENSE).

Sprites, nomes e imagens de Pokémon são propriedade da The Pokémon Company,
Nintendo e Game Freak, e não estão cobertos por essa licença.

## Aviso

Ferramenta para uso pessoal com seus próprios jogos e saves. **Faça backup dos
seus saves.** Este projeto não é afiliado à Nintendo, Game Freak ou The Pokémon
Company.
