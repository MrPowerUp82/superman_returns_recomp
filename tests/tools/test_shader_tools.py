"""tools/shaders/extract_shaders.py and pack_shaders.py on synthetic shader
containers (no game files, no translator, no DXC)."""
import json
import struct
import subprocess
import sys
from pathlib import Path

import pytest

xxhash = pytest.importorskip("xxhash")

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "tools" / "shaders"


def container(pixel=True, ucode=b"\x11" * 16):
    """Minimal XDK shader container accepted by extract_shaders.parse_container."""
    vsize = 0x40
    hdr = struct.pack(">9I", 0x102A1100 | (0 if pixel else 1), vsize, len(ucode), 0, 0x10, 0, 0x20, 0, 0)
    body = bytearray(vsize)
    body[:len(hdr)] = hdr
    body[0x20:0x28] = struct.pack(">2I", 0, len(ucode))
    return bytes(body) + ucode


def run(script, *args):
    return subprocess.run([sys.executable, str(SHADERS / script), *map(str, args)],
                          capture_output=True, text=True, check=True).stdout


def test_extract_finds_unaligned_and_dumped_containers(tmp_path):
    game = tmp_path / "game" / "DATA"
    game.mkdir(parents=True)
    ps, vs = container(True), container(False, b"\x22" * 8)
    # Unaligned (offset 5), a fake magic that fails validation, then a VS.
    (game / "a.AST").write_bytes(b"junk!" + ps + b"\x10\x2a\x11\x00" + b"\0" * 40 + vs)
    dumps = tmp_path / "dumps"
    dumps.mkdir()
    extra = container(True, b"\x33" * 12)
    (dumps / "whatever.ps.bin").write_bytes(extra)
    out = tmp_path / "out"
    run("extract_shaders.py", "--game", game.parent, "--out", out, "--no-xex", "--dump-dir", dumps)
    manifest = json.loads((out / "manifest.json").read_text())
    assert manifest["unique_ps"] == 2 and manifest["unique_vs"] == 1
    assert manifest["game"] == "superman_returns 454107ED"
    hashes = {f"{xxhash.xxh3_64_intdigest(c):016X}" for c in (ps, vs, extra)}
    assert {s["container_hash"] for s in manifest["shaders"]} == hashes
    first = next(o for o in manifest["occurrences"] if o["file"] == "DATA/a.AST")
    assert first["offset"] == 5 and first["offset_mod4"] == 1
    # raw/<hash>.<stage>.bin holds exactly the container bytes.
    h = f"{xxhash.xxh3_64_intdigest(vs):016X}"
    assert (out / "raw" / f"{h}.vs.bin").read_bytes() == vs


def test_pack_format_matches_the_renderer_reader(tmp_path):
    dxil = tmp_path / "dxil"
    dxil.mkdir()
    (dxil / "00000000000000AA.ps.dxil").write_bytes(b"PS-A")
    (dxil / "00000000000000AA.vs.dxil").write_bytes(b"VS-A!")
    (dxil / "0000000000000001.ps.dxil").write_bytes(b"P1")
    (dxil / "ignored.txt").write_bytes(b"x")
    (dxil / "00000000000000AA.ps.lib.dxil").write_bytes(b"lib")  # not a pack entry
    pak = tmp_path / "shaders.pak"
    run("pack_shaders.py", dxil, pak)
    data = pak.read_bytes()
    magic, version, count = struct.unpack_from("<4sII", data, 0)
    assert (magic, version, count) == (b"CNSH", 1, 3)
    entries = [struct.unpack_from("<QIII", data, 12 + 20 * i) for i in range(count)]
    # Sorted by (hash, stage), stage 0 = vs, 1 = ps (shader_pack.h binary search).
    assert [(h, s) for h, s, _, _ in entries] == [(1, 1), (0xAA, 0), (0xAA, 1)]
    blobs = {(h, s): data[o:o + n] for h, s, o, n in entries}
    assert blobs[(0xAA, 0)] == b"VS-A!" and blobs[(0xAA, 1)] == b"PS-A" and blobs[(1, 1)] == b"P1"
