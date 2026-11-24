#!/usr/bin/env python3
"""Tests of mythos_models.pack_glb (a rigged glTF kept as one .glb). Run: python3 tools/test_mythos_models.py"""

import json
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mythos_models as mm  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PETE = os.path.join(HERE, "..", "assets", "characters", "prototype_pete.glb")


def read_glb(data):
    assert data[:4] == b"glTF"
    version, total = struct.unpack_from("<II", data, 4)
    assert version == 2 and total == len(data)
    chunks, at = {}, 12
    while at < len(data):
        length, kind = struct.unpack_from("<II", data, at)
        chunks[kind] = data[at + 8:at + 8 + length]
        at += 8 + length
        assert at % 4 == 0
    return json.loads(chunks[0x4E4F534A]), chunks[0x004E4942]


class PackGlb(unittest.TestCase):
    def test_joins_buffers_and_drops_textures(self):
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, "a.bin"), "wb").write(struct.pack("<3f", 1, 2, 3) + b"xyz")  # 15 bytes: needs padding
            open(os.path.join(d, "b.bin"), "wb").write(struct.pack("<2f", 4, 5))
            gltf = {
                "asset": {"version": "2.0"},
                "buffers": [{"uri": "a.bin", "byteLength": 15}, {"uri": "b.bin", "byteLength": 8}],
                "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 12},
                                {"buffer": 1, "byteOffset": 0, "byteLength": 8}],
                "images": [{"uri": "x.jpg"}], "textures": [{"source": 0}], "samplers": [{}],
                "materials": [{"name": "skin", "pbrMetallicRoughness": {
                    "baseColorFactor": [1, 0.5, 0.25, 1], "baseColorTexture": {"index": 0}},
                    "normalTexture": {"index": 0}}],
            }
            path = os.path.join(d, "m.gltf")
            json.dump(gltf, open(path, "w"))
            out, binary = read_glb(mm.pack_glb(path))
            self.assertEqual(len(out["buffers"]), 1)
            self.assertEqual(out["buffers"][0]["byteLength"], len(binary))
            views = out["bufferViews"]
            self.assertTrue(all(v["buffer"] == 0 for v in views))
            self.assertEqual(struct.unpack_from("<3f", binary, views[0]["byteOffset"]), (1, 2, 3))
            self.assertEqual(struct.unpack_from("<2f", binary, views[1]["byteOffset"]), (4, 5))
            self.assertEqual(views[1]["byteOffset"] % 4, 0)
            for key in ("images", "textures", "samplers"):
                self.assertNotIn(key, out)
            material = out["materials"][0]
            self.assertNotIn("normalTexture", material)
            self.assertNotIn("baseColorTexture", material["pbrMetallicRoughness"])
            self.assertEqual(material["pbrMetallicRoughness"]["baseColorFactor"], [1, 0.5, 0.25, 1])

    @unittest.skipUnless(os.path.exists(PETE), "the shipped character is missing")
    def test_a_real_character_survives_unpack_and_repack(self):
        original, binary = read_glb(open(PETE, "rb").read())
        with tempfile.TemporaryDirectory() as d:
            unpacked = dict(original)
            unpacked["buffers"] = [{"uri": "pete.bin", "byteLength": len(binary)}]
            open(os.path.join(d, "pete.bin"), "wb").write(binary)
            path = os.path.join(d, "pete.gltf")
            json.dump(unpacked, open(path, "w"))
            again, rebinary = read_glb(mm.pack_glb(path))
        self.assertEqual(rebinary.rstrip(b"\0"), binary.rstrip(b"\0"))
        self.assertEqual(again["animations"], original["animations"])
        self.assertEqual(again["skins"], original["skins"])
        self.assertEqual(again["accessors"], original["accessors"])
        self.assertEqual(again["nodes"], original["nodes"])


if __name__ == "__main__":
    unittest.main()
