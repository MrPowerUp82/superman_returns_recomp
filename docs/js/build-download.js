// Large packages travel as individually verified files below GitHub's blob limit.
// The assembled Blob is the same ZIP attached to the release.
import { sha256Hex } from './installer.js';

export async function downloadBuildZip({ buildUrl, versionUrl, fetcher = fetch, onProgress = () => {} }) {
  let info = null;
  try {
    const response = await fetcher(versionUrl, { cache: 'no-cache' });
    if (response.ok) info = await response.json();
  } catch { /* Older builds can still be downloaded without a version label. */ }
  const multipart = info && Object.hasOwn(info, 'zip_parts');
  if (multipart && (!Array.isArray(info.zip_parts) || !info.zip_parts.length ||
      !Number.isSafeInteger(info.zip_size) || info.zip_size <= 0 ||
      !/^[a-f0-9]{64}$/.test(info.zip_sha256))) throw new Error('Invalid build manifest');
  const parts = multipart ? info.zip_parts : [{ name: null }];
  const chunks = [];
  let received = 0;
  for (const part of parts) {
    if (multipart && (!/^[a-zA-Z0-9_.-]+$/.test(part.name) ||
        !Number.isSafeInteger(part.size) || part.size <= 0 ||
        !/^[a-f0-9]{64}$/.test(part.sha256))) throw new Error('Invalid build part');
    const response = await fetcher(multipart ? new URL(part.name, versionUrl).href : buildUrl, { cache: 'no-cache' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const reader = response.body.getReader();
    const buffers = [];
    let size = 0;
    const total = multipart ? info.zip_size : Number(response.headers.get('content-length')) || 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      buffers.push(value);
      size += value.length;
      received += value.length;
      onProgress(received, total);
    }
    const blob = new Blob(buffers);
    if (multipart && (size !== part.size || await sha256Hex(blob) !== part.sha256)) {
      throw new Error('Build part failed integrity verification');
    }
    chunks.push(blob);
  }
  const blob = new Blob(chunks, { type: 'application/zip' });
  if (multipart && (blob.size !== info.zip_size || await sha256Hex(blob) !== info.zip_sha256)) {
    throw new Error('Build ZIP failed integrity verification');
  }
  return { blob, version: info?.version || null };
}
