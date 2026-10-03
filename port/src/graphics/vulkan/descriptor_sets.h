#pragma once
#include "descriptors.h"
#include "resources.h"
namespace superman_returns::graphics::vulkan {
struct DescriptorPage {
  Context* context=nullptr;
  VkDescriptorPool pool=VK_NULL_HANDLE;
  uint32_t draws=0;
  ~DescriptorPage();
};
struct SamplerResource {
  Context* context=nullptr;
  VkSampler handle=VK_NULL_HANDLE;
  ~SamplerResource();
};
struct DescriptorDraw {
  std::array<VkDescriptorSet,4> sets{};
  std::shared_ptr<DescriptorPage> page;
  std::vector<std::shared_ptr<void>> resources;
};
class DescriptorStore {
public:
  explicit DescriptorStore(Context& context):c_(context) {}
  ~DescriptorStore();
  bool Initialize(Error&);
  std::shared_ptr<DescriptorDraw> Prepare(const DrawBindings&,
      const std::array<std::array<uint32_t,6>,32>& fetch,
      ResourceStore&,uint64_t submission,Error&);
  void Retire(uint64_t completed_serial);
  const std::array<VkDescriptorSetLayout,4>& Layouts() const {return layouts_;}
private:
  std::shared_ptr<SamplerResource> Sampler(std::span<const uint32_t,6>,Error&);
  Context& c_;
  std::array<VkDescriptorSetLayout,4> layouts_{};
  std::map<std::array<uint32_t,2>,std::shared_ptr<SamplerResource>> samplers_;
  std::map<uint64_t,std::vector<std::shared_ptr<DescriptorPage>>> pages_;
  SubmissionResources pending_;
  uint64_t constant_serial_=0,completed_=0;
};
} // namespace superman_returns::graphics::vulkan
