import json,struct,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/shaders'))
from compile_vulkan import Toolchain,compile_shader

# Relevant source declarations from the portable common. The host/toolchain
# integration below separately compiles the complete, actual common and corpus.
COMMON='''#ifdef SR_VULKAN_BUFFERS
[[vk::binding(0,0)]] ByteAddressBuffer g_VertexShaderConstants;
[[vk::binding(1,0)]] ByteAddressBuffer g_PixelShaderConstants;
[[vk::binding(2,0)]] ByteAddressBuffer g_SharedConstants;
#define SR_LOAD_FLOAT4(STAGE,OFFSET) asfloat(g_ ## STAGE ## ShaderConstants.Load4(OFFSET))
#else
// Original non-portable implementation remains intact.
#endif
#define g_VertexFetch(i)           SR_SHARED_UINT4(512 + (i)*16)
[[vk::binding(0,1)]] Texture2D<float4> g_Texture2DDescriptorHeap[32];
[[vk::binding(1,1)]] Texture3D<float4> g_Texture3DDescriptorHeap[32];
[[vk::binding(2,1)]] TextureCube<float4> g_TextureCubeDescriptorHeap[32];
[[vk::binding(0,2)]] SamplerState g_SamplerDescriptorHeap[32];
[[vk::binding(0,3)]] ByteAddressBuffer g_VertexBufferHeap[32];
float4 use(uint meta) { return g_VertexBufferHeap[meta.x].Load4(0); }
float4 use2(uint meta) { return g_VertexBufferHeap[meta.x].Load4(4); }
float4 sample() { return g_Texture2DDescriptorHeap[TEX_INDEX(resourceDescriptorIndex)].Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex],0); }
'''

class GlobalContractTests(unittest.TestCase):
 def tools(self,root):
  emitter=root/'emitter.exe';emitter.write_bytes(b'emitter')
  common=root/'common.h';common.write_text(COMMON)
  dxc=root/'dxc.exe';dxc.write_bytes(b'dxc')
  return Toolchain(emitter,common,dxc)
 def runner(self,args,**kwargs):
  if '-Fo' in args:Path(args[args.index('-Fo')+1]).write_bytes(struct.pack('<5I',0x07230203,0x10300,0,1,0))
  else:(Path(args[2])/(Path(args[3]).stem+'.hlsl')).write_text(Path(args[1]).read_text())
  return subprocess.CompletedProcess(args,0,'','')
 def test_profile_separates_cache_and_preserves_source(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);tools=self.tools(root);raw=root/'test.vs.bin';raw.write_bytes(b'container')
   with patch('compile_vulkan.subprocess.run',side_effect=self.runner) as runner:
    portable=compile_shader(raw,'vs',root,tools)
    global_result=compile_shader(raw,'vs',root,tools,profile='global')
    self.assertEqual(global_result.status,'ready',global_result.diagnostic)
    self.assertNotEqual(portable.cache_key,global_result.cache_key)
    self.assertIn('vulkan-global',global_result.binary_path.parts)
    self.assertEqual(tools.common.read_text(),COMMON)
    emitted=runner.call_args_list[2].args[0]
    self.assertNotEqual(Path(emitted[1]),tools.common)
    arguments=runner.call_args_list[3].args[0]
    self.assertIn('-fspv-extension=SPV_EXT_descriptor_indexing',arguments)
    self.assertNotIn('-all-resources-bound',arguments)
    metadata=json.loads(global_result.metadata_path.read_text())
    self.assertEqual(metadata['abi'],'sr-vulkan-global-v1')
    self.assertEqual(metadata['backend'],'vulkan-global')
    self.assertEqual(metadata['contract']['push_bytes'],8)
    self.assertTrue(metadata['contract']['experimental'])
    self.assertEqual(compile_shader(raw,'vs',root,tools,profile='global').status,'ready')
    self.assertEqual(runner.call_count,4)
 def test_hit_rejects_wrong_backend_or_abi_and_tracks_common(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);tools=self.tools(root);raw=root/'test.ps.bin';raw.write_bytes(b'container')
   with patch('compile_vulkan.subprocess.run',side_effect=self.runner) as runner:
    first=compile_shader(raw,'ps',root,tools,profile='global')
    self.assertEqual(first.status,'ready',first.diagnostic)
    for field,value in [('backend','vulkan'),('abi','sr-vulkan-buffers-v1')]:
     metadata=json.loads(first.metadata_path.read_text());metadata[field]=value;first.metadata_path.write_text(json.dumps(metadata))
     self.assertEqual(compile_shader(raw,'ps',root,tools,profile='global').status,'ready')
    self.assertEqual(runner.call_count,6)
    tools.common.write_text(COMMON+'\n// Changed helper behavior.\n')
    changed=compile_shader(raw,'ps',root,tools,profile='global')
    self.assertEqual(changed.status,'ready');self.assertNotEqual(first.cache_key,changed.cache_key)
 def test_unknown_profile_and_incompatible_header_fail_before_tools(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);tools=self.tools(root);raw=root/'test.ps.bin';raw.write_bytes(b'container')
   with patch('compile_vulkan.subprocess.run') as runner:
    result=compile_shader(raw,'ps',root,tools,profile='bogus')
    self.assertEqual(result.status,'failed');self.assertIn('profile',result.diagnostic)
    tools.common.write_text('unsupported common')
    result=compile_shader(raw,'ps',root,tools,profile='global')
    self.assertEqual(result.status,'failed');self.assertIn('common',result.diagnostic)
    runner.assert_not_called()
 def test_adaptation_covers_all_resource_types_and_keeps_packed_flags(self):
  from vulkan_global_contract import prepare_common,build_contract
  adapted=prepare_common(COMMON)
  self.assertIn('struct SrGlobalPush',adapted)
  self.assertIn('NonUniformResourceIndex(TEX_INDEX(resourceDescriptorIndex))',adapted)
  self.assertIn('NonUniformResourceIndex(samplerDescriptorIndex)',adapted)
  self.assertIn('semantic*16',adapted)
  self.assertIn('stream.y+attribute.y',adapted)
  self.assertNotIn('ShaderConstants.Load4',adapted)
  self.assertEqual(len(build_contract()['bindings']),5)
  self.assertIn('shaderStorageBufferArrayDynamicIndexing',build_contract()['required_features'])
  self.assertIn('Original non-portable implementation remains intact.',adapted)
 def test_global_validator_rejects_old_bindings_addresses_and_unknown_capabilities(self):
  from vulkan_global_contract import contract_errors
  good={'capabilities':[1,5301,5302,5307,5308],'extensions':['SPV_EXT_descriptor_indexing'],
   'descriptors':[dict(set=0,binding=1,type='storage_buffer',count=0,runtime_array=True)]}
  self.assertEqual(contract_errors(good),[])
  for changed in [
   {**good,'capabilities':[1,5347]},
   {**good,'capabilities':[1,4427]},
   {**good,'extensions':['unrecognized']},
   {**good,'descriptors':[dict(set=0,binding=1,type='storage_buffer',count=1,runtime_array=False)]},
   {**good,'descriptors':[dict(set=1,binding=0,type='sampled_image_2d',count=32,runtime_array=False)]}]:
   self.assertTrue(contract_errors(changed))

if __name__=='__main__':unittest.main()
