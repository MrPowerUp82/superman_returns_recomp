"""Extract local script evidence from the owner's BGFA archives (no game assets in Git).

BGFA field layout: https://aluigi.altervista.org/bms/nbajamfire.bms
Usage: python tools/analysis/coop_ast_probe.py
"""
import argparse
import json
from pathlib import Path
import re
import struct
import zlib


def refpack(data):
    if data[:2] != b"\x10\xfb":
        raise ValueError("unsupported RefPack header")
    expected = int.from_bytes(data[2:5], "big")
    if expected > 256 * 1024 * 1024:
        raise ValueError("RefPack output too large")
    out = bytearray()
    pos = 5
    while pos < len(data):
        code = data[pos]
        pos += 1
        if code >= 0xFC:
            out.extend(data[pos:pos + (code & 3)])
            break
        if code >= 0xE0:
            literal = ((code & 31) + 1) * 4
            out.extend(data[pos:pos + literal])
            pos += literal
            continue
        if code >= 0xC0:
            a, b, c = data[pos:pos + 3]
            pos += 3
            literal = code & 3
            length = ((code & 12) << 6) + c + 5
            distance = ((code & 16) << 12) + (a << 8) + b + 1
        elif code >= 0x80:
            a, b = data[pos:pos + 2]
            pos += 2
            literal = a >> 6
            length = (code & 63) + 4
            distance = ((a & 63) << 8) + b + 1
        else:
            a = data[pos]
            pos += 1
            literal = code & 3
            length = ((code & 28) >> 2) + 3
            distance = ((code & 96) << 3) + a + 1
        out.extend(data[pos:pos + literal])
        pos += literal
        if distance > len(out) or len(out) + length > expected:
            raise ValueError("invalid RefPack backreference")
        for _ in range(length):
            out.append(out[-distance])
    if len(out) != expected:
        raise ValueError(f"RefPack size mismatch: {len(out)} != {expected}")
    return bytes(out)


def entries(data):
    if data[:8] != b"BGFA1.05":
        raise ValueError("unsupported BGFA header")
    count, offset = struct.unpack_from("<II", data, 12)
    _, flags16, hash_size, offset_size, packed_size, delta_size, shift, _ = data[32:40]
    prefix_count, name_size = struct.unpack_from("<II", data, 40)
    pos = offset + prefix_count * 4
    for _ in range(count):
        flag_size = 2 if flags16 else 1
        flags = int.from_bytes(data[pos:pos + flag_size], "little")
        pos += flag_size + hash_size
        values = []
        for width in (offset_size, packed_size, delta_size):
            values.append(int.from_bytes(data[pos:pos + width], "little"))
            pos += width
        name = data[pos:pos + name_size].split(b"\0", 1)[0].decode("ascii", "replace")
        pos += name_size
        start, size, delta = values
        start <<= shift
        if start + size > len(data):
            raise ValueError(f"entry outside archive: {name}")
        payload = data[start:start + size]
        if delta:
            payload = refpack(payload) if flags & 2 else zlib.decompress(payload)
            if len(payload) != size + delta:
                raise ValueError(f"BGFA size mismatch: {name}")
        yield name, payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path("game/DATA"))
    parser.add_argument("--out", type=Path, default=Path("artifacts/coop-research"))
    parser.add_argument("--archives", nargs="+", default=[
        "preload.AST", "baseworl.AST", "_workinp.AST", "lunits.AST", "metropol.AST", "objtiv.AST"])
    args = parser.parse_args()
    assets = args.out / "assets"
    assets.mkdir(parents=True, exist_ok=True)
    index = []

    def visit(data, archive, depth=0):
        if depth > 10:
            raise ValueError("archive recursion limit")
        for name, payload in entries(data):
            record = {"archive": archive, "name": name, "size": len(payload)}
            index.append(record)
            if payload.startswith(b"BGFA"):
                visit(payload, archive + "/" + name, depth + 1)
            elif payload.startswith(b"\x05\xf5\xe0\xff"):
                # Compiled XML keeps a string table; retain searchable evidence
                # without pretending the string order reconstructs its tree.
                strings = re.findall(rb"[ -~]{3,}", payload)
                if any(b"superman" in s.lower() or b"camera" in s.lower() for s in strings):
                    destination = assets / f"{len(index):06d}.strings.txt"
                    destination.write_text("\n".join(s.decode("ascii") for s in strings), encoding="utf-8")
                    record["strings_output"] = str(destination)
            elif b"\0" not in payload[:256] and payload:
                # XML/Lua/text only. Binary assets stay in the original archive.
                try:
                    text = payload.decode("utf-8")
                except UnicodeDecodeError:
                    continue
                destination = assets / f"{len(index):06d}.txt"
                destination.write_text(text, encoding="utf-8")
                record["output"] = str(destination)

    for name in args.archives:
        visit((args.game / name).read_bytes(), name)
    (args.out / "assets-index.json").write_text(json.dumps(index, indent=2), encoding="utf-8")
    print(f"Indexed {len(index)} entries; extracted {sum('output' in x for x in index)} text files into {assets}")


if __name__ == "__main__":
    main()
