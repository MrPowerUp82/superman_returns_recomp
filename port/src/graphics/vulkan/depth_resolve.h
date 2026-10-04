#pragma once
#include "resolve.h"
namespace superman_returns::graphics::vulkan {
struct AliasOptions {bool hdr_from_ldr_black=true;float clear_alpha=0;};
class DepthResolver {
public:
  explicit DepthResolver(Context& c):c_(c) {}
  ~DepthResolver();
  bool Initialize(Error&);
  bool Record(VkCommandBuffer,const ResolvePlan&,guest::ResourceId raw_destination,TargetStore&,ResourceStore&,ImageState&,Error&);
  bool RecordAlias(VkCommandBuffer,TargetId,const AliasPlan&,TargetStore&,ResourceStore&,ImageState&,Error&,AliasOptions={});
  void Retire(uint64_t serial) {pending_.Retire(serial);}
private:
  Context& c_;VkDescriptorSetLayout descriptors_=VK_NULL_HANDLE;VkPipelineLayout layout_=VK_NULL_HANDLE;VkPipeline pipeline_=VK_NULL_HANDLE,alias_pipeline_=VK_NULL_HANDLE;SubmissionResources pending_;
};
}
