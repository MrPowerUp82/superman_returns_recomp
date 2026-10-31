import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import emitterFactory from '../../docs/wasm/hlsl.mjs';
import dxcFactory from '../../docs/wasm/dxcompiler.mjs';
import { buildD3d12Library } from '../../docs/js/build-d3d12-library.js';
import { buildVulkanLibrary } from '../../docs/js/build-vulkan-library.js';

const root = path.resolve(import.meta.dirname, '../..');
const common = new Uint8Array(fs.readFileSync(path.join(root, 'docs/wasm/shader_common.h')));

// Game-derived inputs stay in the owner's ignored corpus, never in the site.
for (const hash of ['559F422BEF5E5714', '8A6DD1D52752FAD6']) {
  const raw = path.join(root, `artifacts/shaders/raw/${hash}.ps.bin`);
  test(`browser shadow normalization matches native emission and compiles for both APIs: ${hash}`, {
    skip: !fs.existsSync(raw),
  }, async () => {
    const bytes = new Uint8Array(fs.readFileSync(raw));
    const emitter = await emitterFactory({ print: () => {}, printErr: () => {} });
    emitter.FS.writeFile('/shader_common.h', common);
    emitter.FS.writeFile('/shader.ps.bin', bytes);
    assert.equal(emitter.callMain(['/shader_common.h', '/out', '/shader.ps.bin']), 0);
    const source = new TextDecoder().decode(emitter.FS.readFile('/out/shader.ps.hlsl'));
    assert.match(source, /float4 g_ColorOutputScale : packoffset\(c31\)/);
    const scale = source.lastIndexOf('oC0 *= g_ColorOutputScale[0];');
    assert.ok(scale > source.lastIndexOf('clip(oC0.w - g_AlphaThreshold);'));
    assert.ok(scale < source.lastIndexOf('return;'));
    const native = path.join(root, `artifacts/shaders/hlsl/${hash}.ps.hlsl`);
    if (fs.existsSync(native)) {
      assert.equal(source.replaceAll('\r', ''), fs.readFileSync(native, 'utf8').replaceAll('\r', ''));
    }
    const dxc = await dxcFactory();
    for (const build of [buildD3d12Library, buildVulkanLibrary]) {
      const result = await build([{ file: `${hash}.ps.bin`, stage: 'ps', bytes }], { emitterFactory, dxc, common });
      assert.equal(result.report.ready, 1, JSON.stringify(result.report.failed));
      assert.equal(result.report.failed.length, 0);
    }
  });
}
