import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from check_stacked_dump import check_dump


class StackedDumpTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def write_slice(self, z, values=(0, 32767, -32768, 16384), *,
                    pitch=12, fmt=37, identity='A', padding=0xA5, legacy=False):
        suffix = '' if legacy else f'_z{z:03}'
        path = self.root / f'cap_resolve_1E820000_f{fmt}_2x1_id{identity}{suffix}.raw'
        payload = struct.pack('<4e' if fmt == 34 else '<4h', *values)
        path.write_bytes(struct.pack('<4I', 2, 1, fmt, pitch) + payload +
                         bytes([padding]) * max(0, pitch - len(payload)))
        return path

    def test_four_slices_decode_snorm_and_ignore_padding_for_distinctness(self):
        a = self.write_slice(0)
        self.write_slice(1, padding=0xCC)
        self.write_slice(2, (1, 2, 3, 4))
        self.write_slice(3, (0, 0, 0, 0))
        report = check_dump(self.root, 'cap_', depth=4)
        self.assertEqual(report['depth'], 4)
        self.assertEqual(report['header']['format_name'], 'R16G16_SNORM')
        self.assertEqual(report['distinct_pixel_slices'], 3)
        self.assertEqual(report['identical_slice_groups'], [[0, 1]])
        self.assertEqual(report['slices'][0]['sha256'], hashlib.sha256(a.read_bytes()).hexdigest())
        red, green = report['slices'][0]['channels']
        self.assertEqual((red['min'], red['max'], red['mean']), (-1.0, 0.0, -0.5))
        self.assertEqual(green['max'], 1.0)
        self.assertAlmostEqual(green['min'], 16384 / 32767)
        self.assertEqual(report['slices'][3]['channels'][0]['zero'], 2)

    def test_missing_duplicate_and_out_of_range_slices_are_rejected(self):
        for z in (0, 1, 3):
            self.write_slice(z)
        with self.assertRaisesRegex(ValueError, 'missing.*2'):
            check_dump(self.root, 'cap_', depth=4)
        self.write_slice(2)
        self.write_slice(4)
        with self.assertRaisesRegex(ValueError, 'unexpected.*4'):
            check_dump(self.root, 'cap_', depth=4)
        self.root.joinpath('cap_resolve_1E820000_f37_2x1_idA_z004.raw').unlink()
        self.root.joinpath('cap_resolve_1E820000_f37_2x1_idA_z0.raw').write_bytes(
            self.root.joinpath('cap_resolve_1E820000_f37_2x1_idA_z000.raw').read_bytes())
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            check_dump(self.root, 'cap_', depth=4)

    def test_slices_from_different_resource_identities_cannot_form_a_set(self):
        self.write_slice(0, identity='A')
        self.write_slice(1, identity='B')
        with self.assertRaisesRegex(ValueError, 'identit'):
            check_dump(self.root, 'cap_', depth=2)

    def test_inconsistent_header_and_filename_are_rejected(self):
        self.write_slice(0)
        path = self.write_slice(1, pitch=16)
        with self.assertRaisesRegex(ValueError, 'header'):
            check_dump(self.root, 'cap_', depth=2)
        path.write_bytes(struct.pack('<4I', 1, 1, 37, 12) + bytes(12))
        with self.assertRaisesRegex(ValueError, 'filename'):
            check_dump(self.root, 'cap_', depth=2)

    def test_truncated_header_payload_extra_bytes_and_invalid_pitch_are_rejected(self):
        path = self.write_slice(0)
        original = path.read_bytes()
        for payload in (original[:15], original[:-1], original + b'X',
                        struct.pack('<4I', 2, 1, 37, 4) + bytes(4)):
            with self.subTest(size=len(payload)):
                path.write_bytes(payload)
                with self.assertRaises(ValueError):
                    check_dump(self.root, 'cap_', depth=1)

    def test_dxgi34_float16_counts_nonfinite_values_without_invalid_json_numbers(self):
        self.write_slice(0, (float('nan'), float('inf'), -2.0, float('-inf')), fmt=34)
        report = check_dump(self.root, 'cap_', depth=1)
        red, green = report['slices'][0]['channels']
        self.assertEqual((red['nan'], red['finite'], red['min']), (1, 1, -2.0))
        self.assertEqual((green['positive_infinity'], green['negative_infinity']), (1, 1))
        self.assertIsNone(green['mean'])
        json.dumps(report, allow_nan=False)

    def test_multiple_rows_strip_each_padding_region_and_custom_base_is_selected(self):
        self.write_slice(0)  # Different base must not enter the selected set.
        path = self.root / 'cap_resolve_12345000_f37_1x2_idB_z000.raw'
        pixels = struct.pack('<2h', 0, 32767) + struct.pack('<2h', -32767, 0)
        path.write_bytes(struct.pack('<4I', 1, 2, 37, 8) +
                         pixels[:4] + b'\xff' * 4 + pixels[4:] + b'\xff' * 4)
        report = check_dump(self.root, 'cap_', depth=1, base='0x12345000')
        self.assertEqual(report['base'], '12345000')
        self.assertEqual(report['slices'][0]['pixel_sha256'], hashlib.sha256(pixels).hexdigest())
        red, green = report['slices'][0]['channels']
        self.assertEqual((red['samples'], red['mean'], green['mean']), (2, -0.5, 0.5))

    def test_unknown_format_and_nonpositive_depth_fail_closed(self):
        self.write_slice(0, fmt=99)
        with self.assertRaisesRegex(ValueError, 'unsupported DXGI'):
            check_dump(self.root, 'cap_', depth=1)
        for depth in (None, 0, -1):
            with self.subTest(depth=depth), self.assertRaisesRegex(ValueError, 'depth'):
                check_dump(self.root, 'cap_', depth=depth)

    def test_legacy_2d_is_explicit_and_cannot_mask_a_missing_volume(self):
        self.write_slice(0, legacy=True)
        with self.assertRaises(ValueError):
            check_dump(self.root, 'cap_', depth=4)
        report = check_dump(self.root, 'cap_', legacy_2d=True)
        self.assertEqual((report['mode'], report['depth']), ('2d', 1))
        with self.assertRaises(ValueError):
            check_dump(self.root, 'cap_', legacy_2d=True, depth=1)

    def test_prefix_base_and_depth_are_explicit_cli_contracts(self):
        self.write_slice(0)
        with self.assertRaises(ValueError):
            check_dump(self.root, 'other_', depth=1)
        with self.assertRaises(ValueError):
            check_dump(self.root, 'cap_', depth=1, base='NOTHEX')
        script = Path(__file__).resolve().parents[2] / 'tools/analysis/check_stacked_dump.py'
        cmd = [sys.executable, str(script), str(self.root), '--prefix', 'cap_']
        self.assertNotEqual(subprocess.run(cmd, capture_output=True).returncode, 0)
        run = subprocess.run(cmd + ['--depth', '1'], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout)['depth'], 1)


if __name__ == '__main__':
    unittest.main()
