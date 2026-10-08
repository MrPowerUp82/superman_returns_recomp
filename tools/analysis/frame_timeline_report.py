#!/usr/bin/env python3
"""Critical-path report for the timeline CSV written with SR_FRAME_TIMELINE=<file>.

Columns: frame,stage,begin_ns,end_ns,busy_ns,blocked_ns (port/src/graphics/frame_timeline.h).
frame is the guest swap number, shared by every stage. busy_ns excludes the time the stage
waited on another one; blocked_ns is that waiting time.
"""
import argparse
import csv
import json
import math
import sys
from collections import defaultdict

FRONT_END_PARTS = ("fe_begin", "fe_ring", "fe_device", "fe_index", "fe_streams", "fe_end")
STAGES = ("game", "game_guest", "game_other", "capture", "frontend", *FRONT_END_PARTS, "fe_flush",
          "frontend_other", "front_wait", "worker", "record", "gpu")
LIMITERS = ("game", "worker", "record", "gpu")  # stages that can bound the frame rate
REQUIRED = ("game", "worker")  # a frame without these has unusable ids or a lost stage


def percentile(values, p):
    """Nearest-rank percentile; 0.0 for an empty list."""
    if not values:
        return 0.0
    ordered = sorted(values)
    rank = max(1, math.ceil(p / 100.0 * len(ordered)))
    return float(ordered[rank - 1])


def load(path):
    frames = defaultdict(dict)
    with open(path, newline="") as handle:
        for row in csv.DictReader(handle):
            frames[int(row["frame"])][row["stage"]] = {
                key: int(row[key]) for key in ("begin_ns", "end_ns", "busy_ns", "blocked_ns")
            }
    return frames


def _summary(busy_ms, blocked_ms, mean_interval, budget_ms):
    p99 = percentile(busy_ms, 99)
    mean = sum(busy_ms) / len(busy_ms)
    return {
        "mean": mean,
        "p50": percentile(busy_ms, 50),
        "p99": p99,
        "blocked_mean": sum(blocked_ms) / len(blocked_ms),
        "samples": len(busy_ms),
        "util": mean / mean_interval if mean_interval else None,
        "headroom_p99_ms": budget_ms - p99,
    }


def analyze(frames, last=1100, budget_ms=1000.0 / 30.0):
    ids = sorted(frames)
    if last > 0:
        ids = ids[-last:]
    complete = [i for i in ids if all(stage in frames[i] for stage in REQUIRED)]
    result = {"frames": len(complete), "dropped": len(ids) - len(complete), "budget_ms": budget_ms,
              "interval": None, "stages": {}, "limiting_share": {}}
    if not complete:
        return result

    done = set(complete)
    intervals = [(frames[i + 1]["game"]["end_ns"] - frames[i]["game"]["end_ns"]) / 1e6
                 for i in complete if i + 1 in done]
    mean_interval = 0.0
    if intervals:
        mean_interval = sum(intervals) / len(intervals)
        p99 = percentile(intervals, 99)
        result["interval"] = {
            "samples": len(intervals),
            "mean_ms": mean_interval,
            "mean_fps": 1000.0 / mean_interval,
            "p99_ms": p99,
            "low1_fps": 1000.0 / p99,
            "min_fps": 1000.0 / max(intervals),
        }

    stages = {}
    for name in ("game", "capture", "front_wait", "worker", "record", "gpu", "frontend", *FRONT_END_PARTS, "fe_flush"):
        rows = [frames[i][name] for i in complete if name in frames[i]]
        if rows:
            stages[name] = _summary([r["busy_ns"] / 1e6 for r in rows], [r["blocked_ns"] / 1e6 for r in rows],
                                    mean_interval, budget_ms)
    with_capture = [i for i in complete if "capture" in frames[i]]
    if with_capture:  # the game thread's own work: game minus the capture hooks nested in it
        other = [(frames[i]["game"]["busy_ns"] - frames[i]["capture"]["busy_ns"]) / 1e6 for i in with_capture]
        stages["game_other"] = _summary(other, [0.0] * len(other), mean_interval, budget_ms)
    with_frontend = [i for i in complete if "frontend" in frames[i]]
    if with_frontend:  # game thread time outside the renderer hooks, and the hooks' unattributed rest
        guest = [max(0, frames[i]["game"]["busy_ns"] - frames[i]["frontend"]["busy_ns"]) / 1e6 for i in with_frontend]
        stages["game_guest"] = _summary(guest, [0.0] * len(guest), mean_interval, budget_ms)
        rest = [max(0, frames[i]["frontend"]["busy_ns"]
                    - sum(frames[i][part]["busy_ns"] for part in FRONT_END_PARTS if part in frames[i])) / 1e6
                for i in with_frontend]
        stages["frontend_other"] = _summary(rest, [0.0] * len(rest), mean_interval, budget_ms)
    result["stages"] = stages

    present = [name for name in LIMITERS if name in stages]
    counts = {name: 0 for name in present}
    for i in complete:
        busy = {name: frames[i][name]["busy_ns"] for name in present if name in frames[i]}
        counts[max(busy, key=busy.get)] += 1
    result["limiting_share"] = {name: count / len(complete) for name, count in counts.items()}
    return result


def render(result):
    lines = [f"Quadros analisados: {result['frames']} (descartados por falta de estágio: {result['dropped']})"]
    interval = result["interval"]
    if interval:
        lines.append(f"Intervalo médio {interval['mean_ms']:.1f} ms = {interval['mean_fps']:.1f} FPS"
                     f" | 1% low {interval['low1_fps']:.1f} FPS | mínimo {interval['min_fps']:.1f} FPS")
    lines.append(f"Orçamento por quadro: {result['budget_ms']:.1f} ms (tempos em ms)")
    lines.append("")
    lines.append(f"{'estágio':<12}{'média':>8}{'p50':>8}{'p99':>8}{'bloq.':>8}{'util.':>8}{'folga p99':>11}{'limitante':>11}")
    share = result["limiting_share"]
    for name in STAGES:
        stage = result["stages"].get(name)
        if not stage:
            continue
        util = f"{stage['util'] * 100:.0f}%" if stage["util"] is not None else "-"
        limiting = f"{share[name] * 100:.0f}%" if name in share else "-"
        lines.append(f"{name:<12}{stage['mean']:>8.2f}{stage['p50']:>8.2f}{stage['p99']:>8.2f}"
                     f"{stage['blocked_mean']:>8.2f}{util:>8}{stage['headroom_p99_ms']:>11.2f}{limiting:>11}")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv", help="file written with SR_FRAME_TIMELINE")
    parser.add_argument("--last", type=int, default=1100, help="analyze only the last N frames (0 = all)")
    parser.add_argument("--budget-ms", type=float, default=1000.0 / 30.0, help="frame budget (default 30 FPS)")
    parser.add_argument("--json", action="store_true", help="print the raw result as JSON")
    args = parser.parse_args(argv)
    result = analyze(load(args.csv), args.last, args.budget_ms)
    print(json.dumps(result, indent=2) if args.json else render(result))
    return 0 if result["frames"] else 1


if __name__ == "__main__":
    sys.exit(main())
