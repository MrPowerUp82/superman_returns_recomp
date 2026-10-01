import struct

import pytest

from inspect_lsfg_dll import is_compute_spirv, read_rcdata


def fixture_pe():
    """One numeric resource in a synthetic PE, no proprietary shader data."""
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<H", data, 134, 1)
    struct.pack_into("<H", data, 148, 240)
    struct.pack_into("<H", data, 152, 0x20B)
    struct.pack_into("<I", data, 260, 16)
    struct.pack_into("<II", data, 280, 0x1000, 512)
    struct.pack_into("<IIII", data, 400, 512, 0x1000, 512, 512)
    for offset in (512, 536, 560):
        struct.pack_into("<HH", data, offset + 12, 0, 1)
    struct.pack_into("<II", data, 528, 10, 0x80000018)
    struct.pack_into("<II", data, 552, 305, 0x80000030)
    struct.pack_into("<II", data, 576, 1033, 72)
    struct.pack_into("<II", data, 584, 0x1060, 4)
    data[608:612] = b"test"
    return data


def test_numeric_resource():
    assert read_rcdata(fixture_pe()) == {305: b"test"}


@pytest.mark.parametrize("data", [b"", b"MZ", fixture_pe()[:600]])
def test_truncated_pe_rejected(data):
    with pytest.raises(ValueError):
        read_rcdata(data)


def test_out_of_bounds_resource_rejected():
    data = fixture_pe()
    struct.pack_into("<II", data, 584, 0x11FF, 4)
    with pytest.raises(ValueError, match="RVA"):
        read_rcdata(data)


def test_directory_cycle_at_language_rejected():
    data = fixture_pe()
    struct.pack_into("<I", data, 580, 0x80000000)
    with pytest.raises(ValueError, match="data entry"):
        read_rcdata(data)


def test_compute_entry_point_and_instruction_bounds():
    # Header plus OpEntryPoint GLCompute %1 "main".
    words = [0x07230203, 0x10000, 0, 2, 0, (5 << 16) | 15,
             5, 1, 0x6E69616D, 0]
    blob = struct.pack("<10I", *words)
    assert is_compute_spirv(blob)
    assert not is_compute_spirv(blob[:-4])
    assert not is_compute_spirv(blob + b"\0\0\0\0")
    words[6] = 4  # Fragment, not compute.
    assert not is_compute_spirv(struct.pack("<10I", *words))
