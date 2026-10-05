"""tools/analysis/native_ab_compare.py on synthetic A/B dumps."""
import math
import struct

import native_ab_compare as ab


def raw(path, width, height, fmt, pixels, pad=0):
    """pixels: list of (r, g, b) in 0..255."""
    pitch = width * 4 + pad
    rows = bytearray()
    for y in range(height):
        row = bytearray()
        for x in range(width):
            r, g, b = pixels[y * width + x]
            if fmt == ab.DXGI_R8G8B8A8_UNORM:
                row += bytes((r, g, b, 255))
            elif fmt == ab.DXGI_B8G8R8A8_UNORM:
                row += bytes((b, g, r, 255))
            else:
                ten = [round(c * 1023 / 255) for c in (r, g, b)]
                row += struct.pack("<I", ten[0] | (ten[1] << 10) | (ten[2] << 20) | (3 << 30))
        rows += row + b"\0" * pad
    path.write_bytes(struct.pack("<4I", width, height, fmt, pitch) + rows)


PIXELS = [(i * 17 % 256, i * 29 % 256, i * 53 % 256) for i in range(12)]


def test_formats_decode_to_the_same_rgb(tmp_path):
    decoded = []
    for fmt in (ab.DXGI_R8G8B8A8_UNORM, ab.DXGI_B8G8R8A8_UNORM, ab.DXGI_R10G10B10A2_UNORM):
        p = tmp_path / f"{fmt}.raw"
        raw(p, 4, 3, fmt, PIXELS, pad=8)
        decoded.append(ab.read_raw(p))
    assert decoded[0] == decoded[1] == decoded[2]
    assert decoded[0][:2] == (4, 3)


def test_psnr():
    assert ab.psnr(b"\x10\x20", b"\x10\x20") == math.inf
    # One channel off by 1 out of 2 -> MSE 0.5.
    assert abs(ab.psnr(b"\x10\x20", b"\x11\x20") - 10 * math.log10(255 * 255 / 0.5)) < 1e-9


def test_directory_comparison(tmp_path, capsys):
    raw(tmp_path / "s000300_output_1280x720.raw", 4, 3, ab.DXGI_R10G10B10A2_UNORM, PIXELS)
    raw(tmp_path / "s000300_xenos_output.raw", 4, 3, ab.DXGI_R8G8B8A8_UNORM, PIXELS)
    assert ab.main([str(tmp_path), "--ppm"]) == 0
    assert "swap 300: PSNR inf dB ok" in capsys.readouterr().out
    assert (tmp_path / "s000300_diff_x8.ppm").exists()


def test_low_psnr_and_missing_pairs_fail(tmp_path, capsys):
    raw(tmp_path / "s000900_output_1280x720.raw", 4, 3, ab.DXGI_R8G8B8A8_UNORM, PIXELS)
    raw(tmp_path / "s000900_xenos_output.raw", 4, 3, ab.DXGI_R8G8B8A8_UNORM,
        [(255 - r, g, b) for r, g, b in PIXELS])
    raw(tmp_path / "s001800_output_1280x720.raw", 4, 3, ab.DXGI_R8G8B8A8_UNORM, PIXELS)
    assert ab.main([str(tmp_path)]) == 1
    out = capsys.readouterr().out
    assert "swap 900: PSNR" in out and "BELOW" in out
    assert "swap 1800: no Xenos image" in out


def test_empty_directory(tmp_path):
    assert ab.main([str(tmp_path)]) == 1
