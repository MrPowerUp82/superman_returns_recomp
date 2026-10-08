import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { downloadBuildZip } from '../../docs/js/build-download.js';

const buildUrl = 'https://example.com/builds/build.zip';
const versionUrl = 'https://example.com/builds/version.json';
const hash = (bytes) => createHash('sha256').update(bytes).digest('hex');
function fixture() {
  const bytes = Buffer.from('PK\x03\x04-package-with-launcher-and-runtime');
  const files = [bytes.subarray(0, 9), bytes.subarray(9)];
  const info = { version: 'test', zip_size: bytes.length, zip_sha256: hash(bytes),
    zip_parts: files.map((b, i) => ({ name: `build.part${i}`, size: b.length, sha256: hash(b) })) };
  const calls = [];
  const fetcher = async (url) => {
    calls.push(url);
    if (url === versionUrl) return Response.json(info);
    const index = info.zip_parts.findIndex((p) => new URL(p.name, versionUrl).href === url);
    return index < 0 ? new Response('', { status: 404 }) : new Response(files[index]);
  };
  return { bytes, files, info, calls, fetcher };
}
test('reassembles verified parts in manifest order with cumulative progress', async () => {
  const f = fixture(); const progress = [];
  const result = await downloadBuildZip({ buildUrl, versionUrl, fetcher: f.fetcher,
    onProgress: (done, total) => progress.push([done, total]) });
  assert.deepEqual(Buffer.from(await result.blob.arrayBuffer()), f.bytes);
  assert.equal(result.version, 'test');
  assert.deepEqual(progress.at(-1), [f.bytes.length, f.bytes.length]);
  assert.deepEqual(f.calls, [versionUrl, ...f.info.zip_parts.map((p) => new URL(p.name, versionUrl).href)]);
});
test('rejects corrupt parts and a wrong assembled ZIP digest', async () => {
  const f = fixture(); f.files[1] = Buffer.alloc(f.files[1].length);
  await assert.rejects(downloadBuildZip({ buildUrl, versionUrl, fetcher: f.fetcher }), /integrity/);
  const g = fixture(); g.info.zip_sha256 = '0'.repeat(64);
  await assert.rejects(downloadBuildZip({ buildUrl, versionUrl, fetcher: g.fetcher }), /ZIP failed/);
});
test('rejects missing parts, truncated parts and invalid manifests', async () => {
  const f = fixture(); f.files[0] = f.files[0].subarray(1);
  await assert.rejects(downloadBuildZip({ buildUrl, versionUrl, fetcher: f.fetcher }), /integrity/);
  const g = fixture(); g.info.zip_parts = [];
  await assert.rejects(downloadBuildZip({ buildUrl, versionUrl, fetcher: g.fetcher }), /manifest/);
  const h = fixture(); const fetcher = (url) => url === versionUrl ? h.fetcher(url) : new Response('', { status: 404 });
  await assert.rejects(downloadBuildZip({ buildUrl, versionUrl, fetcher }), /HTTP 404/);
});
test('preserves single ZIP downloads for older builds without a multipart manifest', async () => {
  for (const available of [true, false]) {
    const result = await downloadBuildZip({ buildUrl, versionUrl, fetcher: async (url) =>
      url === versionUrl ? (available ? Response.json({ version: 'old' }) : new Response('', { status: 404 })) : new Response('old ZIP') });
    assert.equal(await result.blob.text(), 'old ZIP');
    assert.equal(result.version, available ? 'old' : null);
  }
});
