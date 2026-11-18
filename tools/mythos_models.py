#!/usr/bin/env python3
"""Fetches CC0 3D models from Poly Haven and converts them for a game on mythos-engine (the shared library).

Poly Haven publishes every model under CC0 (public domain), as glTF with 1k JPG textures. The engine reads
Wavefront OBJ whose material names are texture names, so for each model this tool:

  1. downloads the glTF, its .bin and the diffuse (colour) textures into a cache folder,
  2. flattens the node tree and writes assets/models/<name>.obj, rotated by the model's pitch / roll / yaw,
     at life size (Poly Haven's models are in metres, like the world), with its feet at the origin
     and its front facing +z,
  3. bakes each material's textures to BMPs in assets/modeltex/ (a game registers every BMP in that folder as a
     texture, named after the file; the engine reads the <tex>_nrm / _rough / _metal / _ao companions by name),
  4. records the sources in CREDITS.md and in the credits data.

Two callers use it: the engine's own tools/fetch_cc0_models.py (the basic models of houses and offices, shipped in
the engine's assets) and a game's (its own models, laid over the engine's). Either gives `main` its MODELS
(game name -> Spec) and its root folder.

Any licence that allows commercial use is fine (CC0, or CC-BY with credit; never NC, ND-only or
"editorial"). Sources the tool can fetch itself: Poly Haven. For anything else (Sketchfab needs a
logged-in account to download, Smithsonian Open Access 3D, ...) download the glTF yourself and add a
Spec with `local=` (path to the .gltf or the unzipped folder's scene.gltf) and `credit=`
(name, author, license, url); the credit goes on the credits page automatically.

The colour map (with its baked-in shading) always carries over. With --pbr the normal, roughness, metal and
occlusion maps are baked too. Polygon counts are not reduced; pick models that are light enough.

Requires: python3, network, and either `sips` (macOS) or ImageMagick (`magick`) for the textures.
"""

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request

ROOT = None  # the repository the models are written into: set by configure()
MODELS_DIR = TEX_DIR = CREDITS = None


def configure(root):
    """Writes into <root>/assets/models and <root>/assets/modeltex, and credits into <root>/CREDITS.md."""
    global ROOT, MODELS_DIR, TEX_DIR, CREDITS
    ROOT = root
    MODELS_DIR = os.path.join(root, "assets", "models")
    TEX_DIR = os.path.join(root, "assets", "modeltex")
    CREDITS = os.path.join(root, "CREDITS.md")


# The download host rejects urllib's default User-Agent, so identify the tool honestly.
USER_AGENT = "mythos-asset-fetch/1.0 (CC0 model baking script)"
API = "https://api.polyhaven.com"
PBR = False      # also bake normal and roughness maps (--pbr; tools/import_blenderkit.py turns it on)
PBR_SIZE = 512   # their size in pixels: the specular detail needs less than the colour
MARK = "Poly Haven"  # in an OBJ's header: the generator leaves such a file alone

CELL = 0.92  # the footprint of one map cell that furniture may fill


class Spec:
    """How one game model comes from one Poly Haven model.

    ph      the Poly Haven asset id
    scale   a fixed uniform scale (Poly Haven models are in metres and the world is too: 1 unit = 1 m,
            so 1.0 is life size). Prefer it: it keeps proportions, and a piece wider than a cell may
            overhang its neighbours a little (the generator blocks those cells for beds)
    fit     (width, depth) the footprint may fill when no scale is given
    height  target height (None: keep the scale the footprint gives); the model is stretched
            vertically to it when that takes at most `stretch` (a ratio) from the uniform scale
    yaw     degrees to turn the model so its front faces +z (try 180 if it faces the wrong way)
    pitch   degrees about the x axis, applied before yaw, to stand a model up or lay it down (a torch that
            points up becomes one that points along +z with 90 / -90)
    roll    degrees about the z axis, applied first (stands a model that lies on its side upright)
    glass   False drops the model's see-through (BLEND) materials: the renderer has no blending, so a glass pane or a
            lamp chimney would be drawn as a solid black sheet
    art     a game texture to show on the model's "..._artwork" material instead of the model's own placeholder picture
    size    texture size in pixels for this model (default: the --size option); small props need little
    what    what it is in the game, for the credits
    local   a glTF file you downloaded yourself (then `ph` is only a label); needs `credit`
    credit  (name, author, license, url) of a local model
    """

    def __init__(self, ph, scale=None, fit=(CELL, CELL), height=None, yaw=0, stretch=1.35, what="", local=None,
                 credit=None, pitch=0, size=None, roll=0, glass=True, art=None):
        self.pitch, self.size, self.roll = pitch, size, roll
        self.glass, self.art = glass, art
        self.ph, self.scale, self.fit, self.height, self.yaw, self.stretch, self.what = ph, scale, fit, height, yaw, stretch, what
        self.local, self.credit = local, credit
        if local and not credit:
            sys.exit(f"{ph}: a local model needs credit=(name, author, license, url)")


# ---------------------------------------------------------------- download

def fetch(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f)


def api(path):
    req = urllib.request.Request(API + path, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def download(ph, cache):
    """Downloads the 1k glTF of a model; returns (path to the .gltf, info)."""
    folder = os.path.join(cache, ph)
    files = api(f"/files/{ph}")["gltf"]["1k"]["gltf"]
    info = api(f"/info/{ph}")
    gltf_path = os.path.join(folder, ph + ".gltf")
    fetch(files["url"], gltf_path)
    gltf = json.load(open(gltf_path, encoding="utf-8"))
    wanted = {b["uri"] for b in gltf.get("buffers", []) if "uri" in b}
    wanted |= {i["uri"] for i in gltf.get("images", []) if "uri" in i}
    for rel, meta in files.get("include", {}).items():
        if rel in wanted:
            fetch(meta["url"], os.path.join(folder, rel))
    return gltf_path, info


# ---------------------------------------------------------------- glTF

COMPONENT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_accessor(gltf, buffers, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    fmt, size = COMPONENT[acc["componentType"]]
    n = WIDTH[acc["type"]]
    stride = view.get("byteStride") or size * n
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    data = buffers[view["buffer"]]
    out = []
    for i in range(acc["count"]):
        out.append(struct.unpack_from("<" + fmt * n, data, base + i * stride))
    return out


def mat_mul(a, b):  # column-major 4x4
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def node_matrix(node):
    if "matrix" in node:
        return list(node["matrix"])
    tx, ty, tz = node.get("translation", (0, 0, 0))
    x, y, z, w = node.get("rotation", (0, 0, 0, 1))
    sx, sy, sz = node.get("scale", (1, 1, 1))
    r = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
         2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]
    return [r[0] * sx, r[1] * sx, r[2] * sx, 0, r[3] * sy, r[4] * sy, r[5] * sy, 0,
            r[6] * sz, r[7] * sz, r[8] * sz, 0, tx, ty, tz, 1]


def apply(m, p):
    return (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14])


def apply_dir(m, n):
    x = m[0] * n[0] + m[4] * n[1] + m[8] * n[2]
    y = m[1] * n[0] + m[5] * n[1] + m[9] * n[2]
    z = m[2] * n[0] + m[6] * n[1] + m[10] * n[2]
    length = math.sqrt(x * x + y * y + z * z) or 1.0
    return (x / length, y / length, z / length)


def load_geometry(gltf_path):
    """Every triangle of the scene, in world space: {material index: [(pos, normal, uv) x3, ...]}."""
    gltf = json.load(open(gltf_path, encoding="utf-8"))
    folder = os.path.dirname(gltf_path)
    buffers = [open(os.path.join(folder, b["uri"]), "rb").read() for b in gltf["buffers"]]
    groups = {}

    def walk(index, parent):
        node = gltf["nodes"][index]
        m = mat_mul(parent, node_matrix(node))
        if "mesh" in node:
            for prim in gltf["meshes"][node["mesh"]]["primitives"]:
                if prim.get("mode", 4) != 4:
                    continue
                at = prim["attributes"]
                pos = read_accessor(gltf, buffers, at["POSITION"])
                nor = read_accessor(gltf, buffers, at["NORMAL"]) if "NORMAL" in at else None
                uv = read_accessor(gltf, buffers, at["TEXCOORD_0"]) if "TEXCOORD_0" in at else None
                idx = [i[0] for i in read_accessor(gltf, buffers, prim["indices"])] if "indices" in prim else list(range(len(pos)))
                tris = groups.setdefault(prim.get("material", 0), [])
                for t in range(0, len(idx) - 2, 3):
                    tri = []
                    for i in idx[t:t + 3]:
                        tri.append((apply(m, pos[i]), apply_dir(m, nor[i]) if nor else None, uv[i] if uv else (0.0, 0.0)))
                    tris.append(tri)
        for child in node.get("children", []):
            walk(child, m)

    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    scene = gltf["scenes"][gltf.get("scene", 0)]
    for n in scene["nodes"]:
        walk(n, identity)
    return gltf, groups


# ---------------------------------------------------------------- textures

def write_bmp_solid(path, rgb):
    r, g, b = (max(0, min(255, int(round(c * 255)))) for c in rgb)
    w = h = 4
    row = bytes([b, g, r] * w)
    pad = (4 - (len(row) % 4)) % 4
    pixels = (row + b"\0" * pad) * h
    header = b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(header + pixels)


def jpg_to_bmp(src, dst, size):
    if shutil.which("sips"):
        cmd = ["sips", "-Z", str(size), "-s", "format", "bmp", src, "--out", dst]
    elif shutil.which("magick"):
        cmd = ["magick", src, "-resize", f"{size}x{size}>", "BMP3:" + dst]
    else:
        sys.exit("need `sips` (macOS) or ImageMagick (`magick`) to decode the JPEG textures")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def read_bmp(path):
    """Decodes an uncompressed 24/32-bit BMP: (width, height, rows top to bottom of (r, g, b))."""
    d = open(path, "rb").read()
    off, = struct.unpack_from("<I", d, 10)
    w, h, _planes, bpp, comp = struct.unpack_from("<iiHHI", d, 18)
    if bpp not in (24, 32) or comp not in (0, 3):
        sys.exit(f"{path}: need an uncompressed 24 or 32-bit BMP, got {bpp} bpp")
    top_down = h < 0
    h = abs(h)
    step = bpp // 8
    stride = (w * step + 3) // 4 * 4
    rows = []
    for y in range(h):
        base = off + y * stride
        rows.append([(d[base + x * step + 2], d[base + x * step + 1], d[base + x * step]) for x in range(w)])
    return w, h, rows if top_down else rows[::-1]


def write_bmp_gray(path, w, h, rows):
    """A 24-bit BMP whose three channels are all the given 0-255 values (rows top to bottom)."""
    pad = b"\0" * ((4 - (w * 3) % 4) % 4)
    body = b"".join(b"".join(bytes([v, v, v]) for v in row) + pad for row in reversed(rows))
    header = b"BM" + struct.pack("<IHHI", 54 + len(body), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(header + body)


def bake_pbr(gltf, folder, mat, tex_name, size):
    """Writes the maps the engine reads by name (mythos-engine gfx/pbr.h) beside the colour map:
    <tex>_nrm.bmp   the normal map,
    <tex>_rough.bmp roughness (green channel of the glTF metallic-roughness map, times the factor),
    <tex>_metal.bmp metalness (blue channel, times the factor), only if the material has any,
    <tex>_ao.bmp    ambient occlusion (red channel of the occlusion map), only if the material has one.
    A material with none of these writes nothing."""
    def uri_of(info):
        return gltf["images"][gltf["textures"][info["index"]]["source"]]["uri"]

    def decode(info):
        tmp = os.path.join(TEX_DIR, tex_name + ".tmp.bmp")
        jpg_to_bmp(os.path.join(folder, uri_of(info)), tmp, min(PBR_SIZE, size))
        try:
            return read_bmp(tmp)
        finally:
            os.remove(tmp)

    def write_channel(suffix, info, channel, factor):
        w, h, rows = decode(info)
        write_bmp_gray(os.path.join(TEX_DIR, tex_name + suffix + ".bmp"), w, h,
                       [[max(0, min(255, round(px[channel] * factor))) for px in row] for row in rows])

    def write_solid(suffix, value):
        write_bmp_gray(os.path.join(TEX_DIR, tex_name + suffix + ".bmp"), 4, 4, [[max(0, min(255, round(value * 255)))] * 4] * 4)

    nt = mat.get("normalTexture")
    if nt is not None:
        jpg_to_bmp(os.path.join(folder, uri_of(nt)), os.path.join(TEX_DIR, tex_name + "_nrm.bmp"), min(PBR_SIZE, size))
    pbr = mat.get("pbrMetallicRoughness", {})
    mr = pbr.get("metallicRoughnessTexture")
    rough_f, metal_f = pbr.get("roughnessFactor", 1.0), pbr.get("metallicFactor", 1.0)
    if mr is not None:
        write_channel("_rough", mr, 1, rough_f)
        if metal_f > 0:
            w, h, rows = decode(mr)
            if max(px[2] for row in rows for px in row) * metal_f >= 96:  # skip a dielectric's map (JPEG noise is not metal)
                write_bmp_gray(os.path.join(TEX_DIR, tex_name + "_metal.bmp"), w, h,
                               [[min(255, round(px[2] * metal_f)) for px in row] for row in rows])
    else:
        if "roughnessFactor" in pbr:
            write_solid("_rough", rough_f)
        if pbr.get("metallicFactor", 0.0) > 0:
            write_solid("_metal", metal_f)
    ot = mat.get("occlusionTexture")
    if ot is not None:
        write_channel("_ao", ot, 0, 1.0)


# ---------------------------------------------------------------- conversion

def convert(name, spec, cache, size):
    if spec.local:
        gltf_path = spec.local
        cname, author, lic, url = spec.credit
        info = {"name": cname, "authors": {author: "All"}, "license": lic, "url": url}
    else:
        gltf_path, info = download(spec.ph, cache)
        info["license"] = "CC0 1.0"
        info["url"] = f"https://polyhaven.com/a/{spec.ph}"
    gltf, groups = load_geometry(gltf_path)
    if not spec.glass:
        groups = {mi: t for mi, t in groups.items()
                  if (gltf["materials"][mi].get("alphaMode") if gltf.get("materials") else None) != "BLEND"}
    folder = os.path.dirname(gltf_path)

    # Turn it (roll about z, pitch about +x, then yaw about +y), then measure.
    size = spec.size or size
    a = math.radians(spec.yaw)
    ca, sa = math.cos(a), math.sin(a)
    b = math.radians(spec.pitch)
    cb, sb = math.cos(b), math.sin(b)
    c0, s0 = math.cos(math.radians(spec.roll)), math.sin(math.radians(spec.roll))
    rot = lambda p: (lambda q: (q[0] * ca + q[2] * sa, q[1], -q[0] * sa + q[2] * ca))(
        (lambda r: (r[0], r[1] * cb - r[2] * sb, r[1] * sb + r[2] * cb))((p[0] * c0 - p[1] * s0, p[0] * s0 + p[1] * c0, p[2])))
    for tris in groups.values():
        for tri in tris:
            for k, (p, n, uv) in enumerate(tri):
                tri[k] = (rot(p), rot(n) if n else None, uv)
    pts = [p for tris in groups.values() for tri in tris for (p, _, _) in tri]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    w, h, d = hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]
    if spec.scale is not None:
        s = sy = spec.scale
    else:
        s = min(spec.fit[0] / w, spec.fit[1] / d)
        if spec.height is None:
            s = min(s, 1.0)  # no target height: never blow a small thing up
        sy = s
        if spec.height is not None and 1 / spec.stretch <= spec.height / (h * s) <= spec.stretch:
            sy = spec.height / h
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2

    place = lambda p: ((p[0] - cx) * s, (p[1] - lo[1]) * sy, (p[2] - cz) * s)

    # Materials: each gets a BMP in assets/modeltex.
    os.makedirs(TEX_DIR, exist_ok=True)
    mats = {}
    for mi in sorted(groups):
        mat = gltf["materials"][mi] if gltf.get("materials") else {}
        pbr = mat.get("pbrMetallicRoughness", {})
        tex_name = f"m_{name}_{len(mats)}"
        if spec.art and mat.get("name", "").endswith("_artwork"):
            mats[mi] = spec.art  # a texture the game already has
            continue
        mats[mi] = tex_name
        out = os.path.join(TEX_DIR, tex_name + ".bmp")
        bct = pbr.get("baseColorTexture")
        if bct is not None:
            uri = gltf["images"][gltf["textures"][bct["index"]]["source"]]["uri"]
            jpg_to_bmp(os.path.join(folder, uri), out, size)
        else:
            write_bmp_solid(out, pbr.get("baseColorFactor", [0.5, 0.5, 0.5])[:3])
        if PBR:
            bake_pbr(gltf, folder, mat, tex_name, size)

    lines = [f"# {MARK if not spec.local else 'Third-party'} model '{info.get('name', spec.ph)}' by "
             f"{', '.join(info.get('authors', {}).keys()) or 'unknown'}, {info['license']} ({info['url']}).",
             f"# Converted by tools/fetch_cc0_models.py for the game model '{name}': roll {spec.roll}, pitch {spec.pitch}, yaw {spec.yaw}, "
             f"scale {s:.3f} (height {sy:.3f}), feet at the origin, front facing +z. Do not edit by hand."]
    verts, uvs, norms, faces = [], [], [], []
    seen = {}  # (position, uv, normal) -> index: a vertex shared by neighbouring triangles is written once
    tri_count = 0
    for mi in sorted(groups):
        faces.append(f"usemtl {mats[mi]}")
        for tri in groups[mi]:
            ids = []
            for p, n, uv in tri:
                x, y, z = place(p)
                nx, ny, nz = n if n else (0, 1, 0)
                if sy != s:  # a stretched height tilts the normals
                    ny = ny / (sy / s)
                    length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                    nx, ny, nz = nx / length, ny / length, nz / length
                key = (round(x, 4), round(y, 4), round(z, 4), round(uv[0], 4), round(uv[1], 4),
                       round(nx, 3), round(ny, 3), round(nz, 3))
                if key not in seen:
                    verts.append(f"v {key[0]:.4f} {key[1]:.4f} {key[2]:.4f}")
                    uvs.append(f"vt {key[3]:.4f} {1.0 - key[4]:.4f}")  # glTF's v runs down, OBJ's up
                    norms.append(f"vn {key[5]:.3f} {key[6]:.3f} {key[7]:.3f}")
                    seen[key] = len(verts)
                ids.append(seen[key])
            faces.append("f " + " ".join(f"{i}/{i}/{i}" for i in ids))
            tri_count += 1
    path = os.path.join(MODELS_DIR, name + ".obj")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines + verts + uvs + norms + faces) + "\n")
    print(f"{name:10} <- {spec.ph:28} {tri_count:6d} tris  {w * s:.2f} x {h * sy:.2f} x {d * s:.2f}  "
          f"{len(mats)} material(s)")
    return info


def write_credits(done, tool, inherit=None):
    """CREDITS.md's models block, and the credits page data: assets/data/credits.json's "models" section when this
    repository has one (a game: `inherit` names another file of entries, the engine's, to list first), else
    assets/data/model_credits.json (the engine's, for the games to list)."""
    begin, end = "<!-- cc0-models:begin -->", "<!-- cc0-models:end -->"
    rows = [begin, "", "## 3D models — [Poly Haven](https://polyhaven.com) (CC0) and others", "",
            f"Converted to OBJ and BMP by `{tool}` (textures in `assets/modeltex/`).", "",
            "| Model | Source model | Author | Licence | Used for |", "| --- | --- | --- | --- | --- |"]
    for name, (spec, info) in sorted(done.items()):
        authors = ", ".join(info.get("authors", {}).keys()) or "Poly Haven"
        rows.append(f"| `{name}.obj` | [{info.get('name', spec.ph)}]({info['url']}) | {authors} | {info['license']} | {spec.what} |")
    rows += ["", end]
    text = open(CREDITS, encoding="utf-8").read()
    block = "\n".join(rows)
    if begin in text and end in text:
        head, rest = text.split(begin, 1)
        _, tail = rest.split(end, 1)
        text = head + block + tail
    else:
        text = text.rstrip("\n") + "\n\n" + block + "\n"
    open(CREDITS, "w", encoding="utf-8").write(text)

    seen = set()
    entries = []
    if inherit and os.path.exists(inherit):
        for e in json.load(open(inherit, encoding="utf-8")):
            seen.add(e["name"])
            entries.append(e)
    for name, (spec, info) in sorted(done.items()):
        label = info.get("name", spec.ph) + " (Poly Haven)" if not spec.local else info.get("name", spec.ph)
        if label in seen:
            continue
        seen.add(label)
        entries.append({"name": label, "by": ", ".join(info.get("authors", {}).keys()), "license": info["license"], "what": ""})
    path = os.path.join(ROOT, "assets", "data", "credits.json")
    if os.path.exists(path):
        data = json.load(open(path, encoding="utf-8"))
        for section in data["sections"]:
            if section["title"] == "credits.sec.models":
                section["entries"] = entries
        with open(path, "w", encoding="utf-8") as f:
            f.write(json.dumps(data, indent=2, ensure_ascii=False) + "\n")
    else:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(os.path.join(os.path.dirname(path), "model_credits.json"), "w", encoding="utf-8") as f:
            f.write(json.dumps(entries, indent=2, ensure_ascii=False) + "\n")


def run(models, root, tool, inherit_credits=None, cache_name="mythos_polyhaven_cache"):
    """The command line of a fetch tool: bakes `models` (name -> Spec) into the repository at `root`."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cache", default=os.path.join(tempfile.gettempdir(), cache_name))
    ap.add_argument("--only", nargs="*", help="only these model names (the credits are left alone)")
    ap.add_argument("--size", type=int, default=1024, help="texture size in pixels (the longest side)")
    ap.add_argument("--pbr", action="store_true", help="also bake the normal, roughness, metal and occlusion maps (<tex>_nrm, _rough, _metal, _ao)")
    args = ap.parse_args()
    configure(root)
    os.makedirs(MODELS_DIR, exist_ok=True)
    global PBR
    PBR = args.pbr
    names = args.only or list(models)
    done = {}
    for name in names:
        if name not in models:
            sys.exit(f"no such model in MODELS: {name}")
        done[name] = (models[name], convert(name, models[name], args.cache, args.size))
    if not args.only:
        write_credits(done, tool, inherit_credits)
