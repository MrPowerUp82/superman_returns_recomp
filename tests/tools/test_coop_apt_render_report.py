import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'coop_apt_render_report', Path(__file__).resolve().parents[2] / 'tools/analysis/coop_apt_render_report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


def trace():
    carrier = bytearray(64)
    struct.pack_into('>I', carrier, 32, 0x30100000)
    packet = struct.pack('>4I', 0, 0x30200000, 12, 24)
    matrix = struct.pack('>16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 12, 24, 0, 1) + b'\0' * 16
    rows = ['coop: single-instance input preparation;']
    for frame in (120, 240, 360):
        for view in (0, 1):
            rows += [f'coop apt packet: frame={frame} generation=3221225473 view={view} ordinal=1 object=30100000 method=82701000 '
                     f'transform=30200000 carrier_readable=true carrier_data={carrier.hex()} source_matches=true '
                     f'packet={packet.hex()} packet_readable=true packet_matches=true readable=true transform_unchanged=true '
                     f'matrix={matrix.hex()} override=false',
                     f'coop apt pass: frame={frame} generation=3221225473 view={view} adapters=1 captured=1 nodes=4 node_captured=4 carriers=1 limit=256 override=false']
        rows += [f'coop split composite: frame={frame} generation=3221225473 views=2 size=1920x1080']
    return rows


class AptRenderEvidence(unittest.TestCase):
    def test_requires_positive_two_view_bridge_in_composed_frames(self):
        good = trace()
        self.assertTrue(report.analyze(good)['passed'])
        cases = [[], [s for s in good if 'view=1' not in s],
                 [s for s in good if 'apt packet:' not in s],
                 good + [next(s for s in good if 'apt packet:' in s)],
                 good + ['coop split failed: restore'],
                 [s.replace('packet_matches=true', 'packet_matches=false') for s in good],
                 [s.replace('transform_unchanged=true', 'transform_unchanged=false') for s in good],
                 [s.replace('object=30100000', 'object=30100004') for s in good],
                 [s.replace('transform=30200000', 'transform=30200004') for s in good],
                 [s.replace('override=false', 'override=true') for s in good],
                 [s.replace('captured=1', 'captured=2') for s in good],
                 [s.replace('carriers=1', 'carriers=0') for s in good],
                 [s.replace('generation=3221225473', 'generation=3221225474') if 'apt ' in s else s for s in good]]
        for bad in cases:
            with self.subTest(bad=bad):
                self.assertFalse(report.analyze(bad)['passed'])

    def test_latest_startup_discards_previous_run(self):
        self.assertTrue(report.analyze(['coop: single-instance input preparation;', 'Access violation'] + trace())['passed'])
        self.assertFalse(report.analyze(trace() + ['coop: single-instance input preparation;'])['passed'])

    def test_rejects_orphan_evidence_and_missing_sampled_hud_passes(self):
        good = trace()
        packet = next(s for s in good if 'apt packet:' in s)
        summary = next(s for s in good if 'apt pass:' in s)
        composite = next(s for s in good if 'split composite:' in s)
        orphan = packet.replace('frame=120', 'frame=480').replace('3221225473', '3221225474')
        orphan_pass = summary.replace('frame=120', 'frame=480').replace('3221225473', '3221225474')
        cases = [good + [orphan], good + [orphan, orphan_pass],
                 good + [composite.replace('frame=120', 'frame=480')]]
        for bad in cases:
            with self.subTest(bad=bad):
                self.assertFalse(report.analyze(bad)['passed'])

    def test_generation_must_fit_a_positive_guest_word(self):
        for invalid in (-1073741823, 0x1C0000001):
            with self.subTest(generation=invalid):
                rows = [row.replace('3221225473', str(invalid)) for row in trace()]
                self.assertFalse(report.analyze(rows)['passed'])

    def test_cli_reads_rotated_logs_in_run_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = trace()
            (root / 'probe.10.log').write_text('coop: single-instance input preparation;\nAccess violation\n', encoding='utf-8')
            (root / 'probe.2.log').write_text('\n'.join(rows[:8]) + '\n', encoding='utf-8')
            (root / 'probe.log').write_text('\n'.join(rows[8:]) + '\n', encoding='utf-8')
            result = subprocess.run([sys.executable, report.__file__, '--log', str(root / 'probe.log'),
                                     '--output', str(root / 'report.json')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
