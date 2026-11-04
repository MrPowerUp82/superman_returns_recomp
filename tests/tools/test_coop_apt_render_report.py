import importlib.util
import json
from pathlib import Path
import struct
import re
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


def field(line, name, value):
    return re.sub(rf'\b{name}=\S+', lambda _: f'{name}={value}', line)


def preview_trace(currents=(0, 3333, 10000), skipped=0):
    """Independent fixture: nonidentity basis, packet flags and opaque effects."""
    def single(value):
        return struct.unpack('>f', struct.pack('>f', value))[0]

    rows = []
    for line in trace():
        if 'apt packet:' in line:
            data = bytearray.fromhex(re.search(r'carrier_data=(\w+)', line)[1])
            struct.pack_into('>I', data, 4, 30)
            line = field(line, 'carrier_data', data.hex())
            packet = struct.pack('>4I', 0xAABBCCDD, 0x30200000, 12, 24)
            matrix = struct.pack('>16f', 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 12, 24, 0, 1) + bytes(range(16))
            basis = struct.pack('>16f', .75, -.25, .5, .125, 0, 1, 0, 0, 0, 0, 1, 0, 30, 40, 0, 1)
            line = field(field(line, 'packet', packet.hex()), 'matrix', matrix.hex())
            context = re.search(r'frame=\d+ generation=\d+ view=\d+', line)[0]
            current = currents[(int(re.search(r'frame=(\d+)', line)[1]) // 120) - 1]
            scaled = int(single(single(current / 10000) * 10000))
            p = min(1., max(0., single((scaled - 500) / 9500)))
            delta = single(-200 * single(1 - p))
            positions = {2: single(292 + delta), 3: single(108 + delta),
                         4: single(100 + delta), 5: single(90 + single(200 * p))}
            for role, character, depth in [(1, 13, 24), (2, 26, 30), (3, 21, 34), (4, 27, 39), (5, 40, 43)]:
                path = f'1:101:10:63:1:1:1;2:202:20:6:172:2:1;3:303:30:45:1:3:1;4:404:30:{character}:{depth}:3:0;5:600:30:15:1:3:0'
                outer = field(line, 'ordinal', role) + f' carrier=00000258 manager=30300000 role={role} scope_valid=true path={path}'
                if 'view=1' in line and role != 1:
                    outer = field(outer, 'override', 'true')
                    desired, local, scratch = positions[role], 20., 0x30400000
                    private = bytearray(packet + matrix)
                    struct.pack_into('>I', private, 4, scratch + 16)
                    for i, (old, axis) in enumerate(zip((12, 24, 0, 1), (.75, -.25, .5, .125))):
                        struct.pack_into('>f', private, 64 + i * 4, single(old + single(single(desired - local) * axis)))
                    rows.append(f'coop apt preview: {context} ordinal={role} role={role} target=2 current={current} maximum=10000 scaled={scaled} '
                                f'width=200 start=100 notch=90 desired={desired} local={local} basis={basis.hex()} '
                                f'original_packet={packet.hex()} original_matrix={matrix.hex()} scratch={scratch:08X} '
                                f'private={private.hex()} unchanged=true override=true')
                rows.append(outer)
        elif 'apt pass:' in line:
            rows.append(line.replace('adapters=1 captured=1 nodes=4 node_captured=4 carriers=1',
                                     'adapters=5 captured=5 nodes=13 node_captured=13 carriers=5'))
            context = re.search(r'frame=\d+ generation=\d+ view=\d+', line)[0]
            rows.append(f'coop apt scopepass: {context} pushes=13 pops=13 valid=true balanced=true selected=5 mask=1 right=1 body=1 left=1 notch=1 override=false')
            applied = 4 if 'view=1' in line else 0
            rows.append(f'coop apt previewpass: {context} applied={applied} skipped={skipped if applied else 0} override={str(bool(applied)).lower()}')
        else:
            rows.append(line)
    return rows


class AptRenderEvidence(unittest.TestCase):
    def test_preview_positions_and_retained_effect_bytes(self):
        rows = preview_trace(skipped=2)
        result = report.analyze(rows, required_preview=True)
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual(result['ancestry']['selected_pairs'], 3)
        self.assertEqual(result['preview'], {
            'required': True, 'scope_samples': 6, 'packet_samples': 12,
            'verified_pairs': 3, 'generations': [3221225473], 'applied': 12,
            'skipped': 6, 'roles': {'2': 3, '3': 3, '4': 3, '5': 3},
            'targets': [2], 'fully_captured_passes': 6})
        self.assertIn('positions only', result['scope'])
        self.assertIn('original effect frames and shield colors retained', result['scope'])
        self.assertFalse(report.analyze(rows)['passed'])
        self.assertFalse(report.analyze(rows, required_ancestry=True)['passed'])
        self.assertFalse(report.analyze(trace(), required_preview=True)['passed'])
        # Preview lines precede native outer packets; joins must also allow the reverse.
        self.assertTrue(report.analyze(rows[:1] + list(reversed(rows[1:])), required_preview=True)['passed'])

    def test_preview_rejects_corrupt_math_and_geometry(self):
        rows = preview_trace()
        index = next(i for i, row in enumerate(rows) if 'apt preview:' in row)
        cases = {'target': ('0', '255', '4294967296'), 'current': ('-1', '10001'),
                 'maximum': ('9999', '20000'), 'scaled': ('1', '-1', '10001'),
                 'width': ('16', '4097', 'nan', 'inf'), 'start': ('nan',),
                 'notch': ('inf',), 'desired': ('85', 'nan', 'inf'),
                 'local': ('nan', '9000'), 'unchanged': ('false',), 'override': ('false',),
                 'role': ('0', '1', '6'), 'scratch': ('0', 'FFFFFFA1', '100000000', '30200000', '301FFFF0', '30300050')}
        for name, values in cases.items():
            for value in values:
                with self.subTest(field=name, value=value):
                    bad = rows.copy()
                    bad[index] = field(bad[index], name, value)
                    self.assertFalse(report.analyze(bad, required_preview=True)['passed'])
        for currents in ((1, 499, 500), (501, 9999, 10000), (7, 14, 28)):
            result = report.analyze(preview_trace(currents), required_preview=True)
            self.assertTrue(result['passed'], result['errors'])
        # Integer arithmetic would incorrectly accept scaled=7 for current=7.
        bad = [field(row, 'scaled', 7) if 'apt preview:' in row and 'current=7 ' in row else row
               for row in preview_trace((7, 14, 28))]
        self.assertFalse(report.analyze(bad, required_preview=True)['passed'])

    def test_preview_rejects_every_nontranslation_private_byte_change(self):
        rows = preview_trace()
        index = next(i for i, row in enumerate(rows) if 'apt preview:' in row)
        original = bytes.fromhex(re.search(r'private=(\w+)', rows[index])[1])
        for offset in list(range(64)) + list(range(80, 96)):
            with self.subTest(offset=offset):
                private = bytearray(original)
                private[offset] ^= 1
                bad = rows.copy()
                bad[index] = field(bad[index], 'private', private.hex())
                self.assertFalse(report.analyze(bad, required_preview=True)['passed'])
        for offset in (64, 68, 72, 76):
            for value in (999., float('nan'), float('inf')):
                with self.subTest(translation=offset, value=value):
                    private = bytearray(original)
                    struct.pack_into('>f', private, offset, value)
                    bad = rows.copy()
                    bad[index] = field(bad[index], 'private', private.hex())
                    self.assertFalse(report.analyze(bad, required_preview=True)['passed'])
        for name, size in (('basis', 64), ('original_packet', 16), ('original_matrix', 80), ('private', 96)):
            bad = rows.copy()
            bad[index] = field(bad[index], name, '00' * (size - 1))
            self.assertFalse(report.analyze(bad, required_preview=True)['passed'])
        for name in ('basis', 'original_matrix'):
            bad = rows.copy()
            data = bytearray.fromhex(re.search(rf'{name}=(\w+)', bad[index])[1])
            struct.pack_into('>f', data, 0, float('nan'))
            bad[index] = field(bad[index], name, data.hex())
            self.assertFalse(report.analyze(bad, required_preview=True)['passed'])

    def test_preview_pointer_ranges_joins_and_translation_tolerance(self):
        rows = preview_trace()
        index = next(i for i, row in enumerate(rows) if 'apt preview:' in row)
        outer_index = index + 1
        for scratch, valid in ((0xFFFFFFA0, True), (0xFFFFFFA1, False),
                               (0x30200000 + 80, True), (0x30200000 + 79, False),
                               (0x30200000 - 96, True), (0x30200000 - 95, False),
                               (0x30300000 + 84, False)):
            with self.subTest(scratch=hex(scratch)):
                changed = rows.copy()
                private = bytearray.fromhex(re.search(r'private=(\w+)', changed[index])[1])
                struct.pack_into('>I', private, 4, scratch + 16)
                changed[index] = field(field(changed[index], 'private', private.hex()), 'scratch', f'{scratch:08x}')
                result = report.analyze(changed, required_preview=True)
                self.assertEqual(result['passed'], valid, result['errors'])
        for transform, valid in ((0, False), (0xFFFFFFB0, True), (0xFFFFFFB1, False)):
            with self.subTest(transform=hex(transform)):
                changed = rows.copy()
                packet = bytearray.fromhex(re.search(r'original_packet=(\w+)', changed[index])[1])
                struct.pack_into('>I', packet, 4, transform)
                changed[index] = field(changed[index], 'original_packet', packet.hex())
                changed[outer_index] = field(field(changed[outer_index], 'packet', packet.hex()), 'transform', f'{transform:08x}')
                result = report.analyze(changed, required_preview=True)
                self.assertEqual(result['passed'], valid, result['errors'])
        for original, offset, private_offset in (('original_packet', 0, 0), ('original_matrix', 79, 95)):
            changed = rows.copy()
            data = bytearray.fromhex(re.search(rf'{original}=(\w+)', changed[index])[1])
            private = bytearray.fromhex(re.search(r'private=(\w+)', changed[index])[1])
            data[offset] ^= 1
            private[private_offset] ^= 1
            changed[index] = field(field(changed[index], original, data.hex()), 'private', private.hex())
            result = report.analyze(changed, required_preview=True)
            self.assertFalse(result['passed'])
            self.assertIn('preview/outer packet override, role or original readbacks disagree', result['errors'])
        for difference, valid in ((.0005, True), (.002, False)):
            changed = rows.copy()
            private = bytearray.fromhex(re.search(r'private=(\w+)', changed[index])[1])
            value = struct.unpack_from('>f', private, 64)[0]
            struct.pack_into('>f', private, 64, value + difference)
            changed[index] = field(changed[index], 'private', private.hex())
            result = report.analyze(changed, required_preview=True)
            self.assertEqual(result['passed'], valid, result['errors'])

    def test_preview_counts_respect_capture_limit_and_allow_binding_gaps(self):
        rows = []
        for row in preview_trace(skipped=2):
            if 'apt packet:' in row and 'ordinal=1 ' in row:
                rows.append(row)
                foreign = field(row.replace('20:6:172:2:1', '20:6:158:2:1'), 'role', 0)
                rows.extend(field(foreign, 'ordinal', ordinal) for ordinal in range(6, 257))
            elif 'apt pass:' in row:
                rows.append(row.replace('adapters=5 captured=5', 'adapters=257 captured=256').replace('carriers=5', 'carriers=256'))
            elif 'apt previewpass:' in row and 'view=1' in row:
                rows.append(field(row, 'applied', 5))
            else:
                rows.append(row)
        result = report.analyze(rows, required_preview=True)
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual(result['preview']['fully_captured_passes'], 0)
        self.assertEqual(result['preview']['applied'], 15)
        bad = [field(row, 'applied', 3) if 'apt previewpass:' in row and 'view=1' in row else row for row in rows]
        self.assertFalse(report.analyze(bad, required_preview=True)['passed'])
        # A sampled binding gap can have skipped attempts, zero applied records,
        # and all outer packets unchanged, provided three other frames verify.
        gap = []
        for row in preview_trace():
            if 'frame=120' not in row or 'apt preview:' in row:
                continue
            row = row.replace('frame=120', 'frame=480')
            if 'apt packet:' in row: row = field(row, 'override', 'false')
            if 'apt previewpass:' in row and 'view=1' in row:
                row = field(field(field(row, 'applied', 0), 'skipped', 4), 'override', 'false')
            gap.append(row)
        result = report.analyze(preview_trace() + gap, required_preview=True)
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual(result['preview']['verified_pairs'], 3)
        self.assertEqual(result['preview']['skipped'], 4)

    def test_preview_rejects_foreign_widgets_missing_joins_and_p1(self):
        rows = preview_trace()
        preview = next(row for row in rows if 'apt preview:' in row)
        outer = next(row for row in rows if 'apt packet:' in row and 'override=true' in row)
        summary = next(row for row in rows if 'apt previewpass:' in row and 'view=1' in row)
        changes = [(preview, None), (outer, None), (summary, None),
                   (preview, field(preview, 'view', 0)), (preview, field(preview, 'ordinal', 6)),
                   (preview, field(preview, 'frame', 480)), (preview, field(preview, 'generation', 3221225474)),
                   (preview, field(preview, 'role', 3)), (outer, field(outer, 'override', 'false')),
                   (outer, outer.replace('20:6:172:2:1', '20:6:158:2:1')),
                   (outer, field(outer, 'manager', 'FFFFFFF0')),
                   (outer, outer.replace('30:26:30:3:0', '30:26:39:3:0')),
                   (outer, field(outer, 'transform_unchanged', 'false')),
                   (outer, field(outer, 'packet_matches', 'false')),
                   (summary, field(summary, 'applied', 3)), (summary, field(summary, 'applied', 5)),
                   (summary, field(summary, 'override', 'false')), (summary, field(summary, 'skipped', -1))]
        for old, new in changes:
            with self.subTest(change=new):
                bad = [new if row == old else row for row in rows if new is not None or row != old]
                result = report.analyze(bad, required_preview=True)
                self.assertFalse(result['passed'], result)
        for duplicate in (preview, summary):
            self.assertFalse(report.analyze(rows + [duplicate], required_preview=True)['passed'])
        for counter in ('applied', 'skipped'):
            bad = [field(row, counter, 1) if 'apt previewpass:' in row and 'view=0' in row else row for row in rows]
            self.assertFalse(report.analyze(bad, required_preview=True)['passed'])

    def test_preview_requires_three_frames_and_every_composed_generation(self):
        rows = preview_trace()
        for bad in ([row for row in rows if 'frame=360' not in row],
                    [row for row in rows if not ('apt preview:' in row and 'role=5' in row)],
                    [row.replace('generation=3221225473', 'generation=3221225474')
                     if 'frame=360' in row and 'apt preview' not in row else row for row in rows]):
            self.assertFalse(report.analyze(bad, required_preview=True)['passed'])
        multiple = [row.replace('generation=3221225473', 'generation=3221225474')
                    if 'frame=360' in row else row for row in rows]
        result = report.analyze(multiple, required_preview=True)
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual(result['preview']['generations'], [3221225473, 3221225474])

    def test_preview_cli_implies_ancestry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log, output = root / 'preview.log', root / 'report.json'
            for rows, expected in ((preview_trace(), 0),
                                   ([row for row in preview_trace() if 'scopepass:' not in row], 1)):
                log.write_text('\n'.join(rows) + '\n', encoding='utf-8')
                result = subprocess.run([sys.executable, report.__file__, '--log', str(log),
                                         '--output', str(output), '--require-preview'], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                self.assertEqual(json.loads(output.read_text())['passed'], expected == 0)

    def test_ancestry_selects_only_the_exported_stamina_role_chain(self):
        rows = []
        for line in trace():
            if 'apt packet:' in line:
                data = re.search(r'carrier_data=(\w+)', line)[1]
                carrier = bytearray.fromhex(data)
                struct.pack_into('>I', carrier, 4, 30)
                line = line.replace(data, carrier.hex())
                for role, character, depth in [(1, 13, 24), (2, 26, 30), (3, 21, 34), (4, 27, 39), (5, 40, 43)]:
                    path = f'1:101:10:63:1:1:1;2:202:20:6:172:2:1;3:303:30:45:1:3:1;4:404:30:{character}:{depth}:3:0;5:600:30:15:1:3:0'
                    rows += [line.replace('ordinal=1', f'ordinal={role}') + f' carrier=00000258 role={role} scope_valid=true path={path}']
            elif 'apt pass:' in line:
                rows += [line.replace('adapters=1 captured=1 nodes=4 node_captured=4 carriers=1', 'adapters=5 captured=5 nodes=13 node_captured=13 carriers=5')]
                context = re.search(r'frame=\d+ generation=\d+ view=\d+', line)[0]
                rows += [f'coop apt scopepass: {context} pushes=13 pops=13 valid=true balanced=true selected=5 mask=1 right=1 body=1 left=1 notch=1 override=false']
            else:
                rows.append(line)
        self.assertTrue(report.analyze(rows, required_ancestry=True)['passed'])
        cases = [[row for row in rows if 'scopepass:' not in row],
                 [row.replace('balanced=true', 'balanced=false') for row in rows],
                 [row.replace('selected=5', 'selected=4') for row in rows],
                 [row.replace('pops=13', 'pops=12') for row in rows],
                 [row.replace('20:6:172:2:1', '20:6:158:2:1') for row in rows],
                 [row.replace('30:21:34:3:0', '30:21:13:3:0') for row in rows],
                 [row.replace('carrier=00000258', 'carrier=00000259') for row in rows]]
        for bad in cases:
            with self.subTest(bad=bad):
                self.assertFalse(report.analyze(bad, required_ancestry=True)['passed'])

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
