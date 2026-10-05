"""Inspect user-owned Lossless.dll shader resources without loading its code.

Resource selection follows NetherSX2_nx's LSFG-VK shader_registry.cpp.
Only metadata is written: no DLL or proprietary shader is copied into the repo.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def read_rcdata(data: bytes) -> dict[int, bytes]:
    """Read numeric RT_RCDATA resources from a PE32+ file, with bounds checks."""
    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError("PE structure extends beyond the file")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("Not a PE file (missing MZ)")
    pe = unpack("<I", 60)[0]
    if unpack("<4s", pe)[0] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    sections = unpack("<H", pe + 6)[0]
    optional_size = unpack("<H", pe + 20)[0]
    optional = pe + 24
    if optional_size < 136 or unpack("<H", optional)[0] != 0x20B:
        raise ValueError("Expected PE32+ with a resource data directory")
    if unpack("<I", optional + 108)[0] < 3:
        raise ValueError("Missing resource data directory")
    resource_rva, resource_size = unpack("<II", optional + 128)
    if not resource_rva or resource_size < 16:
        raise ValueError("Missing PE resources")
    table = optional + optional_size
    ranges = []
    for index in range(sections):
        _, rva, raw_size, raw = unpack("<IIII", table + 40 * index + 8)
        ranges.append((rva, raw_size, raw))

    def file_offset(rva, size):
        for start, raw_size, raw in ranges:
            if start <= rva and rva + size <= start + raw_size:
                offset = raw + rva - start
                if offset + size <= len(data):
                    return offset
        raise ValueError("Resource RVA is outside file-backed section data")

    base = file_offset(resource_rva, resource_size)

    def resource_offset(relative, size):
        if relative < 0 or relative + size > resource_size:
            raise ValueError("Resource structure extends beyond resource directory")
        return base + relative

    def entries(relative):
        offset = resource_offset(relative, 16)
        named, numeric = unpack("<HH", offset + 12)
        resource_offset(relative + 16, (named + numeric) * 8)
        return [unpack("<II", offset + 16 + i * 8)
                for i in range(named + numeric)]

    def directory(target):
        if not target & 0x80000000:
            raise ValueError("Expected a resource subdirectory")
        return target & 0x7FFFFFFF

    rcdata = next((target for ident, target in entries(0) if ident == 10), None)
    if rcdata is None:
        raise ValueError("No RT_RCDATA resources")
    resources = {}
    for ident, target in entries(directory(rcdata)):
        if ident & 0x80000000:
            continue  # The reference backend selects numeric IDs only.
        languages = entries(directory(target))
        if not languages:
            raise ValueError(f"Resource {ident} has no language entry")
        payloads = []
        for _, leaf in languages:
            if leaf & 0x80000000:
                raise ValueError("Expected a resource data entry")
            rva, size = unpack("<II", resource_offset(leaf, 16))
            offset = file_offset(rva, size)
            payloads.append(data[offset:offset + size])
        if any(payload != payloads[0] for payload in payloads):
            raise ValueError(f"Resource {ident} differs between languages")
        resources[ident] = payloads[0]
    return resources


def is_compute_spirv(blob: bytes) -> bool:
    """Check container structure and a compute entry point, not GPU validity."""
    if len(blob) < 20 or len(blob) % 4:
        return False
    words = struct.unpack(f"<{len(blob) // 4}I", blob)
    if words[0] != 0x07230203 or words[4] != 0:
        return False
    compute = False
    offset = 5
    while offset < len(words):
        count, opcode = words[offset] >> 16, words[offset] & 0xFFFF
        if not count or offset + count > len(words):
            return False
        if opcode == 15 and count >= 4 and words[offset + 1] == 5:
            compute = True
        offset += count
    return compute


def inspect(path: Path) -> dict:
    data = path.read_bytes()
    resources = read_rcdata(data)
    variants = {}
    for precision, precision_offset in (("fp16", 0), ("fp32", 49)):
        for mode, mode_offset in (("quality", 0), ("performance", 23)):
            # Generate is shared between quality and performance modes.
            ids = [49 + 256 + precision_offset] + [
                49 + shader + precision_offset + mode_offset
                for shader in range(257, 279)]
            missing = [ident for ident in ids if ident not in resources]
            invalid = [ident for ident in ids if ident in resources
                       and not is_compute_spirv(resources[ident])]
            variants[f"{precision}_{mode}"] = {
                "resource_ids": ids, "missing": missing,
                "invalid_compute_spirv": invalid,
                "resource_layout_matches": not missing and not invalid,
            }
    return {
        "dll": str(path.resolve()), "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "rcdata_count": len(resources),
        "compute_spirv_count": sum(map(is_compute_spirv, resources.values())),
        "variants": variants,
        "runtime_validated": False,
        "reference": "NaGaa95/NetherSX2_nx@f084dc1038c8c67cdff0fa9ad109bce3efe58f22",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = inspect(args.dll)
    except (OSError, ValueError) as error:
        parser.exit(1, f"DLL inspection failed: {error}\n")
    result = json.dumps(report, indent=2) + "\n"
    if args.output:
        if args.output.resolve() == args.dll.resolve():
            parser.exit(1, "Output must not overwrite the DLL\n")
        args.output.write_text(result, encoding="utf-8")
    print(result, end="")
    return 0 if any(v["resource_layout_matches"] for v in report["variants"].values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
