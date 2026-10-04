#pragma once
#include "game_pipeline.h"
namespace superman_returns::graphics::vulkan {
struct UIVertex {float x,y,u,v;uint32_t color;};
struct UIDraw {
  bool lines=false,repeated=false,linear=true;
  uint32_t count=0,first=0;int32_t base_vertex=0;
  guest::ResourceId texture=0;VkRect2D scissor{};
};
class ImmediateRenderer {
public:
  ImmediateRenderer(Context& c):c_(c),descriptors_(c),pipelines_(c) {}
  bool Initialize(Error&);
  bool SetBatch(ResourceStore&,std::span<const UIVertex>,std::span<const uint16_t>,Error&);
  bool Draw(VkCommandBuffer,const TargetPass&,ResourceStore&,float width,float height,const UIDraw&,Error&);
  void Retire(uint64_t serial) {descriptors_.Retire(serial);pipelines_.Retire(serial);}
private:
  Context& c_;DescriptorStore descriptors_;GamePipelineStore pipelines_;shaders::CompiledShader vertex_,pixel_;
  std::shared_ptr<BufferResource> vertices_,indices_;uint32_t vertex_count_=0,index_count_=0;uint64_t batch_=0;
  std::vector<uint16_t> cpu_indices_;
};
}
