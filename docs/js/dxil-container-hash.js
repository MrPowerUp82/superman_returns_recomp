// DXC ComputeHashRetail: MD5 rounds with the DXIL container padding convention.
// Ported from Microsoft DirectXShaderCompiler, lib/DxilHash/DxilHash.cpp.
// Copyright (C) Microsoft Corporation. University of Illinois Open Source
// License: ../wasm/licenses/DXC-LLVM.txt. This is a format checksum, not security.
const constants = Uint32Array.from({ length: 64 }, (_, i) => Math.floor(Math.abs(Math.sin(i + 1)) * 0x100000000));
const shifts = [[7, 12, 17, 22], [5, 9, 14, 20], [4, 11, 16, 23], [6, 10, 15, 21]];

export function dxilContainerHash(data) {
  const length = data.length, remainder = length % 64, full = length - remainder;
  const padded = new Uint8Array(full + (remainder < 56 ? 64 : 128));
  const view = new DataView(padded.buffer);
  padded.set(data.subarray(0, full));
  if (remainder < 56) {
    view.setUint32(full, length * 8, true);
    padded.set(data.subarray(full), full + 4);
    padded[full + 4 + remainder] = 0x80;
  } else {
    padded.set(data.subarray(full), full);
    padded[full + remainder] = 0x80;
    view.setUint32(full + 64, length * 8, true);
  }
  view.setUint32(padded.length - 4, (length * 2) | 1, true);
  const state = new Uint32Array([0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476]);
  for (let offset = 0; offset < padded.length; offset += 64) {
    let [a, b, c, d] = state;
    for (let i = 0; i < 64; i++) {
      const round = i >>> 4;
      const f = round === 0 ? (b & c) | (~b & d) : round === 1 ? (d & b) | (~d & c) : round === 2 ? b ^ c ^ d : c ^ (b | ~d);
      const word = round === 0 ? i : round === 1 ? (5 * i + 1) % 16 : round === 2 ? (3 * i + 5) % 16 : (7 * i) % 16;
      const sum = (a + f + constants[i] + view.getUint32(offset + word * 4, true)) >>> 0;
      const shift = shifts[round][i % 4];
      [a, b, c, d] = [d, (b + ((sum << shift) | (sum >>> (32 - shift)))) >>> 0, b, c];
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  }
  const hash = new Uint8Array(16), hashView = new DataView(hash.buffer);
  state.forEach((value, i) => hashView.setUint32(i * 4, value, true));
  return hash;
}
