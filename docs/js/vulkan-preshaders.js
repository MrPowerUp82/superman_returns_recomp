// SRVKLIB v1: original containers + production SVR3 results. No game data is
// fetched: the scanner only reads the selected local Blobs in bounded chunks.
export const VULKAN_LIBRARY_NAME = 'superman_returns_vulkan.srvk';
const encoder = new TextEncoder();
const MAX_CONTAINER = 1024 * 1024;

export function fnv1a64(bytes) {
  let hash = 0xcbf29ce484222325n;
  for (const byte of bytes) hash = BigInt.asUintN(64, (hash ^ BigInt(byte)) * 0x100000001b3n);
  return hash;
}

export function parseContainer(data, offset = 0) {
  if (offset + 36 > data.length) return null;
  const view = new DataView(data.buffer, data.byteOffset + offset, data.length - offset);
  const word = (at) => view.getUint32(at, false);
  const flags = word(0), virtual = word(4), physical = word(8), size = virtual + physical;
  const table = word(16), definitions = word(20), shader = word(24);
  if ((flags >>> 8) !== 0x102a11 || word(28) || word(32) || virtual < 36 || !physical ||
      size > MAX_CONTAINER || size > view.byteLength || table >= virtual || definitions >= virtual ||
      !shader || shader + 24 > virtual) return null;
  const physicalOffset = word(shader), microcodeSize = word(shader + 4);
  if (!microcodeSize || microcodeSize % 4 || physicalOffset + microcodeSize > physical) return null;
  return { stage: flags & 1 ? 'vs' : 'ps', size };
}

export async function scanContainers(files, onProgress = () => {}) {
  const found = new Map();
  const sources = [...files].filter(([path]) => /\.ast$/i.test(path)).sort(([a], [b]) => a.localeCompare(b));
  const total = sources.reduce((sum, [, blob]) => sum + blob.size, 0);
  let done = 0;
  for (const [path, blob] of sources) {
    let carry = new Uint8Array();
    for (let offset = 0; offset < blob.size; offset += 4 * 1024 * 1024) {
      const chunk = new Uint8Array(await blob.slice(offset, offset + 4 * 1024 * 1024).arrayBuffer());
      const data = new Uint8Array(carry.length + chunk.length);
      data.set(carry); data.set(chunk, carry.length);
      const final = offset + chunk.length === blob.size;
      const limit = final ? data.length : Math.max(0, data.length - MAX_CONTAINER);
      let position = 0;
      while (position < limit) {
        if (data[position] === 0x10 && data[position + 1] === 0x2a && data[position + 2] === 0x11) {
          const header = parseContainer(data, position);
          if (header) {
            const bytes = data.slice(position, position + header.size);
            // Hash buckets still compare bytes, so a collision cannot alias shaders.
            const key = `${header.stage}:${fnv1a64(bytes).toString(16)}`;
            const bucket = found.get(key) || [];
            if (!bucket.some((old) => old.bytes.length === bytes.length && old.bytes.every((v, i) => v === bytes[i]))) {
              bucket.push({ stage: header.stage, bytes, file: path }); found.set(key, bucket);
            }
            position += header.size;
            continue;
          }
        }
        position++;
      }
      carry = final ? new Uint8Array() : data.slice(position);
      done += chunk.length; onProgress({ phase: 'scan', done, total, shaders: [...found.values()].reduce((n, b) => n + b.length, 0) });
    }
  }
  return [...found.values()].flat();
}

export function compileArguments(stage) {
  if (stage !== 'vs' && stage !== 'ps') throw new Error('Invalid shader stage');
  return ['-spirv', '-fspv-target-env=vulkan1.1', '-D', 'SR_VULKAN_BUFFERS=1', '-HV', '2021',
    '-all-resources-bound', '-Wno-ignored-attributes', '-T', `${stage}_6_0`,
    ...(stage === 'vs' ? ['-fvk-invert-y'] : []), '-Qstrip_debug'];
}

function concatenate(parts) {
  const data = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let offset = 0; for (const part of parts) { data.set(part, offset); offset += part.length; }
  return data;
}
export function words(...values) {
  const data = new Uint8Array(values.length * 4), view = new DataView(data.buffer);
  values.forEach((v, i) => view.setUint32(i * 4, v, true)); return data;
}
export function wireResult(stage, binary, metadata) {
  const text = (value) => { const bytes = encoder.encode(value); if (bytes.length > 16384) throw new Error('Oversized interface type'); return concatenate([words(bytes.length), bytes]); };
  const parts = [words(0x33525653, 1, 1, stage === 'vs' ? 0 : 1), words(...metadata.requirements)];
  for (const rows of [metadata.inputs, metadata.outputs]) {
    parts.push(words(rows.length));
    for (const row of rows) parts.push(words(row.location), text(row.type));
  }
  parts.push(words(binary.length), binary); return concatenate(parts);
}
export function packLibrary(entries) {
  if (!entries.length || entries.length > 16384) throw new Error('No valid shaders or too many shaders');
  const body = concatenate(entries.flatMap(({ stage, bytes, result }) => {
    const header = parseContainer(bytes);
    if (!header || header.size !== bytes.length || header.stage !== stage || result.length > 17 * 1024 * 1024) throw new Error('Invalid library entry');
    return [words(stage === 'vs' ? 0 : 1, bytes.length, result.length, 0), bytes, result];
  }));
  if (body.length + 24 > 256 * 1024 * 1024) throw new Error('Shader library exceeds 256 MiB');
  const header = new Uint8Array(24); header.set(encoder.encode('SRVKLIB\0'));
  const view = new DataView(header.buffer); view.setUint32(8, 1, true); view.setUint32(12, entries.length, true);
  view.setBigUint64(16, fnv1a64(body), true); return concatenate([header, body]);
}
