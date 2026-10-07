import { VULKAN_LIBRARY_NAME } from './vulkan-preshaders.js';
import { D3D12_LIBRARY_NAME } from './d3d12-preshaders.js';

export function generateVulkanShaders(files, onProgress = () => {}, signal) {
  return generateShaders(files, onProgress, signal, 'vulkan');
}

export function generateShaders(files, onProgress = () => {}, signal, api = 'vulkan') {
  return new Promise((resolve, reject) => {
    if (!['vulkan', 'd3d12'].includes(api)) { reject(new Error('Invalid shader API')); return; }
    const libraryName = api === 'd3d12' ? D3D12_LIBRARY_NAME : VULKAN_LIBRARY_NAME;
    if (signal?.aborted) { reject(new DOMException('Cancelled', 'AbortError')); return; }
    const worker = new Worker(new URL('./vulkan-shader-worker.js', import.meta.url), { type: 'module' });
    let timer;
    const cleanup = () => { clearTimeout(timer); worker.terminate(); signal?.removeEventListener('abort', cancel); };
    const cancel = () => { cleanup(); reject(new DOMException('Cancelled', 'AbortError')); };
    const timeout = () => { clearTimeout(timer); timer = setTimeout(() => { cleanup(); reject(new Error('Shader generation timed out')); }, 180000); };
    signal?.addEventListener('abort', cancel, { once: true });
    worker.onmessage = ({ data }) => {
      timeout();
      if (data.type === 'progress') onProgress(data.value);
      else if (data.type === 'done') {
        cleanup();
        const library = new Blob([data.bytes], { type: 'application/octet-stream' });
        const report = new Blob([JSON.stringify(data.report, null, 2)], { type: 'application/json' });
        resolve({ library, report, summary: data.report, files: new Map([[libraryName, library], [`${libraryName}.report.json`, report]]) });
      } else if (data.type === 'error') { cleanup(); reject(new Error(data.message)); }
    };
    worker.onerror = (event) => { cleanup(); reject(new Error(event.message || 'WebAssembly shader worker failed')); };
    timeout(); worker.postMessage({ files: [...files], api });
  });
}
