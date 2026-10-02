"""Build the pre-translated shader library ("pre-shaders") of the native
renderer, the approach of StevensND/nfsmw-nx (nfsmw_shaders.nfsp): one local
file with every ORIGINAL shader container next to its offline DXIL. The game
recognises its shaders by comparing what the XDK creators receive with those
originals (port/src/native_renderer/shader_library.h), so the draw-time hash of
patched guest memory no longer has to match the corpus.

Inputs come from tools/shaders/build_corpus.ps1:
  artifacts/shaders/raw/<hash>.<vs|ps>.bin    original containers
  artifacts/shaders/dxil/<hash>.<vs|ps>.dxil  DXIL (default variant)

The output holds data derived from the game: it is written under artifacts/
or next to the executable, never committed or embedded.

usage:
  python tools/shaders/make_preshaders.py [--corpus artifacts/shaders] [--out FILE] [--install DIR]
  python tools/shaders/make_preshaders.py --verify FILE
  python tools/shaders/make_preshaders.py --diagnose logs/native_shaders/unmatched

Format (little-endian), reader in shader_library.h:
  char[8] "SRSHLIB\\0", u32 version (1), u32 count, u64 FNV-1a 64 of the body
  count x { u64 container_hash, u32 stage (0 = vs, 1 = ps), u32 container_size,
            u32 dxil_size, u32 reserved (0), container bytes, dxil bytes }
  sorted by (container_hash, stage)
"""
import argparse
import re
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from srpaths import ARTIFACTS  # noqa: E402

MAGIC = b"SRSHLIB\0"
VERSION = 1
LIBRARY_NAME = "superman_returns_shaders.srsl"
MIN_CONTAINER = 24   # ShaderLibrary::kMinContainer
MAX_CONTAINER = 1 << 20
NAME = re.compile(r"([0-9A-F]{16})\.(vs|ps)\.bin")

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK64 = (1 << 64) - 1


def fnv1a64(data: bytes, h: int = FNV_OFFSET) -> int:
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK64
    return h


def parse_header(blob: bytes):
    """(is_vertex, virtual_size, physical_size) or None, like
    ParseShaderContainerHeader in shader_container.h."""
    if len(blob) < 12:
        return None
    flags, vsize, psize = struct.unpack_from(">3I", blob)
    if flags & 0xFFFFFF00 != 0x102A1100 or not vsize or not psize:
        return None
    if vsize + psize > MAX_CONTAINER or vsize + psize > len(blob):
        return None
    return bool(flags & 1), vsize, psize


def collect(corpus: Path, check_hash: bool):
    raw, dxil = corpus / "raw", corpus / "dxil"
    if not raw.is_dir():
        raise SystemExit(f"[preshaders] no containers in {raw}: run tools/shaders/build_corpus.ps1 first")
    xxh = None
    if check_hash:
        try:
            import xxhash
            xxh = xxhash.xxh3_64_intdigest
        except ImportError:
            print("[preshaders] xxhash missing (pip install xxhash): container hashes not checked")
    entries, missing, skipped = [], [], []
    for p in sorted(raw.glob("*.bin")):
        m = NAME.fullmatch(p.name)
        if not m:
            continue
        h, stage = int(m.group(1), 16), m.group(2)
        container = p.read_bytes()
        hdr = parse_header(container)
        if not hdr or hdr[1] + hdr[2] != len(container) or hdr[0] != (stage == "vs") \
                or len(container) < MIN_CONTAINER:
            skipped.append(p.name)
            continue
        if xxh and xxh(container) != h:
            raise SystemExit(f"[preshaders] {p.name}: XXH3 of the file is {xxh(container):016X}")
        d = dxil / f"{m.group(1)}.{stage}.dxil"
        if not d.exists():
            missing.append(p.name)
            continue
        entries.append((h, 0 if stage == "vs" else 1, container, d.read_bytes()))
    return entries, missing, skipped


def serialize(entries) -> bytes:
    entries = sorted(entries, key=lambda e: (e[0], e[1]))
    body = bytearray()
    for h, stage, container, dx in entries:
        body += struct.pack("<QIIII", h, stage, len(container), len(dx), 0)
        body += container
        body += dx
    return MAGIC + struct.pack("<IIQ", VERSION, len(entries), fnv1a64(body)) + bytes(body)


def read_library(path: Path):
    data = path.read_bytes()
    if data[:8] != MAGIC:
        raise SystemExit(f"[preshaders] {path}: not a shader library")
    version, count, checksum = struct.unpack_from("<IIQ", data, 8)
    if version != VERSION:
        raise SystemExit(f"[preshaders] {path}: version {version}, expected {VERSION}")
    if fnv1a64(data[24:]) != checksum:
        raise SystemExit(f"[preshaders] {path}: checksum mismatch")
    pos, entries = 24, []
    for _ in range(count):
        h, stage, csize, dsize, _res = struct.unpack_from("<QIIII", data, pos)
        pos += 24
        container = data[pos:pos + csize]
        pos += csize
        dx = data[pos:pos + dsize]
        pos += dsize
        entries.append((h, stage, container, dx))
    if pos != len(data):
        raise SystemExit(f"[preshaders] {path}: trailing data")
    return entries


def diagnose(dump_dir: Path, corpus: Path):
    """Explain why dumped guest containers (sr_native_dump_shader_dir/unmatched)
    were not recognised: closest original and the words that differ."""
    originals = []
    for p in sorted((corpus / "raw").glob("*.bin")):
        if NAME.fullmatch(p.name):
            originals.append((p.name, p.read_bytes()))
    if not originals:
        raise SystemExit(f"[preshaders] no originals in {corpus / 'raw'}")
    dumps = sorted(dump_dir.glob("*.bin"))
    if not dumps:
        raise SystemExit(f"[preshaders] no dumps in {dump_dir}")
    for d in dumps:
        g = d.read_bytes()
        best = None
        for name, o in originals:
            n = min(len(o), len(g))
            same = sum(1 for i in range(0, n - 3, 4) if o[i:i + 4] == g[i:i + 4])
            # The microcode (physical part) is what Direct3D must keep.
            hdr = parse_header(o)
            ucode = o[hdr[1]:] if hdr else b""
            ucode_at = g.find(ucode) if ucode else -1
            score = (ucode_at >= 0, same)
            if not best or score > best[0]:
                best = (score, name, o, ucode_at)
        (ucode_found, same), name, o, ucode_at = best
        n = min(len(o), len(g))
        diff = [i for i in range(0, n - 3, 4) if o[i:i + 4] != g[i:i + 4]]
        print(f"{d.name}: closest {name}, {same}/{n // 4} words equal, "
              f"microcode {'at +' + hex(ucode_at) if ucode_found else 'not found'}")
        for i in diff[:12]:
            print(f"    +{i:04X}: original {o[i:i + 4].hex().upper()}  guest {g[i:i + 4].hex().upper()}")
        if len(diff) > 12:
            print(f"    ... {len(diff) - 12} more differing words")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", default=str(ARTIFACTS), help="artifacts/shaders (raw/ and dxil/)")
    ap.add_argument("--out", default=None, help=f"output file (default: <corpus>/{LIBRARY_NAME})")
    ap.add_argument("--install", action="append", default=[],
                    help="also copy the library into this directory (next to superman_returns.exe)")
    ap.add_argument("--no-hash-check", action="store_true", help="do not re-hash the containers")
    ap.add_argument("--verify", metavar="FILE", help="check an existing library and list it")
    ap.add_argument("--diagnose", metavar="DIR", help="compare unmatched guest dumps with the originals")
    args = ap.parse_args()
    corpus = Path(args.corpus)

    if args.verify:
        entries = read_library(Path(args.verify))
        vs = sum(1 for e in entries if e[1] == 0)
        print(f"[preshaders] {args.verify}: {len(entries)} shaders ({vs} vs, {len(entries) - vs} ps), OK")
        return 0
    if args.diagnose:
        diagnose(Path(args.diagnose), corpus)
        return 0

    entries, missing, skipped = collect(corpus, not args.no_hash_check)
    if not entries:
        raise SystemExit("[preshaders] no container has DXIL: run build_corpus.ps1 (build_catalog.py) first")
    out = Path(args.out) if args.out else corpus / LIBRARY_NAME
    data = serialize(entries)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_name(out.name + ".tmp")
    tmp.write_bytes(data)
    tmp.replace(out)
    # Read back what was written: the game rejects a file that does not parse.
    assert len(read_library(out)) == len(entries)
    vs = sum(1 for e in entries if e[1] == 0)
    print(f"[preshaders] {len(entries)} shaders ({vs} vs, {len(entries) - vs} ps), "
          f"{len(data)} bytes -> {out}")
    if missing:
        print(f"[preshaders] {len(missing)} containers without DXIL (translation failed), "
              f"e.g. {', '.join(missing[:5])}")
    if skipped:
        print(f"[preshaders] {len(skipped)} files skipped (not whole containers): {', '.join(skipped[:5])}")
    for d in args.install:
        dest = Path(d) / LIBRARY_NAME
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(out, dest)
        print(f"[preshaders] installed {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
