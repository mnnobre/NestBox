#!/bin/bash
# Build do NRO para Nintendo Switch, dentro do container devkitpro/devkita64.
#
# Uso (a partir do host):
#   docker exec pokehome-build bash /project/scripts/build-nro.sh
#
# O container e criado uma vez com:
#   docker run -d --name pokehome-build -v "<projeto>:/project" -w /project \
#     devkitpro/devkita64:latest sleep infinity
set -euo pipefail

export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH

BUILD_DIR=/project/build-switch

# GLM nao vem na imagem base e o nanovg/deko3d do borealis precisa dele.
# Idempotente: se ja estiver instalado, o pacman apenas reinstala rapido.
if [ ! -f $DEVKITPRO/portlibs/switch/include/glm/vec2.hpp ]; then
    dkp-pacman -Sy --noconfirm switch-glm
fi

# O build do host (build/) usa outro compilador; nunca reaproveitar o cache.
cmake -S /project -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
    -DPLATFORM_SWITCH=ON \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" -j"$(nproc)"

echo
echo "=== artefatos ==="
ls -lah "$BUILD_DIR"/*.nro "$BUILD_DIR"/*.elf 2>/dev/null || true
