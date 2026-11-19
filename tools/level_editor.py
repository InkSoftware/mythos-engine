#!/usr/bin/env python3
"""Level editor and previewer for any Mythos game.

    python3 level_editor.py [--project DIR] [--port 8765] [--no-open]

Standard library only. DIR is the game's repo root (default: the current
directory). It serves the editor page, reads and writes the game's level files
(and the JSON data files it lists), and renders previews by running the game's
own binary on a throwaway copy of the assets that carries the editor's unsaved
edits, so a preview is what the game draws.

A game configures it with an optional DIR/tools/level_editor.json; every key is
optional (paths are relative to DIR unless noted):

  assets    "assets"            the game's asset folder
  levels    "levels"            level files, relative to assets
  tiles     "data/tiles.json"   the tile legend, relative to assets
  start     ""                  the level to open first
  data      []                  more JSON files the page edits (relative to
                                assets); only these and the levels can be written
  stage     "build*/assets_stage"   glob of the merged asset folder the build
                                makes (engine + game assets): textures, models
  plugin    ""                  a JS file served to the page that teaches it the
                                game (see tools/level_editor.html, "Plugin")
  preview   {}                  {"game": [globs of the game binary],
                                 "overlay": ["levels", "data"],  # stage folders
                                                                # the page may override files in
                                 "timeout": 90}

Binds to 127.0.0.1 only; writes are limited to the levels and the listed data
files, and a request must come from the page itself (custom header, host check).
"""
import argparse
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import webbrowser
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
NAME_RE = re.compile(r"^[A-Za-z0-9_\-]+$")
SHOT_LOCK = threading.Lock()  # one game window at a time


class Project:
    def __init__(self, root):
        self.root = os.path.abspath(root)
        cfg = {}
        p = os.path.join(self.root, "tools", "level_editor.json")
        if os.path.isfile(p):
            with open(p, encoding="utf-8") as f:
                cfg = json.load(f)
        self.cfg = cfg
        self.assets = os.path.join(self.root, cfg.get("assets", "assets"))
        self.levels = os.path.join(self.assets, cfg.get("levels", "levels"))
        self.tiles_rel = cfg.get("tiles", "data/tiles.json")
        self.data_rel = list(cfg.get("data", []))
        self.plugin = cfg.get("plugin", "")
        self.preview = cfg.get("preview", {})
        self.stage = self._newest(cfg.get("stage", "build*/assets_stage"), dirs=True)
        self.game = self._game()
        self._bbox = {}

    def _newest(self, pattern, dirs=False):
        c = [x for x in glob.glob(os.path.join(self.root, pattern))
             if (os.path.isdir(x) if dirs else os.path.isfile(x))]
        c.sort(key=os.path.getmtime, reverse=True)
        return c[0] if c else None

    def _game(self):
        best = None
        for pat in self.preview.get("game", []):
            f = self._newest(pat)
            if f and (best is None or os.path.getmtime(f) > os.path.getmtime(best)):
                best = f
        return best

    def asset_path(self, rel):
        full = os.path.normpath(os.path.join(self.assets, rel))
        if not full.startswith(self.assets + os.sep):
            raise ValueError("path outside the assets folder")
        return full

    def folders(self, sub):
        return [d for d in (os.path.join(self.assets, sub),
                            os.path.join(self.stage, sub) if self.stage else None)
                if d and os.path.isdir(d)]

    def model_names(self):
        names = set()
        for d in self.folders("models"):
            names.update(f[:-4] for f in os.listdir(d) if f.endswith(".obj"))
        return sorted(names)

    def model_bbox(self, name):
        """[minx, minz, maxx, maxz, height] of an OBJ (front faces +z), cached."""
        if name in self._bbox:
            return self._bbox[name]
        self._bbox[name] = None
        for d in self.folders("models"):
            f = os.path.join(d, name + ".obj")
            if not os.path.isfile(f):
                continue
            x0, z0, x1, z1, y1 = 1e9, 1e9, -1e9, -1e9, 0.0
            with open(f, encoding="utf-8", errors="replace") as fh:
                for line in fh:
                    if line.startswith("v "):
                        x, y, z = (float(t) for t in line.split()[1:4])
                        x0, x1, z0, z1, y1 = min(x0, x), max(x1, x), min(z0, z), max(z1, z), max(y1, y)
            if x1 >= x0:
                self._bbox[name] = [round(v, 3) for v in (x0, z0, x1, z1, y1)]
            break
        return self._bbox[name]

    def texture(self, name):
        for d in self.folders("."):
            f = os.path.join(d, name + ".bmp")
            if os.path.isfile(f):
                return f
        return None


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def to_png(path):
    """A PPM (P6) or PNG picture as PNG bytes, standard library only."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return data
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not m:
        raise RuntimeError("the game wrote a picture in an unknown format")
    w, h = int(m.group(1)), int(m.group(2))
    px, stride, raw = data[m.end():], int(m.group(1)) * 3, bytearray()
    for y in range(h):
        raw.append(0)
        raw += px[y * stride:(y + 1) * stride]

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 3)) + chunk(b"IEND", b""))


def render_preview(pr, req):
    """Run the game on an overlay of the stage holding the page's edits.

    req: {"args": [...], "files": {path relative to assets: text}}; "{out}" and
    "{assets}" in an argument are replaced by the picture path and the overlay.
    """
    if not pr.game:
        raise RuntimeError("no game binary found (preview.game in tools/level_editor.json): build the game first")
    if not pr.stage:
        raise RuntimeError("no merged asset stage found (stage in tools/level_editor.json): build the game first")
    work = tempfile.mkdtemp(prefix="mythos_preview_")
    try:
        overlay, out = os.path.join(work, "assets"), os.path.join(work, "shot.ppm")
        os.makedirs(overlay)
        split = set(pr.preview.get("overlay", ["levels", "data"]))
        for entry in os.listdir(pr.stage):
            src = os.path.join(pr.stage, entry)
            if entry in split and os.path.isdir(src):
                os.makedirs(os.path.join(overlay, entry))
                for f in os.listdir(src):
                    os.symlink(os.path.join(src, f), os.path.join(overlay, entry, f))
            else:
                os.symlink(src, os.path.join(overlay, entry))
        for rel, text in (req.get("files") or {}).items():
            parts = rel.split("/")
            if len(parts) < 2 or parts[0] not in split or ".." in parts or not isinstance(text, str):
                raise ValueError("cannot override " + rel)
            dst = os.path.join(overlay, *parts)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if os.path.lexists(dst):
                os.remove(dst)
            write(dst, text)
        args = [str(a).replace("{out}", out).replace("{assets}", overlay)
                for a in req.get("args") or []]
        with SHOT_LOCK:
            p = subprocess.run([pr.game] + args, capture_output=True, text=True,
                               timeout=pr.preview.get("timeout", 90))
        if not os.path.exists(out):
            raise RuntimeError("the game made no picture (exit %d):\n%s" %
                               (p.returncode, (p.stdout + p.stderr)[-1500:]))
        return to_png(out)
    finally:
        shutil.rmtree(work, ignore_errors=True)


class Handler(BaseHTTPRequestHandler):
    server_version = "LevelEditor/2"
    pr = None
    port = 0

    def log_message(self, fmt, *a):
        sys.stderr.write("  " + fmt % a + "\n")

    def send(self, code, body, ctype="application/json"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def fail(self, code, msg):
        self.send(code, json.dumps({"error": str(msg)}))

    def body(self):
        return self.rfile.read(int(self.headers.get("Content-Length") or 0)).decode("utf-8")

    def allowed(self, mutating):
        """Only the editor page itself may talk to us (no DNS rebinding, no cross-site writes)."""
        host = (self.headers.get("Host") or "").split(":")[0]
        if host not in ("127.0.0.1", "localhost"):
            return False
        return not mutating or self.headers.get("X-Level-Editor") == "1"

    def do_GET(self):
        if not self.allowed(False):
            return self.fail(403, "forbidden")
        pr = self.pr
        try:
            path = self.path.split("?")[0]
            if path in ("/", "/index.html"):
                return self.send(200, read(os.path.join(HERE, "level_editor.html")), "text/html; charset=utf-8")
            if path == "/plugin.js":
                if not pr.plugin:
                    return self.send(200, "", "text/javascript")
                return self.send(200, read(os.path.join(pr.root, pr.plugin)), "text/javascript; charset=utf-8")
            if path == "/api/project":
                data = {rel: json.loads(read(pr.asset_path(rel))) for rel in pr.data_rel}
                return self.send(200, json.dumps({
                    "levels": sorted(os.path.basename(f)[:-4] for f in glob.glob(os.path.join(pr.levels, "*.txt"))),
                    "models": pr.model_names(), "tiles": json.loads(read(pr.asset_path(pr.tiles_rel))),
                    "data": data, "levelsRel": pr.cfg.get("levels", "levels"), "start": pr.cfg.get("start"), "preview": bool(pr.game and pr.stage), "plugin": bool(pr.plugin),
                    "root": pr.root}))
            if path == "/api/bbox":
                q = self.path.split("names=")[-1] if "names=" in self.path else ""
                return self.send(200, json.dumps({n: pr.model_bbox(n) for n in re.findall(r"[\w\-]+", q.replace(",", " "))}))
            m = re.match(r"^/api/level/([\w\-]+)$", path)
            if m:
                return self.send(200, read(os.path.join(pr.levels, m.group(1) + ".txt")), "text/plain; charset=utf-8")
            m = re.match(r"^/tex/([\w\-]+)\.bmp$", path)
            if m and pr.texture(m.group(1)):
                with open(pr.texture(m.group(1)), "rb") as fh:
                    return self.send(200, fh.read(), "image/bmp")
            self.send(404, b"not found", "text/plain")
        except FileNotFoundError:
            self.send(404, b"not found", "text/plain")
        except Exception as e:  # noqa: BLE001
            self.fail(500, e)

    def do_PUT(self):
        if not self.allowed(True):
            return self.fail(403, "forbidden")
        pr = self.pr
        try:
            path = self.path.split("?")[0]
            m = re.match(r"^/api/level/([\w\-]+)$", path)
            if m:
                text = self.body()
                if "[map]" not in text:
                    return self.fail(400, "not a level file")
                write(os.path.join(pr.levels, m.group(1) + ".txt"), text)
                return self.send(200, "{}")
            if path == "/api/data":
                rel = self.headers.get("X-Data-Path", "")
                if rel not in pr.data_rel:
                    return self.fail(403, "not an editable data file")
                write(pr.asset_path(rel), json.dumps(json.loads(self.body()), indent=2, ensure_ascii=False) + "\n")
                return self.send(200, "{}")
            self.fail(404, "not found")
        except Exception as e:  # noqa: BLE001
            self.fail(500, e)

    def do_POST(self):
        if not self.allowed(True):
            return self.fail(403, "forbidden")
        try:
            if self.path.split("?")[0] == "/api/preview":
                return self.send(200, render_preview(self.pr, json.loads(self.body())), "image/png")
            self.fail(404, "not found")
        except Exception as e:  # noqa: BLE001
            self.fail(500, e)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--project", default=".", help="the game's repo root (default: here)")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-open", action="store_true")
    a = ap.parse_args()
    pr = Project(a.project)
    if not os.path.isdir(pr.levels):
        sys.exit("no levels folder at %s (is --project the game's repo root?)" % pr.levels)
    Handler.pr = pr
    print("project     :", pr.root)
    print("game binary :", pr.game or "none (previews off)")
    print("asset stage :", pr.stage or "none (textures and engine models only from the repo)")
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), Handler)
    url = "http://127.0.0.1:%d/" % a.port
    print("level editor:", url, " (Ctrl-C to stop)")
    if not a.no_open:
        webbrowser.open(url)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
