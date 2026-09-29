"""Summarize draws captured by --sr_renderer=trace.

Usage:
    python tools/gpu_trace_report.py logs/gpu_trace.csv logs/gpu_passes.csv
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path


PASS_FIELDS = ("vs_hash", "ps_hash", "rb_mode", "rb_surface", "rb_color", "rb_depth")


def analyze(path: Path):
    frames: dict[int, Counter[str]] = defaultdict(Counter)
    passes: Counter[tuple[str, ...]] = Counter()
    accepted: Counter[str] = Counter()
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        required = {"frame", "kind", "accepted", *PASS_FIELDS}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("trace CSV is missing required columns; rebuild the trace renderer")
        for row in reader:
            frame = int(row["frame"])
            kind = row["kind"]
            frames[frame][kind] += 1
            if kind == "draw":
                accepted[row["accepted"]] += 1
                if row["accepted"] == "1":
                    passes[tuple(
                        row[field].upper() if field.endswith("_hash") else row[field]
                        for field in PASS_FIELDS
                    )] += 1
    return frames, passes, accepted


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace_csv", type=Path)
    parser.add_argument("passes_csv", type=Path)
    args = parser.parse_args()

    frames, passes, accepted = analyze(args.trace_csv)
    args.passes_csv.parent.mkdir(parents=True, exist_ok=True)
    with args.passes_csv.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(("draws", *PASS_FIELDS))
        for key, count in passes.most_common():
            writer.writerow((count, *key))

    total = Counter()
    for counts in frames.values():
        total.update(counts)
    print(f"frames={len(frames)} draws={total['draw']} skipped={total['skipped']} "
          f"copies={total['copy']} swaps={total['swap']}")
    print(f"draws accepted={accepted['1']} rejected={accepted['0']}")
    print(f"distinct accepted pass states={len(passes)} -> {args.passes_csv}")
    for key, count in passes.most_common(10):
        print(f"{count:6d}  VS {key[0]}  PS {key[1]}  mode {key[2]}  surface {key[3]}")


if __name__ == "__main__":
    main()
