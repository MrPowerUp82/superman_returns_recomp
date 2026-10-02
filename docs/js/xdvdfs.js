// Reads an Xbox 360 disc image (XDVDFS / "XISO") from a Blob without loading
// it into memory: only the volume descriptor and the directory tables are
// read, the files stay as slices of the original Blob.

const SECTOR = 2048;
const MAGIC = 'MICROSOFT*XBOX*MEDIA';
// Where the game partition starts: a plain XISO, then the XGD1 / XGD2 / XGD3
// layouts of retail discs.
const PARTITION_OFFSETS = [0x00000000, 0x18300000, 0x0fd90000, 0x02080000];
const ATTRIBUTE_DIRECTORY = 0x10;

async function readBytes(blob, offset, length) {
  if (offset < 0 || offset >= blob.size) return new Uint8Array(0);
  const end = Math.min(blob.size, offset + length);
  return new Uint8Array(await blob.slice(offset, end).arrayBuffer());
}

function ascii(bytes, start, end) {
  let s = '';
  for (let i = start; i < end; i++) s += String.fromCharCode(bytes[i]);
  return s;
}

/** Locates the volume descriptor; throws when the Blob is not a disc image. */
export async function openVolume(blob) {
  for (const base of PARTITION_OFFSETS) {
    const descriptor = await readBytes(blob, base + 32 * SECTOR, 28);
    if (descriptor.length === 28 && ascii(descriptor, 0, 20) === MAGIC) {
      const view = new DataView(descriptor.buffer, descriptor.byteOffset, descriptor.byteLength);
      return { base, rootSector: view.getUint32(20, true), rootSize: view.getUint32(24, true) };
    }
  }
  throw new Error('Not an Xbox 360 disc image (XDVDFS volume descriptor not found)');
}

async function readDirectory(blob, volume, sector, size) {
  const data = await readBytes(blob, volume.base + sector * SECTOR, size);
  if (data.length < size) throw new Error('Truncated disc image (directory beyond the end of the file)');
  return data;
}

/** Walks the binary tree of one directory table. */
function parseEntries(data) {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const entries = [];
  const seen = new Set();
  const stack = [0];
  while (stack.length) {
    const offset = stack.pop();
    if (offset + 14 > data.length || seen.has(offset)) continue;
    seen.add(offset);
    const left = view.getUint16(offset, true) * 4;
    const right = view.getUint16(offset + 2, true) * 4;
    const sector = view.getUint32(offset + 4, true);
    const size = view.getUint32(offset + 8, true);
    const attributes = data[offset + 12];
    const nameLength = data[offset + 13];
    if (sector === 0xffffffff || nameLength === 0) continue;  // unused padding
    entries.push({ name: ascii(data, offset + 14, offset + 14 + nameLength), sector, size, attributes });
    if (left) stack.push(left);
    if (right) stack.push(right);
  }
  return entries;
}

/**
 * Every file of the image as { path, size, offset } ("DATA/baseworl.AST");
 * `offset` is where the file starts inside the Blob.
 */
export async function listFiles(blob, volume) {
  const files = [];
  const pending = [{ prefix: '', sector: volume.rootSector, size: volume.rootSize }];
  while (pending.length) {
    const dir = pending.pop();
    if (dir.size === 0) continue;
    const data = await readDirectory(blob, volume, dir.sector, dir.size);
    for (const entry of parseEntries(data)) {
      const path = dir.prefix + entry.name;
      if (entry.attributes & ATTRIBUTE_DIRECTORY) {
        pending.push({ prefix: path + '/', sector: entry.sector, size: entry.size });
      } else {
        files.push({ path, size: entry.size, offset: volume.base + entry.sector * SECTOR });
      }
    }
  }
  files.sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
  return files;
}

/** A disc image Blob as a map of path -> Blob slice. */
export async function openDisc(blob) {
  const volume = await openVolume(blob);
  const files = await listFiles(blob, volume);
  return new Map(files.map((f) => [f.path, blob.slice(f.offset, f.offset + f.size)]));
}
