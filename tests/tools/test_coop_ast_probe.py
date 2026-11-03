import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec = importlib.util.spec_from_file_location(
    "coop_ast_probe", Path(__file__).resolve().parents[2] / "tools/analysis/coop_ast_probe.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class CoopArchives(unittest.TestCase):
    def test_refpack_overlapping_backreference(self):
        # Four literals followed by a 32-byte copy from four bytes back.
        packed = bytes.fromhex("10 fb 00 00 24 e0 41 42 43 44 9c 00 03 fc")
        self.assertEqual(probe.refpack(packed), b"ABCD" * 9)
        with self.assertRaises(ValueError):
            probe.refpack(packed[:4] + b"\x25" + packed[5:])

    def test_archive_offsets_and_zlib(self):
        raw = b"function main() end\n" * 20
        packed = zlib.compress(raw)
        data = bytearray(256 + len(packed))
        data[:8] = b"BGFA1.05"
        struct.pack_into("<II", data, 12, 1, 128)
        data[32:40] = bytes([0, 0, 8, 2, 2, 2, 0, 0])
        struct.pack_into("<II", data, 40, 0, 8)
        data[128] = 0x11
        struct.pack_into("<HHH", data, 137, 256, len(packed), len(raw) - len(packed))
        data[143:151] = b"SCRIPT\0\0"
        data[256:] = packed
        self.assertEqual(list(probe.entries(data)), [("SCRIPT", raw)])
        struct.pack_into("<H", data, 137, len(data))
        with self.assertRaises(ValueError):
            list(probe.entries(data))


if __name__ == "__main__":
    unittest.main()
