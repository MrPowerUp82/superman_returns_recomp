// The installer's logic, free of DOM: it checks the game files the user picked
// and writes the package (the port's build + the user's own game files) to a
// sink. Everything is read and written inside the browser.

import { readZip, entryStream } from './zip.js';

/** SHA-256 of the default.xex this port is built from (see README). */
export const XEX_SHA256 = 'c8f243acd99de9a91f5ae4f409721c0e954e3d5eb96861419d3da07b8106db2b';

export const REQUIRED_DATA_FILES = 12;

function hex(buffer) {
  return [...new Uint8Array(buffer)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

export async function sha256Hex(blob) {
  return hex(await crypto.subtle.digest('SHA-256', await blob.arrayBuffer()));
}

/**
 * The game files inside a map of path -> Blob (a disc image, or a picked
 * folder with any prefix): the folder holding default.xex is the root.
 * Returns the files to copy, keyed by their path inside game/.
 */
export function findGameFiles(files) {
  let root = null;
  for (const path of files.keys()) {
    const parts = path.split('/');
    if (parts[parts.length - 1].toLowerCase() === 'default.xex') {
      if (root === null || parts.length - 1 < root.split('/').filter(Boolean).length) {
        root = parts.slice(0, -1).join('/');
      }
    }
  }
  if (root === null) return null;
  const prefix = root ? root + '/' : '';
  const game = new Map();
  for (const [path, blob] of files) {
    if (!path.startsWith(prefix)) continue;
    const relative = path.slice(prefix.length);
    const lower = relative.toLowerCase();
    if (lower === 'default.xex') game.set('default.xex', blob);
    else if (lower.startsWith('data/') && relative.indexOf('/', 5) < 0) game.set('DATA/' + relative.slice(5), blob);
  }
  return game;
}

/** Checks the game files; `problems` are fatal, `warnings` are not. */
export async function inspectGame(files) {
  const result = { ok: false, problems: [], warnings: [], game: null, dataFiles: 0, bytes: 0, xexHash: '' };
  const game = findGameFiles(files);
  if (!game || !game.has('default.xex')) {
    result.problems.push('default.xex not found');
    return result;
  }
  result.game = game;
  result.xexHash = await sha256Hex(game.get('default.xex'));
  if (result.xexHash !== XEX_SHA256) {
    result.problems.push('default.xex is not the version this port is built from');
  }
  for (const [path, blob] of game) {
    if (path.startsWith('DATA/')) result.dataFiles++;
    result.bytes += blob.size;
  }
  if (result.dataFiles < REQUIRED_DATA_FILES) {
    result.problems.push(`DATA/ has ${result.dataFiles} files, ${REQUIRED_DATA_FILES} expected`);
  }
  result.ok = result.problems.length === 0;
  return result;
}

/**
 * Writes the package: the entries of `buildZip` (a Blob), then the game files
 * under game/. `sink.put(path, stream, onBytes)` stores one file.
 */
export async function writePackage({ buildZip, game, sink, onProgress = () => {} }) {
  const entries = (await readZip(buildZip)).filter((e) => !e.name.endsWith('/'));
  let total = game ? [...game.values()].reduce((sum, b) => sum + b.size, 0) : 0;
  total += entries.reduce((sum, e) => sum + e.size, 0);
  let done = 0;
  const count = (n) => {
    done += n;
    onProgress(done, total);
  };
  for (const entry of entries) {
    await sink.put(entry.name, await entryStream(buildZip, entry), count, entry.size);
  }
  if (game) {
    for (const [path, blob] of game) {
      await sink.put('game/' + path, blob.stream(), count, blob.size);
    }
  }
  await sink.close();
  onProgress(total, total);
}
