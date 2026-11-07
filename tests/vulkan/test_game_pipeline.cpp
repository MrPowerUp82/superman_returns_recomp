#include "game_pipeline.h"
#include "vulkan_pipeline_seed.h"
#include "vulkan_cache_audit.h"
#include "test_main.h"
#include <fstream>
#include <cstring>
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
namespace shaders=superman_returns::graphics::shaders;

SR_TEST(vulkan_pipeline_seed_roundtrip_is_abi_scoped_and_contains_only_normalized_words) {
  std::vector<std::vector<uint64_t>> keys{{1,3,0,1,0,2,0,15,1,0,0,0,37,0,0,0,UINT64_MAX,123,456}};
  auto bytes=SerializeVulkanPipelineSeed(1,keys);
  std::vector<std::vector<uint64_t>> decoded;std::string error;
  SR_CHECK(ParseVulkanPipelineSeed(bytes,1,decoded,error));SR_CHECK(decoded==keys);
  SR_CHECK(!ParseVulkanPipelineSeed(bytes,2,decoded,error));SR_CHECK(decoded.empty());
  bytes.back()^=1;SR_CHECK(!ParseVulkanPipelineSeed(bytes,1,decoded,error));
  bytes.pop_back();SR_CHECK(!ParseVulkanPipelineSeed(bytes,1,decoded,error));
}

SR_TEST(vulkan_pipeline_seed_rejects_empty_invalid_and_unbounded_records) {
  SR_CHECK(SerializeVulkanPipelineSeed(0,{}).empty());
  SR_CHECK(SerializeVulkanPipelineSeed(1,{{1,2}}).empty());
  SR_CHECK(SerializeVulkanPipelineSeed(1,{std::vector<uint64_t>(2049)}).empty());
}
SR_TEST(vulkan_embedded_seed_merges_without_losing_local_records_or_order) {
  std::vector<uint64_t> a(19,1),b(19,2),c(19,3);
  std::vector<std::vector<uint64_t>> local{a,b,a};std::string error;
  auto bytes=SerializeVulkanPipelineSeed(1,{b,c,c});
  SR_CHECK(MergeVulkanPipelineSeed(bytes,1,local,error));
  SR_CHECK((local==std::vector<std::vector<uint64_t>>{a,b,c}));
  SR_CHECK(MergeVulkanPipelineSeed(bytes,1,local,error));
  SR_CHECK_EQ(local.size(),3u);
}
SR_TEST(vulkan_embedded_seed_validation_failure_preserves_every_local_record) {
  const std::vector<std::vector<uint64_t>> original{std::vector<uint64_t>(19,7)};
  auto bytes=SerializeVulkanPipelineSeed(1,{std::vector<uint64_t>(19,8)});
  std::string error;auto local=original;
  SR_CHECK(!MergeVulkanPipelineSeed(bytes,2,local,error));SR_CHECK(local==original);
  bytes.back()^=1;
  SR_CHECK(!MergeVulkanPipelineSeed(bytes,1,local,error));SR_CHECK(local==original);
}
SR_TEST(vulkan_embedded_seed_capacity_failure_is_atomic) {
  std::vector<std::vector<uint64_t>> local;
  for(uint64_t i=0;i<8192;++i){local.emplace_back(19,0);local.back()[0]=i;}
  const auto original=local;std::string error;
  auto extra=std::vector<uint64_t>(19,0);extra[0]=8192;
  SR_CHECK(!MergeVulkanPipelineSeed(SerializeVulkanPipelineSeed(1,{extra}),1,local,error));
  SR_CHECK(local==original);
}
SR_TEST(vulkan_shipped_seed_has_decodable_normalized_states) {
  const auto path=std::filesystem::path(__FILE__).parent_path()/"../../port/assets/vulkan_pipeline_seed.bin";
  std::ifstream file(path,std::ios::binary|std::ios::ate);SR_CHECK(bool(file));if(!file)return;
  const auto size=file.tellg();SR_CHECK(size>0);if(size<=0)return;
  std::vector<uint8_t> bytes(size_t(size),uint8_t{});file.seekg(0);
  SR_CHECK(bool(file.read(reinterpret_cast<char*>(bytes.data()),bytes.size())));
  std::vector<std::vector<uint64_t>> records;std::string error;
  SR_CHECK(ParseVulkanPipelineSeed(bytes,1,records,error));SR_CHECK_EQ(records.size(),308u);
  for(const auto& key:records){GamePipelinePlan plan;TargetPass pass;Error e;SR_CHECK(DecodeGamePipelineSeed(key,plan,pass,e));}
}
namespace {
uint32_t layout_calls=0,layout_sets=0,layout_push_count=0,pipeline_calls=0;
VkResult pipeline_result=VK_SUCCESS;
VkPushConstantRange layout_push{};
VkResult VKAPI_CALL PipelineLayout(VkDevice,const VkPipelineLayoutCreateInfo* info,const VkAllocationCallbacks*,VkPipelineLayout* out) {
  ++layout_calls;layout_sets=info->setLayoutCount;layout_push_count=info->pushConstantRangeCount;
  layout_push=layout_push_count?info->pPushConstantRanges[0]:VkPushConstantRange{};
  *out=reinterpret_cast<VkPipelineLayout>(1);return VK_SUCCESS;
}
VkResult VKAPI_CALL PipelineCache(VkDevice,const VkPipelineCacheCreateInfo*,const VkAllocationCallbacks*,VkPipelineCache* out) {*out=reinterpret_cast<VkPipelineCache>(2);return VK_SUCCESS;}
VkResult VKAPI_CALL PipelineShader(VkDevice,const VkShaderModuleCreateInfo*,const VkAllocationCallbacks*,VkShaderModule* out) {*out=reinterpret_cast<VkShaderModule>(3);return VK_SUCCESS;}
VkResult VKAPI_CALL GraphicsPipeline(VkDevice,VkPipelineCache,uint32_t,const VkGraphicsPipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline* out) {++pipeline_calls;if(pipeline_result==VK_SUCCESS)*out=reinterpret_cast<VkPipeline>(4);return pipeline_result;}
VkResult VKAPI_CALL SeedRenderPass(VkDevice,const VkRenderPassCreateInfo*,const VkAllocationCallbacks*,VkRenderPass* out) {*out=reinterpret_cast<VkRenderPass>(5);return VK_SUCCESS;}
VkResult VKAPI_CALL SeedDriverData(VkDevice,VkPipelineCache,size_t* size,void* data) {
  if(!data){*size=32;return VK_SUCCESS;}
  const uint32_t words[8]{32,VK_PIPELINE_CACHE_HEADER_VERSION_ONE};
  std::memcpy(data,words,sizeof(words));*size=sizeof(words);return VK_SUCCESS;
}
void VKAPI_CALL FreeSeedRenderPass(VkDevice,VkRenderPass,const VkAllocationCallbacks*) {}
void VKAPI_CALL FreePipelineLayout(VkDevice,VkPipelineLayout,const VkAllocationCallbacks*) {}
void VKAPI_CALL FreePipelineCache(VkDevice,VkPipelineCache,const VkAllocationCallbacks*) {}
void VKAPI_CALL FreePipelineShader(VkDevice,VkShaderModule,const VkAllocationCallbacks*) {}
void VKAPI_CALL FreePipeline(VkDevice,VkPipeline,const VkAllocationCallbacks*) {}
VkResult VKAPI_CALL PipelineIdle(VkDevice) {return VK_SUCCESS;}
Dispatch PipelineDispatch() {
  Dispatch f;f.vkCreatePipelineLayout=PipelineLayout;f.vkDestroyPipelineLayout=FreePipelineLayout;
  f.vkCreatePipelineCache=PipelineCache;f.vkDestroyPipelineCache=FreePipelineCache;
  f.vkCreateShaderModule=PipelineShader;f.vkDestroyShaderModule=FreePipelineShader;
  f.vkCreateGraphicsPipelines=GraphicsPipeline;f.vkDestroyPipeline=FreePipeline;f.vkDeviceWaitIdle=PipelineIdle;
  f.vkCreateRenderPass=SeedRenderPass;f.vkDestroyRenderPass=FreeSeedRenderPass;return f;
}
void GlobalPipelineDevice(Context& c);
shaders::CompiledShader PipelineArtifact(shaders::ShaderStage stage,shaders::ShaderAbi abi);
SR_TEST(vulkan_pipeline_seed_reconstructs_fixed_state_and_vertex_layout_without_handles) {
  guest::DrawPacket draw;draw.registers[0x104]=15;draw.registers[0x201]=1|(1<<16);
  draw.registers[0x200]=2|4|(3<<4);draw.depth_bias=-5;draw.slope_bias=.75f;
  draw.attributes={{2,8,0x2c23a5,0,0}};guest::VertexStream stream;stream.stream=2;stream.stride=24;draw.streams={stream};
  TargetPass pass;pass.color_count=1;pass.formats[0]=VK_FORMAT_R8G8B8A8_UNORM;pass.depth_format=VK_FORMAT_D24_UNORM_S8_UINT;
  shaders::CompiledShader vs;vs.inputs={{0,"float2"}};GamePipelinePlan original,decoded;Error e;TargetPass compatible;
  SR_CHECK(PlanGamePipeline(draw,pass,vs,original,e));original.key.push_back(123);original.key.push_back(456);
  SR_CHECK(DecodeGamePipelineSeed(original.key,decoded,compatible,e));
  SR_CHECK(decoded.key==original.key);SR_CHECK(compatible.render_pass==VK_NULL_HANDLE);
  SR_CHECK_EQ(decoded.bindings[0].binding,2u);SR_CHECK_EQ(decoded.bindings[0].stride,24u);
  SR_CHECK_EQ(decoded.attributes[0].offset,8u);SR_CHECK_EQ(decoded.raster.depthBiasConstantFactor,-5.f);
  SR_CHECK_EQ(decoded.raster.depthBiasSlopeFactor,.75f);SR_CHECK_EQ(compatible.depth_format,pass.depth_format);
  auto broken=original.key;broken[11]=0x7f800000;
  SR_CHECK(!DecodeGamePipelineSeed(broken,decoded,compatible,e));
  broken=original.key;broken.erase(broken.begin()+18);SR_CHECK(!DecodeGamePipelineSeed(broken,decoded,compatible,e));
}

SR_TEST(vulkan_builtin_prewarm_reuses_pipeline_and_counts_failed_runtime_attempts) {
  Context c(PipelineDispatch());GlobalPipelineDevice(c);c.properties.limits.maxColorAttachments=4;
  VkDescriptorSetLayout layout{};Error e;GamePipelineStore store(c);
  SR_CHECK(store.Initialize({&layout,1},{},e,shaders::ShaderAbi::kGlobal));
  auto vs=PipelineArtifact(shaders::ShaderStage::kVertex,shaders::ShaderAbi::kGlobal);
  auto ps=PipelineArtifact(shaders::ShaderStage::kPixel,shaders::ShaderAbi::kGlobal);
  guest::DrawPacket draw;TargetPass pass;pass.color_count=1;pass.formats[0]=VK_FORMAT_R8G8B8A8_UNORM;
  const auto startup=VulkanCacheAudit::startup_attempts.load(),runtime=VulkanCacheAudit::pipeline_attempts.load();
  SR_CHECK(store.PrewarmBuiltin(draw,pass.formats[0],vs,&ps,e));
  SR_CHECK_EQ(VulkanCacheAudit::startup_attempts.load(),startup+1);
  SR_CHECK(bool(store.Acquire(draw,pass,vs,&ps,1,e)));
  SR_CHECK_EQ(VulkanCacheAudit::pipeline_attempts.load(),runtime);
  pipeline_result=VK_ERROR_OUT_OF_DEVICE_MEMORY;draw.registers[0x201]=6;
  SR_CHECK(!store.Acquire(draw,pass,vs,&ps,1,e));pipeline_result=VK_SUCCESS;
  SR_CHECK_EQ(VulkanCacheAudit::pipeline_attempts.load(),runtime+1);SR_CHECK_EQ(store.stats().failed,1u);
}
void GlobalPipelineDevice(Context& c) {
  c.device=reinterpret_cast<VkDevice>(1);
  auto& l=c.properties.limits;l.maxPushConstantsSize=128;l.maxBoundDescriptorSets=1;l.maxVertexInputAttributes=16;l.maxVertexOutputComponents=64;l.maxFragmentInputComponents=64;
  // Zero legacy descriptor limits deliberately cannot fit GameBindingLayout.
  c.enabled_features.shaderStorageBufferArrayDynamicIndexing=true;
  auto& f=c.descriptor_indexing_features;f.runtimeDescriptorArray=true;f.shaderSampledImageArrayNonUniformIndexing=true;f.shaderStorageBufferArrayNonUniformIndexing=true;
  f.descriptorBindingPartiallyBound=true;f.descriptorBindingSampledImageUpdateAfterBind=true;f.descriptorBindingStorageBufferUpdateAfterBind=true;
}
shaders::CompiledShader PipelineArtifact(shaders::ShaderStage stage,shaders::ShaderAbi abi) {
  shaders::CompiledShader shader;shader.stage=stage;shader.abi=abi;
  shader.words={0x07230203,0x10300,0,2,0,(5u<<16)|15,stage==shaders::ShaderStage::kVertex?0u:4u,1,0x6e69616d,0};
  shader.requirements.descriptor_sets=1;
  if(abi==shaders::ShaderAbi::kGlobal) shader.requirements.storage_buffer_dynamic_indexing=true;
  return shader;
}
}
SR_TEST(vulkan_observed_descriptions_persist_and_prewarm_from_ready_library_only) {
  const auto directory=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::create_directory(directory);const auto cache=directory/"driver.cache";
  auto dispatch=PipelineDispatch();dispatch.vkGetPipelineCacheData=SeedDriverData;
  Context c(dispatch);GlobalPipelineDevice(c);c.properties.limits.maxColorAttachments=4;
  auto vs=PipelineArtifact(shaders::ShaderStage::kVertex,shaders::ShaderAbi::kGlobal);
  auto ps=PipelineArtifact(shaders::ShaderStage::kPixel,shaders::ShaderAbi::kGlobal);
  VkDescriptorSetLayout layout{};Error e;guest::DrawPacket draw;
  TargetPass pass;pass.color_count=1;pass.formats[0]=VK_FORMAT_R8G8B8A8_UNORM;
  {
    GamePipelineStore first(c);SR_CHECK(first.Initialize({&layout,1},cache,e,shaders::ShaderAbi::kGlobal));
    SR_CHECK(bool(first.Acquire(draw,pass,vs,&ps,1,e)));SR_CHECK(first.CheckpointCache(e));
  }
  auto descriptions=cache;descriptions+=".descriptions";SR_CHECK(std::filesystem::is_regular_file(descriptions));
  auto word=[](std::vector<uint8_t>& bytes,uint32_t value){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(8*i)));};
  auto wire=[&](const shaders::CompiledShader& shader) {
    std::vector<uint8_t> bytes;
    for(uint32_t value:{0x33525653u,2u,1u,uint32_t(shader.stage)})word(bytes,value);
    for(unsigned i=0;i<12;++i)word(bytes,0);word(bytes,0);word(bytes,0);word(bytes,uint32_t(shader.words.size()*4));
    for(auto value:shader.words)word(bytes,value);return bytes;
  };
  std::vector<uint8_t> body;
  for(const auto* shader:{&vs,&ps}) {
    const auto result=wire(*shader);
    word(body,uint32_t(shader->stage));word(body,24);word(body,uint32_t(result.size()));word(body,0);
    body.insert(body.end(),24,uint8_t(shader->stage));body.insert(body.end(),result.begin(),result.end());
  }
  std::vector<uint8_t> library{'S','R','V','K','L','I','B',0};word(library,2);word(library,2);
  const auto checksum=VulkanSeedChecksum(body);word(library,uint32_t(checksum));word(library,uint32_t(checksum>>32));
  library.insert(library.end(),body.begin(),body.end());
  shaders::VulkanShaderConfig config;config.cache=directory/"shaders";config.library=directory/"test.srvk";config.abi=shaders::ShaderAbi::kGlobal;
  {std::ofstream file(config.library,std::ios::binary);file.write(reinterpret_cast<const char*>(library.data()),library.size());}
  {
    shaders::VulkanShaderService service(config,{});SR_CHECK_EQ(service.PrecompiledCount(),2u);
    GamePipelineStore second(c);SR_CHECK(second.Initialize({&layout,1},cache,e,shaders::ShaderAbi::kGlobal));
    const auto fresh=directory/"fresh.cache";
    std::ifstream input(descriptions,std::ios::binary|std::ios::ate);
    const auto length=input.tellg();std::vector<uint8_t> bytes(size_t(length),uint8_t{});
    input.seekg(0);SR_CHECK(bool(input.read(reinterpret_cast<char*>(bytes.data()),bytes.size())));
    {
      GamePipelineStore cold(c);SR_CHECK(cold.Initialize({&layout,1},fresh,e,shaders::ShaderAbi::kGlobal));
      SR_CHECK(cold.ImportDescriptionSeed(bytes,e));
      const auto attempts=VulkanCacheAudit::pipeline_attempts.load();
      SR_CHECK(cold.PrewarmObserved(service,e));SR_CHECK_EQ(cold.stats().prewarmed,1u);
      SR_CHECK(bool(cold.Acquire(draw,pass,vs,&ps,1,e)));
      SR_CHECK_EQ(VulkanCacheAudit::pipeline_attempts.load(),attempts);
      SR_CHECK(cold.CheckpointCache(e));
    }
    {
      GamePipelineStore disabled(c);SR_CHECK(disabled.Initialize({&layout,1},{},e,shaders::ShaderAbi::kGlobal));
      SR_CHECK(!disabled.ImportDescriptionSeed(bytes,e));
      SR_CHECK(disabled.PrewarmObserved(service,e));SR_CHECK_EQ(disabled.stats().prewarmed,0u);
    }
    const auto runtime=VulkanCacheAudit::pipeline_attempts.load();
    SR_CHECK(second.PrewarmObserved(service,e));SR_CHECK_EQ(second.stats().prewarmed,1u);
    SR_CHECK(bool(second.Acquire(draw,pass,vs,&ps,1,e)));
    SR_CHECK_EQ(VulkanCacheAudit::pipeline_attempts.load(),runtime);
  }
  {
    shaders::VulkanShaderConfig missing;missing.cache=directory/"empty";missing.abi=shaders::ShaderAbi::kGlobal;
    shaders::VulkanShaderService service(missing,{});
    GamePipelineStore third(c);SR_CHECK(third.Initialize({&layout,1},cache,e,shaders::ShaderAbi::kGlobal));
    SR_CHECK(!third.PrewarmObserved(service,e));SR_CHECK_EQ(third.stats().prewarm_failed,1u);
    SR_CHECK(bool(third.Acquire(draw,pass,vs,&ps,1,e))); // fallback remains available
  }
  std::filesystem::remove_all(directory);
}
SR_TEST(game_pipeline_global_layout_requires_optin_and_declares_draw_push_constants) {
  Context c(PipelineDispatch());GlobalPipelineDevice(c);Error e;
  VkDescriptorSetLayout layouts[4]{};
  {
    GamePipelineStore portable(c);SR_CHECK(!portable.Initialize({layouts,1},{},e));
    SR_CHECK(portable.Initialize(layouts,{},e));SR_CHECK_EQ(layout_sets,4u);SR_CHECK_EQ(layout_push_count,0u);
  }
  {
    GamePipelineStore global(c);SR_CHECK(!global.Initialize(layouts,{},e,shaders::ShaderAbi::kGlobal));
    SR_CHECK(global.Initialize({layouts,1},{},e,shaders::ShaderAbi::kGlobal));
    SR_CHECK_EQ(layout_sets,1u);SR_CHECK_EQ(layout_push_count,1u);SR_CHECK_EQ(layout_push.offset,0u);SR_CHECK_EQ(layout_push.size,8u);
    SR_CHECK_EQ(layout_push.stageFlags,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT);
  }
}
SR_TEST(game_pipeline_global_profile_requires_enabled_heap_features_and_push_capacity) {
  Context c(PipelineDispatch());GlobalPipelineDevice(c);Error e;VkDescriptorSetLayout layout{};
  using Feature=VkBool32 VkPhysicalDeviceDescriptorIndexingFeatures::*;
  for(Feature feature:{&VkPhysicalDeviceDescriptorIndexingFeatures::runtimeDescriptorArray,&VkPhysicalDeviceDescriptorIndexingFeatures::shaderSampledImageArrayNonUniformIndexing,
      &VkPhysicalDeviceDescriptorIndexingFeatures::shaderStorageBufferArrayNonUniformIndexing,&VkPhysicalDeviceDescriptorIndexingFeatures::descriptorBindingPartiallyBound,
      &VkPhysicalDeviceDescriptorIndexingFeatures::descriptorBindingSampledImageUpdateAfterBind,&VkPhysicalDeviceDescriptorIndexingFeatures::descriptorBindingStorageBufferUpdateAfterBind}) {
    c.descriptor_indexing_features.*feature=false;auto before=layout_calls;GamePipelineStore store(c);
    SR_CHECK(!store.Initialize({&layout,1},{},e,shaders::ShaderAbi::kGlobal));SR_CHECK_EQ(layout_calls,before);c.descriptor_indexing_features.*feature=true;
  }
  c.enabled_features.shaderStorageBufferArrayDynamicIndexing=false;
  {GamePipelineStore store(c);SR_CHECK(!store.Initialize({&layout,1},{},e,shaders::ShaderAbi::kGlobal));}
  c.enabled_features.shaderStorageBufferArrayDynamicIndexing=true;c.properties.limits.maxPushConstantsSize=7;
  {GamePipelineStore store(c);SR_CHECK(!store.Initialize({&layout,1},{},e,shaders::ShaderAbi::kGlobal));}
}
SR_TEST(game_pipeline_profiles_reject_mixed_artifacts_before_pipeline_creation) {
  Context c(PipelineDispatch());GlobalPipelineDevice(c);Error e;VkDescriptorSetLayout layouts[4]{};guest::DrawPacket d;TargetPass pass;
  auto vs=PipelineArtifact(shaders::ShaderStage::kVertex,shaders::ShaderAbi::kGlobal),ps=PipelineArtifact(shaders::ShaderStage::kPixel,shaders::ShaderAbi::kGlobal);
  pipeline_calls=0;
  {
    GamePipelineStore portable(c);SR_CHECK(portable.Initialize(layouts,{},e));
    SR_CHECK(!portable.Acquire(d,pass,vs,&ps,1,e));SR_CHECK(e.message.find("ABI")!=std::string::npos);
  }
  {
    GamePipelineStore global(c);SR_CHECK(global.Initialize({layouts,1},{},e,shaders::ShaderAbi::kGlobal));
    ps.abi=shaders::ShaderAbi::kPortable;SR_CHECK(!global.Acquire(d,pass,vs,&ps,1,e));SR_CHECK_EQ(pipeline_calls,0u);
    ps.abi=shaders::ShaderAbi::kGlobal;vs.abi=shaders::ShaderAbi::kPortable;SR_CHECK(!global.Acquire(d,pass,vs,&ps,1,e));SR_CHECK_EQ(pipeline_calls,0u);
    vs.abi=shaders::ShaderAbi::kGlobal;
    SR_CHECK(bool(global.Acquire(d,pass,vs,&ps,1,e)));SR_CHECK_EQ(pipeline_calls,1u);
  }
}
SR_TEST(game_pipeline_global_profile_validates_static_stage_limits_without_legacy_layout_counts) {
  Context c(PipelineDispatch());GlobalPipelineDevice(c);Error e;VkDescriptorSetLayout layout{};guest::DrawPacket d;TargetPass pass;
  auto vs=PipelineArtifact(shaders::ShaderStage::kVertex,shaders::ShaderAbi::kGlobal),ps=PipelineArtifact(shaders::ShaderStage::kPixel,shaders::ShaderAbi::kGlobal);
  GamePipelineStore store(c);SR_CHECK(store.Initialize({&layout,1},{},e,shaders::ShaderAbi::kGlobal));
  auto reject=[&](auto change) {auto v=vs,p=ps;change(v.requirements,p.requirements);SR_CHECK(!store.Acquire(d,pass,v,&p,1,e));};
  using R=shaders::ShaderRequirements&;
  reject([](R v,R){v.vertex_attributes=17;});reject([](R v,R){v.vertex_output_components=65;});
  reject([](R,R p){p.fragment_input_components=65;});reject([](R v,R){v.clip_distance=true;});reject([](R,R p){p.cull_distance=true;});
  reject([](R v,R){v.descriptor_sets=2;});reject([](R v,R){v.storage_buffers=1;});
  vs.requirements.vertex_output_components=64;ps.requirements.fragment_input_components=64;
  SR_CHECK(bool(store.Acquire(d,pass,vs,&ps,1,e)));
}
SR_TEST(game_shader_entry_point_comes_from_spirv_instead_of_helper_names) {
  shaders::CompiledShader shader;shader.words={0x07230203,0x10300,0,2,0,(5u<<16)|15,0,1,0x6e69616d,0};Error e;
  SR_CHECK(ShaderEntryPoint(shader,e)=="main");SR_CHECK(e.message.empty());
  shader.words[6]=4;SR_CHECK(ShaderEntryPoint(shader,e).empty());SR_CHECK(!e.message.empty());
  shader.stage=shaders::ShaderStage::kPixel;SR_CHECK(ShaderEntryPoint(shader,e)=="main");
  shader.words[5]=(99u<<16)|15;SR_CHECK(ShaderEntryPoint(shader,e).empty());
}
SR_TEST(game_pipeline_key_changes_with_fixed_state_and_formats) {
  guest::DrawPacket d;d.registers[0x104]=15;d.registers[0x201]=1|(1<<16);
  TargetPass pass;pass.color_count=1;pass.formats[0]=VK_FORMAT_R8G8B8A8_UNORM;
  shaders::CompiledShader vs;GamePipelinePlan a,b;Error e;
  SR_CHECK(PlanGamePipeline(d,pass,vs,a,e));
  d.registers[0x201]^=6;SR_CHECK(PlanGamePipeline(d,pass,vs,b,e));SR_CHECK(a.key!=b.key);
  d.registers[0x200]=1|(2<<8);SR_CHECK(PlanGamePipeline(d,pass,vs,b,e));SR_CHECK(a.key!=b.key);
  pass.formats[0]=VK_FORMAT_R16G16B16A16_SFLOAT;SR_CHECK(PlanGamePipeline(d,pass,vs,b,e));SR_CHECK(a.key!=b.key);
}
SR_TEST(game_vertex_locations_keep_gaps_and_ushort2_contract) {
  guest::DrawPacket d;d.registers[0x104]=15;d.attributes={{0,0,0x2c2259,0,0},{1,0,0x2c23a5,5,7}};
  guest::VertexStream a,b;a.stream=0;a.stride=4;b.stream=1;b.stride=8;d.streams={a,b};
  shaders::CompiledShader vs;vs.inputs={{0,"float32x4"},{20,"float32x4"}};
  TargetPass pass;pass.color_count=1;pass.formats[0]=VK_FORMAT_R8G8B8A8_UNORM;
  GamePipelinePlan plan;Error e;SR_CHECK(PlanGamePipeline(d,pass,vs,plan,e));
  SR_CHECK_EQ(plan.attributes.size(),2u);SR_CHECK_EQ(plan.attributes[1].location,20u);
  SR_CHECK_EQ(plan.attributes[0].format,VK_FORMAT_R16G16_UNORM);
}
SR_TEST(game_pipeline_requires_enabled_independent_blend_and_valid_vertex_limits) {
  GamePipelinePlan plan;VkPhysicalDeviceLimits limits{};limits.maxColorAttachments=4;limits.maxVertexInputBindings=16;limits.maxVertexInputAttributes=16;limits.maxVertexInputBindingStride=2048;limits.maxVertexInputAttributeOffset=2047;
  VkPhysicalDeviceFeatures features{};Error e;plan.bindings={{31,16,VK_VERTEX_INPUT_RATE_VERTEX}};
  SR_CHECK(!ValidateGamePipelineFeatures(plan,1,limits,features,e));plan.bindings={{0,16,VK_VERTEX_INPUT_RATE_VERTEX}};
  plan.attributes={{20,0,VK_FORMAT_R32G32_SFLOAT,0}};SR_CHECK(!ValidateGamePipelineFeatures(plan,1,limits,features,e));plan.attributes.clear();
  plan.colors[0].colorWriteMask=15;plan.colors[1].colorWriteMask=3;
  SR_CHECK(!ValidateGamePipelineFeatures(plan,2,limits,features,e));features.independentBlend=true;SR_CHECK(ValidateGamePipelineFeatures(plan,2,limits,features,e));
}
// The memo returns the previous pipeline only when every raw input PlanGamePipeline (and the pipeline cache key)
// reads is identical. Each input is changed alone: a stale hit would draw with the wrong fixed-function state.
SR_TEST(game_pipeline_memo_hits_only_when_every_input_is_identical) {
  auto shader=[](shaders::ShaderStage stage) {auto s=std::make_shared<shaders::CompiledShader>();s->stage=stage;return s;};
  std::shared_ptr<const shaders::CompiledShader> vs=shader(shaders::ShaderStage::kVertex),ps=shader(shaders::ShaderStage::kPixel);
  guest::DrawPacket d;d.registers[0x104]=15;d.registers[0x201]=1|(1<<16);d.attributes={{0,0,0x2c23a5,0,0},{1,8,0x1a23a6,5,1}};
  guest::VertexStream s0,s1;s0.stream=0;s0.stride=16;s1.stream=1;s1.stride=32;d.streams={s0,s1};
  TargetPass pass;pass.color_count=1;pass.formats[0]=VK_FORMAT_R8G8B8A8_UNORM;pass.depth_format=VK_FORMAT_D24_UNORM_S8_UINT;
  auto pipeline=std::make_shared<GamePipeline>();
  GamePipelineMemo memo;
  SR_CHECK(!memo.Matches(d,pass,vs,ps));SR_CHECK(!memo.pipeline());  // empty memo never hits
  memo.Store(d,pass,vs,ps,pipeline);
  SR_CHECK(memo.Matches(d,pass,vs,ps));SR_CHECK(memo.pipeline()==pipeline);
  // Another pass object with the same formats is the same pipeline (the cache key ignores the pass identity).
  TargetPass same_formats=pass;SR_CHECK(memo.Matches(d,same_formats,vs,ps));
  int misses=0;
  auto expect_miss=[&](const char* what,auto change) {
    auto draw=d;auto pass_copy=pass;auto vertex=vs,pixel=ps;change(draw,pass_copy,vertex,pixel);
    if(memo.Matches(draw,pass_copy,vertex,pixel)) {++misses;std::printf("  memo still hits after changing: %s\n",what);}
  };
  using Draw=guest::DrawPacket&;using Pass=TargetPass&;using Shader=std::shared_ptr<const shaders::CompiledShader>&;
  expect_miss("vs",[&](Draw,Pass,Shader v,Shader) {v=shader(shaders::ShaderStage::kVertex);});
  expect_miss("ps",[&](Draw,Pass,Shader,Shader p) {p=shader(shaders::ShaderStage::kPixel);});
  expect_miss("ps absent",[&](Draw,Pass,Shader,Shader p) {p.reset();});
  expect_miss("primitive",[&](Draw x,Pass,Shader,Shader) {x.primitive=guest::Primitive::kTriangleStrip;});
  expect_miss("indexed",[&](Draw x,Pass,Shader,Shader) {x.indexed=true;});
  expect_miss("restart",[&](Draw x,Pass,Shader,Shader) {x.primitive_restart=true;});
  expect_miss("reg 0x200",[&](Draw x,Pass,Shader,Shader) {x.registers[0x200]^=2;});
  expect_miss("reg 0x201",[&](Draw x,Pass,Shader,Shader) {x.registers[0x201]^=6;});
  expect_miss("reg 0x205",[&](Draw x,Pass,Shader,Shader) {x.registers[0x205]^=2;});
  expect_miss("reg 0x104",[&](Draw x,Pass,Shader,Shader) {x.registers[0x104]^=1;});
  expect_miss("reg 0x10d",[&](Draw x,Pass,Shader,Shader) {x.registers[0x10d]^=0x100;});
  expect_miss("depth bias",[&](Draw x,Pass,Shader,Shader) {x.depth_bias=3;});
  expect_miss("slope bias",[&](Draw x,Pass,Shader,Shader) {x.slope_bias=0.5f;});
  expect_miss("inline vertices",[&](Draw x,Pass,Shader,Shader) {x.inline_vertices=true;});
  expect_miss("inline stride",[&](Draw x,Pass,Shader,Shader) {x.inline_stride=12;});
  expect_miss("attribute stream",[&](Draw x,Pass,Shader,Shader) {x.attributes[0].stream=1;});
  expect_miss("attribute offset",[&](Draw x,Pass,Shader,Shader) {x.attributes[1].offset=4;});
  expect_miss("attribute type",[&](Draw x,Pass,Shader,Shader) {x.attributes[1].type=0x2c23a5;});
  expect_miss("attribute usage",[&](Draw x,Pass,Shader,Shader) {x.attributes[1].usage=3;});
  expect_miss("attribute index",[&](Draw x,Pass,Shader,Shader) {x.attributes[1].index=2;});
  expect_miss("attribute count",[&](Draw x,Pass,Shader,Shader) {x.attributes.pop_back();});
  expect_miss("attribute added",[&](Draw x,Pass,Shader,Shader) {x.attributes.push_back({1,0,0,0,0});});
  expect_miss("stream id",[&](Draw x,Pass,Shader,Shader) {x.streams[1].stream=2;});
  expect_miss("stream stride",[&](Draw x,Pass,Shader,Shader) {x.streams[0].stride=20;});
  expect_miss("stream count",[&](Draw x,Pass,Shader,Shader) {x.streams.pop_back();});
  expect_miss("stream order",[&](Draw x,Pass,Shader,Shader) {std::swap(x.streams[0],x.streams[1]);});
  expect_miss("color count",[&](Draw,Pass p,Shader,Shader) {p.color_count=2;});
  expect_miss("color format",[&](Draw,Pass p,Shader,Shader) {p.formats[0]=VK_FORMAT_R16G16B16A16_SFLOAT;});
  expect_miss("unused color slot format",[&](Draw,Pass p,Shader,Shader) {p.formats[3]=VK_FORMAT_R32_SFLOAT;});
  expect_miss("depth format",[&](Draw,Pass p,Shader,Shader) {p.depth_format=VK_FORMAT_UNDEFINED;});
  SR_CHECK_EQ(misses,0);
  // Fields PlanGamePipeline does not read must not cause a miss (the memo would never hit otherwise).
  auto other=d;other.count=99;other.first=7;other.command_serial=12345;other.registers[0x105]=1;other.base_vertex=-3;SR_CHECK(memo.Matches(other,pass,vs,ps));
  // A new Store replaces the memo; Clear forgets it.
  auto next=std::make_shared<GamePipeline>();auto changed=d;changed.registers[0x201]^=6;memo.Store(changed,pass,vs,ps,next);
  SR_CHECK(!memo.Matches(d,pass,vs,ps));SR_CHECK(memo.Matches(changed,pass,vs,ps));SR_CHECK(memo.pipeline()==next);
  memo.Clear();SR_CHECK(!memo.Matches(changed,pass,vs,ps));SR_CHECK(!memo.pipeline());
}
