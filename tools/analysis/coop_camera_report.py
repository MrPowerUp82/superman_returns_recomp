"""Check sampled camera handoffs in the latest isolated probe run.

This verifies data propagation for one camera, not split-screen or scheduling.
Only uses local logs; no game image or extracted assets are needed.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re


def vector(value: str, count: int) -> list[float]:
    result = [float(x) for x in value.split(",")]
    if len(result) != count or not all(math.isfinite(x) for x in result):
        raise ValueError("invalid/non-finite camera vector")
    return result


def analyze(lines, required_targets=(), tolerance=0.001, required_secondary=False) -> dict:
    rows = {"update": {}, "submit": {}, "consume": {}, "apply": {}}
    errors = []
    started = False
    render_frame = None
    render_groups = []
    techniques = []
    secondary = []
    for line in lines:
        if "coop: single-instance input preparation;" in line:
            rows = {key: {} for key in rows}
            errors = []
            started = True
            render_frame = None
            render_groups = []
            techniques = []
            secondary = []
        if not started:
            continue
        if "Unhandled guest access violation" in line or re.search(r"coop Lua: status=[1-9]", line):
            errors.append("guest/script failure in latest run")
        if "coop second camera:" in line:
            values = dict(re.findall(r"(\w+)=([^\s]+)", line))
            try:
                owner, primary = int(values["owner"], 16), int(values["primary"], 16)
                target, primary_target = int(values["target"]), int(values["primary_target"])
                if not owner or not primary or owner == primary or target in (0, 255) or target == primary_target:
                    raise ValueError("second camera aliases primary camera/target")
                if values["singleton_unchanged"] != "true" or values["finite"] != "true":
                    raise ValueError("second camera changed singleton or produced invalid data")
                position = vector(values["position"], 3)
                primary_position = vector(values["primary_position"], 3)
                secondary.append({"owner": owner, "target": target,
                                  "separation": math.dist(position, primary_position)})
            except (KeyError, ValueError) as error:
                errors.append(f"second camera: {error}")
        if "coop camera render frame:" in line:
            render_frame = dict(re.findall(r"(\w+)=([^\s]+)", line))
            render_frame["sampled_groups"] = list(render_groups)
            render_frame["sampled_techniques"] = list(techniques)
            render_groups.clear()
            techniques.clear()
        elif "coop camera render group:" in line:
            render_groups.append(dict(re.findall(r"(\w+)=([^\s]+)", line)))
        elif "coop camera technique:" in line:
            techniques.append(dict(re.findall(r"(\w+)=([^\s]+)", line)))
        match = re.search(r"coop camera (update|packet|apply): (.*)", line)
        if not match:
            continue
        values = dict(re.findall(r"(\w+)=([^\s]+)", match[2]))
        kind = values.get("stage") if match[1] == "packet" else match[1]
        if kind not in rows:
            errors.append("unrecognized camera stage")
            continue
        try:
            sequence = int(values["seq"])
            if values["readable"] != "true":
                raise ValueError("unreadable camera snapshot")
            if kind in ("submit", "consume"):
                if values["finite"] != "true":
                    raise ValueError("non-finite camera packet")
                transform = vector(values["transform"], 16)
                values["position_values"] = transform[12:15]
                projection = vector(values["projection"], 4)
                if not (0 < projection[0] < projection[1] and 0 < projection[2] < math.pi and projection[3] > 0):
                    raise ValueError("invalid camera projection")
                if values["viewport"] != "00000000,050002D0":
                    raise ValueError("unexpected viewport; this probe checks the original 1280x720 view")
            else:
                values["position_values"] = vector(values["position"], 3)
                if kind == "update":
                    values["target"] = int(values["target"])
            rows[kind][sequence] = values
        except (KeyError, ValueError) as error:
            errors.append(f"{kind}: {error}")

    if not started:
        errors.append("probe startup marker missing")
    targets = sorted({row["target"] for row in rows["update"].values()})
    verified_targets = set()

    def compare(left, right):
        distances = []
        for sequence in rows[left].keys() & rows[right].keys():
            a, b = rows[left][sequence], rows[right][sequence]
            # The consumer and apply observers count separate render phases.
            # Pair only the same sampled ordinal AND presentation interval/view.
            if left == "consume" and (a.get("present") != b.get("present") or b.get("view") != "0"):
                continue
            distance = math.dist(a["position_values"], b["position_values"])
            distances.append(distance)
            if distance > tolerance:
                errors.append(f"{left}->{right} seq {sequence}: position delta {distance}")
            elif left == "update":
                verified_targets.add(a["target"])
        if not distances:
            errors.append(f"no matched {left}->{right} samples")
        return {"pairs": len(distances), "max_position_delta": max(distances, default=None)}

    producer = compare("update", "submit")
    consumer = compare("consume", "apply")
    for target in required_targets:
        if target not in verified_targets:
            errors.append(f"camera target {target} has no matching submission")
    if required_secondary and len(secondary) < 3:
        errors.append("fewer than three valid second-camera samples")
    return {"passed": not errors, "scope": "sampled single-camera propagation",
            "targets": targets, "verified_submission_targets": sorted(verified_targets),
            "samples": {k: len(v) for k, v in rows.items()},
            "update_to_submit": producer, "consume_to_apply": consumer,
            "last_render_frame": render_frame,
            "second_camera": {"samples": len(secondary),
                              "owners": sorted({row["owner"] for row in secondary}),
                              "targets": sorted({row["target"] for row in secondary}),
                              "max_separation": max((row["separation"] for row in secondary), default=None)},
            "errors": sorted(set(errors))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, default=Path("artifacts/coop-test/probe.log"))
    parser.add_argument("--output", type=Path, default=Path("artifacts/coop-test/camera-check.json"))
    parser.add_argument("--require-target", type=int, action="append", default=[])
    parser.add_argument("--require-secondary", action="store_true")
    args = parser.parse_args()
    # spdlog names older segments probe.1.log, probe.2.log, ... . Read oldest
    # first so the latest startup can reset both previous-run errors and samples.
    rotations = []
    pattern = re.compile(re.escape(args.log.stem) + r"\.(\d+)" + re.escape(args.log.suffix))
    for candidate in args.log.parent.iterdir():
        match = pattern.fullmatch(candidate.name)
        if match:
            rotations.append((int(match[1]), candidate))
    paths = [path for _, path in sorted(rotations, reverse=True)] + [args.log]

    def lines():
        for path in paths:
            with path.open(encoding="utf-8", errors="replace") as source:
                yield from source

    report = analyze(lines(), args.require_target, required_secondary=args.require_secondary)
    report["input_files"] = [str(path) for path in paths]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
