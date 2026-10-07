// SRSHLIB v1, identical to tools/shaders/make_preshaders.py and ShaderLibrary.
import { fnv1a64, parseContainer, words } from './vulkan-preshaders.js';
import './vendor/xxhash3.umd.min.mjs';
import { dxilContainerHash } from './dxil-container-hash.js';

export const D3D12_LIBRARY_NAME = 'superman_returns_shaders.srsl';
const encoder = new TextEncoder();

export function compileArguments(stage) {
  if (stage !== 'vs' && stage !== 'ps') throw new Error('Invalid shader stage');
  return ['-T', `${stage}_6_0`, '-HV', '2021', '-all-resources-bound', '-Wno-ignored-attributes', '-Qstrip_debug'];
}

export function validateDxil(binary) {
  if (!(binary instanceof Uint8Array) || binary.length < 36 || binary.length > 16 * 1024 * 1024 ||
      new TextDecoder().decode(binary.subarray(0, 4)) !== 'DXBC') throw new Error('DXC produced no valid DXIL container');
  const view = new DataView(binary.buffer, binary.byteOffset, binary.byteLength);
  const count = view.getUint32(28, true);
  if (view.getUint32(24, true) !== binary.length || !count || count > (binary.length - 32) / 4) throw new Error('Invalid DXIL container bounds');
  let hasDxil = false;
  for (let i = 0; i < count; i++) {
    const offset = view.getUint32(32 + i * 4, true);
    if (offset < 32 + count * 4 || offset + 8 > binary.length || offset + 8 + view.getUint32(offset + 4, true) > binary.length) throw new Error('Invalid DXIL chunk bounds');
    if (view.getUint32(offset, true) === 0x4c495844) hasDxil = true;
  }
  if (!hasDxil) throw new Error('DXIL chunk missing from shader container');
}

export function signDxil(binary) {
  validateDxil(binary);
  // Hash starts at Version (byte 20), after the magic and 16-byte digest.
  // The WASM DXC build does not load Windows dxil.dll to write this checksum.
  binary.set(dxilContainerHash(binary.subarray(20)), 4);
  return binary;
}

export async function packLibrary(entries) {
  if (!entries.length || entries.length > 16384) throw new Error('No valid shaders or too many shaders');
  const rows = [];
  let size = 0;
  for (const entry of entries) {
    const header = parseContainer(entry.bytes);
    if (!header || header.size !== entry.bytes.length || header.stage !== entry.stage) throw new Error('Invalid library entry');
    validateDxil(entry.result);
    const hash = BigInt(`0x${await globalThis.hashwasm.xxhash3(entry.bytes)}`);
    rows.push({ ...entry, hash, type: entry.stage === 'vs' ? 0 : 1 });
    size += 24 + entry.bytes.length + entry.result.length;
    if (size + 24 > 256 * 1024 * 1024) throw new Error('Shader library exceeds 256 MiB');
  }
  rows.sort((a, b) => a.hash < b.hash ? -1 : a.hash > b.hash ? 1 : a.type - b.type);
  const library = new Uint8Array(24 + size), view = new DataView(library.buffer);
  let offset = 24;
  for (const row of rows) {
    view.setBigUint64(offset, row.hash, true);
    library.set(words(row.type, row.bytes.length, row.result.length, 0), offset + 8);
    library.set(row.bytes, offset + 24);
    library.set(row.result, offset + 24 + row.bytes.length);
    offset += 24 + row.bytes.length + row.result.length;
  }
  library.set(encoder.encode('SRSHLIB\0'));
  view.setUint32(8, 1, true); view.setUint32(12, rows.length, true);
  view.setBigUint64(16, fnv1a64(library.subarray(24)), true);
  return library;
}
