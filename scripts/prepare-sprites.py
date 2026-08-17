#!/usr/bin/env python
"""Prepara os sprites do Pokemon Home para o romfs.

Os originais do PokeAPI (sprites/pokemon/other/home) sao 512x512 — bonitos,
mas 1 MB de VRAM cada em RGBA. O pool de imagens do borealis no Switch tem
4 MB (IMAGES_POOL_SIZE), e a tela de lista mostra 78 sprites ao mesmo tempo.

Gera quatro conjuntos:
  romfs/sprites/            128x128  grades (caixas, lista, miniaturas da dex)
  romfs/sprites_big/        256x256  tela de detalhes e painel da Pokedex
  romfs/sprites_shiny/      128x128  o mesmo, para shiny (spec 036)
  romfs/sprites_big_shiny/  256x256

O shiny e o MESMO nome de arquivo num caminho diferente na PokeAPI:
  sprites/pokemon/other/home/<dex>.png        — normal
  sprites/pokemon/other/home/shiny/<dex>.png  — shiny

Uso:
  # a partir de um clone/copia local do repo PokeAPI/sprites
  python scripts/prepare-sprites.py <dir-com-os-512x512>

  # baixando direto (mais lento; ~2050 imagens)
  python scripts/prepare-sprites.py --download

O diretorio de origem deve conter <dex>.png e, para shiny, um subdiretorio
shiny/ com os mesmos nomes. Sem ele, so os normais sao gerados.
"""

import os
import sys
import urllib.request

from PIL import Image

GRID_SIZE = 128
BIG_SIZE = 256

# Ate Pecharunt. Antes era 386 (gen3), mas o romfs ja carrega 1025 desde a
# spec 035 — o script e que estava para tras.
DEX_MAX = 1025

BASE_URL = ("https://raw.githubusercontent.com/PokeAPI/sprites/master"
            "/sprites/pokemon/other/home")


def convert(src_dir: str, out_dir: str, size: int) -> tuple[int, int]:
    """Redimensiona <dex>.png de src_dir para out_dir. Ausentes sao pulados."""
    if not os.path.isdir(src_dir):
        return 0, 0
    os.makedirs(out_dir, exist_ok=True)
    done = 0
    total_bytes = 0
    for dex in range(1, DEX_MAX + 1):
        src = os.path.join(src_dir, f"{dex}.png")
        if not os.path.exists(src):
            continue
        dst = os.path.join(out_dir, f"{dex}.png")
        with Image.open(src) as img:
            # LANCZOS preserva o contorno; os sprites do Home tem alpha, e
            # convert("RGBA") garante que ele sobreviva ao resize.
            resized = img.convert("RGBA").resize((size, size), Image.LANCZOS)

            # Quantiza para paleta antes de salvar. Sem isto o PNG sai em RGBA
            # e fica ~3,3x maior (10 KB contra 3 KB por sprite de 128px), o que
            # no conjunto completo e a diferenca entre 117 MB e 34 MB de romfs.
            #
            # FASTOCTREE preserva o canal alpha, que os sprites do Home usam.
            # 255 cores em vez de 256 deixam um indice livre para a
            # transparencia. Conferido contra os sprites que ja estavam no
            # repo: RMSE 0.00 — reproduz exatamente o que havia antes.
            resized.quantize(colors=255, method=Image.FASTOCTREE).save(
                dst, optimize=True
            )
        done += 1
        total_bytes += os.path.getsize(dst)
    return done, total_bytes


def download(dest: str) -> int:
    """Baixa os 512x512 (normais e shiny) para dest/ e dest/shiny/."""
    os.makedirs(dest, exist_ok=True)
    os.makedirs(os.path.join(dest, "shiny"), exist_ok=True)

    got = 0
    for kind, sub in (("normal", ""), ("shiny", "shiny")):
        for dex in range(1, DEX_MAX + 1):
            out = os.path.join(dest, sub, f"{dex}.png")
            if os.path.exists(out):
                got += 1
                continue
            url = f"{BASE_URL}/{sub}/{dex}.png" if sub else f"{BASE_URL}/{dex}.png"
            try:
                urllib.request.urlretrieve(url, out)
                got += 1
            except Exception as exc:  # noqa: BLE001 — falta de sprite e normal
                # Nem toda dex tem sprite (formas, entradas vagas). Seguir e
                # o certo: convert() pula o que nao existe.
                if os.path.exists(out):
                    os.remove(out)
                print(f"  {kind} {dex}: {exc}", file=sys.stderr)
            if dex % 100 == 0:
                print(f"  {kind}: {dex}/{DEX_MAX}", file=sys.stderr)
    return got


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    if sys.argv[1] == "--download":
        src = os.path.join(root, "build", "sprites-src")
        print(f"baixando para {src} ...")
        n = download(src)
        print(f"{n} arquivos disponiveis")
    else:
        src = sys.argv[1]
        if not os.path.isdir(src):
            print(f"erro: '{src}' nao e um diretorio")
            return 1

    shiny_src = os.path.join(src, "shiny")

    jobs = [
        ("sprites", src, GRID_SIZE),
        ("sprites_big", src, BIG_SIZE),
        ("sprites_shiny", shiny_src, GRID_SIZE),
        ("sprites_big_shiny", shiny_src, BIG_SIZE),
    ]

    for out_name, src_dir, size in jobs:
        out = os.path.join(root, "romfs", out_name)
        count, size_bytes = convert(src_dir, out, size)
        if count == 0:
            print(f"{out_name}: nada gerado (origem ausente: {src_dir})")
            continue
        print(f"{out_name}: {count}/{DEX_MAX} em {size}x{size}, "
              f"{size_bytes // 1024} KB")

    return 0


if __name__ == "__main__":
    sys.exit(main())
