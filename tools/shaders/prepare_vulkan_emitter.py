"""Prepare an isolated emitter tree; never rewrite the existing DXIL toolchain."""
from pathlib import Path
import argparse,hashlib,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
PIN='339af41df2c23dbe3256c1c377716b81a0e0fe6b'
def has_buffer_abi(src):
 # Check both halves of 0011, not patch context that later patches may change.
 markers={
  'shader_common.h':(
   '[[vk::binding(0,0)]] ByteAddressBuffer g_VertexShaderConstants;',
   '[[vk::binding(1,0)]] ByteAddressBuffer g_PixelShaderConstants;',
   '[[vk::binding(2,0)]] ByteAddressBuffer g_SharedConstants;',
   '#define SR_LOAD_FLOAT4(STAGE,OFFSET) asfloat(g_ ## STAGE ## ShaderConstants.Load4(OFFSET))',
   '#define SR_SHARED_FLOAT4(OFFSET) asfloat(g_SharedConstants.Load4(OFFSET))',
   '[[vk::binding(0,1)]] Texture2D<float4> g_Texture2DDescriptorHeap[32];',
   '[[vk::binding(1,1)]] Texture3D<float4> g_Texture3DDescriptorHeap[32];',
   '[[vk::binding(2,1)]] TextureCube<float4> g_TextureCubeDescriptorHeap[32];',
   '[[vk::binding(0,2)]] SamplerState g_SamplerDescriptorHeap[32];',
   '[[vk::binding(0,3)]] ByteAddressBuffer g_VertexBufferHeap[32];'),
  'shader_recompiler.cpp':(
   'println("#define {} SR_LOAD_FLOAT4({}, {})",',
   'println("#define s{}_Texture{}DescriptorIndex SR_SHARED_UINT({})",',
   'println("#define s{}_SamplerDescriptorIndex SR_SHARED_UINT({})",',
   'println("#define CONST_REL(INDEX) select((uint)(INDEX) < 256, SR_LOAD_FLOAT4({}, min((uint)(INDEX), 255) * 16), 0.0)",',
   'static constexpr DeclUsageLocation portableLocations[] = {',
   'println("[[vk::location({})]]", usage == DeclUsage::TexCoord ? usageIndex : (usage == DeclUsage::Color ? 16 : 18) + usageIndex);')}
 for name,required in markers.items():
  text=(src/name).read_text()
  if not all(marker in text for marker in required):return False
 return True
def prepare(root=ROOT):
 source=root/'.tools/xenosrecomp';target=root/'build/vulkan-m2/emitter-tree'
 current=subprocess.run(['git','-C',str(source/'src'),'rev-parse','HEAD'],capture_output=True,text=True)
 if current.returncode or current.stdout.strip()!=PIN:raise RuntimeError('Unexpected emitter source revision; source preserved')
 if not (source/'src/XenosRecomp/shader_recompiler.cpp').is_file():raise RuntimeError('Missing emitter source')
 shutil.copytree(source/'src/XenosRecomp',target/'src/XenosRecomp',dirs_exist_ok=True)
 for name in ('fmt','xxHash'):
  shutil.copytree(source/'deps'/name,target/'deps'/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.git','test','tests','doc','docs','build'))
 patch=root/'tools/shaders/xenosrecomp/patches/0011-vulkan-buffer-bindings.patch'
 args=['git','apply','--directory',str(target.relative_to(root)/'src')]
 if not has_buffer_abi(target/'src/XenosRecomp'):
  check=subprocess.run([*args,'--check',str(patch)],cwd=root,capture_output=True,text=True)
  if check.returncode:
   reverse=subprocess.run([*args,'--reverse','--check',str(patch)],cwd=root,capture_output=True,text=True)
   if reverse.returncode:raise RuntimeError('ABI patch conflicts with copied emitter source; original source preserved: '+check.stderr)
  else:subprocess.run([*args,str(patch)],cwd=root,check=True)
 fingerprint=hashlib.sha256(patch.read_bytes()).hexdigest()
 (target/'abi-patch.sha256').write_text(fingerprint,encoding='ascii')
 return target
if __name__=='__main__':
 try:print(prepare())
 except (OSError,RuntimeError,subprocess.CalledProcessError) as e:print(str(e),file=sys.stderr);sys.exit(1)
