"""Check sampled camera handoffs in the latest isolated probe run.

This verifies primary propagation, secondary transport and optional draw-entry
camera selection. It does not prove final pixels or split-screen composition.
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


def analyze(lines, required_targets=(), tolerance=0.001, required_secondary=False, required_handoff=False,
            required_preview=False, required_split=False) -> dict:
    required_handoff = required_handoff or required_preview or required_split
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
    previews, preview_draws = {}, {}
    blocked_preview_generations = set()
    split_world, split_captures, split_composites = {}, {}, {}
    split_positions = []
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
            previews, preview_draws = {}, {}
            blocked_preview_generations = set()
            split_world, split_captures, split_composites = {}, {}, {}
            split_positions = []
        if not started:
            continue
        if re.search(r"coop split(?: capture)? failed:", line):
            errors.append("split world/capture verification failed")
        split = re.search(r"coop split (world|capture|composite): (.*)", line)
        if split:
            values = dict(re.findall(r"(\w+)=([^\s]+)", split[2]))
            try:
                frame, generation = int(values["frame"]), int(values["generation"])
                if not frame or not generation: raise ValueError("missing frame/binding")
                if split[1] == "world":
                    if any(values[key] != "true" for key in ("selected", "primary_restored", "hud_unchanged", "phase_restored")):
                        raise ValueError("camera/HUD selection failed")
                    if (values["passes"], values["extra_prepare"], values["extra_dt"]) != ("2", "1", "0"):
                        raise ValueError("unexpected render repetition")
                    split_positions.append({"generation": generation,
                                            "primary": vector(values["primary"], 3),
                                            "secondary": vector(values["secondary"], 3)})
                    split_world[frame] = generation
                elif split[1] == "capture":
                    view = int(values["view"])
                    if view not in (0, 1) or (values["width"], values["height"]) != ("1280", "720"):
                        raise ValueError("invalid capture view/extent")
                    if values["ready"] != ("true" if view else "false"):
                        raise ValueError("incomplete/out-of-order capture")
                    if (frame, view) in split_captures:
                        raise ValueError("duplicate view capture")
                    split_captures[frame, view] = generation
                else:
                    if values["views"] != "2": raise ValueError("not two views")
                    width, height = map(int, values["size"].split("x"))
                    if width<1280 or height<720 or width%2 or width*9!=height*16:
                        raise ValueError("unsupported composite extent")
                    split_composites[frame] = generation
            except (KeyError, ValueError) as error:
                errors.append(f"split: {error}")
        if "Unhandled guest access violation" in line or re.search(r"coop Lua: status=[1-9]", line):
            errors.append("guest/script failure in latest run")
        if "coop camera transport failed:" in line or "coop camera transport refused:" in line:
            errors.append("camera transport write/readback failed or slot occupied")
        if "coop camera preview failed:" in line or "coop camera preview refused:" in line:
            errors.append("camera preview rejected or failed verification")
        if "coop camera preview:" in line or "coop camera preview draw:" in line:
            values = dict(re.findall(r"(\w+)=([^\s]+)", line))
            drawing = "preview draw:" in line
            try:
                sequence, generation = int(values["seq"]), int(values["generation"])
                camera, hud = int(values["camera"], 16), int(values["hud"], 16)
                if not generation or not camera or not hud or camera == hud:
                    raise ValueError("missing generation or distinct world/HUD cameras")
                flags = ("matrix_matches", "selection_matches") if drawing else (
                    "matrix_matches", "selection_matches", "packet_unchanged", "hud_unchanged")
                if any(values[key] != "true" for key in flags):
                    raise ValueError("preview changed packet/HUD or matrix mismatched")
                row = {"generation": generation, "camera": camera, "hud": hud,
                       "position": vector(values["position"], 3)}
                if generation in blocked_preview_generations:
                    raise ValueError("preview reused a generation after its consumed clear")
                (preview_draws if drawing else previews)[sequence] = row
                if drawing:
                    interval = next((entry for entry in reversed(intervals) if entry["generation"] == generation), None)
                    if interval is not None:
                        interval["previews"].append(row)
                    elif required_handoff:
                        raise ValueError("preview generation has no publication interval")
            except (KeyError, ValueError) as error:
                errors.append(f"preview: {error}")
        if "coop camera preview restored:" in line:
            values = dict(re.findall(r"(\w+)=([^\s]+)", line))
            try:
                camera, hud = int(values["camera"], 16), int(values["hud"], 16)
                if (values["generation"] != "0" or not camera or not hud or camera == hud or
                        values["matrix_matches"] != "true" or values["selection_matches"] != "true"):
                    raise ValueError("primary return did not match the current world camera")
                if not intervals or intervals[-1]["generation"] != 0:
                    raise ValueError("primary return has no clear publication interval")
                intervals[-1]["restored"].append(vector(values["position"], 3))
            except (KeyError, ValueError) as error:
                errors.append(f"preview restored: {error}")
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
                    intervals.append({"generation": version, "published": [], "received": [],
                                      "previews": [], "restored": []})
                    blocked_preview_generations.discard(version)
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
                            intervals[-1]["received"].append({"generation": 0, "position": transform[12:15]})
                            blocked_preview_generations.update(entry["generation"] for entry in intervals
                                                               if entry["generation"])
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
    preview_position_matches = restored_position_matches = 0
    for interval in intervals:
        version = interval["generation"]
        matches = sum(any(math.dist(a["position"], b["position"]) <= 0.05
                          for a in interval["published"]) for b in interval["received"]) if version else 0
        lifecycle.append({"generation": version, "received_samples": len(interval["received"]),
                          "matching_sampled_positions": matches})
        preview_matches = sum(any(math.dist(a["position"], b["position"]) <= 0.05
                                  for a in interval["received"]) for b in interval["previews"])
        restored_matches = sum(any(math.dist(a["position"], b) <= 0.05
                                   for a in interval["received"]) for b in interval["restored"])
        preview_position_matches += preview_matches
        restored_position_matches += restored_matches
        if required_preview:
            if version and not preview_matches:
                errors.append(f"preview lifecycle interval {len(lifecycle)}: no position matches consumed P2")
            if not version and not restored_matches:
                errors.append(f"preview lifecycle interval {len(lifecycle)}: no verified return to consumed P1")
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
    preview_pairs = 0
    drawn_generations = set()
    for sequence, draw in preview_draws.items():
        applied = previews.get(sequence)
        if applied != draw:
            errors.append(f"preview draw {sequence}: no matching camera application")
        else:
            preview_pairs += 1
            drawn_generations.add(draw["generation"])
    if required_preview:
        if preview_pairs < 3 or len(previews) != preview_pairs:
            errors.append("fewer than three complete preview application/draw pairs")
        if generations - drawn_generations:
            errors.append("published generation has no verified preview draw")
    split_pairs = sum(split_world.get(frame) == version and
                      split_captures.get((frame, 0)) == version and
                      split_captures.get((frame, 1)) == version
                      for frame, version in split_composites.items())
    if required_split and (split_pairs < 3 or split_pairs != len(split_composites)):
        errors.append("fewer than three complete same-frame split world/capture/composite pairs")
    split_matches = set()
    for sample in split_positions:
        primary_matches = any(math.dist(sample["primary"], row["position_values"]) <= 0.05
                              for row in rows["consume"].values())
        secondary_matches = any(row["generation"] == sample["generation"] and
                                math.dist(sample["secondary"], row["position"]) <= 0.05 for row in received)
        if primary_matches and secondary_matches:
            split_matches.add(sample["generation"])
    if required_split and set(split_composites.values()) - split_matches:
        errors.append("split camera positions have no matching consumed P1/P2 samples")
    if required_split and generations - set(split_composites.values()):
        errors.append("published binding has no verified split composition")
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
            "preview": {"applied_samples": len(previews), "draw_samples": len(preview_draws),
                        "verified_pairs": preview_pairs, "generations": sorted(drawn_generations),
                        "consumed_position_matches": preview_position_matches,
                        "restored_position_matches": restored_position_matches},
            "split": {"world_samples": len(split_world), "composite_samples": len(split_composites),
                      "verified_pairs": split_pairs,
                      "matched_consumed_generations": sorted(split_matches),
                      "generations": sorted(set(split_composites.values()))},
            "errors": sorted(set(errors))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, default=Path("artifacts/coop-test/probe.log"))
    parser.add_argument("--output", type=Path, default=Path("artifacts/coop-test/camera-check.json"))
    parser.add_argument("--require-target", type=int, action="append", default=[])
    parser.add_argument("--require-secondary", action="store_true")
    parser.add_argument("--require-handoff", action="store_true")
    parser.add_argument("--require-preview", action="store_true")
    parser.add_argument("--require-split", action="store_true")
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

    report = analyze(lines(), args.require_target, required_secondary=args.require_secondary,
                     required_handoff=args.require_handoff, required_preview=args.require_preview,
                     required_split=args.require_split)
    report["input_files"] = [str(path) for path in paths]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
