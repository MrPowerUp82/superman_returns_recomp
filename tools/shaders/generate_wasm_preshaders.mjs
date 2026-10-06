// CLI integration probe using the same scanner/emitter/compiler as the page.
// Output is local game-derived data under artifacts/, never site content.
import fs from 'node:fs';
import { openAsBlob } from 'node:fs';
import path from 'node:path';
import { scanContainers, VULKAN_LIBRARY_NAME } from '../../docs/js/vulkan-preshaders.js';
import { buildVulkanLibrary } from '../../docs/js/build-vulkan-library.js';
import emitterFactory from '../../docs/wasm/hlsl.mjs';
import dxcFactory from '../../docs/wasm/dxcompiler.mjs';

const root = path.resolve(import.meta.dirname, '../..');
const game = path.resolve(process.argv[2] || path.join(root, 'game'));
const output = path.resolve(process.argv[3] || path.join(root, 'artifacts/shaders/wasm', VULKAN_LIBRARY_NAME));
const files = new Map();
for (const name of fs.readdirSync(path.join(game, 'DATA')).sort()) {
  const file = path.join(game, 'DATA', name);
  if (fs.statSync(file).isFile()) files.set(`DATA/${name}`, await openAsBlob(file));
}
let lastPercent = -1;
const progress = (value) => {
  const percent = Math.floor(value.done / Math.max(1, value.total) * 100);
  if (value.phase === 'scan' ? percent >= lastPercent + 10 : value.done % 25 === 0 || value.done === value.total) {
    console.log(JSON.stringify(value)); if (value.phase === 'scan') lastPercent = percent;
  }
};
const containers = await scanContainers(files, progress);
console.log(`${containers.length} unique original containers on disc`);
const dxc = await dxcFactory(), common = new Uint8Array(fs.readFileSync(path.join(root, 'docs/wasm/shader_common.h')));
const result = await buildVulkanLibrary(containers, { emitterFactory, dxc, common }, progress);
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, result.bytes);fs.writeFileSync(`${output}.report.json`, JSON.stringify(result.report, null, 2));
console.log(`${result.report.ready} pre-shaders, ${result.report.failed.length} failed -> ${output}`);
