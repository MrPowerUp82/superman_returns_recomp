import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import fs from 'node:fs';
import os from 'node:os';
import dxcFactory from '../../docs/wasm/dxcompiler.mjs';
import { compileArguments, packLibrary, validateDxil, signDxil } from '../../docs/js/d3d12-preshaders.js';
import { buildD3d12Library } from '../../docs/js/build-d3d12-library.js';
import { dxilContainerHash } from '../../docs/js/dxil-container-hash.js';

const root = path.resolve(import.meta.dirname, '../..');
const dxc = await dxcFactory();
function container(stage = 'vs') {
  const bytes = new Uint8Array(64), view = new DataView(bytes.buffer);
  for (const [offset, value] of [[0, stage === 'vs' ? 0x102a1101 : 0x102a1100], [4, 60], [8, 4], [24, 36], [40, 4]]) view.setUint32(offset, value, false);
  return bytes;
}
function compile(stage) {
  const source = stage === 'vs' ? 'float4 main(float4 pos : POSITION) : SV_Position { return pos; }'
    : 'float4 main() : SV_Target { return float4(1,0,0,1); }';
  const result = dxc.compileToSpirv(source, compileArguments(stage));
  assert.equal(result.error, '', result.error);
  return new Uint8Array(result.spirv);
}

test('DXIL checksum matches Microsoft ComputeHashRetail across padding boundaries', () => {
  // Golden vectors from the upstream C++ implementation on bytes 0..length-1.
  for (const [length, digest] of [
    [0, '140d60f6b775e2ba4e4abed401b2e9a1'],
    [1, '2964836e48bd2a4cb17003dac0716b0a'],
    [55, '842e55534ba93daa93e94bd5af9b0c03'],
    [56, '00f9cc964ff2ec81959d4a3f092ce63f'],
    [63, '4400d428a41d0e75dc98b936693f7bfb'],
    [64, 'f42eb06ca921c878e435e3b8d4f92b13'],
    [119, 'ba3a67ecd4641ce4ed08f291896e1bc6'],
    [120, '882fce4f4c95194392e7adefd53fe343'],
  ]) {
    assert.equal(Buffer.from(dxilContainerHash(Uint8Array.from({ length }, (_, i) => i))).toString('hex'), digest);
  }
});

test('browser DXC produces valid DXIL for both D3D12 stages', () => {
  for (const stage of ['vs', 'ps']) validateDxil(compile(stage));
  assert.throws(() => compileArguments('compute'), /stage/i);
  assert.throws(() => validateDxil(new Uint8Array([3, 2, 35, 7])), /DXIL/);
});

test('browser DXIL signature matches the native Microsoft validator', {
  skip: !fs.existsSync(path.join(root, '.tools/dxc/bin/x64/dxv.exe')),
}, () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'sr-dxil-'));
  try {
    for (const stage of ['vs', 'ps']) {
      const binary = compile(stage), input = path.join(temporary, `${stage}.dxil`), output = path.join(temporary, `${stage}.signed.dxil`);
      fs.writeFileSync(input, binary);
      execFileSync(path.join(root, '.tools/dxc/bin/x64/dxv.exe'), [input, `-o=${output}`]);
      signDxil(binary);
      assert.deepEqual(Buffer.from(binary), fs.readFileSync(output));
    }
  } finally { fs.rmSync(temporary, { recursive: true, force: true }); }
});

test('D3D12 library matches native Python packing, XXH3 keys and ordering byte for byte', async () => {
  const entries = ['vs', 'ps'].map((stage) => ({ stage, bytes: container(stage), result: compile(stage) }));
  const bytes = await packLibrary(entries);
  const expected = execFileSync('python', ['-c',
    "import sys,json,xxhash;sys.path.insert(0,'tools/shaders');from make_preshaders import serialize;rows=json.loads(sys.stdin.read());sys.stdout.buffer.write(serialize([(xxhash.xxh3_64_intdigest(bytes(r['bytes'])),0 if r['stage']=='vs' else 1,bytes(r['bytes']),bytes(r['result'])) for r in rows]))"],
  { cwd: root, input: JSON.stringify(entries.map((e) => ({ stage: e.stage, bytes: [...e.bytes], result: [...e.result] }))) });
  assert.deepEqual(Buffer.from(bytes), expected);
  await assert.rejects(packLibrary([]), /No valid/);
  await assert.rejects(packLibrary([{ ...entries[0], stage: 'ps' }]), /entry/);
  await assert.rejects(packLibrary([{ ...entries[0], result: new Uint8Array([3, 2, 35, 7]) }]), /DXIL/);
});

test('D3D12 builder supplies runtime spec constants and isolates failed translations', async () => {
  let invocation = 0;
  // Only the native translator is replaced: it needs game microcode. DXC,
  // binary validation, XXH3 and library serialization all run for real.
  const emitterFactory = async () => {
    const files = new Map(), fail = invocation++ === 0;
    return { FS: { writeFile: (name, value) => files.set(name, value), readFile: (name) => files.get(name) },
      callMain() {
        if (fail) throw new Error('Unsupported microcode');
        files.set('/out/shader.ps.hlsl', new TextEncoder().encode('#define CONAN_RECOMP\nuint g_SpecConstantsRuntime;\nuint g_SpecConstants();\nfloat4 main() : SV_Target { return g_SpecConstants(); }'));
        return 0;
      } };
  };
  const progress = [];
  const result = await buildD3d12Library([
    { file: 'bad.AST', stage: 'vs', bytes: container() },
    { file: 'good.AST', stage: 'ps', bytes: container('ps') },
  ], { emitterFactory, dxc, common: new Uint8Array() }, (value) => progress.push(value));
  assert.equal(result.report.ready, 1, JSON.stringify(result.report));
  assert.equal(result.report.failed.length, 1);
  assert.match(result.report.failed[0].diagnostic, /Unsupported microcode/);
  assert.equal(new TextDecoder().decode(result.bytes.subarray(0, 8)), 'SRSHLIB\0');
  assert.equal(progress.at(-1).done, 2);
});
