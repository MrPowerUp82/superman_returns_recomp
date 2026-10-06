// Where the package goes. A sink has put(path, stream, onBytes) and close().

import { ZipWriter } from './zip.js';

function counting(onBytes) {
  return new TransformStream({
    transform(chunk, controller) {
      onBytes(chunk.length);
      controller.enqueue(chunk);
    },
  });
}

/** Writes the files straight into a folder the user picked (File System Access API). */
export function directorySink(rootHandle) {
  const directories = new Map([['', rootHandle]]);
  async function directoryFor(parts) {
    let key = '';
    let handle = rootHandle;
    for (const part of parts) {
      key += part + '/';
      if (!directories.has(key)) directories.set(key, await handle.getDirectoryHandle(part, { create: true }));
      handle = directories.get(key);
    }
    return handle;
  }
  return {
    async put(path, stream, onBytes = () => {}) {
      const parts = path.split('/');
      const name = parts.pop();
      const directory = await directoryFor(parts);
      const file = await directory.getFileHandle(name, { create: true });
      const writable = await file.createWritable();
      await stream.pipeThrough(counting(onBytes)).pipeTo(writable);
    },
    async close() {},
    async abort() {}, // Files already completed remain useful; no stream stays open.
  };
}

/** Writes one ZIP archive to a WritableStream (a file picked with showSaveFilePicker). */
export function zipSink(writable, rootName = '') {
  const writer = writable.getWriter();
  const zip = new ZipWriter({ write: (bytes) => writer.write(bytes) });
  const prefix = rootName ? rootName + '/' : '';
  return {
    async put(path, stream, onBytes = () => {}) {
      await zip.addFile(prefix + path, stream, onBytes);
    },
    async close() {
      await zip.finish();
      await writer.close();
    },
    async abort() { await writer.abort(); },
  };
}
