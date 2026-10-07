import { scanContainers } from './vulkan-preshaders.js';
import { buildVulkanLibrary } from './build-vulkan-library.js';
import { buildD3d12Library } from './build-d3d12-library.js';

self.onmessage = async ({ data }) => {
  try {
    const api = data.api || 'vulkan';
    if (!['vulkan', 'd3d12'].includes(api)) throw new Error('Invalid shader API');
    const progress = (value) => self.postMessage({ type: 'progress', value });
    progress({ phase: 'tools' });
    const [{ default: emitterFactory }, { default: dxcFactory }, commonResponse] = await Promise.all([
      import('../wasm/hlsl.mjs'), import('../wasm/dxcompiler.mjs'), fetch(new URL('../wasm/shader_common.h', import.meta.url)),
    ]);
    if (!commonResponse.ok) throw new Error(`shader_common.h: HTTP ${commonResponse.status}`);
    const common = new Uint8Array(await commonResponse.arrayBuffer());
    const dxc = await dxcFactory();
    const containers = await scanContainers(data.files, progress);
    if (!containers.length) throw new Error('No original shader containers found in DATA/*.AST');
    const builder = api === 'd3d12' ? buildD3d12Library : buildVulkanLibrary;
    const result = await builder(containers, { emitterFactory, dxc, common }, progress);
    self.postMessage({ type: 'done', bytes: result.bytes, report: result.report }, [result.bytes.buffer]);
  } catch (error) {
    self.postMessage({ type: 'error', message: error.message || String(error) });
  }
};
