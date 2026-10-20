"""Experimental offline ABI for immutable global Vulkan resource tables.

The game runtime must explicitly implement this ABI before consuming artifacts.
The portable header and DXIL toolchain are never modified in place.
"""
from vulkan_contract import build_contract as portable_contract,compile_arguments as portable_arguments

ABI='sr-vulkan-global-v1'
BACKEND='vulkan-global'

def compile_arguments(stage):
 args=[arg for arg in portable_arguments(stage) if arg!='-all-resources-bound']
 return args+['-fspv-extension=SPV_EXT_descriptor_indexing']

def build_contract():
 rows=[(0,'sampled_image_2d'),(1,'storage_buffer'),(2,'sampler'),(3,'sampled_image_3d'),(4,'sampled_image_cube')]
 return {'abi':ABI,'version':1,'experimental':True,'target':'vulkan1.1',
  'bindings':[dict(set=0,binding=b,type=t,runtime_array=True) for b,t in rows],
  'vertex_locations':portable_contract()['vertex_locations'],
  'push_bytes':8,'table_header_bytes':16,'draw_record_bytes':16,
  'constant_bytes':4096,'shared_bytes':512,'layout_attributes':224,'stream_record_bytes':16,
  'required_features':['runtimeDescriptorArray','shaderStorageBufferArrayDynamicIndexing','shaderSampledImageArrayNonUniformIndexing',
   'shaderStorageBufferArrayNonUniformIndexing','descriptorBindingPartiallyBound',
   'descriptorBindingSampledImageUpdateAfterBind','descriptorBindingStorageBufferUpdateAfterBind'],
  'resource_lifetime':'Slots and referenced resource versions remain immutable until submission completion.'}

def contract_errors(metadata):
 errors=[];allowed={row['binding']:row['type'] for row in build_contract()['bindings']}
 for capability in metadata['capabilities']:
  if capability not in (0,1,28,29,30,31,32,33,50,51,52,5301,5302,5307,5308):
   errors.append('Unexpected global ABI capability '+str(capability))
 for extension in metadata['extensions']:
  if extension not in ('SPV_EXT_descriptor_indexing','SPV_KHR_storage_buffer_storage_class'):
   errors.append('Unexpected global ABI extension '+extension)
 for row in metadata['descriptors']:
  if row['set']!=0 or allowed.get(row['binding'])!=row['type'] or not row['runtime_array'] or row['count']!=0:
   errors.append('Global descriptor ABI mismatch '+str(row))
 return errors

def prepare_common(source):
 """Fail closed if the known portable declarations no longer match."""
 def replace(text,old,new,count=1):
  if text.count(old)!=count:raise ValueError('Unsupported portable common declaration: '+old[:100])
  return text.replace(old,new)
 begin='[[vk::binding(0,0)]] ByteAddressBuffer g_VertexShaderConstants;'
 if source.count(begin)!=1:raise ValueError('Unsupported portable common: missing constant banks')
 start=source.index(begin)
 try:end=source.index('\n#else',start)
 except ValueError:raise ValueError('Unsupported portable common: missing constant bank branch') from None
 block='''// sr-vulkan-global-v1: per-submission table and per-draw index.
[[vk::binding(1,0)]] ByteAddressBuffer sr_GlobalBuffers[];
struct SrGlobalPush { uint draw_id; uint table_slot; };
[[vk::push_constant]] ConstantBuffer<SrGlobalPush> sr_GlobalPush;
uint4 sr_GlobalHeader() { return sr_GlobalBuffers[sr_GlobalPush.table_slot].Load4(0); }
uint4 sr_GlobalRecord() { return sr_GlobalBuffers[sr_GlobalPush.table_slot].Load4(16+sr_GlobalPush.draw_id*16); }
#define SR_BANK_Vertex 0u
#define SR_BANK_Pixel 4096u
#define SR_GLOBAL_CONSTANTS sr_GlobalBuffers[NonUniformResourceIndex(sr_GlobalHeader().x)]
#define SR_LOAD_FLOAT4(STAGE,OFFSET) asfloat(SR_GLOBAL_CONSTANTS.Load4(sr_GlobalRecord().z + SR_BANK_ ## STAGE + (OFFSET)))
#define SR_SHARED_UINT(OFFSET) SR_GLOBAL_CONSTANTS.Load(sr_GlobalRecord().z + 8192u + (OFFSET))
#define SR_SHARED_UINT4(OFFSET) SR_GLOBAL_CONSTANTS.Load4(sr_GlobalRecord().z + 8192u + (OFFSET))
#define SR_SHARED_FLOAT(OFFSET) asfloat(SR_SHARED_UINT(OFFSET))
#define SR_SHARED_FLOAT2(OFFSET) asfloat(SR_GLOBAL_CONSTANTS.Load2(sr_GlobalRecord().z + 8192u + (OFFSET)))
#define SR_SHARED_FLOAT4(OFFSET) asfloat(SR_SHARED_UINT4(OFFSET))
uint4 sr_GlobalVertexFetch(uint semantic) {
    uint4 record=sr_GlobalRecord();
    uint4 attribute=sr_GlobalBuffers[NonUniformResourceIndex(sr_GlobalHeader().y)].Load4(record.x + semantic*16);
    uint4 stream=sr_GlobalBuffers[NonUniformResourceIndex(sr_GlobalHeader().z)].Load4(record.y + attribute.x*16);
    return uint4(stream.x,stream.y+attribute.y,stream.z,attribute.w);
}'''
 source=source[:start]+block+source[end:]
 replacements=[
  ('#define g_VertexFetch(i)           SR_SHARED_UINT4(512 + (i)*16)','#define g_VertexFetch(i)           sr_GlobalVertexFetch(i)',1),
  ('[[vk::binding(0,1)]] Texture2D<float4> g_Texture2DDescriptorHeap[32];','[[vk::binding(0,0)]] Texture2D<float4> g_Texture2DDescriptorHeap[];',1),
  ('[[vk::binding(1,1)]] Texture3D<float4> g_Texture3DDescriptorHeap[32];','[[vk::binding(3,0)]] Texture3D<float4> g_Texture3DDescriptorHeap[];',1),
  ('[[vk::binding(2,1)]] TextureCube<float4> g_TextureCubeDescriptorHeap[32];','[[vk::binding(4,0)]] TextureCube<float4> g_TextureCubeDescriptorHeap[];',1),
  ('[[vk::binding(0,2)]] SamplerState g_SamplerDescriptorHeap[32];','[[vk::binding(2,0)]] SamplerState g_SamplerDescriptorHeap[];',1),
  ('[[vk::binding(0,3)]] ByteAddressBuffer g_VertexBufferHeap[32];','#define g_VertexBufferHeap sr_GlobalBuffers',1),
  ('g_VertexBufferHeap[meta.x]','g_VertexBufferHeap[NonUniformResourceIndex(meta.x)]',2)]
 for old,new,count in replacements:source=replace(source,old,new,count)
 # Keep packed texture flags intact for gamma, filtering and guest texel scale.
 source=source.replace('[TEX_INDEX(resourceDescriptorIndex)]','[NonUniformResourceIndex(TEX_INDEX(resourceDescriptorIndex))]')
 return source.replace('[samplerDescriptorIndex]','[NonUniformResourceIndex(samplerDescriptorIndex)]')
