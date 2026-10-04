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
  ~GamePipeline();
};
class GamePipelineStore {
public:
  explicit GamePipelineStore(Context& c):c_(c) {}
  ~GamePipelineStore();
  bool Initialize(std::span<const VkDescriptorSetLayout>,const std::filesystem::path& cache,Error&);
  std::shared_ptr<GamePipeline> Acquire(const guest::DrawPacket&,const TargetPass&,
    const shaders::CompiledShader&,const shaders::CompiledShader*,uint64_t submission,Error&);
  VkPipelineLayout Layout() const {return layout_;}
  // Call on the recording thread, serialized with Acquire on the same store.
  bool CheckpointCache(Error&);
  void Retire(uint64_t serial) {pending_.Retire(serial);}
private:
  Context& c_;VkPipelineLayout layout_=VK_NULL_HANDLE;VkPipelineCache driver_cache_=VK_NULL_HANDLE;
  std::filesystem::path cache_path_;std::map<GamePipelineKey,std::shared_ptr<GamePipeline>> pipelines_;SubmissionResources pending_;
  bool cache_dirty_=false;
};
}
