"""Reject misleading object-motion attribution in offline captures."""
import csv
import struct
import tempfile
import unittest
from pathlib import Path
import numpy as np
from inspect_rigid_camera_motion import inspect

class OwnerCameraTests(unittest.TestCase):
    def fixture(self, directory):
        suffix = 's1'
        values = np.zeros(38, dtype='<f4')
        values[:16] = np.eye(4).reshape(-1)
        values[16:32] = np.eye(4).reshape(-1)
        values[32:34] = (6, 1)
        values.view('<u4')[34] = 1
        reference = directory / ('motion_camera_reference_' + suffix + '.bin')
        reference.write_bytes(struct.pack('<4I', 0x4d564352, 1, 38, 0) + values.tobytes())
        def raw(name, fmt, values):
            directory.joinpath(name + '_' + suffix + '.raw').write_bytes(struct.pack('<4I', 6, 1, fmt, values.nbytes) + values.tobytes())
        raw('object_motion_depth', 40, np.full(6, .5, dtype='<f4'))
        velocity = np.zeros((1, 6, 2), dtype='<f2')
        velocity[0, :, 0] = np.arange(2, 8)
        for name in ('object_motion_velocity', 'observed_rigid_motion_velocity'):
            raw(name, 34, velocity)
        raw('object_motion_validity', 62, np.array([1, 1, 1, 0, 1, 1], dtype='u1'))
        raw('observed_rigid_motion_validity', 62, np.array([1, 1, 1, 1, 0, 1], dtype='u1'))
        raw('object_pixel_owners', 42, np.array([1, 2, 3 | 0x40000000, 4, 5, 6], dtype='<u4'))
        manifest = directory / ('object_motion_draws_' + suffix + '.csv')
        with manifest.open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=['owner', 'vs', 'valid', 'proof'])
            writer.writeheader()
            for owner in range(1, 7):
                writer.writerow(dict(owner=owner, vs='AB' if owner < 6 else 'CD', valid=1, proof=1 if owner == 2 else 2))
        return reference, manifest

    def test_requires_production_proof_and_not_private_diagnostic_coverage(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            reference, _ = self.fixture(directory)
            families = inspect(directory, reference, 'd32', [0xAB, 0xEE])['owner_sources']
            selected = families['00000000000000AB']
            self.assertEqual(selected['valid_production_pixels'], 2)
            self.assertEqual(selected['camera_residual_percentiles_pixels'][0], 2.)
            self.assertEqual(selected['camera_residual_percentiles_pixels'][-1], 6.)
            self.assertEqual(selected['residual_above_pixels']['1'], 2)
            self.assertEqual(families['00000000000000EE']['valid_production_pixels'], 0)
            self.assertEqual(families['00000000000000EE']['camera_residual_percentiles_pixels'], [])

    def test_duplicate_owner_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            reference, manifest = self.fixture(directory)
            with manifest.open('a') as stream:
                stream.write('1,AB,1,2\n')
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                inspect(directory, reference, 'd32', [0xAB])

    def test_mismatched_owner_footprint_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            reference, _ = self.fixture(directory)
            directory.joinpath('object_pixel_owners_s1.raw').write_bytes(struct.pack('<4I', 5, 1, 42, 24) + bytes(24))
            with self.assertRaisesRegex(ValueError, 'footprint'):
                inspect(directory, reference, 'd32', [0xAB])

if __name__ == '__main__':
    unittest.main()
