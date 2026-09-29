#!/usr/bin/env python3
"""Compare the native renderer's output with the Xenos output of the same
guest swap (A/B mode, docs/native-port-plan.md section 8, step 11).

Run the game with
  --sr_renderer=native --sr_native_ab_mode=true
  --sr_native_ab_swaps=300,900,... --sr_native_dump_dir=<dir>
Each listed swap then leaves, in <dir>:
  s<swap>_output_1280x720.raw   native renderer output (native_renderer.cpp)
  s<swap>_xenos_output.raw      Xenos output (sr_graphics_system.cpp)
Both start with u32 width, height, DXGI format, row pitch (little-endian),
then the rows.

For every pair this prints the PSNR over 8-bit RGB (the kit reported
47-58 dB for Conan; its acceptance bar was >= 40 dB) and, with --ppm, writes
both images and an amplified difference as PPM files next to them.

usage: python tools/native_ab_compare.py <dir> [--ppm] [--min-psnr 40]
Exit code 1 when a pair is below --min-psnr or a file cannot be read.
"""
import argparse
import math
import re
import struct
import sys
from pathlib import Path

# DXGI formats the dumps use.
DXGI_R10G10B10A2_UNORM = 24
DXGI_R8G8B8A8_UNORM = 28
DXGI_B8G8R8A8_UNORM = 87


def read_raw(path):
    """Returns (width, height, rgb bytes: 3 per pixel, 8 bits per channel)."""
    data = Path(path).read_bytes()
    if len(data) < 16:
        raise ValueError(f"{path}: too short")
    width, height, fmt, pitch = struct.unpack_from("<4I", data, 0)
    if width == 0 or height == 0 or pitch < width * 4:
        raise ValueError(f"{path}: bad header {width}x{height} pitch {pitch}")
    body = memoryview(data)[16:]
    if len(body) < pitch * (height - 1) + width * 4:
        raise ValueError(f"{path}: truncated")
    rgb = bytearray(width * height * 3)
    o = 0
    for y in range(height):
        row = body[y * pitch: y * pitch + width * 4]
        if fmt == DXGI_R8G8B8A8_UNORM:
            for x in range(width):
                rgb[o:o + 3] = row[4 * x: 4 * x + 3]
                o += 3
        elif fmt == DXGI_B8G8R8A8_UNORM:
            for x in range(width):
                rgb[o] = row[4 * x + 2]
                rgb[o + 1] = row[4 * x + 1]
                rgb[o + 2] = row[4 * x]
                o += 3
        elif fmt == DXGI_R10G10B10A2_UNORM:
            for (v,) in struct.iter_unpack("<I", row):
                rgb[o] = ((v & 0x3FF) * 255 + 511) // 1023
                rgb[o + 1] = (((v >> 10) & 0x3FF) * 255 + 511) // 1023
                rgb[o + 2] = (((v >> 20) & 0x3FF) * 255 + 511) // 1023
                o += 3
        else:
            raise ValueError(f"{path}: unsupported DXGI format {fmt}")
    return width, height, bytes(rgb)


def psnr(a, b):
    """PSNR in dB between two equally sized 8-bit buffers (inf when equal)."""
    if len(a) != len(b):
        raise ValueError("size mismatch")
    se = sum((x - y) * (x - y) for x, y in zip(a, b))
    if se == 0:
        return math.inf
    mse = se / len(a)
    return 10.0 * math.log10(255.0 * 255.0 / mse)


def write_ppm(path, width, height, rgb):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (width, height))
        f.write(rgb)


def pairs(directory):
    d = Path(directory)
    for native in sorted(d.glob("s*_output_*.raw")):
        m = re.match(r"s(\d+)_output_", native.name)
        if not m:
            continue
        xenos = d / f"s{m.group(1)}_xenos_output.raw"
        yield int(m.group(1)), native, xenos


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir")
    ap.add_argument("--ppm", action="store_true", help="write native/xenos/diff PPM images")
    ap.add_argument("--min-psnr", type=float, default=40.0)
    args = ap.parse_args(argv)
    failed = False
    found = False
    for swap, native_path, xenos_path in pairs(args.dir):
        found = True
        if not xenos_path.exists():
            print(f"swap {swap}: no Xenos image ({xenos_path.name})")
            failed = True
            continue
        try:
            nw, nh, n = read_raw(native_path)
            xw, xh, x = read_raw(xenos_path)
        except ValueError as e:
            print(f"swap {swap}: {e}")
            failed = True
            continue
        if (nw, nh) != (xw, xh):
            print(f"swap {swap}: size differs, native {nw}x{nh} vs xenos {xw}x{xh} "
                  "(compare at sr_native_render_scale=1)")
            failed = True
            continue
        value = psnr(n, x)
        ok = value >= args.min_psnr
        failed |= not ok
        print(f"swap {swap}: PSNR {value:.2f} dB {'ok' if ok else 'BELOW ' + str(args.min_psnr)}")
        if args.ppm:
            base = Path(args.dir) / f"s{swap:06d}"
            write_ppm(f"{base}_native.ppm", nw, nh, n)
            write_ppm(f"{base}_xenos.ppm", xw, xh, x)
            diff = bytes(min(255, abs(a - b) * 8) for a, b in zip(n, x))
            write_ppm(f"{base}_diff_x8.ppm", nw, nh, diff)
    if not found:
        print(f"no s<swap>_output_*.raw dumps in {args.dir}")
        return 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
