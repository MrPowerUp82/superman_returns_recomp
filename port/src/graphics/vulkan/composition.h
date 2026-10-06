#pragma once
#include "game_pipeline.h"
namespace superman_returns::graphics::vulkan {
VkRect2D PlanPresentRect(VkExtent2D,uint32_t aspect_x,uint32_t aspect_y);
struct CompositionDraw {
  DescriptorDraw descriptors;std::shared_ptr<GamePipeline> pipeline;
  VkRect2D rectangle{};
};
class FrontbufferCompositor {
public:
  explicit FrontbufferCompositor(Context& c):c_(c),descriptors_(c),pipelines_(c) {}
  bool Initialize(Error&);
  std::shared_ptr<CompositionDraw> Prepare(VkCommandBuffer,const TargetPass&,std::shared_ptr<TextureResource>,ResourceStore&,ImageState&,std::span<const uint32_t,256> gamma,bool gamma_enabled,uint32_t aspect_x,uint32_t aspect_y,Error&);
  void Record(VkCommandBuffer,const CompositionDraw&);
  void Retire(uint64_t serial) {descriptors_.Retire(serial);pipelines_.Retire(serial);}
private:
  Context& c_;DescriptorStore descriptors_;GamePipelineStore pipelines_;shaders::CompiledShader vertex_,pixel_;uint64_t draw_serial_=0;
};
}
