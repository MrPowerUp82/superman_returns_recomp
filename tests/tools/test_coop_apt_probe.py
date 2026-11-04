import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'coop_apt_probe', Path(__file__).resolve().parents[2] / 'tools/analysis/coop_apt_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class Fixture:
    def __init__(self):
        self.apt = bytearray(b'Apt Data:1:7:4\x1a\0')
        self.root = self.alloc(64)
        self.frames = self.alloc(8)
        chars = self.alloc(4)
        controls = self.alloc(4)
        self.control = self.alloc(12)
        self.word(chars, self.root)
        self.word(controls, self.control)
        self.controls = controls
        struct.pack_into('>II', self.apt, self.frames, 1, controls)
        struct.pack_into('>16I', self.apt, self.root, 9, 0x09876543, 0, 0,
                         1, self.frames, 0, 1, chars, 1280, 720, 16, 0, 0, 0, 0)
        self.const = bytearray(b'Apt constant file\x1a\0\0')
        self.const += struct.pack('>III', self.root, 4, 32)
        self.const += struct.pack('>8I', 1, 64, 4, 1, 7, 10000, 6, 0x3F000000)
        self.const += b'SetValue\0'

    def alloc(self, count):
        self.apt.extend(b'\0' * (-len(self.apt) % 4))
        start = len(self.apt)
        self.apt.extend(b'\0' * count)
        return start

    def word(self, at, value):
        struct.pack_into('>I', self.apt, at, value)

    def actions(self, code):
        self.start = self.alloc(len(code))
        self.apt[self.start:self.start + len(code)] = code
        struct.pack_into('>III', self.apt, self.control, 8, 0, self.start)
        return self.start

    def function(self):
        # Absolute file alignment, typed constant pool, anonymous assigned method.
        start = self.actions(bytes.fromhex(
            '88 00 00 00 00 00 00 01 00 00 00 00 '
            'a2 00 8e 00 00 00 00 00 00 00 00 00 '
            '00 05 00 01 00 00 00 00 00 00 00 15 '
            '98 76 54 32 12 34 56 78 '
            '96 00 00 00 00 00 00 02 00 00 00 00 '
            'b4 3c 23 d7 0a b5 ff 0c 3e 4f 00'))
        dictionary = self.alloc(4)
        self.word(dictionary, 0)
        values = self.alloc(8)
        self.word(values, 1)
        self.word(values + 4, 2)
        self.word(start + 8, dictionary)
        self.word(start + 52, values)
        return start


class AptEvidence(unittest.TestCase):
    def test_decodes_typed_constants_function_body_and_ppc_immediates(self):
        f = Fixture()
        start = f.function()
        result = probe.inspect_pair(f.apt, f.const)
        self.assertEqual(result['scope'], 'static APT structure and bytecode; execution not observed')
        self.assertEqual(result['constants'][1], {'type': 'register', 'value': 1})
        self.assertEqual(result['movie']['width'], 1280)
        stream = result['streams'][0]
        self.assertEqual(stream['context'], {'character': 0, 'frame': 0, 'control': 0, 'kind': 'init_action', 'sprite': 0})
        function = stream['instructions'][2]
        self.assertEqual(function['offset'], start + 14)
        self.assertEqual(function['assigned_member'], 'SetValue')
        self.assertEqual(function['register_count'], 5)
        self.assertEqual(function['flags'], 1)
        body = function['body']
        self.assertEqual(body[0]['values'], [{'type': 'register', 'value': 1}, {'type': 'integer', 'value': 10000}])
        self.assertAlmostEqual(body[1]['value'], .01, places=7)
        self.assertEqual(body[2]['value'], -1)
        self.assertEqual(body[-1]['name'], 'Return')

    def test_preserves_branch_target_and_rejects_mid_operand_target(self):
        f = Fixture()
        start = f.actions(bytes.fromhex('73 9d 00 00 00 00 00 01 17 00'))
        result = probe.inspect_pair(f.apt, f.const)
        self.assertEqual(result['streams'][0]['instructions'][1]['target'], start + 9)
        f.word(start + 4, 0xFFFFFFFD)
        with self.assertRaisesRegex(ValueError, 'branch'):
            probe.inspect_pair(f.apt, f.const)

    def test_fails_closed_for_corrupt_headers_pointers_counts_and_opcodes(self):
        for kind in ['tag', 'const_magic', 'const_count', 'string', 'root', 'frame',
                     'controls', 'stream', 'opcode', 'truncated', 'dictionary', 'function_extent']:
            with self.subTest(kind=kind):
                f = Fixture()
                start = f.function()
                if kind == 'tag': f.apt[13] = ord('8')
                elif kind == 'const_magic': f.const[0] = 0
                elif kind == 'const_count': struct.pack_into('>I', f.const, 24, 0xFFFFFFFF)
                elif kind == 'string': struct.pack_into('>I', f.const, 36, len(f.const))
                elif kind == 'root': struct.pack_into('>I', f.const, 20, len(f.apt))
                elif kind == 'frame': f.word(f.root + 20, len(f.apt))
                elif kind == 'controls': f.word(f.frames, 0xFFFFFFFF)
                elif kind == 'stream': f.word(f.control + 8, 0)
                elif kind == 'opcode': f.apt[start] = 0x80
                elif kind == 'truncated': del f.apt[start + 1:]
                elif kind == 'dictionary': f.apt[start + 13] = 5
                elif kind == 'function_extent': f.word(start + 32, 0x7FFFFFFF)
                with self.assertRaises(ValueError):
                    probe.inspect_pair(f.apt, f.const)

    def test_unknown_timeline_control_is_not_silently_ignored(self):
        f = Fixture()
        f.actions(b'\0')
        f.word(f.control, 99)
        with self.assertRaisesRegex(ValueError, 'control'):
            probe.inspect_pair(f.apt, f.const)

    def test_placed_instance_and_clip_actions_use_offsets_after_six_float_matrix(self):
        f = Fixture()
        placement = f.alloc(76)
        name = f.alloc(8)
        f.apt[name:name + 8] = b'mNotch\0\0'
        events = f.alloc(8)
        event = f.alloc(12)
        code = f.alloc(1)
        struct.pack_into('>II', f.apt, events, 1, event)
        struct.pack_into('>III', f.apt, event, 0x80, 0, code)
        struct.pack_into('>4I', f.apt, placement, 9, 0xE6, 3, 0)
        struct.pack_into('>6f', f.apt, placement + 16, 1, 0, 0, 1, 12, 24)
        f.word(placement + 52, name)
        f.word(placement + 56, 0xFFFFFFFF)  # Clip depth is not the event pointer.
        f.word(placement + 60, events)
        f.word(f.controls, placement)
        result = probe.inspect_pair(f.apt, f.const)
        self.assertEqual(result['movie']['characters'][0]['frames'][0][0]['instance_name'], 'mNotch')
        self.assertEqual(result['movie']['characters'][0]['frames'][0][0]['matrix'], [1, 0, 0, 1, 12, 24])
        self.assertEqual(result['movie']['characters'][0]['frames'][0][0]['clip_depth'], 0xFFFFFFFF)
        self.assertEqual(result['streams'][0]['context']['kind'], 'clip_action')
        self.assertEqual(result['streams'][0]['context']['triggers'], 0x80)

    def test_unterminated_stream_and_nonfinite_numbers_are_rejected(self):
        f = Fixture()
        f.actions(b'\x04')
        with self.assertRaises(ValueError):
            probe.inspect_pair(f.apt, f.const)
        f = Fixture()
        start = f.function()
        f.apt[start + 57:start + 61] = bytes.fromhex('7fc00000')
        with self.assertRaisesRegex(ValueError, 'nonfinite'):
            probe.inspect_pair(f.apt, f.const)
        f = Fixture()
        f.actions(b'\0')
        struct.pack_into('>I', f.const, 60, 0x7F800000)
        with self.assertRaisesRegex(ValueError, 'nonfinite'):
            probe.inspect_pair(f.apt, f.const)

    def test_root_cannot_be_omitted_from_character_inventory(self):
        f = Fixture()
        f.actions(b'\0')
        f.word(f.root + 28, 0)
        with self.assertRaisesRegex(ValueError, 'root movie'):
            probe.inspect_pair(f.apt, f.const)

    def test_aggregate_action_budget_counts_referenced_pool_items(self):
        f = Fixture()
        f.function()
        inspector = probe.Inspector(f.apt, f.const)
        inspector.budget = 13  # All non-pool records fit; the three expanded values do not.
        with self.assertRaisesRegex(ValueError, 'budget'):
            inspector.inspect()

    def test_parameter_expansion_also_consumes_the_aggregate_budget(self):
        f = Fixture()
        start = f.function()
        parameter = f.alloc(8)
        name = f.alloc(8)
        f.apt[name:name + 8] = b'value\0\0\0'
        struct.pack_into('>II', f.apt, parameter, 2, name)
        f.word(start + 20, 1)
        f.word(start + 28, parameter)
        result = probe.inspect_pair(f.apt, f.const)
        self.assertEqual(result['streams'][0]['instructions'][2]['params'], [{'name': 'value', 'register': 2}])
        inspector = probe.Inspector(f.apt, f.const)
        inspector.budget = 16  # Non-parameter records including expanded pools fit exactly.
        with self.assertRaisesRegex(ValueError, 'budget'):
            inspector.inspect()

    def test_assignment_pattern_cannot_read_outside_enclosing_function(self):
        f = Fixture()
        start = f.function()
        rows, end = probe.Inspector(f.apt, f.const).actions(start, start + 65)
        self.assertEqual(end, start + 65)
        self.assertNotIn('assigned_member', rows[2])

    def test_cli_preserves_inputs_and_rejects_corrupt_asset_without_writing_output(self):
        f = Fixture()
        f.function()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            apt, const, output = root / 'movie.apt', root / 'movie.const', root / 'evidence.json'
            apt.write_bytes(f.apt)
            const.write_bytes(f.const)
            command = [sys.executable, str(Path(probe.__file__)), '--apt', str(apt), '--const', str(const)]
            success = subprocess.run(command + ['--output', str(output)], capture_output=True, text=True)
            self.assertEqual(success.returncode, 0, success.stderr)
            self.assertTrue(output.exists())
            overwrite = subprocess.run(command + ['--output', str(apt)], capture_output=True, text=True)
            self.assertNotEqual(overwrite.returncode, 0)
            self.assertIn('differ', overwrite.stderr)
            self.assertEqual(apt.read_bytes(), f.apt)
            apt.write_bytes(b'bad')
            output.unlink()
            corrupt = subprocess.run(command + ['--output', str(output)], capture_output=True, text=True)
            self.assertNotEqual(corrupt.returncode, 0)
            self.assertFalse(output.exists())
            self.assertEqual(const.read_bytes(), f.const)


if __name__ == '__main__':
    unittest.main()
