"""Synthetic data only. Run with unittest discovery from tools/shaders/."""
import hashlib
import struct
import unittest

from cache_audit import audit_route_text, inspect_library, compare_corpus, inspect_pipeline_cache
from make_preshaders import fnv1a64
from pack_pipeline_seed import HEADER, merge_seeds


def container(vertex=True):
    return struct.pack('>3I', 0x102A1101 if vertex else 0x102A1100, 12, 12) + bytes(12)


def library(entries):
    body = b''.join(struct.pack('<QIIII', key, stage, len(raw), len(dx), 0) + raw + dx
                    for key, stage, raw, dx in entries)
    return b'SRSHLIB\0' + struct.pack('<IIQ', 1, len(entries), fnv1a64(body)) + body


class CacheAuditTests(unittest.TestCase):
    def test_library_cannot_claim_coverage_with_wrong_or_missing_container(self):
        raw = container()
        entries = inspect_library(library([(11, 0, raw, b'DXBCtest')]), 'd3d12')
        result = compare_corpus({('vs', hashlib.sha256(raw).hexdigest()), ('ps', 'missing')}, entries)
        self.assertFalse(result['complete'])
        self.assertEqual(result['covered'], 1)
        self.assertEqual(result['total'], 2)

    def test_truncated_and_empty_bytecode_are_rejected_even_with_valid_checksum(self):
        raw = container()
        with self.assertRaises(ValueError):
            inspect_library(library([(11, 0, raw, b'')]), 'd3d12')
        with self.assertRaises(ValueError):
            inspect_library(library([(11, 0, raw, b'DXBCtest')])[:-1], 'd3d12')

    def test_duplicate_shader_and_stage_mismatch_are_rejected(self):
        raw = container()
        for rows in [[(11, 0, raw, b'DXBCtest')] * 2, [(11, 1, raw, b'DXBCtest')]]:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                inspect_library(library(rows), 'd3d12')

    def test_no_events_is_unknown_not_zero_miss(self):
        result = audit_route_text('native: pipeline cache: 813 pipelines precompiled', 'title')
        self.assertFalse(result['zero_runtime_misses_demonstrated'])
        self.assertIn('missing_route_boundaries', result['insufficient_evidence'])

    def test_explicit_positive_window_is_required(self):
        begin = 'native cache audit: route_begin name=title\n'
        end = ('native cache audit: route_end name=title frames=120 draws=1000 '
               'pso_runtime_creates=0 shader_runtime_compiles=0 shader_misses=0\n')
        self.assertTrue(audit_route_text(begin + end, 'title')['zero_runtime_misses_demonstrated'])
        self.assertFalse(audit_route_text(begin + end.replace('draws=1000', 'draws=0'), 'title')
                         ['zero_runtime_misses_demonstrated'])

    def test_miss_or_compile_event_cannot_be_hidden_by_zero_summary(self):
        text = ('native cache audit: route_begin name=title\n'
                'native: PSO #1 created in 0.2 ms\n'
                'runtime shaders: 1 translated, 0 failed, 0 queued, avg 1 ms\n'
                'native cache audit: route_end name=title frames=120 draws=1000 '
                'pso_runtime_creates=0 shader_runtime_compiles=0 shader_misses=0\n')
        result = audit_route_text(text, 'title')
        self.assertFalse(result['zero_runtime_misses_demonstrated'])
        self.assertEqual(result['events_in_window']['pso_runtime_creates'], 1)
        self.assertEqual(result['events_in_window']['shader_translation_summaries'], 1)

    def test_route_window_does_not_hide_session_history(self):
        text = ('native: PSO #1 created in 0.2 ms\n'
                'native cache audit: route_begin name=title\n'
                'native cache audit: route_end name=title frames=120 draws=1000 '
                'pso_runtime_creates=0 shader_runtime_compiles=0 shader_misses=0\n')
        result = audit_route_text(text, 'title')
        self.assertTrue(result['zero_runtime_misses_demonstrated'])
        self.assertEqual(result['session_events']['pso_runtime_creates'], 1)

    def test_duplicate_boundaries_fail_closed(self):
        text = ('native cache audit: route_begin name=title\n' * 2 +
                'native cache audit: route_end name=title frames=120 draws=1000 '
                'pso_runtime_creates=0 shader_runtime_compiles=0 shader_misses=0\n')
        self.assertFalse(audit_route_text(text, 'title')['zero_runtime_misses_demonstrated'])

    def test_interrupted_route_is_not_accepted_despite_zero_counters(self):
        text = ('native cache audit: route_begin name=title\n'
                'native cache audit: route_end name=title frames=120 draws=1000 '
                'pso_runtime_creates=0 shader_runtime_compiles=0 shader_misses=0 valid=0\n')
        self.assertFalse(audit_route_text(text, 'title')['zero_runtime_misses_demonstrated'])

    def test_vulkan_external_scope_and_inflight_jobs_cannot_certify_zero(self):
        markers=('native cache audit: route_begin name=title\n'
                 'native cache audit: route_end name=title frames=120 draws=1000 '
                 'pso_runtime_creates=0 shader_runtime_compiles=0 shader_misses=0 valid=1\n')
        for prefix in ['native cache audit: vulkan_uninstrumented_scope feature=frame_generation\n',
                       'native cache audit: vulkan_inflight_shader_jobs count=1\n']:
            result=audit_route_text(prefix+'native cache audit: vulkan_totals runtime_attempts=0\n'+markers,'title')
            self.assertFalse(result['zero_runtime_misses_demonstrated'])


class MergeSeedTests(unittest.TestCase):
    def test_merges_captures_preserving_distinct_descriptions_with_same_key(self):
        first = struct.pack('<Q', 11) + bytes(24)
        alias = struct.pack('<Q', 22) + first[8:]
        changed = first[:-1] + b'\1'
        seed, counts = merge_seeds([HEADER.pack(b'CNPS', 1, 32, 0) + first,
                                   HEADER.pack(b'CNPS', 1, 32, 0) + alias + changed])
        self.assertEqual(counts, [1, 2])
        self.assertEqual(seed, HEADER.pack(b'CNPS', 1, 32, 0) + first + changed)

    def test_rejects_mixed_record_abis(self):
        with self.assertRaises(ValueError):
            merge_seeds([HEADER.pack(b'CNPS', 1, 32, 0) + bytes(32),
                         HEADER.pack(b'CNPS', 1, 40, 0) + bytes(40)])

    def test_v1_base_description_is_promoted_when_merging_v2_variants(self):
        base = struct.pack('<Q', 11) + bytes(24)
        variant = base + struct.pack('<II', 2, 0)
        seed, counts = merge_seeds([HEADER.pack(b'CNPS', 1, 32, 0) + base,
                                   HEADER.pack(b'CNPS', 2, 40, 0) + variant])
        self.assertEqual(counts, [1, 1])
        self.assertEqual(seed, HEADER.pack(b'CNPS', 2, 40, 0) + base + bytes(8) + variant)

    def test_seed_audit_preserves_variant_domain_and_missing_originals(self):
        record = struct.pack('<3Q', 11, 22, 33) + bytes(696 - 24) + struct.pack('<II', 2, 0)
        report = inspect_pipeline_cache(HEADER.pack(b'CNPS', 2, 704, 0) + record,
                                        {('vs', 'digest'): 22})
        self.assertEqual(report['pixel_variants']['coverage'], 1)
        self.assertEqual(report['missing_original_shaders'], [('ps', '0000000000000021')])


if __name__ == '__main__':
    unittest.main()
