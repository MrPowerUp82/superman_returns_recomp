// node --test tests/web
// Exercises the installer page's modules (docs/js) on real files: a disc
// image packed by xdvdfs, a folder layout, and the package written to a folder
// and to a ZIP. The real game is used when game/ is present.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { Writable } from 'node:stream';
import { openAsBlob } from 'node:fs';

import { openDisc } from '../../docs/js/xdvdfs.js';
import { findGameFiles, inspectGame, writePackage, XEX_SHA256 } from '../../docs/js/installer.js';
import { ZipWriter, readZip, entryStream } from '../../docs/js/zip.js';

const root = path.resolve(import.meta.dirname, '..', '..');
const xdvdfs = path.join(root, '.tools', 'xdvdfs', 'xdvdfs.exe');
const python = process.env.PYTHON || 'python';
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'sr-installer-'));

function sha256(file) {
  return createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function nodeDirectorySink(dir) {
  return {
    async put(rel, stream, onBytes = () => {}) {
      const target = path.join(dir, ...rel.split('/'));
      fs.mkdirSync(path.dirname(target), { recursive: true });
      const out = fs.createWriteStream(target);
      const reader = stream.getReader();
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        onBytes(value.length);
        if (!out.write(value)) await new Promise((r) => out.once('drain', r));
      }
      await new Promise((r) => out.end(r));
    },
    async close() {},
  };
}

function nodeZipSink(file, rootName) {
  const out = fs.createWriteStream(file);
  const zip = new ZipWriter({
    write: (bytes) => new Promise((resolve, reject) => out.write(bytes, (e) => (e ? reject(e) : resolve()))),
  });
  return {
    async put(rel, stream, onBytes = () => {}) {
      await zip.addFile(rootName + '/' + rel, stream, onBytes);
    },
    async close() {
      await zip.finish();
      await new Promise((r) => out.end(r));
    },
  };
}

function makeBuildZip(dir, zipPath) {
  execFileSync(python, ['-c', `
import zipfile, os, sys
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:
    for base, _, names in os.walk(src):
        for n in names:
            full = os.path.join(base, n)
            z.write(full, os.path.relpath(full, src).replace(os.sep, '/'))
`, dir, zipPath]);
}

test('XDVDFS reader lists a packed image like the source folder', { skip: !fs.existsSync(xdvdfs) }, async () => {
  const src = path.join(tmp, 'disc');
  fs.mkdirSync(path.join(src, 'DATA'), { recursive: true });
  fs.writeFileSync(path.join(src, 'default.xex'), Buffer.from('xex-contents-'.repeat(1000)));
  for (let i = 0; i < 14; i++) fs.writeFileSync(path.join(src, 'DATA', `file${i}.AST`), Buffer.alloc(5000 + i * 7, i + 1));
  fs.mkdirSync(path.join(src, 'Nested', 'Deeper'), { recursive: true });
  fs.writeFileSync(path.join(src, 'Nested', 'Deeper', 'x.bin'), Buffer.from('deep'));
  const iso = path.join(tmp, 'small.iso');
  execFileSync(xdvdfs, ['pack', src, iso], { stdio: 'ignore' });

  const files = await openDisc(await openAsBlob(iso));
  assert.deepEqual([...files.keys()].sort(), [
    'DATA/file0.AST', 'DATA/file1.AST', 'DATA/file10.AST', 'DATA/file11.AST', 'DATA/file12.AST',
    'DATA/file13.AST', 'DATA/file2.AST', 'DATA/file3.AST', 'DATA/file4.AST', 'DATA/file5.AST',
    'DATA/file6.AST', 'DATA/file7.AST', 'DATA/file8.AST', 'DATA/file9.AST', 'Nested/Deeper/x.bin',
    'default.xex',
  ].sort());
  for (const [rel, blob] of files) {
    const original = fs.readFileSync(path.join(src, ...rel.split('/')));
    assert.equal(blob.size, original.length, rel);
    assert.ok(Buffer.from(await blob.arrayBuffer()).equals(original), rel);
  }
});

test('the real disc image: layout, SHA-256 of default.xex and every file', { skip: !fs.existsSync(path.join(root, 'game', 'default.xex')) || !fs.existsSync(xdvdfs) }, async () => {
  const iso = path.join(tmp, 'game.iso');
  execFileSync(xdvdfs, ['pack', path.join(root, 'game'), iso], { stdio: 'ignore' });
  const files = await openDisc(await openAsBlob(iso));
  const report = await inspectGame(files);
  assert.deepEqual(report.problems, []);
  assert.equal(report.xexHash, XEX_SHA256);
  assert.equal(report.dataFiles, 12);
  for (const [rel, blob] of report.game) {
    const original = path.join(root, 'game', ...rel.split('/'));
    assert.equal(blob.size, fs.statSync(original).size, rel);
  }
  const first = [...report.game].filter(([rel]) => rel.startsWith('DATA/'))[0];
  const head = Buffer.from(await first[1].slice(0, 1 << 20).arrayBuffer());
  const original = fs.readFileSync(path.join(root, 'game', ...first[0].split('/'))).subarray(0, 1 << 20);
  assert.ok(head.equals(original));
});

test('findGameFiles accepts a folder with a prefix and any case', () => {
  const blob = (n) => new Blob([new Uint8Array(n)]);
  const files = new Map([
    ['Games/SR/default.xex', blob(10)],
    ['Games/SR/Data/a.AST', blob(3)],
    ['Games/SR/data/b.AST', blob(4)],
    ['Games/SR/Data/sub/ignored.bin', blob(1)],
    ['Games/Other/readme.txt', blob(1)],
  ]);
  const game = findGameFiles(files);
  assert.deepEqual([...game.keys()].sort(), ['DATA/a.AST', 'DATA/b.AST', 'default.xex']);
  assert.equal(findGameFiles(new Map([['x/y.txt', blob(1)]])), null);
});

test('inspectGame rejects a wrong default.xex and a short DATA folder', async () => {
  const files = new Map([
    ['default.xex', new Blob(['not the right xex'])],
    ['DATA/a.AST', new Blob(['1'])],
  ]);
  const report = await inspectGame(files);
  assert.equal(report.ok, false);
  assert.match(report.problems.join('|'), /not the version/);
  assert.match(report.problems.join('|'), /1 files, 12 expected/);
});

test('the package is written to a folder and to a ZIP, byte for byte', async () => {
  const buildDir = path.join(tmp, 'build');
  fs.mkdirSync(path.join(buildDir, 'shader_tools'), { recursive: true });
  fs.writeFileSync(path.join(buildDir, 'superman_returns.exe'), Buffer.alloc(300000, 7));
  fs.writeFileSync(path.join(buildDir, 'run.cmd'), '@echo off\r\n');
  fs.writeFileSync(path.join(buildDir, 'shader_tools', 'dxc.exe'), Buffer.from(Array.from({ length: 70000 }, (_, i) => (i * 31) & 255)));
  const buildZip = path.join(tmp, 'build.zip');
  makeBuildZip(buildDir, buildZip);

  const gameDir = path.join(tmp, 'gamefolder');
  fs.mkdirSync(path.join(gameDir, 'DATA'), { recursive: true });
  fs.writeFileSync(path.join(gameDir, 'default.xex'), Buffer.from('xex'.repeat(5000)));
  const big = Buffer.alloc(9 * 1024 * 1024);
  for (let i = 0; i < big.length; i += 4) big.writeUInt32LE((i * 2654435761) >>> 0, i);
  fs.writeFileSync(path.join(gameDir, 'DATA', 'big.AST'), big);
  const game = new Map([
    ['default.xex', await openAsBlob(path.join(gameDir, 'default.xex'))],
    ['DATA/big.AST', await openAsBlob(path.join(gameDir, 'DATA', 'big.AST'))],
  ]);
  const zipBlob = await openAsBlob(buildZip);

  // 1. folder
  const out = path.join(tmp, 'out-folder');
  const progress = [];
  await writePackage({ buildZip: zipBlob, game, sink: nodeDirectorySink(out), onProgress: (d, t) => progress.push([d, t]) });
  for (const rel of ['superman_returns.exe', 'run.cmd', 'shader_tools/dxc.exe']) {
    assert.equal(sha256(path.join(out, ...rel.split('/'))), sha256(path.join(buildDir, ...rel.split('/'))), rel);
  }
  assert.equal(sha256(path.join(out, 'game', 'DATA', 'big.AST')), sha256(path.join(gameDir, 'DATA', 'big.AST')));
  assert.equal(sha256(path.join(out, 'game', 'default.xex')), sha256(path.join(gameDir, 'default.xex')));
  assert.deepEqual(progress.at(-1), progress.at(-1).map(() => progress.at(-1)[1]));

  // 2. zip, validated by an independent reader
  const zipOut = path.join(tmp, 'package.zip');
  await writePackage({ buildZip: zipBlob, game, sink: nodeZipSink(zipOut, 'superman_returns') });
  const listing = execFileSync(python, ['-c', `
import zipfile, sys, hashlib, json
z = zipfile.ZipFile(sys.argv[1])
assert z.testzip() is None
print(json.dumps({n: hashlib.sha256(z.read(n)).hexdigest() for n in z.namelist()}))
`, zipOut]).toString();
  const hashes = JSON.parse(listing);
  assert.equal(hashes['superman_returns/game/DATA/big.AST'], sha256(path.join(gameDir, 'DATA', 'big.AST')));
  assert.equal(hashes['superman_returns/shader_tools/dxc.exe'], sha256(path.join(buildDir, 'shader_tools', 'dxc.exe')));
  assert.equal(Object.keys(hashes).length, 5);

  // 3. our own reader on our own zip
  const entries = await readZip(await openAsBlob(zipOut));
  assert.equal(entries.length, 5);
  const e = entries.find((x) => x.name.endsWith('run.cmd'));
  const text = await new Response(await entryStream(await openAsBlob(zipOut), e)).text();
  assert.equal(text, '@echo off\r\n');
});

test('cleanup', () => {
  fs.rmSync(tmp, { recursive: true, force: true });
});
