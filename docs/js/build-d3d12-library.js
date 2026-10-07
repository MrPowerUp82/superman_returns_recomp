import { compileArguments, packLibrary, signDxil } from './d3d12-preshaders.js';

export async function buildD3d12Library(containers, { emitterFactory, dxc, common }, onProgress = () => {}) {
  const entries = [], failed = [], decoder = new TextDecoder();
  for (let index = 0; index < containers.length; index++) {
    const container = containers[index], name = `shader.${container.stage}`, diagnostics = [];
    try {
      const emitter = await emitterFactory({ print: () => {}, printErr: (line) => diagnostics.push(line) });
      emitter.FS.writeFile('/shader_common.h', common);
      emitter.FS.writeFile(`/${name}.bin`, container.bytes);
      const rc = emitter.callMain(['/shader_common.h', '/out', `/${name}.bin`]);
      if (rc) throw new Error(`Translator exited ${rc}`);
      // Same standalone variant as build_catalog.py: spec constants are read
      // from the renderer's shared constant buffer, rather than fixed at zero.
      const source = decoder.decode(emitter.FS.readFile(`/out/${name}.hlsl`)) +
        '\n#ifndef __spirv__\n#ifdef CONAN_RECOMP\nuint g_SpecConstants() { return g_SpecConstantsRuntime; }\n#else\nuint g_SpecConstants() { return 0; }\n#endif\n#endif\n';
      // The generic WASM DXC wrapper calls its output "spirv" even when the
      // arguments select DXIL. Validate the actual bytes before packing them.
      const compiled = dxc.compileToSpirv(source, compileArguments(container.stage));
      if (compiled.error || !compiled.spirv?.length) throw new Error(compiled.error || 'DXC produced no shader');
      const binary = new Uint8Array(compiled.spirv);
      signDxil(binary);
      entries.push({ ...container, result: binary });
    } catch (error) {
      failed.push({ file: container.file, stage: container.stage, diagnostic: [...diagnostics, error.message || String(error)].join('\n').slice(0, 8192) });
    }
    onProgress({ phase: 'compile', done: index + 1, total: containers.length, ready: entries.length, failed: failed.length });
  }
  return {
    bytes: await packLibrary(entries),
    report: { schema: 1, api: 'd3d12', format: 'SRSHLIB-v1', total: containers.length, ready: entries.length, failed,
      coverage: 'Original containers found in DATA/*.AST. Compressed executable and dynamically created shaders compile at runtime. Driver pipelines remain GPU-specific.' },
  };
}
