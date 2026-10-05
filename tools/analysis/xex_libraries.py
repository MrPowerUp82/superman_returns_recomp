#!/usr/bin/env python3
"""List the static libraries (and so the XDK revision) an Xbox 360 XEX was
linked with, from its XEX2 header. The header is not encrypted, so this works
on the original default.xex without decoding the executable.

The native renderer's hook table and D3DDevice layout come from Conan, built
with XDK 2.0.5632 (rexglue-native-kit README). If Superman Returns' D3D9
library has the same version, the device offsets likely match; if not, expect
differences (docs/native-port-plan.md section 4).

usage: python tools/analysis/xex_libraries.py game/default.xex [--json]

XEX2 layout (Xenia xex2_info.h): 'XEX2', u32 module flags, u32 PE offset,
u32 reserved, u32 security info offset, u32 optional header count, then
{u32 key, u32 value} pairs. Key 0x000200FF (static libraries): value = file
offset of {u32 size, entries}; entry = char name[8], u16 major, u16 minor,
u16 build, u8 approval type, u8 qfe. All big-endian.
"""
import argparse
import json
import struct
import sys

XEX2_MAGIC = b"XEX2"
KEY_STATIC_LIBRARIES = 0x000200FF
APPROVAL = {0: "unapproved", 1: "possible", 2: "approved", 3: "expired"}


def parse_static_libraries(data: bytes):
    """Returns [{name, version, approval}] or raises ValueError."""
    if len(data) < 24 or data[:4] != XEX2_MAGIC:
        raise ValueError("not an XEX2 file")
    count = struct.unpack_from(">I", data, 0x14)[0]
    if 24 + 8 * count > len(data):
        raise ValueError("truncated optional header table")
    for i in range(count):
        key, value = struct.unpack_from(">II", data, 24 + 8 * i)
        if key != KEY_STATIC_LIBRARIES:
            continue
        if value + 4 > len(data):
            raise ValueError("static library table outside the file")
        size = struct.unpack_from(">I", data, value)[0]
        if size < 4 or value + size > len(data):
            raise ValueError("bad static library table size")
        libs = []
        for off in range(value + 4, value + size - 15, 16):
            name, major, minor, build, approval, qfe = struct.unpack_from(">8sHHHBB", data, off)
            libs.append({
                "name": name.rstrip(b"\0").decode("ascii", "replace"),
                "version": f"{major}.{minor}.{build}.{qfe}",
                "approval": APPROVAL.get(approval & 3, str(approval)),
            })
        return libs
    return []


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("xex")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    with open(args.xex, "rb") as f:
        head = f.read(1 << 20)  # headers live in the first pages
    try:
        libs = parse_static_libraries(head)
    except ValueError as e:
        print(f"{args.xex}: {e}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(libs, indent=1))
    else:
        if not libs:
            print("no static library header")
        for lib in libs:
            print(f"{lib['name']:<8} {lib['version']:<14} {lib['approval']}")
        print("reference: Conan (rexglue-native-kit) was built with XDK 2.0.5632")
    return 0


if __name__ == "__main__":
    sys.exit(main())
