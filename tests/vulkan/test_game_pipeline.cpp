#include "game_pipeline.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
namespace shaders=superman_returns::graphics::shaders;
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
