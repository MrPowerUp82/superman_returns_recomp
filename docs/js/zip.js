// Minimal ZIP support, no dependencies:
//  - ZipWriter: streams entries (stored, no compression) to a sink, so a
//    multi-gigabyte package never has to fit in memory;
//  - readZip / entryStream: reads the central directory of a ZIP Blob and
//    decompresses entries (stored or deflate) with DecompressionStream.

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

export function crc32Update(crc, bytes) {
  let c = ~crc >>> 0;
  for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
  return ~c >>> 0;
}

function dosTime(date) {
  const time = (date.getHours() << 11) | (date.getMinutes() << 5) | (date.getSeconds() >> 1);
  const day = ((date.getFullYear() - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate();
  return { time, day };
}

const encoder = new TextEncoder();
const decoder = new TextDecoder();

/** Streams a ZIP archive into `sink.write(Uint8Array)`. */
export class ZipWriter {
  constructor(sink) {
    this.sink = sink;
    this.offset = 0;
    this.entries = [];
  }

  async #write(bytes) {
    await this.sink.write(bytes);
    this.offset += bytes.length;
  }

  /** `source` is a Blob or a ReadableStream of Uint8Array chunks. */
  async addFile(name, source, onBytes = () => {}) {
    const nameBytes = encoder.encode(name);
    const { time, day } = dosTime(new Date());
    const localOffset = this.offset;
    if (localOffset >= 0xffffffff) throw new Error('The package is larger than 4 GB (ZIP64 is not supported)');
    const header = new DataView(new ArrayBuffer(30));
    header.setUint32(0, 0x04034b50, true);
    header.setUint16(4, 20, true);
    header.setUint16(6, 0x0808, true);  // data descriptor follows, UTF-8 names
    header.setUint16(8, 0, true);       // stored
    header.setUint16(10, time, true);
    header.setUint16(12, day, true);
    header.setUint16(26, nameBytes.length, true);
    await this.#write(new Uint8Array(header.buffer));
    await this.#write(nameBytes);

    let crc = 0;
    let size = 0;
    const stream = source instanceof Blob ? source.stream() : source;
    const reader = stream.getReader();
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      crc = crc32Update(crc, value);
      size += value.length;
      if (size >= 0xffffffff) throw new Error(`${name} is larger than 4 GB (ZIP64 is not supported)`);
      await this.#write(value);
      onBytes(value.length);
    }
    const descriptor = new DataView(new ArrayBuffer(16));
    descriptor.setUint32(0, 0x08074b50, true);
    descriptor.setUint32(4, crc, true);
    descriptor.setUint32(8, size, true);
    descriptor.setUint32(12, size, true);
    await this.#write(new Uint8Array(descriptor.buffer));
    this.entries.push({ nameBytes, crc, size, localOffset, time, day });
  }

  async finish() {
    const centralStart = this.offset;
    for (const e of this.entries) {
      const central = new DataView(new ArrayBuffer(46));
      central.setUint32(0, 0x02014b50, true);
      central.setUint16(4, 20, true);
      central.setUint16(6, 20, true);
      central.setUint16(8, 0x0808, true);
      central.setUint16(10, 0, true);
      central.setUint16(12, e.time, true);
      central.setUint16(14, e.day, true);
      central.setUint32(16, e.crc, true);
      central.setUint32(20, e.size, true);
      central.setUint32(24, e.size, true);
      central.setUint16(28, e.nameBytes.length, true);
      central.setUint32(42, e.localOffset, true);
      await this.#write(new Uint8Array(central.buffer));
      await this.#write(e.nameBytes);
    }
    const centralSize = this.offset - centralStart;
    if (this.entries.length >= 0xffff || centralStart >= 0xffffffff) {
      throw new Error('The package needs ZIP64, which is not supported');
    }
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, this.entries.length, true);
    end.setUint16(10, this.entries.length, true);
    end.setUint32(12, centralSize, true);
    end.setUint32(16, centralStart, true);
    await this.#write(new Uint8Array(end.buffer));
  }
}

/** The entries of a ZIP Blob: { name, method, compressedSize, size, crc, localOffset }. */
export async function readZip(blob) {
  const tailSize = Math.min(blob.size, 65557);
  const tail = new Uint8Array(await blob.slice(blob.size - tailSize).arrayBuffer());
  let end = -1;
  for (let i = tail.length - 22; i >= 0; i--) {
    if (tail[i] === 0x50 && tail[i + 1] === 0x4b && tail[i + 2] === 0x05 && tail[i + 3] === 0x06) {
      end = i;
      break;
    }
  }
  if (end < 0) throw new Error('Not a ZIP file (end of central directory not found)');
  const eocd = new DataView(tail.buffer, tail.byteOffset + end, 22);
  const count = eocd.getUint16(10, true);
  const centralSize = eocd.getUint32(12, true);
  const centralOffset = eocd.getUint32(16, true);
  if (count === 0xffff || centralOffset === 0xffffffff) throw new Error('ZIP64 archives are not supported');
  const central = new Uint8Array(await blob.slice(centralOffset, centralOffset + centralSize).arrayBuffer());
  const view = new DataView(central.buffer, central.byteOffset, central.byteLength);
  const entries = [];
  let p = 0;
  for (let i = 0; i < count; i++) {
    if (view.getUint32(p, true) !== 0x02014b50) throw new Error('Corrupt ZIP central directory');
    const nameLength = view.getUint16(p + 28, true);
    const extraLength = view.getUint16(p + 30, true);
    const commentLength = view.getUint16(p + 32, true);
    entries.push({
      method: view.getUint16(p + 10, true),
      crc: view.getUint32(p + 16, true),
      compressedSize: view.getUint32(p + 20, true),
      size: view.getUint32(p + 24, true),
      localOffset: view.getUint32(p + 42, true),
      name: decoder.decode(central.subarray(p + 46, p + 46 + nameLength)),
    });
    p += 46 + nameLength + extraLength + commentLength;
  }
  return entries;
}

/** Uncompressed contents of one entry as a ReadableStream. */
export async function entryStream(blob, entry) {
  const local = new DataView(await blob.slice(entry.localOffset, entry.localOffset + 30).arrayBuffer());
  if (local.getUint32(0, true) !== 0x04034b50) throw new Error(`Corrupt ZIP entry ${entry.name}`);
  const start = entry.localOffset + 30 + local.getUint16(26, true) + local.getUint16(28, true);
  const raw = blob.slice(start, start + entry.compressedSize).stream();
  if (entry.method === 0) return raw;
  if (entry.method === 8) return raw.pipeThrough(new DecompressionStream('deflate-raw'));
  throw new Error(`Unsupported ZIP method ${entry.method} in ${entry.name}`);
}
