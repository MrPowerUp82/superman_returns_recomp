import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from native_frame_report import analyze


class NativeFrameReportTests(unittest.TestCase):
    def test_reads_rotated_parts_in_numeric_chronological_order(self):
        from native_frame_report import read_log
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'game.10.log').write_text('trace begin frame 7\n', encoding='utf-8')
            for number in range(3, 10):
                (root / f'game.{number}.log').write_text('ordinary log noise\n', encoding='utf-8')
            (root / 'game.2.log').write_text('trace draw p0 #0 prim 4 vs 000000000000000A ps 000000000000000B\n', encoding='utf-8')
            (root / 'game.1.log').write_text('trace end of frame\n', encoding='utf-8')
            (root / 'game.log').write_text('guest fps: 10\n', encoding='utf-8')
            (root / 'game.unrelated.log').write_text('trace begin frame 99\n', encoding='utf-8')
            self.assertEqual(analyze(read_log(root / 'game.log'), {'shaders': []})['draws'], 1)

    def test_rejects_missing_rotated_part_inside_trace(self):
        from native_frame_report import read_log
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'game.3.log').write_text('trace begin frame 7\n', encoding='utf-8')
            (root / 'game.1.log').write_text('trace end of frame\n', encoding='utf-8')
            (root / 'game.log').write_text('ordinary log noise\n', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'rotation gap'):
                analyze(read_log(root / 'game.log'), {'shaders': []})

    def test_allows_rotation_gap_before_selected_trace(self):
        from native_frame_report import read_log
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'game.3.log').write_text('ordinary log noise\n', encoding='utf-8')
            (root / 'game.1.log').write_text('trace begin frame 7\n', encoding='utf-8')
            (root / 'game.log').write_text('trace end of frame\n', encoding='utf-8')
            self.assertEqual(analyze(read_log(root / 'game.log'), {'shaders': []})['frame'], 7)

    def test_requires_complete_trace(self):
        for text in ['', 'trace begin frame 7\ntrace draw p0 #0 prim 4 vs 000000000000000A ps 000000000000000B']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                analyze(text, {'shaders': []})

    def test_requires_selection_for_multiple_frames(self):
        text = 'trace begin frame 7\ntrace end of frame (0 draws)\ntrace begin frame 8\ntrace end of frame (0 draws)'
        with self.assertRaises(ValueError):
            analyze(text, {'shaders': []})
        self.assertEqual(analyze(text, {'shaders': []}, 8)['frame'], 8)
        with self.assertRaises(ValueError):
            analyze(text, {'shaders': []}, 9)

    def test_joins_container_hash_and_keeps_unknown(self):
        catalog = {'shaders': [{'container_hash': '000000000000000A', 'ucode_hash': '000000000000000B',
                                'type': 'vs', 'reflection': {'constants': []}}]}
        text = ('trace draw p0 #9 prim 4 vs 000000000000000C ps 000000000000000D\n'
                'trace begin frame 7 after_guest_swap=100\n'
                'trace draw p0 #0 prim 4 vs 000000000000000a ps 000000000000000B stride0 32\n'
                'trace end of frame (1 draws since last event)')
        result = analyze(text, catalog)
        self.assertEqual(result['draws'], 1)
        self.assertEqual(result['after_guest_swap'], 100)
        self.assertEqual([(s['stage'], s['hash'], s['draws']) for s in result['unknown_shaders']],
                         [('ps', '000000000000000B', 1)])
        self.assertEqual(len(result['shaders']), 2)

    def test_retains_event_order_and_matrix_stage(self):
        catalog = {'shaders': [{'container_hash': '000000000000000A', 'type': 'vs',
                    'reflection': {'constants': [{'name': 'gViewProjectionXform', 'reg': 55, 'count': 4},
                                                {'name': 'unrelated', 'reg': 1, 'count': 1}]}}]}
        text = ('[info] trace begin frame 7\n'
                '[info] trace p0 after 0 draws: bind RT [surface] DS depth\n'
                '[info] guest fps: 10\n'
                '[info] trace draw p0 #0 prim 4 vs 000000000000000A ps 000000000000000B\n'
                '[info] trace p0 after 1 draws: resolve flags 1 src surface\n'
                '[info] trace end of frame (0 draws since last event)')
        result = analyze(text, catalog)
        self.assertEqual([e['kind'] for e in result['events']], ['bind', 'draw', 'resolve'])
        shader = next(s for s in result['shaders'] if s['stage'] == 'vs')
        self.assertEqual(shader['candidate_constants'], [{'name': 'gViewProjectionXform', 'reg': 55, 'count': 4}])
        self.assertFalse(result['roles_confirmed'])

    def test_rejects_nested_or_repeated_frame_boundaries(self):
        for text in ['trace begin frame 1\ntrace begin frame 2\ntrace end of frame',
                     'trace begin frame 1\ntrace end of frame\ntrace begin frame 1\ntrace end of frame']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                analyze(text, {'shaders': []})


if __name__ == '__main__':
    unittest.main()
