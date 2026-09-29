"""Summarize geometry and constant reuse in a --sr_gpu_pass_probe_path CSV.

The batching figures are counts of opportunities, not a claim that reordering
or instancing is visually valid for the selected pass.
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from itertools import groupby
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe_csv", type=Path)
    args = parser.parse_args()

    with args.probe_csv.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise SystemExit("no matching draws were captured")

    def count(field: str) -> int:
        return len({row[field] for row in rows})

    geometry = lambda row: (
        row["index_base"], row["index_length"], row["vf0_addr"], row["vf0_size"]
    )
    mesh_keys = [geometry(row) for row in rows]
    ordered_runs = sum(1 for _ in groupby(mesh_keys))
    per_material_mesh = {(row["c4_10_hash"], geometry(row)) for row in rows}
    print(f"draws: {len(rows)} in {count('frame')} guest frame(s)")
    print(f"vertex buffers: {count('vf0_addr')}")
    print(f"index ranges: {count('index_base')}")
    print(f"geometry keys: {len(set(mesh_keys))}")
    print(f"object constant sets (c0-c3): {count('c0_3_hash')}")
    print(f"other constant sets (c4-c10): {count('c4_10_hash')}")
    for field in ("rb_color_info", "rb_depth_info", "rb_blend0", "rb_depth_control"):
        if field in rows[0]:
            print(f"{field}: {count(field)} state(s)")
    print(f"consecutive geometry runs: {ordered_runs} ({len(rows) - ordered_runs} repeated calls)")
    print(f"material/geometry groups if reordering were valid: {len(per_material_mesh)}")
    print("top index ranges:")
    for address, occurrences in Counter(row["index_base"] for row in rows).most_common(8):
        print(f"  {address}: {occurrences} draws")


if __name__ == "__main__":
    main()
