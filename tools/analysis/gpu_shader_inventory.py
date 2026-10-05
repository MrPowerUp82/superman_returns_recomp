"""Inventory Xenos shader dumps without copying game data into the repository.

Generate dumps with --dump_shaders=<logs/shaders>, then run:
    python tools/analysis/gpu_shader_inventory.py logs/shaders logs/shader_inventory.csv
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


UCODE_NAME = re.compile(r"^shader_([0-9A-Fa-f]{16})\.ucode\.(vert|frag)$")


def inventory(directory: Path) -> list[dict[str, str | int]]:
    rows: list[dict[str, str | int]] = []
    for source in directory.rglob("shader_*.ucode.*"):
        match = UCODE_NAME.fullmatch(source.name)
        if not match:
            continue
        shader_hash, stage = match.groups()
        binary = source.with_name(f"shader_{shader_hash}.ucode.bin.{stage}")
        prefix = f"shader_{shader_hash}_"
        host_variants = sum(
            1
            for path in source.parent.glob(f"{prefix}*.{stage}")
            if path.is_file() and ".ucode." not in path.name
        )
        rows.append(
            {
                "stage": "vertex" if stage == "vert" else "pixel",
                "hash": shader_hash.upper(),
                "ucode_bytes": binary.stat().st_size if binary.is_file() else 0,
                "disassembly_bytes": source.stat().st_size,
                "host_variants": host_variants,
            }
        )
    rows.sort(key=lambda row: (str(row["stage"]), str(row["hash"])))
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump_dir", type=Path)
    parser.add_argument("output_csv", type=Path)
    args = parser.parse_args()
    if not args.dump_dir.is_dir():
        parser.error(f"shader dump directory does not exist: {args.dump_dir}")

    rows = inventory(args.dump_dir)
    args.output_csv.parent.mkdir(parents=True, exist_ok=True)
    with args.output_csv.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(
            output,
            fieldnames=("stage", "hash", "ucode_bytes", "disassembly_bytes", "host_variants"),
        )
        writer.writeheader()
        writer.writerows(rows)
    vertex = sum(row["stage"] == "vertex" for row in rows)
    pixel = sum(row["stage"] == "pixel" for row in rows)
    print(f"{len(rows)} shaders ({vertex} vertex, {pixel} pixel) -> {args.output_csv}")


if __name__ == "__main__":
    main()
