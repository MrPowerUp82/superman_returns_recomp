#pragma once
#include "../guest/render_packet.h"
#include "../guest/edram_alias.h"
#include "resources.h"
#include "image_state.h"
namespace superman_returns::graphics::vulkan {
using guest::AliasSurface;using guest::AliasAction;using guest::AliasPlan;using guest::EdramOverlaps;using guest::PlanEdramAlias;
using TargetId=uint64_t;
struct PassPlan {
  std::array<guest::SurfaceDesc,4> colors{};guest::SurfaceDesc depth{};
  uint32_t color_count=0;VkExtent2D extent{};VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
};
PassPlan PlanAttachments(const guest::DrawPacket&,Error&,VkExtent2D tiling={});
bool ValidatePassPlan(const PassPlan&,Error&);
struct TargetResource {
  Context* context=nullptr;ImageState* state=nullptr;
  TargetId id=0;guest::SurfaceDesc description{};
  VkImage image=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;
  VkImageView attachment=VK_NULL_HANDLE,sampled=VK_NULL_HANDLE;
  VkFormat format=VK_FORMAT_UNDEFINED;VkImageAspectFlags aspects=0;
  uint64_t last_write=0;bool initialized=false;
  ~TargetResource();
};
struct TargetPass {
  bool owns_handles=true;
  Context* context=nullptr;VkRenderPass render_pass=VK_NULL_HANDLE;VkFramebuffer framebuffer=VK_NULL_HANDLE;
  VkExtent2D extent{};std::array<VkFormat,4> formats{};VkFormat depth_format=VK_FORMAT_UNDEFINED;
  uint32_t color_count=0;std::vector<std::shared_ptr<TargetResource>> targets;
  ~TargetPass();
};
class TargetStore {
public:
  TargetStore(Context& c,ImageState& state):c_(c),state_(state) {}
  ~TargetStore();
  bool BeginSubmission(VkCommandBuffer,uint64_t serial,Error&);
  TargetId Acquire(const guest::SurfaceDesc&,Error&);
  std::shared_ptr<TargetResource> Get(TargetId,Error&);
  std::shared_ptr<const TargetResource> Find(const guest::SurfaceDesc&) const;
  // When `open` is the pass this plan maps to, its attachments are already
  // initialized and in attachment layout: no transition is recorded.
  std::shared_ptr<TargetPass> PreparePass(const PassPlan&,Error&,const TargetPass* open=nullptr);
  bool Clear(const guest::ClearPacket&,Error&);
  void MarkWritten(const TargetPass&);
  void MarkWritten(TargetId id) {targets_.at(id)->last_write=++write_serial_;}
  std::vector<std::pair<TargetId,AliasPlan>> Aliases(const TargetPass&) const;
  void Retire(uint64_t serial) {completed_=std::max(completed_,serial);pending_.Retire(serial);}
  // Diagnostics: every live target, in creation order.
  std::vector<std::shared_ptr<TargetResource>> All() const {std::vector<std::shared_ptr<TargetResource>> out;for(auto& [id,t]:targets_) out.push_back(t);return out;}
private:
  bool Initialize(const std::shared_ptr<TargetResource>&,Error&);
  Context& c_;ImageState& state_;VkCommandBuffer command_=VK_NULL_HANDLE;uint64_t serial_=0,completed_=0,write_serial_=0,next_id_=0;
  using Key=std::array<uint32_t,5>;
  std::map<Key,TargetId> keys_;std::map<TargetId,std::shared_ptr<TargetResource>> targets_;
  std::map<guest::ResourceId,TargetId> guest_targets_;
  std::map<std::array<TargetId,5>,std::shared_ptr<TargetPass>> passes_;
  SubmissionResources pending_;
};
} // namespace superman_returns::graphics::vulkan
