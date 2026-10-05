"""tools/analysis/xex_libraries.py on synthetic XEX2 headers (no game files)."""
import struct

import pytest

import xex_libraries


def make_xex(libs, extra_keys=()):
    table = b"".join(struct.pack(">8sHHHBB", n.encode(), *v) for n, v in libs)
    keys = list(extra_keys) + [(0x000200FF, None)]
    header_size = 24 + 8 * len(keys)
    out = bytearray(b"XEX2" + struct.pack(">5I", 0, 0x3000, 0, 0x100, len(keys)))
    lib_offset = header_size + 16
    for key, value in keys:
        out += struct.pack(">II", key, lib_offset if value is None else value)
    out += b"\0" * (lib_offset - len(out))
    out += struct.pack(">I", 4 + len(table)) + table
    return bytes(out)


def test_lists_libraries_with_versions():
    data = make_xex([("D3D9", (2, 0, 5632, 2, 1)), ("XAPILIB", (2, 0, 5632, 2, 0))],
                    extra_keys=[(0x00010001, 0x82000000)])
    libs = xex_libraries.parse_static_libraries(data)
    assert libs == [
        {"name": "D3D9", "version": "2.0.5632.1", "approval": "approved"},
        {"name": "XAPILIB", "version": "2.0.5632.0", "approval": "approved"},
    ]


def test_no_static_library_header():
    data = b"XEX2" + struct.pack(">5I", 0, 0, 0, 0, 1) + struct.pack(">II", 0x00010001, 0)
    assert xex_libraries.parse_static_libraries(data) == []


@pytest.mark.parametrize("data", [b"", b"XEX1" + b"\0" * 40, b"XEX2" + struct.pack(">5I", 0, 0, 0, 0, 1000)])
def test_rejects_bad_files(data):
    with pytest.raises(ValueError):
        xex_libraries.parse_static_libraries(data)


def test_cli(tmp_path, capsys):
    path = tmp_path / "default.xex"
    path.write_bytes(make_xex([("D3D9", (2, 0, 3529, 1, 0))]))
    assert xex_libraries.main([str(path)]) == 0
    out = capsys.readouterr().out
    assert "D3D9" in out and "2.0.3529.0" in out and "possible" in out
