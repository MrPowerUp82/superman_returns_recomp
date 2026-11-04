"""Check sampled camera/radar handoffs in the latest isolated probe run.

This verifies primary propagation, secondary transport and optional draw-entry,
radar and split capture/composition marker correlation. It does not prove final
pixels, complete HUD ownership or performance.
Only uses local logs; no game image or extracted assets are needed.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import struct


def vector(value: str, count: int) -> list[float]:
    result = [float(x) for x in value.split(",")]
    if len(result) != count or not all(math.isfinite(x) for x in result):
        raise ValueError("invalid/non-finite camera vector")
    return result


def stamina(target, current, maximum, scaled):
    target, current, maximum, scaled = map(int, (target, current, maximum, scaled))
    if not 0<target<=0xFFFFFFFF or target==255 or current<0 or maximum<=0 or current>maximum or maximum>0x7FFFFFFF:
        raise ValueError("invalid actor stamina")
    f32 = lambda value: struct.unpack(">f", struct.pack(">f", value))[0]
    expected = int(f32(f32(f32(current)/f32(max(maximum,500)))*10000))
    if scaled!=expected: raise ValueError("incorrect native stamina scale")
    return {"target": target, "current": current, "maximum": maximum, "scaled": scaled}


def analyze(lines, required_targets=(), tolerance=0.001, required_secondary=False, required_handoff=False,
            required_preview=False, required_split=False, required_radar=False, required_status=False) -> dict:
    required_split = required_split or required_radar or required_status
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
    radar_producers, radar_draws = [], {}
    status_producers, status_draws, status_unavailable = [], {}, 0
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
            radar_producers, radar_draws = [], {}
            status_producers, status_draws, status_unavailable = [], {}, 0
        if not started:
            continue
        if re.search(r"coop split(?: capture)? failed:", line):
            errors.append("split world/capture verification failed")
        if "coop status failed:" in line:
            errors.append("status native-model verification failed")
        status = re.search(r"coop status (producer|draw): (.*)", line)
        if status:
            values = dict(re.findall(r"(\w+)=([^\s]+)", status[2]))
            try:
                version=int(values["generation"])
                if version & 0xC0000000 != 0xC0000000 or not version & 0x3FFFFFFF:
                    raise ValueError("invalid status binding")
                if values["apt_override"]!="false": raise ValueError("unexpected APT override")
                if status[1]=="producer":
                    if int(values["seq"])<=0 or values["model_unchanged"]!="true":
                        raise ValueError("missing source sequence or changed native model")
                    a=stamina(values["primary"],values["primary_current"],values["primary_max"],values["primary_scaled"])
                    b=stamina(values["secondary"],values["secondary_current"],values["secondary_max"],values["secondary_scaled"])
                    if a["target"]==b["target"]: raise ValueError("status actors alias")
                    if values["primary_meter_matches"] not in ("true","false"):
                        raise ValueError("invalid native meter flag")
                    int(values["city_cache"],16)
                    status_producers.append({"generation":version,"sequence":int(values["seq"]),"players":[a,b],
                                             "primary_meter_matches":values["primary_meter_matches"]=="true"})
                else:
                    frame,view=int(values["frame"]),int(values["view"])
                    if frame<=0 or view not in (0,1): raise ValueError("invalid status draw")
                    if values["ready"]=="false": status_unavailable+=1
                    elif values["ready"]=="true":
                        if int(values["source_seq"])<=0 or (frame,view) in status_draws:
                            raise ValueError("missing source sequence or duplicate draw")
                        if version in blocked_preview_generations:
                            raise ValueError("status reused cleared binding")
                        row=stamina(values["target"],values["current"],values["maximum"],values["scaled"])
                        status_draws[frame,view]={"generation":version,"source_sequence":int(values["source_seq"]),"value":row}
                    else: raise ValueError("invalid ready state")
            except (KeyError,ValueError,OverflowError) as error:
                errors.append(f"status: {error}")
        if "coop radar failed:" in line:
            errors.append("radar producer/packet/cache verification failed")
        radar = re.search(r"coop radar (producer|draw): (.*)", line)
        if radar:
            values = dict(re.findall(r"(\w+)=([^\s]+)", radar[2]))
            try:
                generation, target = int(values["generation"]), int(values["target"])
                if generation & 0xC0000000 != 0xC0000000 or not generation & 0x3FFFFFFF:
                    raise ValueError("invalid radar binding")
                row = {"generation": generation, "target": target,
                       "position": vector(values["position"], 3),
                       "terrain": vector(values["terrain"], 1)[0],
                       "heading": vector(values["heading"], 1)[0]}
                if radar[1] == "producer":
                    if target in (0, 255) or values["primary_unchanged"] != "true":
                        raise ValueError("invalid radar target or changed primary manager")
                    radar_producers.append(row)
                else:
                    frame, view = int(values["frame"]), int(values["view"])
                    if not frame or view not in (0, 1) or (frame, view) in radar_draws:
                        raise ValueError("invalid/duplicate radar draw")
                    if values["packet_verified"] != "true" or values["cache_restored"] != "true":
                        raise ValueError("radar packet/cache not verified")
                    if values["retargeted"] != ("true" if view else "false"):
                        raise ValueError("wrong radar retarget view")
                    if view and (target in (0, 255) or generation in blocked_preview_generations):
                        raise ValueError("missing target or radar reused cleared binding")
                    if values["aspect_applied"] not in ("true", "false"):
                        raise ValueError("invalid radar aspect flag")
                    row["aspect_applied"] = values["aspect_applied"] == "true"
                    radar_draws[frame, view] = row
            except (KeyError, ValueError) as error:
                errors.append(f"radar: {error}")
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
    radar_pairs, radar_matched_generations = 0, set()
    for frame, generation in split_composites.items():
        a, b = radar_draws.get((frame, 0)), radar_draws.get((frame, 1))
        if a and b and a["generation"] == b["generation"] == generation:
            radar_pairs += 1
            if any(row["generation"] == generation and row["target"] == b["target"] and
                   math.dist(row["position"], b["position"]) <= tolerance and
                   abs(row["terrain"]-b["terrain"]) <= tolerance and
                   abs(row["heading"]-b["heading"]) <= tolerance for row in radar_producers):
                radar_matched_generations.add(generation)
    if required_radar:
        if radar_pairs < 3:
            errors.append("fewer than three same-frame radar/split composition pairs")
        if generations - radar_matched_generations:
            errors.append("radar binding has no matching native P2 producer/draw")
        for view in (0, 1):
            if sum(row["aspect_applied"] for (frame, v), row in radar_draws.items() if v == view) < 3:
                errors.append(f"radar view {view}: fewer than three corrected-aspect draws")
    status_pairs, status_matches = 0, set()
    # Logs sample only some native publications. An unsampled source can be
    # corroborated by matching values, but a sampled source must never disagree
    # with the draw that explicitly names it, even if an older row matches.
    for (frame,view), draw in status_draws.items():
        known=[row for row in status_producers if row["generation"]==draw["generation"] and
               row["sequence"]==draw["source_sequence"]]
        if any(row["players"][view]!=draw["value"] for row in known):
            errors.append(f"status draw {frame}/{view}: referenced native source contradicts actor values")
    for frame, version in split_composites.items():
        draws=[status_draws.get((frame,view)) for view in (0,1)]
        if all(row and row["generation"]==version for row in draws):
            if any(row["generation"]==version and row["players"]==[d["value"] for d in draws]
                   for row in status_producers):
                status_pairs+=1;status_matches.add(version)
    if required_status:
        if status_pairs<3: errors.append("fewer than three verified native status/composition pairs")
        if generations-status_matches: errors.append("status binding lacks matching native actor values for both views")
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
            "radar": {"producer_samples": len(radar_producers), "draw_samples": len(radar_draws),
                      "verified_pairs": radar_pairs,
                      "matched_producer_generations": sorted(radar_matched_generations),
                      "targets": sorted({row["target"] for row in radar_producers})},
            "status": {"producer_samples":len(status_producers), "draw_samples":len(status_draws),
                       "unavailable_samples":status_unavailable,"verified_pairs":status_pairs,
                       "matched_generations":sorted(status_matches),
                       "primary_meter_mismatch_samples":sum(not row["primary_meter_matches"] for row in status_producers)},
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
    parser.add_argument("--require-radar", action="store_true")
    parser.add_argument("--require-status", action="store_true")
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
                     required_split=args.require_split, required_radar=args.require_radar,
                     required_status=args.require_status)
    report["input_files"] = [str(path) for path in paths]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
