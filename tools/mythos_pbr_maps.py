#!/usr/bin/env python3
"""Roughness and metal maps for the engine's common procedural textures (materials.cpp).

The common textures are made in code, so they have no PBR maps and would all be matt under the Materials
setting. This tool writes a tileable roughness map (and a metal map for metal) per material that should look
different, as <texture>_rough.bmp / <texture>_metal.bmp in assets/modeltex/. A game that registers the BMPs of its
modeltex folder as textures (the engine reads the companions by name: src/engine/gfx/pbr.h) gets them; a material
not listed stays matt. Games import `write_maps` to do the same for their own textures.

    python3 tools/mythos_pbr_maps.py            # the common materials, into this repo's assets/modeltex
"""

import os
import struct
import sys

SIZE = 128

# name: (roughness mean, roughness variation, metal mean or None, metal variation)
#   roughness 0 = mirror, 1 = fully matt; variation is the amplitude of the noise around the mean.
COMMON_PROFILES = {
    "tile": (0.22, 0.10, None, 0),         # glazed ceramic: glossy
    "enamel": (0.12, 0.05, None, 0),       # baths, sinks, cookers: glossy white
    "metal": (0.38, 0.22, 1.0, 0.06),      # brushed, scratched
    "wood": (0.55, 0.18, None, 0),         # varnished boards
    "door": (0.48, 0.14, None, 0),         # painted wood
    "pine": (0.70, 0.12, None, 0),
    "stone": (0.72, 0.16, None, 0),
    "rock": (0.82, 0.10, None, 0),
    "brick": (0.88, 0.08, None, 0),
    "plaster": (0.85, 0.08, None, 0),
    "wallpaper": (0.70, 0.08, None, 0),    # paper with a faint sheen
    "wallpaper70s": (0.62, 0.08, None, 0),  # vinyl: a little shinier
    "mirror": (0.04, 0.02, None, 0),       # glass
    "skin": (0.55, 0.08, None, 0),
}


def write_bmp_gray(path, w, h, rows):
    """A 24-bit BMP whose three channels are all the given 0-255 values (rows top to bottom)."""
    pad = b"\0" * ((4 - (w * 3) % 4) % 4)
    body = b"".join(b"".join(bytes([v, v, v]) for v in row) + pad for row in reversed(rows))
    header = b"BM" + struct.pack("<IHHI", 54 + len(body), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(header + body)


def _lattice(period, seed):
    """A tileable grid of pseudo-random values in [0, 1), period x period."""
    state = seed * 2654435761 & 0xFFFFFFFF
    out = []
    for _ in range(period * period):
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        out.append(state / 4294967296.0)
    return out


def _noise(x, y, period, grid):
    """Bilinear value noise at (x, y) in 0..SIZE, tiling every SIZE."""
    smooth = lambda t: t * t * (3 - 2 * t)
    fx, fy = x / SIZE * period, y / SIZE * period
    x0, y0 = int(fx) % period, int(fy) % period
    x1, y1 = (x0 + 1) % period, (y0 + 1) % period
    tx, ty = smooth(fx - int(fx)), smooth(fy - int(fy))
    a = grid[y0 * period + x0] * (1 - tx) + grid[y0 * period + x1] * tx
    b = grid[y1 * period + x0] * (1 - tx) + grid[y1 * period + x1] * tx
    return a * (1 - ty) + b * ty


def make_map(mean, variation, seed):
    coarse, fine = _lattice(8, seed), _lattice(32, seed + 1)
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            n = 0.65 * _noise(x, y, 8, coarse) + 0.35 * _noise(x, y, 32, fine)  # 0..1, mean 0.5
            row.append(max(0, min(255, round((mean + (n - 0.5) * 2 * variation) * 255))))
        rows.append(row)
    return rows


def write_maps(profiles, out_dir):
    """Writes <name>_rough.bmp (and <name>_metal.bmp) for each profile into out_dir."""
    os.makedirs(out_dir, exist_ok=True)
    for i, (name, (rough, rough_var, metal, metal_var)) in enumerate(sorted(profiles.items())):
        write_bmp_gray(os.path.join(out_dir, name + "_rough.bmp"), SIZE, SIZE, make_map(rough, rough_var, 100 + i * 7))
        line = f"{name:14} rough {rough:.2f} +-{rough_var:.2f}"
        if metal is not None:
            write_bmp_gray(os.path.join(out_dir, name + "_metal.bmp"), SIZE, SIZE, make_map(metal, metal_var, 500 + i * 7))
            line += f"  metal {metal:.2f}"
        print(line)


if __name__ == "__main__":
    write_maps(COMMON_PROFILES, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "modeltex"))
