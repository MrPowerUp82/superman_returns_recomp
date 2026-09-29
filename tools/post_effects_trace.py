#!/usr/bin/env python3
"""Check what sr_post_effects=false skipped in a raw trace of
--sr_renderer=trace (tools/capture_gpu_trace.ps1 ... -ExtraArgs
"--sr_post_effects=false"; tools/post_effects_check.ps1 -Trace does both).

Prints:
  * skipped passes and resolves per frame (min / max over the frames);
  * the skipped groups (kind, VS, PS, surface) with their counts;
  * skipped resolves whose RB_COPY_CONTROL also clears EDRAM color or depth
    (skipping them also drops that clear: suspect these first if the image
    breaks);
  * the order of one frame: every quad-list draw (primitive 13), resolve and
    skipped event, with the other draws folded into one line per run. The
    first 1280 quads after the skipped chain are the composite candidates.

usage: python tools/post_effects_trace.py logs/gpu_trace_<name>.csv [--frame N]
Exit code 1 when the trace has no skipped event (the filter matched nothing).
"""
from __future__ import annotations

import argparse
import csv
import sys
from collections import Counter
from pathlib import Path

QUAD_LIST = 13
COLOR_CLEAR = 1 << 8   # RB_COPY_CONTROL.color_clear_enable
DEPTH_CLEAR = 1 << 9   # RB_COPY_CONTROL.depth_clear_enable
REQUIRED = {"frame", "event", "kind", "primitive", "vs_hash", "ps_hash", "rb_mode",
            "rb_surface", "rb_copy"}


def read(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if not REQUIRED.issubset(reader.fieldnames or []):
            raise ValueError(f"{path}: not a raw trace of --sr_renderer=trace")
        return list(reader)


def pitch(surface: str) -> int:
    return int(surface) & 0x3FFF


def summarize(rows: list[dict[str, str]], frame: int | None) -> tuple[list[str], int]:
    out: list[str] = []
    frames = sorted({int(r["frame"]) for r in rows})
    if not frames:
        return ["empty trace"], 0
    per_frame = {f: Counter() for f in frames}
    groups: Counter[tuple[str, str, str, int]] = Counter()
    clears: Counter[tuple[str, int, str]] = Counter()
    for r in rows:
        kind = r["kind"]
        if kind not in ("skipped", "skipped_copy"):
            continue
        per_frame[int(r["frame"])][kind] += 1
        groups[(kind, r["vs_hash"].upper(), r["ps_hash"].upper(), pitch(r["rb_surface"]))] += 1
        copy = int(r["rb_copy"])
        if kind == "skipped_copy" and copy & (COLOR_CLEAR | DEPTH_CLEAR):
            what = "+".join(n for n, b in (("color", COLOR_CLEAR), ("depth", DEPTH_CLEAR))
                            if copy & b)
            clears[(r["ps_hash"].upper(), pitch(r["rb_surface"]), what)] += 1
    total = sum(sum(c.values()) for c in per_frame.values())
    out.append(f"frames {frames[0]}..{frames[-1]} ({len(frames)})")
    for kind in ("skipped", "skipped_copy"):
        counts = [per_frame[f][kind] for f in frames]
        out.append(f"{kind:13s} per frame: min {min(counts)} max {max(counts)} "
                   f"total {sum(counts)}")
    out.append("")
    out.append("skipped groups (kind, VS, PS, surface pitch): count")
    for (kind, vs, ps, p), n in sorted(groups.items()):
        out.append(f"  {kind:13s} {vs} {ps} {p:5d}: {n}")
    out.append("")
    if clears:
        out.append("WARNING: skipped resolves that also clear EDRAM (PS, pitch, clear): count")
        for (ps, p, what), n in sorted(clears.items()):
            out.append(f"  {ps} {p:5d} {what}: {n}")
    else:
        out.append("no skipped resolve clears EDRAM")
    out.append("")

    shown = frame if frame is not None else frames[len(frames) // 2]
    out.append(f"frame {shown} order (quads, resolves, skipped; other draws folded):")
    folded: Counter[int] = Counter()

    def flush() -> None:
        if folded:
            text = ", ".join(f"{n} on {p}" for p, n in sorted(folded.items()))
            out.append(f"      ... other draws: {text}")
            folded.clear()

    for r in rows:
        if int(r["frame"]) != shown:
            continue
        kind = r["kind"]
        prim = int(r["primitive"])
        mode = int(r["rb_mode"]) & 7
        if kind == "draw" and mode == 6:
            continue  # the resolve's own draw; its copy row is shown
        if kind == "draw" and prim != QUAD_LIST:
            folded[pitch(r["rb_surface"])] += 1
            continue
        flush()
        label = {"draw": "quad", "copy": "resolve", "skipped": "SKIP quad" if prim == QUAD_LIST
                 else "SKIP draw", "skipped_copy": "SKIP resolve", "swap": "swap"}.get(kind, kind)
        out.append(f"  {int(r['event']):6d} {label:13s} VS {r['vs_hash'].upper()} "
                   f"PS {r['ps_hash'].upper()} pitch {pitch(r['rb_surface'])} mode {mode}")
    flush()
    return out, total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("trace_csv", type=Path)
    parser.add_argument("--frame", type=int, help="frame to list (default: the middle one)")
    args = parser.parse_args()
    lines, total = summarize(read(args.trace_csv), args.frame)
    print("\n".join(lines))
    if total == 0:
        print("\nNo skipped event: sr_post_effects=false matched nothing in this trace.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
