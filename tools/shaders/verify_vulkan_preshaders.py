"""Independently verify native/browser library bytes, reflection and Vulkan ABI."""
import argparse
import struct
from pathlib import Path
from make_preshaders import fnv1a64, parse_header
from make_vulkan_preshaders import MAGIC
from runtime_vulkan_shader import ready


def verify(data):
    if len(data) < 24 or len(data) > 256 * 1024 * 1024 or data[:8] != MAGIC:
        raise ValueError('Invalid library size/magic')
    version, count, checksum = struct.unpack_from('<IIQ', data, 8)
    if version != 1 or not 0 < count <= 16384 or fnv1a64(data[24:]) != checksum:
        raise ValueError('Invalid library version/count/checksum')
    position, seen = 24, set()
    for _ in range(count):
        if position + 16 > len(data):
            raise ValueError('Truncated entry header')
        stage, csize, rsize, reserved = struct.unpack_from('<4I', data, position)
        position += 16
        if stage > 1 or reserved or csize > 1024 * 1024 or rsize > 17 * 1024 * 1024 or position + csize + rsize > len(data):
            raise ValueError('Invalid entry bounds')
        container = data[position:position + csize]
        result = data[position + csize:position + csize + rsize]
        position += csize + rsize
        header = parse_header(container)
        if not header or header[0] != (stage == 0) or sum(header[1:]) != csize or (stage, container) in seen:
            raise ValueError('Invalid/duplicate container')
        seen.add((stage, container))
        # Read the result's location tables to reach SPIR-V, then regenerate
        # the entire wire message from independent Python ABI reflection.
        if len(result) < 72 or struct.unpack_from('<4I', result) != (0x33525653, 1, 1, stage):
            raise ValueError('Invalid shader result')
        at = 64
        for _ in range(2):
            if at + 4 > len(result):
                raise ValueError('Truncated locations')
            locations, = struct.unpack_from('<I', result, at)
            at += 4
            if locations > 256:
                raise ValueError('Too many locations')
            for _ in range(locations):
                if at + 8 > len(result):
                    raise ValueError('Truncated location')
                _, length = struct.unpack_from('<2I', result, at)
                at += 8 + length
                if length > 16384 or at > len(result):
                    raise ValueError('Invalid type length')
        if at + 4 > len(result):
            raise ValueError('Missing SPIR-V size')
        size, = struct.unpack_from('<I', result, at)
        binary = result[at + 4:]
        if size != len(binary) or ready('vs' if stage == 0 else 'ps', binary) != result:
            raise ValueError('SPIR-V metadata differs from production reflection')
    if position != len(data):
        raise ValueError('Trailing library bytes')
    return count


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library', type=Path)
    args = parser.parse_args()
    print(f'{verify(args.library.read_bytes())} pre-shaders verified: {args.library}')
