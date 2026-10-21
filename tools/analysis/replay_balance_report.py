"""Replay CSV v1: keep overlapping wall durations and process replicates distinct."""
import argparse
import csv
import json
import math
import random
import statistics
from collections import defaultdict
from pathlib import Path

import frame_timeline_report as timeline_report

HEADER = 'schema,kind,sequence,closing_swap,api,path,shards,scheduling,commands,draws,wall_ns,prepare_ns,parallel_ns,join_wait_ns,direct_decode_ns,direct_arena_ns,dispatch_ns,shard,begin_ns,end_ns,arena_ns,decode_ns,clock_anchor_ns,clock_anchor_utc_ns'.split(',')
IDENTITY = ('api', 'path', 'shards', 'scheduling', 'clock_anchor_ns', 'clock_anchor_utc_ns')
TEXT = {'kind', 'api', 'path', 'scheduling'}


def summarize(samples):
    if not samples:
        return dict(samples=0, mean=None, p50=None, p95=None, p99=None)
    if any(not math.isfinite(v) for v in samples):
        raise ValueError('Non-finite measurement')
    ordered = sorted(samples)
    return dict(samples=len(samples), mean=statistics.fmean(samples),
                **{f'p{p}': ordered[max(0, math.ceil(p*len(ordered)/100)-1)] for p in (50, 95, 99)})


def _required(row, fields):
    if any(row.get(field) is None for field in fields):
        raise ValueError(f'Missing measurement in batch {row.get("sequence")}')


def load_replay(path):
    batches = []
    current = None
    run_identity = None

    def complete():
        if current is None:
            return
        expected = current['shards'] if current['path'] == 'prepared' else 0
        if len(current['shard_rows']) != expected:
            raise ValueError(f'Missing shard in batch {current["sequence"]}')
        if expected and sum(s['commands'] for s in current['shard_rows']) != current['commands']:
            raise ValueError(f'Command counts disagree in batch {current["sequence"]}')

    with Path(path).open(newline='', encoding='utf-8-sig') as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != HEADER:
            raise ValueError('Unknown/truncated replay CSV header')
        for line, raw in enumerate(reader, 2):
            if None in raw or any(v is None for v in raw.values()):
                raise ValueError(f'Truncated replay CSV at line {line}')
            try:
                row = {k: (v if k in TEXT else (int(v) if v != '' else None)) for k, v in raw.items()}
            except ValueError as error:
                raise ValueError(f'Invalid integer at replay line {line}') from error
            _required(row, ('schema', 'sequence', 'closing_swap', 'shards', 'commands', 'draws', 'shard', 'clock_anchor_ns', 'clock_anchor_utc_ns'))
            if row['schema'] != 1 or row['kind'] not in ('batch', 'shard') or row['api'] != 'vulkan':
                raise ValueError(f'Unknown replay schema/kind/API at line {line}')
            if row['path'] not in ('direct', 'prepared') or row['scheduling'] not in ('contiguous', 'interleaved'):
                raise ValueError(f'Unknown replay mode at line {line}')
            if not 1 <= row['shards'] <= 8 or row['sequence'] < 1:
                raise ValueError(f'Invalid replay configuration at line {line}')
            if any(v is not None and v < 0 for k, v in row.items() if k not in TEXT and k != 'shard'):
                raise ValueError(f'Negative replay value at line {line}')
            if row['draws'] > row['commands']:
                raise ValueError(f'More draws than commands at line {line}')
            identity = tuple(row[k] for k in IDENTITY)
            if run_identity is None:
                run_identity = identity
            if identity != run_identity:
                raise ValueError(f'Changed configuration/clock in replay at line {line}')
            if row['kind'] == 'batch':
                complete()
                if current and row['sequence'] != current['sequence']+1:
                    raise ValueError(f'Duplicate/missing batch sequence at line {line}')
                if row['shard'] != -1:
                    raise ValueError('Batch row has shard identity')
                if row['path'] == 'prepared':
                    _required(row, ('wall_ns', 'prepare_ns', 'parallel_ns', 'join_wait_ns', 'dispatch_ns'))
                    if row['join_wait_ns'] > row['parallel_ns'] or row['prepare_ns'] > row['wall_ns']:
                        raise ValueError('Inconsistent nested replay duration')
                else:
                    _required(row, ('direct_decode_ns', 'direct_arena_ns'))
                    if row['shards'] != 1 or any(row[k] is not None for k in ('wall_ns', 'prepare_ns', 'parallel_ns', 'join_wait_ns', 'dispatch_ns')):
                        raise ValueError('Direct replay contains prepared measurements')
                row['shard_rows'] = []
                current = row
                batches.append(row)
            else:
                if current is None or current['path'] != 'prepared' or row['sequence'] != current['sequence'] or row['closing_swap'] != current['closing_swap']:
                    raise ValueError(f'Orphan shard at line {line}')
                _required(row, ('begin_ns', 'end_ns', 'arena_ns', 'decode_ns'))
                if row['shard'] != len(current['shard_rows']) or row['shard'] >= current['shards']:
                    raise ValueError(f'Duplicate/out-of-order shard at line {line}')
                duration = row['end_ns']-row['begin_ns']
                if duration < 0 or row['begin_ns'] < current['dispatch_ns'] or row['arena_ns'] > duration or row['decode_ns'] > duration:
                    raise ValueError(f'Inconsistent shard duration at line {line}')
                # Constant configuration/empty batch columns were validated;
                # avoid retaining 24 dictionary entries per shard in long traces.
                current['shard_rows'].append({k:row[k] for k in ('shard', 'commands', 'draws', 'begin_ns', 'end_ns', 'arena_ns', 'decode_ns')})
        complete()
    return batches


def aggregate_frames(batches):
    frames, pending = [], []
    previous_swap = None
    prefix = 0
    for batch in batches:
        pending.append(batch)
        swap = batch['closing_swap']
        if not swap:
            continue
        if previous_swap is not None and swap <= previous_swap:
            raise ValueError('Duplicate/backwards swap number')
        if previous_swap is None:
            prefix = len(pending)
        else:
            prepared = batch['path'] == 'prepared'
            totals = defaultdict(int)
            max_sum = 0
            for item in pending:
                durations = [s['end_ns']-s['begin_ns'] for s in item['shard_rows']]
                max_sum += max(durations, default=0)
                for shard in item['shard_rows']:
                    totals[shard['shard']] += shard['end_ns']-shard['begin_ns']
            frames.append(dict(swap=swap, previous_swap=previous_swap, batches=pending,
                               replay_ns=sum((b['wall_ns'] if prepared else b['direct_decode_ns']+b['direct_arena_ns']) for b in pending),
                               prepare_ns=sum(b['prepare_ns'] or 0 for b in pending) if prepared else None,
                               sum_batch_max_shard_ns=max_sum if prepared else None,
                               max_shard_total_ns=max(totals.values(), default=0) if prepared else None,
                               draws=sum(b['draws'] for b in pending)))
        pending = []
        previous_swap = swap
    return dict(frames=frames, dropped_prefix_batches=prefix,
                dropped_suffix_batches=len(pending))


def _read_json(path):
    with Path(path).open(encoding='utf-8-sig') as handle:
        return json.load(handle)


def analyze(replay, timeline, windows):
    batches = load_replay(replay)
    grouped = aggregate_frames(batches)
    timeline_frames = timeline_report.load(timeline)
    metadata = _read_json(windows)
    if metadata.get('schema') != 1:
        raise ValueError('Unknown window metadata schema')
    scenarios = {}
    for window in metadata['windows']:
        name = window['scenario']
        if name not in ('idle', 'forward') or name in scenarios or window['end_utc_ns'] <= window['begin_utc_ns']:
            raise ValueError('Invalid/duplicate measurement window')
        selected, dropped = [], 0
        for frame in grouped['frames']:
            stages = timeline_frames.get(frame['swap'], {})
            if 'game' not in stages or 'worker' not in stages:
                dropped += 1
                continue
            first = frame['batches'][0]
            offset = first['clock_anchor_utc_ns']-first['clock_anchor_ns']
            begin = min(stages[s]['begin_ns'] for s in ('game', 'worker'))+offset
            end = max(stages[s]['end_ns'] for s in ('game', 'worker'))+offset
            if window['begin_utc_ns'] <= begin and end <= window['end_utc_ns']:
                selected.append((frame, stages))
        metrics = {'replay_ms': [f['replay_ns']/1e6 for f, _ in selected],
                   'draws': [f['draws'] for f, _ in selected]}
        intervals = []
        for frame, stages in selected:
            previous = timeline_frames.get(frame['previous_swap'], {}).get('game')
            if previous and frame['swap'] == frame['previous_swap']+1:
                offset = frame['batches'][0]['clock_anchor_utc_ns']-frame['batches'][0]['clock_anchor_ns']
                if previous['end_ns']+offset >= window['begin_utc_ns']:
                    interval = stages['game']['end_ns']-previous['end_ns']
                    if interval <= 0:
                        raise ValueError('Nonpositive game frame interval')
                    intervals.append(interval/1e6)
        metrics['frame_interval_ms'] = intervals
        for name_stage in ('game', 'worker', 'record', 'gpu'):
            metrics[name_stage+'_ms'] = [s[name_stage]['busy_ns']/1e6 for _, s in selected if name_stage in s]
        for name_metric in ('prepare_ns', 'sum_batch_max_shard_ns', 'max_shard_total_ns'):
            metrics[name_metric.removesuffix('_ns')+'_ms'] = [f[name_metric]/1e6 for f, _ in selected if f[name_metric] is not None]
        shard_ms, dispatch_ms, batch_ms = [], [], defaultdict(list)
        per_shard = defaultdict(lambda:defaultdict(list))
        imbalances = []
        for frame, _ in selected:
            for batch in frame['batches']:
                duration = batch['wall_ns'] if batch['path'] == 'prepared' else batch['direct_decode_ns']+batch['direct_arena_ns']
                batch_ms[batch['commands']].append(duration/1e6)
                active_durations = []
                for shard in batch['shard_rows']:
                    target = per_shard[str(shard['shard'])]
                    target['commands'].append(shard['commands'])
                    target['draws'].append(shard['draws'])
                    if shard['commands']:
                        elapsed = (shard['end_ns']-shard['begin_ns'])/1e6
                        delay = (shard['begin_ns']-batch['dispatch_ns'])/1e6
                        active_durations.append(elapsed)
                        shard_ms.append(elapsed)
                        dispatch_ms.append(delay)
                        for key, value in (('elapsed_ms', elapsed), ('dispatch_delay_ms', delay),
                                           ('arena_ms', shard['arena_ns']/1e6), ('decode_ms', shard['decode_ns']/1e6)):
                            target[key].append(value)
                if active_durations and sum(active_durations) > 0:
                    imbalances.append(max(active_durations)/statistics.fmean(active_durations))
        scenarios[name] = {key:summarize(values) for key, values in metrics.items()}
        scenarios[name].update(shard_ms=summarize(shard_ms), dispatch_delay_ms=summarize(dispatch_ms),
                               shards={k:{metric:summarize(values) for metric, values in detail.items()} for k, detail in per_shard.items()},
                               imbalance_max_over_mean=summarize(imbalances),
                               batches_by_command_count={str(k):summarize(v) for k, v in batch_ms.items()},
                               missing_timeline_frames=dropped)
    return dict(scenarios=scenarios, dropped_prefix_batches=grouped['dropped_prefix_batches'],
                dropped_suffix_batches=grouped['dropped_suffix_batches'],
                timing='instrumented; sums of shard durations overlap and are not OS CPU time')


def paired_bootstrap(control, candidate, seed=20261021, resamples=10000):
    if not control or len(control) != len(candidate) or resamples < 1:
        raise ValueError('Bootstrap requires matched nonempty process pairs')
    if any(not math.isfinite(v) or v <= 0 for v in control) or any(not math.isfinite(v) or v < 0 for v in candidate):
        raise ValueError('Invalid paired measurement')
    reductions = [(a-b)/a for a, b in zip(control, candidate)]
    rng = random.Random(seed)
    means = sorted(statistics.fmean(rng.choices(reductions, k=len(reductions))) for _ in range(resamples))
    bounds = [means[max(0, math.ceil(p*resamples)-1)] for p in (.025, .975)]
    return dict(pairs=len(control), reduction=statistics.fmean(reductions), ci95=bounds, seed=seed,
                independent_unit='process execution', enough_runs_for_screening=len(control) >= 10)


def analyze_matrix(manifest, seed=20261021):
    manifest = Path(manifest)
    matrix = _read_json(manifest)
    if matrix.get('schema') != 1 or matrix.get('phase') not in ('Isolated', 'Game'):
        raise ValueError('Unknown matrix schema/phase')
    measurements, runs, skipped = defaultdict(dict), [], []

    def resolve(files, key):
        if not files.get(key):
            raise ValueError(f'Missing {key} evidence')
        path = Path(files[key])
        return path if path.is_absolute() else manifest.parent/path

    for run in matrix['runs']:
        if run['status'] != 'complete' or run.get('exit_code') != 0:
            skipped.append(dict(id=run['id'], status=run['status'], error=run.get('error')))
            continue
        files = run['scenario_files']
        values = {}
        if matrix['phase'] == 'Isolated':
            data = _read_json(resolve(files, 'isolated'))
            if data.get('schema') != 1 or not data.get('samples'):
                raise ValueError('Missing isolated process samples')
            if data.get('checksum') != 31793585826180:
                raise ValueError('Isolated checksum mismatch')
            values['isolated:cycle_ms'] = statistics.median(s['cycle_ms'] for s in data['samples'])
        else:
            metadata = _read_json(resolve(files, 'windows'))
            if metadata.get('schema') != 1:
                raise ValueError('Unknown window metadata schema')
            for window in metadata['windows']:
                fps = window.get('fps_samples', [])
                if not fps or any(not math.isfinite(f) or f <= 0 for f in fps):
                    raise ValueError('Missing/invalid clean FPS samples')
                # A descriptive reciprocal of mean periodic FPS, never a frame percentile.
                values[window['scenario']+':periodic_fps_reciprocal_ms'] = 1000/statistics.fmean(fps)
            if run['profiled']:
                detail = analyze(resolve(files, 'replay'), resolve(files, 'timeline'), resolve(files, 'windows'))
                for scenario, metrics in detail['scenarios'].items():
                    for metric in ('replay_ms', 'worker_ms', 'game_ms', 'record_ms', 'gpu_ms'):
                        if metrics[metric]['samples']:
                            values[scenario+':'+metric] = metrics[metric]['mean']
                run = dict(run, detail=detail)
        if any(not math.isfinite(v) or v <= 0 for v in values.values()):
            raise ValueError('Nonpositive process measurement')
        for metric, value in values.items():
            key = (bool(run['profiled']), metric, run['variant'])
            block = run['block']
            if block in measurements[key]:
                raise ValueError('Duplicate process in a variant/block')
            measurements[key][block] = value
        runs.append(dict(run, measurements=values))
    comparisons = []
    for (profiled, metric, variant), candidates in measurements.items():
        if variant == 'direct':
            continue
        controls = ['direct']
        if variant.startswith('interleaved-'):
            controls.append('contiguous-'+variant.split('-')[1])
        for control in controls:
            baseline = measurements.get((profiled, metric, control))
            if not baseline:
                continue
            common = sorted(set(candidates)&set(baseline))
            if common:
                result = paired_bootstrap([baseline[b] for b in common], [candidates[b] for b in common], seed)
                comparisons.append(dict(result, variant=variant, control=control, metric=metric, profiled=profiled,
                                        excluded_unpaired=len(set(candidates)^set(baseline))))
    return dict(schema=1, phase=matrix['phase'], settings=matrix.get('settings', {}), runs=runs,
                comparisons=comparisons, skipped=skipped,
                note='Frame percentiles come only from instrumented timeline runs; periodic FPS minima are not 1% lows.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--matrix', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=20261021)
    parser.add_argument('--json', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = analyze_matrix(args.matrix, args.seed)
        args.json.write_text(json.dumps(result, indent=2, allow_nan=False), encoding='utf-8')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(2, f'Invalid replay evidence: {error}\n')
    print(f'{len(result["runs"])} process runs; {len(result["comparisons"])} paired comparisons; {len(result["skipped"])} failed/incomplete runs')


if __name__ == '__main__':
    main()
