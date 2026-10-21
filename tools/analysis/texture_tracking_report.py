"""Fail-closed review of controlled D3D12 tracking experiments; no FPS inference from bytes."""
import argparse
import json
import math
from statistics import mean

REQUIRED_ARGS={"--sr_native_fps_limit=60","--sr_renderer=native","--sr_native_api=d3d12","--sr_native_render_scale=1","--sr_native_msaa_samples=1","--sr_lsfg=false","--vsync=true","--window_width=1280","--window_height=720"}


def analyze(manifest):
    reasons=[]
    runs=manifest.get("runs",[])
    if [r.get("mode") for r in runs] != ["off","on","on","off"] or [r.get("order") for r in runs] != [1,2,3,4]:
        reasons.append("requires OFF/ON/ON/OFF order")
    if len({r.get("name") for r in runs}) != len(runs):reasons.append("duplicate run identity")
    baseline=runs[0] if runs else {}
    fps={"idle":{"off":[],"on":[]},"forward":{"off":[],"on":[]}}
    for run in runs:
        for field in ("exe_sha256","runtime_sha256","scene"):
            if not run.get(field) or run.get(field)!=baseline.get(field):reasons.append(f"{field} differs or missing")
        # Per-run log paths are evidence destinations, not render configuration.
        config=lambda r:[a for a in r.get("args",[]) if not a.startswith("--log_file=")]
        if config(run)!=config(baseline) or not REQUIRED_ARGS.issubset(run.get("args",[])):reasons.append("render configuration differs or invalid")
        if run.get("profile") is not False or run.get("concurrent_work") is not False:reasons.append("profiling/concurrent work in acceptance")
        if not run.get("hud_start") or not run.get("hud_end"):reasons.append("gameplay HUD confirmation missing")
        if run.get("image_alignment")!="aligned":reasons.append("camera/state alignment unproven")
        if run.get("image_gate_pass") is not True:reasons.append("image gate not passed")
        scenarios=run.get("scenarios",[])
        if {s.get("name") for s in scenarios}!={"idle","forward"} or len(scenarios)!=2:reasons.append("missing scenarios")
        for sample in scenarios:
            values=sample.get("fps",[])
            valid=len(values)>=8 and all(isinstance(v,(int,float)) and math.isfinite(v) and v>0 for v in values)
            duration=sample.get("duration_s")
            if not isinstance(duration,(int,float)) or isinstance(duration,bool) or not math.isfinite(duration) or not 19.8<=duration<=21 or not valid:reasons.append("short/invalid FPS window");continue
            if sample.get("name") in fps and run.get("mode") in ("off","on"):
                fps[sample["name"]][run["mode"]].append(mean(values))
    for scenario,modes in fps.items():
        if len(modes["off"])==2 and len(modes["on"])==2 and all(v<mean(modes["off"]) for v in modes["on"]):
            reasons.append(f"repeated FPS regression: {scenario}")
    stages=manifest.get("stages",[])
    for sample in stages:
        if any(sample.get(k)!=baseline.get(k) for k in ("exe_sha256","runtime_sha256","scene")) or sample.get("args")!=[a for a in baseline.get("args",[]) if not a.startswith("--log_file=")]:
            reasons.append("stage binary/configuration differs")
        if sample.get("profile") is not True or sample.get("concurrent_work") is not False:reasons.append("stage instrumentation not controlled")
    grouped={m:[s for s in stages if s.get("mode")==m] for m in ("off","on")}
    stage_gain=False
    if any(len(grouped[m])<2 for m in grouped):reasons.append("repeated stage timings missing")
    elif any(s.get("runtime_accounted") is not True for s in stages):reasons.append("runtime protection/fault/host cost unaccounted")
    elif any(not isinstance(s.get(k),(int,float)) or not math.isfinite(s[k]) or s[k]<0 for s in stages for k in ("validation_ns","runtime_ns","resource_bytes","resource_checks")):
        reasons.append("invalid stage measurements")
    else:
        comparable=True
        for k in ("resource_bytes","resource_checks"):
            reference=mean(s[k] for s in grouped["off"])
            comparable &= reference>0 and all(abs(s[k]/reference-1)<=.1 for s in stages)
        if not comparable:reasons.append("resource workloads differ")
        off=mean(s["validation_ns"]+s["runtime_ns"] for s in grouped["off"])
        stage_gain=comparable and off>0 and all(s["validation_ns"]+s["runtime_ns"]<=off*.7+1e-9 for s in grouped["on"])
        if not stage_gain:reasons.append("repeated 30 percent total-stage gain not demonstrated")
    reasons=list(dict.fromkeys(reasons))
    return dict(status="validated" if not reasons else "inconclusive",recommend_on=not reasons,stage_gain=stage_gain,
                sixty_fps_verified=False,fps=fps,reasons=reasons)


def main():
    parser=argparse.ArgumentParser();parser.add_argument("manifest");parser.add_argument("--json",dest="output")
    args=parser.parse_args()
    with open(args.manifest,encoding="utf-8-sig") as stream:result=analyze(json.load(stream))
    text=json.dumps(result,indent=2,ensure_ascii=False)
    if args.output:
        with open(args.output,"w",encoding="utf-8") as stream:stream.write(text+"\n")
    print(text)
    return 0 if result["recommend_on"] else 2

if __name__=="__main__":raise SystemExit(main())
