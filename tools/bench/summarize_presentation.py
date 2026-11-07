"""Summarize actual presentation API returns; does not infer visible scanout FPS."""
import argparse
import csv
import json
import math
import statistics
from collections import Counter
from pathlib import Path


def summarize(path):
    with Path(path).open(newline='', encoding='utf-8-sig') as stream:
        rows = list(csv.DictReader(stream))
    accepted = [r for r in rows if r['accepted'] == '1']
    intervals = [int(r['accepted_interval_ns']) / 1e6 for r in accepted
                 if int(r['accepted_interval_ns']) > 0]
    intervals.sort()
    def percentile(fraction):
        return intervals[max(0, math.ceil(len(intervals) * fraction) - 1)] if intervals else None
    elapsed = (int(accepted[-1]['return_ns']) - int(accepted[0]['return_ns'])) / 1e9 if len(accepted) > 1 else 0
    real_rows = [r for r in accepted if r['kind'] == 'real' and int(r['source_serial']) > 0]
    real = [int(r['source_serial']) for r in real_rows]
    real_times = [int(r['return_ns']) for r in real_rows]
    real_intervals = sorted((b-a)/1e6 for a,b in zip(real_times, real_times[1:]) if b > a)
    real_elapsed = (real_times[-1]-real_times[0])/1e9 if len(real_times)>1 else 0
    replaced = sum(max(0, b - a - 1) for a, b in zip(real, real[1:]) if b >= a)
    return {'path': str(path), 'measurement': 'CPU presentation API return; not scanout',
            'calls': len(rows), 'accepted': len(accepted), 'rejected': len(rows) - len(accepted),
            'accepted_api_fps': (len(accepted)-1)/elapsed if elapsed else None,
            'unique_source_presentations': len(real_rows),
            'unique_source_api_fps': (len(real_rows)-1)/real_elapsed if real_elapsed>0 else None,
            'unique_source_interval_stddev_ms': statistics.pstdev(real_intervals) if real_intervals else None,
            'unique_source_interval_p99_ms': real_intervals[math.ceil(len(real_intervals)*.99)-1] if real_intervals else None,
            'kinds': dict(Counter(r['kind'] for r in accepted)),
            'source_serial_gaps': replaced, 'interval_p50_ms': percentile(.50),
            'interval_p95_ms': percentile(.95), 'interval_p99_ms': percentile(.99),
            'interval_p999_ms': percentile(.999),
            'interval_mean_ms': statistics.mean(intervals) if intervals else None,
            'interval_stddev_ms': statistics.pstdev(intervals) if intervals else None,
            'low1_api_fps': 1000/statistics.mean(intervals[-max(1,math.ceil(len(intervals)*.01)):]) if intervals else None,
            'low01_api_fps': 1000/statistics.mean(intervals[-max(1,math.ceil(len(intervals)*.001)):]) if intervals else None,
            'interval_max_ms': max(intervals) if intervals else None,
            'skipped_deadline_slots': max((int(r['skipped_slots']) for r in rows), default=0)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', nargs='+')
    args = parser.parse_args()
    print(json.dumps([summarize(path) for path in args.csv], indent=2))
