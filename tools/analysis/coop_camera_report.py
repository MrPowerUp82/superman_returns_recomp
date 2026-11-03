"""Check sampled camera handoffs in the latest isolated probe run.

This verifies primary propagation and optional secondary transport, not drawing.
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


def analyze(lines, required_targets=(), tolerance=0.001, required_secondary=False, required_handoff=False) -> dict:
    rows = {"update": {}, "submit": {}, "consume": {}, "apply": {}}
    errors = []
    started = False
    render_frame = None
    render_groups = []
    techniques = []
    secondary = []
    published = []
    received = []
    cleared = 0
    intervals = []
    for line in lines:
        if "coop: single-instance input preparation;" in line:
            rows = {key: {} for key in rows}
            errors = []
            started = True
            render_frame = None
            render_groups = []
            techniques = []
            secondary = []
            published = []
            received = []
            cleared = 0
            intervals = []
        if not started:
            continue
        if "Unhandled guest access violation" in line or re.search(r"coop Lua: status=[1-9]", line):
            errors.append("guest/script failure in latest run")
        if "coop camera transport failed:" in line or "coop camera transport refused:" in line:
            errors.append("camera transport write/readback failed or slot occupied")
        if "coop camera transport:" in line:
            values=dict(re.findall(r"(\w+)=([^\s]+)",line))
            try:
                if values["primary_unchanged"]!="true" or values["secondary_matches"]!="true":
                    raise ValueError("primary changed or secondary readback mismatched")
                version = int(values["generation"])
                if values["active"] not in ("true", "false"):
                    raise ValueError("invalid active state")
                if values["active"] == "false" and version:
                    raise ValueError("inactive view has nonzero generation")
                if not intervals or intervals[-1]["generation"] != version:
                    intervals.append({"generation": version, "published": [], "received": []})
                if values["active"] == "true":
                    if not version: raise ValueError("active view without generation")
                    row = {"generation": version, "position": vector(values["position"], 3)}
                    published.append(row)
                    intervals[-1]["published"].append(row)
            except (KeyError,ValueError) as error:
                errors.append(f"transport: {error}")
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
                if kind=="consume" and "secondary_flags" in values:
                    version=int(values["secondary_flags"],16)
                    if version:
                        position=vector(values["secondary_position"],3)
                        p=vector(values["secondary_projection"],4)
                        if not (0<p[0]<p[1] and 0<p[2]<math.pi and p[3]>0):
                            raise ValueError("invalid received secondary projection")
                        if values["secondary_viewport"]!="00000000,050002D0":
                            raise ValueError("invalid received secondary viewport")
                        row = {"generation": version, "position": position}
                        received.append(row)
                        if intervals and intervals[-1]["generation"] == version:
                            intervals[-1]["received"].append(row)
                    else:
                        cleared+=1
                        # Startup zeros cannot prove a later leave was consumed.
                        if intervals and intervals[-1]["generation"] == 0:
                            if (any(vector(values["secondary_position"], 3)) or
                                    any(vector(values["secondary_projection"], 4)) or
                                    values["secondary_viewport"] != "00000000,00000000"):
                                raise ValueError("received clear retains secondary camera data")
                            intervals[-1]["received"].append({"generation": 0})
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
    generations={row["generation"] for row in published}
    unknown={row["generation"] for row in received}-generations
    matching=sum(any(a["generation"]==b["generation"] and math.dist(a["position"],b["position"])<=0.05
                     for a in published) for b in received)
    lifecycle = []
    for interval in intervals:
        version = interval["generation"]
        matches = sum(any(math.dist(a["position"], b["position"]) <= 0.05
                          for a in interval["published"]) for b in interval["received"]) if version else 0
        lifecycle.append({"generation": version, "received_samples": len(interval["received"]),
                          "matching_sampled_positions": matches})
        if required_handoff:
            if not interval["received"]:
                errors.append(f"secondary lifecycle interval {len(lifecycle)} generation {version}: no matching consumption")
            elif version and not matches:
                errors.append(f"secondary lifecycle interval {len(lifecycle)} generation {version}: no matching position")
    if required_handoff:
        if len(published)<3 or len(received)<3:
            errors.append("fewer than three active publication/consumption samples")
        if unknown: errors.append("received secondary generation was never published")
        if not matching: errors.append("no received position matches a sampled publication")
    return {"passed": not errors, "scope": "sampled primary propagation and secondary transport",
            "targets": targets, "verified_submission_targets": sorted(verified_targets),
            "samples": {k: len(v) for k, v in rows.items()},
            "update_to_submit": producer, "consume_to_apply": consumer,
            "last_render_frame": render_frame,
            "second_camera": {"samples": len(secondary),
                              "owners": sorted({row["owner"] for row in secondary}),
                              "targets": sorted({row["target"] for row in secondary}),
                              "max_separation": max((row["separation"] for row in secondary), default=None)},
            "secondary_handoff": {"published_samples":len(published),"received_samples":len(received),
                                  "generations":sorted(generations),"unknown_generations":sorted(unknown),
                                  "matching_sampled_positions":matching,"inactive_received_samples":cleared,
                                  "lifecycle_intervals": lifecycle},
            "errors": sorted(set(errors))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, default=Path("artifacts/coop-test/probe.log"))
    parser.add_argument("--output", type=Path, default=Path("artifacts/coop-test/camera-check.json"))
    parser.add_argument("--require-target", type=int, action="append", default=[])
    parser.add_argument("--require-secondary", action="store_true")
    parser.add_argument("--require-handoff", action="store_true")
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

    report = analyze(lines(), args.require_target, required_secondary=args.require_secondary, required_handoff=args.require_handoff)
    report["input_files"] = [str(path) for path in paths]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
