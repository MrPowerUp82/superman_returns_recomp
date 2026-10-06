import { compileArguments, packLibrary, wireResult } from './vulkan-preshaders.js';
import { inspectSpirv } from './spirv-metadata.js';

export async function buildVulkanLibrary(containers, { emitterFactory, dxc, common }, onProgress = () => {}) {
  const entries = [], failed = [], decoder = new TextDecoder();
  for (let index = 0; index < containers.length; index++) {
    const container = containers[index], name = `shader.${container.stage}`;
    const diagnostics = [];
    try {
      // A translator assertion aborts its module. Give each source its own
      // instance so one unsupported shader cannot poison subsequent shaders.
      const emitter = await emitterFactory({ print: () => {}, printErr: (line) => diagnostics.push(line) });
      emitter.FS.writeFile('/shader_common.h', common);
      emitter.FS.writeFile(`/${name}.bin`, container.bytes);
      const rc = emitter.callMain(['/shader_common.h', '/out', `/${name}.bin`]);
      if (rc) throw new Error(`Translator exited ${rc}`);
      const source = decoder.decode(emitter.FS.readFile(`/out/${name}.hlsl`));
      const compiled = dxc.compileToSpirv(source, compileArguments(container.stage));
      if (compiled.error || !compiled.spirv?.length) throw new Error(compiled.error || 'DXC produced no shader');
      const binary = new Uint8Array(compiled.spirv);
      const metadata = inspectSpirv(binary, container.stage);
      entries.push({ ...container, result: wireResult(container.stage, binary, metadata) });
    } catch (error) {
      failed.push({ file: container.file, stage: container.stage, diagnostic: [...diagnostics, error.message || String(error)].join('\n').slice(0, 8192) });
    }
    onProgress({ phase: 'compile', done: index + 1, total: containers.length, ready: entries.length, failed: failed.length });
  }
  return {
    bytes: packLibrary(entries),
    report: { schema: 1, abi: 'sr-vulkan-buffers-v1', total: containers.length, ready: entries.length, failed,
      coverage: 'Original containers found in DATA/*.AST. Compressed executable and dynamically created shaders compile at runtime. Driver pipelines remain GPU-specific.' },
  };
}
