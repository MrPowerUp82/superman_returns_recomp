"""Run the title flyover with fixed quality; preserve arguments, hashes and results."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("label")
    parser.add_argument("--exe", type=Path, default=ROOT / "port/out/build/win-amd64-release/superman_returns.exe")
    parser.add_argument("--profile", action="store_true")
    parser.add_argument("--batch", choices=("default", "32", "128", "256"), default="default")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--flag", action="append", default=[])
    options = parser.parse_args()
    directory = ROOT / "artifacts/d3d12-fps-20261104" / options.label
    directory.mkdir(parents=True, exist_ok=False)
    exe = options.exe.resolve()
    env = os.environ.copy()
    for key in list(env):
        if key.startswith("SR_"):
            del env[key]
    if options.profile:
        env.update(SR_NATIVE_CPU_PROFILE="1", SR_NATIVE_CONSTANTS_PROFILE="1",
                   SR_NATIVE_STREAMS_PROFILE="1", SR_FRAME_TIMELINE_DETAIL="1",
                   SR_FRAME_TIMELINE=str(directory / "timeline.csv"))
    if options.batch != "default":
        env["SR_NATIVE_BATCH_COMMANDS"] = options.batch
    args = [str(exe), f"--game_data_root={ROOT / 'game'}", f"--log_file={directory / 'game.log'}",
            "--sr_renderer=native", "--sr_native_api=d3d12", "--sr_preset=custom",
            "--sr_benchmark=true", "--sr_benchmark_headless=true", "--sr_skip_intro=true",
            "--sr_benchmark_warmup_s=5", f"--sr_benchmark_measure_s={options.seconds}",
            "--window_width=1280", "--window_height=720", "--fullscreen=false",
            "--vsync=false", "--sr_native_fps_limit=0", "--sr_native_render_scale=1",
            "--sr_native_anisotropic_filtering=-1", "--sr_native_fxaa=false",
            "--sr_native_shadow_quality=1", "--sr_native_msaa_samples=1",
            "--sr_lsfg=false", "--mnk_mode=false", "--mnk_mouse=false", *options.flag]
    manifest = {"args": args, "environment": {k: v for k, v in env.items() if k.startswith("SR_")},
                "hashes": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in [exe, exe.parent / "rexruntime.dll"]}, "started": time.time()}
    manifest_path = directory / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2))
    with (directory / "stdout.txt").open("w") as stdout:
        process = subprocess.Popen(args, cwd=exe.parent, env=env, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            manifest["exit_code"] = process.wait(timeout=options.seconds + 180)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=15)
            manifest.update(exit_code=process.returncode, timeout=True)
    manifest["ended"] = time.time()
    results = sorted((exe.parent / "benchmarks").glob("*.json"), key=lambda p: p.stat().st_mtime)
    if results and results[-1].stat().st_mtime >= manifest["started"]:
        result = results[-1]
        shutil.copy2(result, directory / "result.json")
        shutil.copy2(result.with_suffix(".csv"), directory / "frames.csv")
        manifest["result"] = json.loads(result.read_text(encoding="utf-8"))
    manifest_path.write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest.get("result", manifest), indent=2), flush=True)
    return 0 if manifest["exit_code"] == 0 and manifest.get("result", {}).get("valid") else 1


if __name__ == "__main__":
    raise SystemExit(main())
