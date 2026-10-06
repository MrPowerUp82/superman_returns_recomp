#pragma once
#include "render_targets.h"
#include "descriptor_sets.h"
#include "../shaders/vulkan_shader_service.h"
namespace superman_returns::graphics::vulkan {
using GamePipelineKey=std::vector<uint64_t>;
struct GamePipelinePlan {
  GamePipelineKey key;
  VkPrimitiveTopology topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  bool restart=false;
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  std::array<VkPipelineColorBlendAttachmentState,4> colors{};
  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
};
bool PlanGamePipeline(const guest::DrawPacket&,const TargetPass&,const shaders::CompiledShader&,GamePipelinePlan&,Error&);
std::string ShaderEntryPoint(const shaders::CompiledShader&,Error&);
bool ValidateGamePipelineFeatures(const GamePipelinePlan&,uint32_t color_count,const VkPhysicalDeviceLimits&,const VkPhysicalDeviceFeatures&,Error&);
struct GamePipeline {
  Context* context=nullptr;VkPipeline handle=VK_NULL_HANDLE;
  std::vector<VkVertexInputBindingDescription> bindings;
  uint64_t held_serial=0;  // submission whose list already holds this pipeline (SubmissionResources::Hold)
  ~GamePipeline();
};
// Last pipeline handed out and the raw inputs it was acquired with. A draw whose inputs are all identical is the same
// pipeline, so the plan, the cache key and the map lookup are skipped. Every input PlanGamePipeline reads, plus the
// shader artifacts (which add their SPIR-V digest to the key), is compared; when in doubt an input belongs here.
class GamePipelineMemo {
public:
  using Shader=std::shared_ptr<const shaders::CompiledShader>;
  bool Matches(const guest::DrawPacket&,const TargetPass&,const Shader& vs,const Shader& ps) const;
  void Store(const guest::DrawPacket&,const TargetPass&,const Shader& vs,const Shader& ps,std::shared_ptr<GamePipeline>);
  void Clear() {pipeline_.reset();in_=Inputs{};}
  const std::shared_ptr<GamePipeline>& pipeline() const {return pipeline_;}
private:
  struct Inputs {
    Shader vs,ps;  // kept alive so the addresses compared stay unique
    guest::Primitive primitive=guest::Primitive::kTriangles;bool indexed=false,restart=false,inline_vertices=false;
    uint32_t r200=0,r201=0,r205=0,r104=0,r10d=0,inline_stride=0;int32_t depth_bias=0;uint32_t slope_bits=0;
    uint32_t color_count=0;std::array<VkFormat,4> formats{};VkFormat depth_format=VK_FORMAT_UNDEFINED;
    std::vector<guest::VertexAttribute> attributes;std::vector<std::pair<uint32_t,uint32_t>> streams;  // (stream, stride)
  };
  Inputs in_;std::shared_ptr<GamePipeline> pipeline_;
};
class GamePipelineStore {
public:
  explicit GamePipelineStore(Context& c):c_(c) {}
  ~GamePipelineStore();
  bool Initialize(std::span<const VkDescriptorSetLayout>,const std::filesystem::path& cache,Error&);
  std::shared_ptr<GamePipeline> Acquire(const guest::DrawPacket&,const TargetPass&,
    const shaders::CompiledShader&,const shaders::CompiledShader*,uint64_t submission,Error&);
  // Same result; retains the artifacts and caches their SPIR-V digest and
  // pair validation instead of rehashing every draw.
  std::shared_ptr<GamePipeline> Acquire(const guest::DrawPacket&,const TargetPass&,
    const std::shared_ptr<const shaders::CompiledShader>&,const std::shared_ptr<const shaders::CompiledShader>&,uint64_t submission,Error&);
  VkPipelineLayout Layout() const {return layout_;}
  // Call on the recording thread, serialized with Acquire on the same store.
  bool CheckpointCache(Error&);
  void Retire(uint64_t serial) {pending_.Retire(serial);}
  struct Stats {uint64_t plans=0,memo_hits=0,created=0;};  // PlanGamePipeline runs, draws answered by the memo, pipelines created (all cumulative)
  const Stats& stats() const {return stats_;}
private:
  bool Validate(const shaders::CompiledShader&,const shaders::CompiledShader*,Error&) const;
  std::shared_ptr<GamePipeline> AcquireValidated(const guest::DrawPacket&,const TargetPass&,const shaders::CompiledShader&,const shaders::CompiledShader*,uint64_t vs_digest,uint64_t ps_digest,uint64_t submission,Error&);
  struct ShaderPair {std::shared_ptr<const shaders::CompiledShader> vs,ps;uint64_t vs_digest=0,ps_digest=0;};
  std::map<std::pair<const shaders::CompiledShader*,const shaders::CompiledShader*>,ShaderPair> validated_;
  Context& c_;VkPipelineLayout layout_=VK_NULL_HANDLE;VkPipelineCache driver_cache_=VK_NULL_HANDLE;
  std::filesystem::path cache_path_;std::map<GamePipelineKey,std::shared_ptr<GamePipeline>> pipelines_;SubmissionResources pending_;
  bool cache_dirty_=false;
  GamePipelineMemo memo_;Stats stats_;
};
}
