cmake_minimum_required(VERSION 3.20)
# C e necessario alem de CXX: borealis traz glad, nanovg e glfw em C.
project(pokehome C CXX)

# Versao do app. Deve casar com a tag da release no GitHub — e o que o
# auto-update compara. Ao publicar uma release nova, subir aqui tambem.
set(NESTBOX_VERSION "0.9.8")
# Repo publico so de releases — o codigo vive no repo privado mnnobre/NestBox.
set(NESTBOX_REPO "mnnobre/NestBox")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core: parsing puro, sem dependencia de plataforma. Compila em PC e no Switch.
add_library(pokehome_core STATIC
  source/core/gen3_save.cpp
  source/core/za_save.cpp
  source/core/pkm_crypto.cpp
  source/core/sha256.cpp
  source/core/pkm_model.cpp
  source/core/pk8.cpp
  source/core/pk9.cpp
source/core/pa8.cpp
  source/core/pb7.cpp
  source/core/pb8.cpp
  source/core/swish_crypto.cpp
  source/core/save_sandbox.cpp
  source/core/save_writer.cpp
  source/core/bag_writer.cpp
  source/core/pkm_convert.cpp
  source/core/transfer_rules.cpp
  source/core/moveset_memory.cpp
  source/core/transfer.cpp
  source/core/legality.cpp
  source/core/gen3_transfer.cpp
  source/core/modern_box_view.cpp
  source/core/nlog.cpp
)
target_include_directories(pokehome_core PUBLIC source/core)

# Os avisos valem para TUDO que o projeto compila, nao so para o core.
#
# Ate a spec 039 estes flags eram PRIVATE do pokehome_core, entao os alvos de
# teste e a UI compilavam sem eles — varios evidence-logs afirmavam "sem
# warning com -Wall -Wextra" sobre um gate que nao cobria o codigo em questao.
# Conferido ao corrigir: 0 warnings, entao a afirmacao era verdadeira por
# sorte, nao por verificacao.
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

# CLI de teste — so no PC.
add_executable(pokehome-cli src/cli/main.cpp)
target_link_libraries(pokehome-cli PRIVATE pokehome_core)

# Testes
enable_testing()
# Cifra dos Pokemon modernos (spec 054). As fixtures sao os arquivos do
# OpenHome, que saem do PkHeX — a fonte de verdade da descoberta.
add_executable(test_pkm_crypto tests/test_pkm_crypto.cpp)
target_link_libraries(test_pkm_crypto PRIVATE pokehome_core)
target_compile_definitions(test_pkm_crypto PRIVATE
  PKM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/OpenHome/test-files/pkm-files/")
add_test(NAME pkm_crypto COMMAND test_pkm_crypto)

# SwishCrypto dos saves SwSh/PLA/SV (spec 055), validado nos saves REAIS
# do simulador — so leitura, roundtrip em memoria.
add_executable(test_swish_crypto tests/test_swish_crypto.cpp)
target_link_libraries(test_swish_crypto PRIVATE pokehome_core)
target_compile_definitions(test_swish_crypto PRIVATE
  SIM_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/build/switch-sim/saves/")
add_test(NAME swish_crypto COMMAND test_swish_crypto)

# Parser PK8 (spec 058), validado campo a campo contra os JSONs do PkHeX.
add_executable(test_pk8 tests/test_pk8.cpp)
target_link_libraries(test_pk8 PRIVATE pokehome_core)
target_compile_definitions(test_pk8 PRIVATE
  PKM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/OpenHome/test-files/pkm-files/"
  PKHEX_JSON="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pkhex/")
add_test(NAME pk8 COMMAND test_pk8)

# Parser PK9 (spec 059) — SV e o formato do Z-A.
add_executable(test_pk9 tests/test_pk9.cpp)
target_link_libraries(test_pk9 PRIVATE pokehome_core)
target_compile_definitions(test_pk9 PRIVATE
  PKM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/OpenHome/test-files/pkm-files/"
  PKHEX_JSON="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pkhex/")
add_test(NAME pk9 COMMAND test_pk9)

# Parser PB8 (spec 061) — BDSP. As fixtures sao Pokemon reais extraidos do
# save de BDSP do dono; o oraculo continua sendo o JSON do PkHeX.
add_executable(test_pb8 tests/test_pb8.cpp)
target_link_libraries(test_pb8 PRIVATE pokehome_core)
target_compile_definitions(test_pb8 PRIVATE
  PKM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/OpenHome/test-files/pkm-files/"
  PKHEX_JSON="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pkhex/")
add_test(NAME pb8 COMMAND test_pb8)

# Parser PA8 (spec 060) — Legends: Arceus, bloco de cifra 0x58 (360/376).
add_executable(test_pa8 tests/test_pa8.cpp)
target_link_libraries(test_pa8 PRIVATE pokehome_core)
target_compile_definitions(test_pa8 PRIVATE
  PKM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/OpenHome/test-files/pkm-files/"
  PKHEX_JSON="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pkhex/")
add_test(NAME pa8 COMMAND test_pa8)

# Parser PB7 (spec 062) — Let's Go. As fixtures saem do save REAL do dono
# (nao ha .pb7 no OpenHome); e o primeiro teste da cifra de bloco 0x38.
add_executable(test_pb7 tests/test_pb7.cpp)
target_link_libraries(test_pb7 PRIVATE pokehome_core)
target_compile_definitions(test_pb7 PRIVATE
  PKM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/OpenHome/test-files/pkm-files/"
  PKHEX_JSON="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pkhex/")
add_test(NAME pb7 COMMAND test_pb7)

# Cifra de PARTY dos 4 formatos de bloco grande (spec 066). Fecha a pendencia
# de 054/062: as fixtures de PK8/PK9/PA8/PB8 estao gravadas DECIFRADAS, entao
# o parser nunca chamava Decrypt e a segunda passada da cifra (a regiao de
# party) nunca era exercitada. As fixtures `party-*` saem dos saves REAIS e
# vem cifradas, com o `.dec` do PkHeX ao lado como oraculo externo.
add_executable(test_party tests/test_party.cpp)
target_link_libraries(test_party PRIVATE pokehome_core)
target_compile_definitions(test_party PRIVATE
  PKM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/OpenHome/test-files/pkm-files/")
add_test(NAME party COMMAND test_party)

# Fixtures SINTETICAS (spec 067). As fixtures reais saem de saves de inicio de
# jogo: HT, memorias, ribbons, PP-ups, relearn, egg date e home tracker estao
# ZERADOS em todas, entao o teste do formato compara 0 contra 0 e passa lendo
# de qualquer offset zerado (provado na 066). Estas nascem do PkHeX com cada
# campo vizinho recebendo um valor DIFERENTE.
add_executable(test_synth tests/test_synth.cpp)
target_link_libraries(test_synth PRIVATE pokehome_core)
target_compile_definitions(test_synth PRIVATE
  SYNTH_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/synth/")
add_test(NAME synth COMMAND test_synth)

# Escrita de PKM (spec 063, G01) — o inverso dos cinco parsers. O criterio e o
# roundtrip byte-identico nas 141 fixtures: se regravar sem alterar ja muda
# bytes, qualquer transferencia futura estaria corrompendo dado.
add_executable(test_pkm_write tests/test_pkm_write.cpp)
target_link_libraries(test_pkm_write PRIVATE pokehome_core)
target_compile_definitions(test_pkm_write PRIVATE
  PKM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/OpenHome/test-files/pkm-files/"
  PKHEX_JSON="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pkhex/")
add_test(NAME pkm_write COMMAND test_pkm_write)

# Conversao de formato (spec 069, G06) — o nucleo tecnico da transferencia.
# O criterio: as 20 direcoes (5 formatos x 4 destinos) preservam os campos
# [HOME], com a especie passando pela conversao de indice do gen9. Comparar
# `species` cru entre formatos daria falso-verde em 4 das 5 origens.
add_executable(test_pkm_convert tests/test_pkm_convert.cpp)
target_link_libraries(test_pkm_convert PRIVATE pokehome_core)
target_compile_definitions(test_pkm_convert PRIVATE
  PKM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/OpenHome/test-files/pkm-files/"
  SYNTH_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/synth/")
add_test(NAME pkm_convert COMMAND test_pkm_convert)

# Produtor dos PKM convertidos para a validacao EXTERNA (spec 069). Nao e um
# teste do ctest: gera o arquivo que `tools/pkhex-pkm dump` submete ao PkHeX.
add_executable(convert_dump tests/convert_dump.cpp)
target_link_libraries(convert_dump PRIVATE pokehome_core)

# HOME Tracker e memoria de moveset (spec 071 — G10/G11/G12). O reset por
# nivel NAO usa numero escrito a mao: compara as 14.032 combinacoes de
# tests/fixtures/pkhex/moveset-reset.txt, geradas por tools/pkhex-moveset a
# partir de Learnset.SetEncounterMoves do PKHeX.Core. E o que fecha a pendencia
# do desempate deixada em aberto pela spec 065.
add_executable(test_tracker_moveset tests/test_tracker_moveset.cpp)
target_link_libraries(test_tracker_moveset PRIVATE pokehome_core)
target_compile_definitions(test_tracker_moveset PRIVATE
  PKHEX_JSON="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pkhex/")
add_test(NAME tracker_moveset COMMAND test_tracker_moveset)

add_executable(test_gen3 tests/test_gen3.cpp)
target_link_libraries(test_gen3 PRIVATE pokehome_core)
add_test(NAME gen3 COMMAND test_gen3)

# Subida gen3 -> moderno (spec 109).
add_executable(test_gen3_transfer tests/test_gen3_transfer.cpp)
target_link_libraries(test_gen3_transfer PRIVATE pokehome_core)
add_test(NAME gen3_transfer COMMAND test_gen3_transfer)

# Matriz de validacao pre-PROD (spec 114): normal, nao-convencional e QA.
add_executable(test_qa_matrix tests/test_qa_matrix.cpp)
target_link_libraries(test_qa_matrix PRIVATE pokehome_core)
target_compile_definitions(test_qa_matrix PRIVATE
  CLEAN_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/tests/saves-limpos/"
  SIM_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/build/switch-sim/saves/")
add_test(NAME qa_matrix COMMAND test_qa_matrix)

# Movimentacao entre caixas (spec 019). Header-only: linka o core so pelos
# tipos de gen3_save.h.
add_executable(test_box_move tests/test_box_move.cpp)
target_link_libraries(test_box_move PRIVATE pokehome_core)
add_test(NAME box_move COMMAND test_box_move)

# Ordenacao da lista (spec 024).
add_executable(test_box_sort tests/test_box_sort.cpp)
target_link_libraries(test_box_sort PRIVATE pokehome_core)
add_test(NAME box_sort COMMAND test_box_sort)

# Contagem de Pokedex sobre varias fontes (spec 026).
add_executable(test_dex_count tests/test_dex_count.cpp)
target_link_libraries(test_dex_count PRIVATE pokehome_core)
add_test(NAME dex_count COMMAND test_dex_count)

# Formato de arquivo do NestBox (spec 027).
add_executable(test_nestbox_file tests/test_nestbox_file.cpp)
target_link_libraries(test_nestbox_file PRIVATE pokehome_core)
add_test(NAME nestbox_file COMMAND test_nestbox_file)

# Gravacao atomica por dupla de arquivos (spec 091).
add_executable(test_nestbox_ab tests/test_nestbox_ab.cpp)
target_link_libraries(test_nestbox_ab PRIVATE pokehome_core)
add_test(NAME nestbox_ab COMMAND test_nestbox_ab)

# Compatibilidade por jogo (spec 034).
add_executable(test_game_species tests/test_game_species.cpp)
target_link_libraries(test_game_species PRIVATE pokehome_core)
add_test(NAME game_species COMMAND test_game_species)

# Tabelas de dado extraidas do PkHeX (spec 065): conversao de especie do
# gen9, learnset por nivel de PLA/BDSP e especies por jogo. A conversao e
# provada contra as 16 fixtures do PK9, por isso precisa dos dois caminhos.
add_executable(test_tabelas tests/test_tabelas.cpp)
target_link_libraries(test_tabelas PRIVATE pokehome_core)
target_compile_definitions(test_tabelas PRIVATE
  PKM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/OpenHome/test-files/pkm-files/"
  PKHEX_JSON="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pkhex/")
add_test(NAME tabelas COMMAND test_tabelas)

# Characteristic, tipos e memo da tela de summary (spec 103).
add_executable(test_summary_facts tests/test_summary_facts.cpp)
target_link_libraries(test_summary_facts PRIVATE pokehome_core)
add_test(NAME summary_facts COMMAND test_summary_facts)

# Compatibilidade de golpes (spec 038) — o aviso amarelo, que NAO bloqueia.
add_executable(test_game_moves tests/test_game_moves.cpp)
target_link_libraries(test_game_moves PRIVATE pokehome_core)
add_test(NAME game_moves COMMAND test_game_moves)

# Descoberta de save data: regras compartilhadas Switch/simulador (spec 039).
add_executable(test_switch_sim tests/test_switch_sim.cpp)
target_link_libraries(test_switch_sim PRIVATE pokehome_core)
add_test(NAME switch_sim COMMAND test_switch_sim)

# Backup de save (spec 032) — rede de seguranca antes da escrita.
add_executable(test_save_backup tests/test_save_backup.cpp)
target_link_libraries(test_save_backup PRIVATE pokehome_core)
add_test(NAME save_backup COMMAND test_save_backup)

# Sandbox de save (spec 064, G04) — a copia temporaria que toda escrita futura
# vai usar. O teste hasheia o save REAL antes e depois: se o original mudar, o
# alvo fica vermelho.
add_executable(test_save_sandbox tests/test_save_sandbox.cpp)
target_link_libraries(test_save_sandbox PRIVATE pokehome_core)
target_compile_definitions(test_save_sandbox PRIVATE
  SIM_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/build/switch-sim/saves/")
add_test(NAME save_sandbox COMMAND test_save_sandbox)

# Restauracao de backup (spec 037) — data legivel e ordem da lista.
add_executable(test_restore tests/test_restore.cpp)
target_link_libraries(test_restore PRIVATE pokehome_core)
add_test(NAME restore COMMAND test_restore)

# Save writer (spec 068, G02+G03) — o PORTAO DE SEGURANCA do epico de escrita.
# O criterio central e o roundtrip byte-identico sobre os saves REAIS: abrir e
# regravar sem alterar nada tem de produzir arquivo identico. Se isso falhar,
# nenhuma transferencia pode acontecer. Opera sempre em copia (SaveSandbox).
add_executable(test_save_writer tests/test_save_writer.cpp)
target_link_libraries(test_save_writer PRIVATE pokehome_core)
target_compile_definitions(test_save_writer PRIVATE
  SIM_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/build/switch-sim/saves/")
add_test(NAME save_writer COMMAND test_save_writer)

# Escrita em save moderno pela sessao (spec 086): o payload pkm::Pokemon viaja
# intacto pela MoveSession, nada toca o disco antes do commit, e o roundtrip
# mover-salvar-reabrir preserva os bytes crus. Somente leitura do save real —
# todo o trabalho e em memoria.
add_executable(test_modern_write tests/test_modern_write.cpp)
target_link_libraries(test_modern_write PRIVATE pokehome_core)
target_compile_definitions(test_modern_write PRIVATE
  SIM_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/build/switch-sim/saves/")
add_test(NAME modern_write COMMAND test_modern_write)

# Regra do item segurado (spec 072, G08): ao depositar, o item volta para a
# Bag do save de ORIGEM. Cobre SwSh e LGPE — os dois formatos cujo slot de bag
# e auto-descritivo (id + quantidade no proprio u32).
add_executable(test_item_rule tests/test_item_rule.cpp)
target_link_libraries(test_item_rule PRIVATE pokehome_core)
target_compile_definitions(test_item_rule PRIVATE
  SIM_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/build/switch-sim/saves/")
add_test(NAME item_rule COMMAND test_item_rule)

# Regras de transferencia (spec 070): quem pode entrar em cada jogo e o que
# muda ao entrar. Puro — nao le save nenhum.
add_executable(test_transfer_rules tests/test_transfer_rules.cpp)
target_link_libraries(test_transfer_rules PRIVATE pokehome_core)
add_test(NAME transfer_rules COMMAND test_transfer_rules)

# Transferencia end-to-end (spec 075, G13): junta 063..072 com commit atomico.
# Opera nos saves REAIS do dono, sempre atraves do SaveSandbox.
add_executable(test_transfer tests/test_transfer.cpp)
target_link_libraries(test_transfer PRIVATE pokehome_core)
target_compile_definitions(test_transfer PRIVATE
  SIM_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/build/switch-sim/saves/")
add_test(NAME transfer COMMAND test_transfer)

# Bateria de cenarios (spec 078, G14): a matriz par x cenario, sobre os saves
# LIMPOS da spec 077 (8 Pokemon 100% legais cada). Criterio ABSOLUTO de
# legalidade, e nao o DELTA da 075.
add_executable(test_bateria tests/test_bateria.cpp)
target_link_libraries(test_bateria PRIVATE pokehome_core)
target_compile_definitions(test_bateria PRIVATE
  CLEAN_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/tests/saves-limpos/"
  CLEAN_OUT="${CMAKE_CURRENT_BINARY_DIR}/bateria-out/")
add_test(NAME bateria COMMAND test_bateria)

# Verificador de legalidade (spec 079): coerencia interna, sem encounter DB.
# O criterio que mais importa e o FALSO POSITIVO — os 40 Pokemon de
# tests/saves-limpos sao 100% legais pelo PkHeX e nenhum pode ser reprovado.
# Os saves reais entram como corpus de deteccao, SOMENTE LEITURA.
add_executable(test_legality tests/test_legality.cpp)
target_link_libraries(test_legality PRIVATE pokehome_core)
target_compile_definitions(test_legality PRIVATE
  CLEAN_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/tests/saves-limpos/"
  SIM_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/build/switch-sim/saves/")
add_test(NAME legality COMMAND test_legality)

# Ciclo de vida do save (spec 081): backup -> salvar -> movimentar -> salvar de
# novo -> reabrir -> validar. Todo teste ate aqui faz UMA operacao e confere; as
# classes de bug que so aparecem no uso real (erro acumulado entre gravacoes,
# estado nao limpo entre ciclos, backup sobrescrito) exigem o ciclo inteiro.
# Opera nos saves LIMPOS da spec 077, sempre atraves do SaveSandbox.
add_executable(test_ciclo_vida tests/test_ciclo_vida.cpp)
target_link_libraries(test_ciclo_vida PRIVATE pokehome_core)
target_compile_definitions(test_ciclo_vida PRIVATE
  CLEAN_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/tests/saves-limpos/"
  CICLO_OUT="${CMAKE_CURRENT_BINARY_DIR}/ciclo-out/")
add_test(NAME ciclo_vida COMMAND test_ciclo_vida)

# Ponte motor -> TELA (spec 082): a conversao pkm::Pokemon -> g3::BoxPokemon.
# O oraculo e EXTERNO — os inventario.json gerados pelo PkHeX na spec 077 —,
# nunca a propria funcao testada (licao da spec 069). Somente leitura.
add_executable(test_ui_modern_source tests/test_ui_modern_source.cpp)
target_link_libraries(test_ui_modern_source PRIVATE pokehome_core)
target_compile_definitions(test_ui_modern_source PRIVATE
  CLEAN_SAVES="${CMAKE_CURRENT_SOURCE_DIR}/tests/saves-limpos/")
add_test(NAME ui_modern_source COMMAND test_ui_modern_source)

# Log de diagnostico (spec 083). O criterio que mais importa e o DESLIGAMENTO:
# o dono pediu tudo ligado enquanto testa, e o requisito de design e que exista
# um unico ponto de corte (NESTBOX_LOG_LEVEL) antes do release. O teste falha se
# alguem desligar e esquecer de reativar.
add_executable(test_nlog tests/test_nlog.cpp)
target_link_libraries(test_nlog PRIVATE pokehome_core)
add_test(NAME nlog COMMAND test_nlog)

# UI — opcional, exige o submodule do borealis.
# Desligue com -DPOKEHOME_UI=OFF para buildar so o core e os testes.
option(POKEHOME_UI "Build da interface grafica (borealis)" ON)

if(POKEHOME_UI AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/library/CMakeLists.txt)
  # Borealis exige um alvo de plataforma. No PC e DESKTOP; quando o projeto for
  # para o Switch, vira PLATFORM_SWITCH (spec futura).
  if(PLATFORM_SWITCH)
    # No Switch o borealis renderiza com deko3d (API nativa do console).
    # Sem isso ele cai no caminho do GLFW, que nao existe para aarch64.
    set(BOREALIS_USE_DEKO3D ON)

    # O nanovg/deko3d inclui <glm/...>, mas o borealis nao adiciona portlibs ao
    # include path — o toolchain do devkitPro so o poe no CMAKE_FIND_ROOT_PATH.
    include_directories(SYSTEM $ENV{DEVKITPRO}/portlibs/switch/include)
  else()
    set(PLATFORM_DESKTOP ON)
  endif()

  # O CMakeLists do borealis usa BRLS_UNITY_BUILD sem valor padrao; vazio
  # quebra o set_target_properties dele. OFF para builds incrementais rapidos.
  set(BRLS_UNITY_BUILD OFF)

  # Borealis monta os caminhos como BRLS_RESOURCES_DIR + "/resources/", e o
  # default e "." — relativo ao diretorio de trabalho, nao ao executavel. Sem
  # isso, rodar de fora de build/ abre a janela sem fonte nenhuma.
  set(BRLS_RESOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis)

  set(BRLS_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/resources/)
  add_subdirectory(lib/borealis/library)

  # switch_wrapper.c define userAppInit/userAppExit (romfsInit, plInitialize,
  # setsysInitialize...). O CMakeLists do borealis NAO o inclui — espera que o
  # app o compile. Sem ele o romfs nunca e montado e o app morre ao procurar a
  # fonte, antes de desenhar qualquer coisa.
  set(POKEHOME_UI_SRC src/ui/main.cpp src/ui/updater.cpp
                     src/ui/device_saves.cpp)
  if(PLATFORM_SWITCH)
    list(APPEND POKEHOME_UI_SRC
      lib/borealis/library/lib/platforms/switch/switch_wrapper.c)
  endif()

  add_executable(pokehome-ui ${POKEHOME_UI_SRC})
  target_link_libraries(pokehome-ui PRIVATE pokehome_core borealis)

  # Alvo de diagnostico: tela minima (um label), sem grid nem imagens.
  # Serve para separar "bug no meu conteudo" de "bug na inicializacao do
  # borealis/deko3d". Nao entra no fluxo normal.
  if(PLATFORM_SWITCH AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/minimal.cpp)
    add_executable(pokehome-min
      src/ui/minimal.cpp
      lib/borealis/library/lib/platforms/switch/switch_wrapper.c)
    target_link_libraries(pokehome-min PRIVATE borealis)
    # Precisa de romfs: switch_wrapper.c chama romfsInit() incondicionalmente,
    # e um NRO sem romfs faz o servico de filesystem falhar na inicializacao.
    nx_create_nro(pokehome-min
      NAME "PokeHome Min"
      AUTHOR "Matheus Nobre"
      ROMFS ${NRO_ROMFS})
  endif()
  # No Switch os assets vem do romfs embutido; no PC, do diretorio do projeto.
  if(PLATFORM_SWITCH)
    set(POKEHOME_SPRITES_DIR "romfs:/sprites/")
    set(POKEHOME_SPRITES_BIG_DIR "romfs:/sprites_big/")
    set(POKEHOME_SPRITES_SHINY_DIR "romfs:/sprites_shiny/")
    set(POKEHOME_SPRITES_BIG_SHINY_DIR "romfs:/sprites_big_shiny/")
    set(POKEHOME_UI_DIR "romfs:/ui/")
  else()
    set(POKEHOME_SPRITES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/romfs/sprites/")
    set(POKEHOME_SPRITES_BIG_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/romfs/sprites_big/")
    set(POKEHOME_SPRITES_SHINY_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/romfs/sprites_shiny/")
    set(POKEHOME_SPRITES_BIG_SHINY_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/romfs/sprites_big_shiny/")
    set(POKEHOME_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/romfs/ui/")
  endif()

  # BRLS_RESOURCES so no PC: no Switch o proprio borealis ja define como
  # "romfs:/", e sobrescrever com o caminho do projeto faz o app procurar a
  # fonte num diretorio que nao existe no console — crash na inicializacao.
  target_compile_definitions(pokehome-ui PRIVATE
    POKEHOME_SPRITES="${POKEHOME_SPRITES_DIR}"
    POKEHOME_SPRITES_BIG="${POKEHOME_SPRITES_BIG_DIR}"
    POKEHOME_SPRITES_SHINY="${POKEHOME_SPRITES_SHINY_DIR}"
    POKEHOME_SPRITES_BIG_SHINY="${POKEHOME_SPRITES_BIG_SHINY_DIR}"
    NESTBOX_SIM_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/build/switch-sim"
    POKEHOME_UI_ASSETS="${POKEHOME_UI_DIR}"
    NESTBOX_VERSION="${NESTBOX_VERSION}"
    NESTBOX_REPO="${NESTBOX_REPO}")

  # Auto-update: consulta a API do GitHub e baixa o .nro novo. So no Switch —
  # no PC o app e recompilado, nao atualizado.
  if(PLATFORM_SWITCH)
    find_library(CURL_LIB curl PATHS $ENV{DEVKITPRO}/portlibs/switch/lib)
    find_library(MBEDTLS_LIB mbedtls PATHS $ENV{DEVKITPRO}/portlibs/switch/lib)
    find_library(MBEDX509_LIB mbedx509 PATHS $ENV{DEVKITPRO}/portlibs/switch/lib)
    find_library(MBEDCRYPTO_LIB mbedcrypto
                 PATHS $ENV{DEVKITPRO}/portlibs/switch/lib)
    find_library(Z_LIB z PATHS $ENV{DEVKITPRO}/portlibs/switch/lib)
    if(CURL_LIB AND MBEDTLS_LIB)
      target_link_libraries(pokehome-ui PRIVATE
        ${CURL_LIB} ${MBEDTLS_LIB} ${MBEDX509_LIB} ${MBEDCRYPTO_LIB} ${Z_LIB})
      target_compile_definitions(pokehome-ui PRIVATE NESTBOX_HAS_CURL)
    else()
      message(WARNING "libcurl ausente — auto-update desabilitado")
    endif()
  endif()
  if(NOT PLATFORM_SWITCH)
    target_compile_definitions(pokehome-ui PRIVATE
      BRLS_RESOURCES="${BRLS_RESOURCES}")
  endif()

  if(PLATFORM_SWITCH)
    # Empacota o .elf em .nro com o romfs embutido (sprites + resources do
    # borealis). No console nao ha filesystem do projeto — tudo vem do romfs.
    # Montado no configure, nao no build: nx_create_nro valida a existencia do
    # diretorio ainda na configuracao.
    set(NRO_ROMFS ${CMAKE_BINARY_DIR}/romfs)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/romfs/sprites DESTINATION ${NRO_ROMFS})
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/romfs/sprites_big DESTINATION ${NRO_ROMFS})
    # Shiny (spec 036): so entram no romfs se existirem — o dono roda
  # scripts/prepare-sprites.py para gera-los. Sem eles o app cai no sprite
  # normal, sem quebrar.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/romfs/sprites_shiny)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/romfs/sprites_shiny
         DESTINATION ${NRO_ROMFS})
  endif()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/romfs/sprites_big_shiny)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/romfs/sprites_big_shiny
         DESTINATION ${NRO_ROMFS})
  endif()
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/romfs/ui DESTINATION ${NRO_ROMFS})
    file(COPY ${BRLS_RESOURCES} DESTINATION ${NRO_ROMFS}/resources)

    # Shaders do deko3d. O renderer os carrega de romfs:/shaders/*.dksh
    # (dk_renderer.cpp:428), mas o CMakeLists do borealis nao os compila —
    # espera que o app o faca, como acontece com switch_wrapper.c.
    #
    # Sem eles a GPU tenta ler codigo de shader em endereco invalido e o app
    # morre no primeiro draw.
    set(BRLS_SHADER_SRC
      ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/library/lib/extern/nanovg/deko3d/shaders)
    file(MAKE_DIRECTORY ${NRO_ROMFS}/shaders)
    # O sufixo do nome indica o estagio: _vsh = vertex, _fsh = fragment.
    foreach(shader fill_vsh fill_fsh fill_aa_fsh)
      if(shader MATCHES "_vsh$")
        set(uam_stage vert)
      else()
        set(uam_stage frag)
      endif()
      execute_process(
        COMMAND $ENV{DEVKITPRO}/tools/bin/uam
                --stage=${uam_stage}
                -o ${NRO_ROMFS}/shaders/${shader}.dksh
                ${BRLS_SHADER_SRC}/${shader}.glsl
        RESULT_VARIABLE uam_result
        ERROR_VARIABLE uam_err)
      if(NOT uam_result EQUAL 0)
        message(FATAL_ERROR "uam falhou em ${shader}: ${uam_err}")
      endif()
    endforeach()

    # NAME/AUTHOR/VERSION nao sao parametros de nx_create_nro — ela so aceita
    # TARGET/OUTPUT/ICON/NACP/ROMFS, e ignorava os outros em silencio. Por isso
    # o NRO saia como "pokehome / Unspecified Author" e sem versao nenhuma.
    # O jeito certo e gerar o .nacp e passa-lo em NACP.
    set(NESTBOX_NACP ${CMAKE_BINARY_DIR}/nestbox.nacp)
    add_custom_command(
      OUTPUT ${NESTBOX_NACP}
      COMMAND $ENV{DEVKITPRO}/tools/bin/nacptool --create
              "NestBox" "Matheus Nobre" "${NESTBOX_VERSION}" ${NESTBOX_NACP}
      COMMENT "Gerando NACP (nome, autor e versao do NRO)"
      VERBATIM)
    add_custom_target(nestbox_nacp DEPENDS ${NESTBOX_NACP})
    add_dependencies(pokehome-ui nestbox_nacp)

    # O nome do arquivo importa alem da estetica: o Sphaira deriva o
    # ApplicationId do forwarder de um hash do CAMINHO do .nro
    # (sphaira/source/owo.cpp:917). Um forwarder criado para pokehome.nro na
    # v0.1.0 deixou aquele id registrado, e toda tentativa seguinte colidia
    # com ele — dai o loading eterno e o erro 2123-0011.
    #
    # Nome novo = id novo, sem colisao. Tambem ajuda o auto-update: arquivo
    # que o loader nunca segurou.
    nx_create_nro(pokehome-ui
      OUTPUT ${CMAKE_BINARY_DIR}/nestbox.nro
      ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg
      NACP ${NESTBOX_NACP}
      ROMFS ${NRO_ROMFS})
  else()
    # No PC, borealis procura resources em ./resources relativo ao cwd.
    # Sem isso a janela abre mas nao desenha texto algum.
    add_custom_command(TARGET pokehome-ui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${BRLS_RESOURCES} $<TARGET_FILE_DIR:pokehome-ui>/resources
      COMMENT "Copiando resources do borealis")
  endif()
elseif(POKEHOME_UI)
  message(WARNING
    "lib/borealis ausente — UI desabilitada. Rode: git submodule update --init")
endif()
