#!/usr/bin/env python3
"""Writes the engine's procedural stand-in models (mythos_assets.COMMON_MODELS) to assets/models.

    python3 tools/generate_models.py

A model baked from a scan (tools/fetch_cc0_models.py) of the same name is left alone (its OBJ carries a "Poly Haven"
header).
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mythos_assets as ma  # noqa: E402

OUT = os.path.join(os.path.dirname(HERE), "assets", "models")

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, build in ma.COMMON_MODELS.items():
        path = os.path.join(OUT, name + ".obj")
        if os.path.exists(path) and "Poly Haven" in open(path).read(2000):
            continue
        build().write(path, name, "tools/generate_models.py")
    print("wrote", len(ma.COMMON_MODELS), "models")
