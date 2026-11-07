"""Sequential 3-round presentation matrix. Captures rendered output, not scanout."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import shutil
import subprocess

from summarize_presentation import summarize


def quiet():
    command = '@(Get-Process superman_returns,clang,clang++,lld-link,ninja,cmake,sr_native_tests -ErrorAction SilentlyContinue).Id'
    state = subprocess.run(['powershell', '-NoProfile', '-Command', command], capture_output=True, text=True, check=True)
    if state.stdout.strip():
        raise RuntimeError('Game, build or test process active; run the matrix after it finishes')


def capture_png(path):
    # Native readback dump retains pitch; this is a rendered-output image and
    # makes no claim about physical display delivery.
    from PIL import Image
    data = path.read_bytes()
    width, height, fmt, pitch = struct.unpack('<4I', data[:16])
    if len(data) != 16 + pitch * height:
        raise ValueError(f'Invalid capture size: {path}')
    if fmt == 28:
        image = Image.frombytes('RGBA', (width, height), data[16:], 'raw', 'RGBA', pitch)
    elif fmt == 24:
        pixels = bytearray()
        for y in range(height):
            for x in range(width):
                value = struct.unpack_from('<I', data, 16+y*pitch+4*x)[0]
                pixels.extend(((value >> shift & 1023)*255+511)//1023 for shift in (0, 10, 20))
        image = Image.frombytes('RGB', (width, height), bytes(pixels))
    else:
        raise ValueError(f'Unsupported capture format {fmt}: {path}')
    result = path.with_suffix('.png')
    image.save(result)
    return str(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True)
    parser.add_argument('--api', choices=['d3d12', 'vulkan'], default='d3d12')
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--warmup', type=float, default=5)
    parser.add_argument('--measure', type=float, default=30)
    parser.add_argument('--capture-swap', type=int, default=0)
    parser.add_argument('--compare-legacy', action='store_true', help='Compare prior guest vblank/sleep + Present(0) with dedicated pacing + Present(1)')
    parser.add_argument('--plan-only', action='store_true')
    parser.add_argument('--exe', type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.name) or args.rounds < 1 or args.measure <= 0 or args.warmup < 0:
        parser.error('Invalid label, round count or durations')
    root = Path(__file__).resolve().parents[2]
    exe = (args.exe or root/'port/out/build/win-amd64-release/superman_returns.exe').resolve()
    dest = root/'logs'/args.name
    variants = [(fps, vsync) for fps in (30, 60, 120) for vsync in (True, False)]
    plan = [(round_number, fps, sync) for round_number in range(1, args.rounds+1)
            for fps, sync in (variants if round_number % 2 else list(reversed(variants)))]
    if args.plan_only:
        print(json.dumps({'api': args.api, 'runs': plan, 'output': str(dest)}, indent=2))
        return
    quiet()
    identity = hashlib.sha256(exe.read_bytes()).hexdigest()
    dest.mkdir(exist_ok=False)
    reports = []
    for round_number, fps, sync in plan:
        quiet()
        if hashlib.sha256(exe.read_bytes()).hexdigest() != identity:
            raise RuntimeError('Executable changed during presentation matrix')
        mechanism=('dedicated' if sync else 'legacy') if args.compare_legacy else f'vsync_{int(sync)}'
        folder = dest/f'r{round_number}_{fps}_{mechanism}'
        folder.mkdir()
        csv_path = folder/'presentation.csv'
        command = [str(exe), f'--game_data_root={root/"game"}', f'--log_file={folder/"game.log"}',
                   '--sr_renderer=native', f'--sr_native_api={args.api}', '--sr_benchmark=true',
                   '--sr_benchmark_exit=true', '--sr_benchmark_headless=false',
                   f'--sr_benchmark_warmup_s={args.warmup}', f'--sr_benchmark_measure_s={args.measure}',
                   f'--sr_native_fps_limit={fps}', f'--vsync={str(sync or args.compare_legacy).lower()}',
                   '--sr_native_object_motion=false','--sr_native_render_scale=1','--sr_native_msaa_samples=1','--sr_native_fxaa=false',
                   '--sr_lsfg=false', '--sr_dlss=off', '--sr_rt_shadows=false',
                   '--sr_character_shadows=false', '--sr_rt_reflections=false',
                   '--window_width=1280', '--window_height=720',
                   f'--sr_native_ab_swaps={args.capture_swap}', f'--sr_native_dump_dir={folder/"capture"}']
        env = os.environ.copy()
        env.setdefault('SR_LAUNCHER_SETTINGS',str(folder/'launcher.ini'))
        env['SR_PRESENTATION_CSV'] = str(csv_path)
        env['SR_PRESENTATION_LEGACY'] = '1' if args.compare_legacy and not sync else '0'
        with (folder/'stdout.log').open('wb') as stdout, (folder/'stderr.log').open('wb') as stderr:
            result = subprocess.run(command, cwd=exe.parent, env=env, stdout=stdout, stderr=stderr,
                                    timeout=args.warmup+args.measure+120, creationflags=subprocess.CREATE_NO_WINDOW)
        if not csv_path.is_file():
            raise RuntimeError(f'No presentation telemetry from {folder}; verify rebuilt SDK/presenter')
        with csv_path.open(newline='') as stream:
            reader = csv.DictReader(stream)
            fields, rows = reader.fieldnames, list(reader)
        # The benchmark exits immediately after its measurement window. Retain
        # that final duration while preserving the complete original CSV.
        end = max((int(row['return_ns']) for row in rows), default=0)
        measured = folder/'measured_presentation.csv'
        with measured.open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(row for row in rows if int(row['return_ns']) >= end-int(args.measure*1e9))
        benchmark={}
        text=(folder/'game.log').read_text(errors='replace')
        match=re.search(r'([^|\r\n]*benchmark-\d+-\d+\.json)\s*$',text,re.MULTILINE)
        if match:
            source=Path(match[1].strip())
            benchmark=json.loads(source.read_text())
            shutil.copy2(source,folder/'benchmark.json')
            if source.with_suffix('.csv').exists():shutil.copy2(source.with_suffix('.csv'),folder/'frame_times.csv')
        screenshots, capture_errors = [], []
        for raw in (folder/'capture').glob('*output*.raw'):
            try:
                screenshots.append(capture_png(raw))
            except (ImportError, ValueError) as error:
                capture_errors.append(str(error))
        report = {'round': round_number, 'target_fps': fps, 'vsync': sync or args.compare_legacy, 'mechanism':mechanism, 'api': args.api,
                  'exit_code': result.returncode, 'benchmark':benchmark, 'command': command, 'summary': summarize(measured),
                  'rendered_output_images': screenshots, 'capture_errors': capture_errors,
                  'limits': 'API acceptance and rendered output are not scanout proof; capture may perturb one frame'}
        reports.append(report)
        (folder/'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        if hashlib.sha256(exe.read_bytes()).hexdigest() != identity:
            raise RuntimeError('Executable changed during presentation measurement')
        (dest/'matrix.json').write_text(json.dumps({'exe_sha256': identity,
                                                  'runs': reports}, indent=2), encoding='utf-8')
        print(f'{folder.name}: accepted_api_fps={report["summary"]["accepted_api_fps"]} exit={result.returncode}', flush=True)
        if result.returncode:
            raise SystemExit(f'Benchmark failed: {folder}; remaining trials not run')


if __name__ == '__main__':
    main()
