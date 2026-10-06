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
