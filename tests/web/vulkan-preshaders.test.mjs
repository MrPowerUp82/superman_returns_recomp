import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { parseContainer, scanContainers, packLibrary, fnv1a64, compileArguments, wireResult } from '../../docs/js/vulkan-preshaders.js';
import { inspectSpirv } from '../../docs/js/spirv-metadata.js';
import { buildVulkanLibrary } from '../../docs/js/build-vulkan-library.js';
import { writePackage } from '../../docs/js/installer.js';
import { ZipWriter } from '../../docs/js/zip.js';

const root = path.resolve(import.meta.dirname, '../..');
function container(stage = 'vs') {
  const bytes = new Uint8Array(64), v = new DataView(bytes.buffer);
  for (const [offset, n] of [[0, stage === 'vs' ? 0x102a1101 : 0x102a1100], [4, 60], [8, 4], [24, 36], [40, 4]]) v.setUint32(offset, n, false);
  return bytes;
}
test('scanner reads byte offsets, chunk boundaries, duplicates and both stages', async () => {
  const vs = container(), ps = container('ps'), data = new Uint8Array(4 * 1024 * 1024 + 128);
  data.set(vs, 3); data.set(vs, 101); data.set(ps, 4 * 1024 * 1024 - 12);
  const found = await scanContainers(new Map([['DATA/a.AST', new Blob([data])], ['ignored.txt', new Blob([ps])]]));
  assert.equal(found.length, 2); assert.deepEqual(found.map((v) => v.stage).sort(), ['ps', 'vs']);
  assert.equal(parseContainer(vs).size, 64);
  const bad = vs.slice(); new DataView(bad.buffer).setUint32(24, 100, false); assert.equal(parseContainer(bad), null);
});
test('compile flags stay identical to the production Vulkan contract', () => {
  for (const stage of ['vs', 'ps']) {
    const expected = JSON.parse(execFileSync('python', ['-c', `import sys,json;sys.path.insert(0,'tools/shaders');from vulkan_contract import compile_arguments;print(json.dumps(compile_arguments('${stage}')+['-Qstrip_debug']))`], { cwd: root }));
    assert.deepEqual(compileArguments(stage), expected);
  }
});
test('library wire layout and checksum match the independent Python packer', () => {
  const binary = new Uint8Array(new Uint32Array([0x07230203, 0x10300, 0, 1, 0]).buffer);
  const result = wireResult('vs', binary, { requirements: Array(12).fill(0), inputs: [], outputs: [] });
  const bytes = packLibrary([{ stage: 'vs', bytes: container(), result }]);
  const view = new DataView(bytes.buffer); assert.equal(view.getUint32(12, true), 1);
  assert.equal(view.getBigUint64(16, true), fnv1a64(bytes.subarray(24)));
  const expected = execFileSync('python', ['-c', "import sys;sys.path.insert(0,'tools/shaders');from make_vulkan_preshaders import serialize;import json;base=json.loads(sys.stdin.read());sys.stdout.buffer.write(serialize([(0,bytes(base[0]),bytes(base[1]))]))"], { cwd: root, input: JSON.stringify([[...container()], [...result]]) });
  assert.deepEqual(Buffer.from(bytes), expected);
});
test('reflection rejects malformed instruction lengths and nonportable capabilities', () => {
  const binary = (words) => new Uint8Array(new Uint32Array(words).buffer);
  assert.throws(() => inspectSpirv(binary([0x07230203, 0x10300, 0, 4, 0, 0]), 'vs'), /instruction/);
  assert.throws(() => inspectSpirv(binary([0x07230203, 0x10300, 0, 4, 0, (2 << 16) | 17, 11, (4 << 16) | 15, 0, 1, 0]), 'vs'), /capability/);
});
test('generated library replaces a same-name build entry and participates in package progress', async () => {
  const chunks = [], zip = new ZipWriter({ write: async (bytes) => chunks.push(bytes) });
  await zip.addFile('superman_returns.exe', new Blob(['exe']).stream());
  await zip.addFile('superman_returns_vulkan.srvk', new Blob(['stale']).stream());
  await zip.finish();
  const files = new Map(), progress = [];
  await writePackage({ buildZip: new Blob(chunks), extraFiles: new Map([['superman_returns_vulkan.srvk', new Blob(['new'])], ['report.json', new Blob(['{}'])]]),
    sink: { async put(name, stream, onBytes) { const bytes = new Uint8Array(await new Response(stream).arrayBuffer()); onBytes(bytes.length); assert.ok(!files.has(name)); files.set(name, new TextDecoder().decode(bytes)); }, async close() {} },
    onProgress: (done, total) => progress.push([done, total]) });
  assert.equal(files.get('superman_returns_vulkan.srvk'), 'new'); assert.equal(files.size, 3);
  assert.deepEqual(progress.at(-1), [8, 8]);
});
test('real WebAssembly emitter and DXC compile original local containers', { skip: !fs.existsSync(path.join(root, 'artifacts/shaders/raw')) }, async () => {
  const { default: emitterFactory } = await import('../../docs/wasm/hlsl.mjs');
  const { default: dxcFactory } = await import('../../docs/wasm/dxcompiler.mjs');
  const dxc = await dxcFactory();
  const common = new Uint8Array(fs.readFileSync(path.join(root, 'docs/wasm/shader_common.h')));
  const names = fs.readdirSync(path.join(root, 'artifacts/shaders/raw')).filter((n) => n.endsWith('.bin'));
  const samples = [names.find((n) => n.endsWith('.vs.bin')), names.find((n) => n.endsWith('.ps.bin'))];
  const containers = samples.map((name) => ({ file: name, stage: name.includes('.vs.') ? 'vs' : 'ps', bytes: new Uint8Array(fs.readFileSync(path.join(root, 'artifacts/shaders/raw', name))) }));
  const result = await buildVulkanLibrary(containers, { emitterFactory, dxc, common });
  assert.equal(result.report.ready, 2, JSON.stringify(result.report.failed));
  assert.equal(result.report.failed.length, 0);
  fs.mkdirSync(path.join(root, 'artifacts/shaders/wasm-test'), { recursive: true });
  fs.writeFileSync(path.join(root, 'artifacts/shaders/wasm-test/samples.srvk'), result.bytes);
});
