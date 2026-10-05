#include "composition.h"
#include "composition_vs.h"
#include "composition_ps.h"
#include <algorithm>
#include <cstring>
namespace superman_returns::graphics::vulkan {
VkRect2D PlanPresentRect(VkExtent2D surface,uint32_t x,uint32_t y) {
  if(!surface.width || !surface.height || !x || !y) return {};
  VkExtent2D fit=surface;
  if(uint64_t(surface.width)*y>uint64_t(surface.height)*x) fit.width=uint32_t(uint64_t(surface.height)*x/y);
  else fit.height=uint32_t(uint64_t(surface.width)*y/x);
  return {{int32_t((surface.width-fit.width)/2),int32_t((surface.height-fit.height)/2)},fit};
}
bool FrontbufferCompositor::Initialize(Error& e) {
  vertex_.words.assign(std::begin(composition_vs),std::end(composition_vs));vertex_.outputs={{0,"float2"}};
  pixel_.stage=shaders::ShaderStage::kPixel;pixel_.words.assign(std::begin(composition_ps),std::end(composition_ps));pixel_.inputs={{0,"float2"}};
  pixel_.requirements.sampled_images=32;pixel_.requirements.samplers=32;pixel_.requirements.storage_buffers=1;pixel_.requirements.descriptor_sets=3;pixel_.requirements.fragment_input_components=2;vertex_.requirements.vertex_output_components=2;
  return descriptors_.Initialize(e) && pipelines_.Initialize(descriptors_.Layouts(),{},e);
}
std::shared_ptr<CompositionDraw> FrontbufferCompositor::Prepare(VkCommandBuffer command,const TargetPass& pass,std::shared_ptr<TextureResource> source,ResourceStore& resources,ImageState& state,std::span<const uint32_t,256> gamma,bool gamma_enabled,uint32_t aspect_x,uint32_t aspect_y,Error& e) {
  if(!command || !source || !source->state || source->view_type!=VK_IMAGE_VIEW_TYPE_2D || pass.color_count!=1 || pass.depth_format!=VK_FORMAT_UNDEFINED || !resources.CurrentSerial()) {e={"Frontbuffer composition",VK_ERROR_INITIALIZATION_FAILED,"Invalid image, submission or target"};return {};}
  auto result=std::make_shared<CompositionDraw>();result->rectangle=PlanPresentRect(pass.extent,aspect_x,aspect_y);
  if(!result->rectangle.extent.width || !result->rectangle.extent.height) {e={"Frontbuffer composition",VK_ERROR_INITIALIZATION_FAILED,"Invalid display aspect"};return {};}
  if(!state.Transition(command,source->handle,{VK_IMAGE_ASPECT_COLOR_BIT,0,source->mips,0,source->layers},ImageUsage::Sampled(),e)) return {};
  constexpr guest::ResourceId source_id=UINT64_MAX-8;
  if(!resources.BindTextureView(source_id,source,{VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A},e)) return {};
  guest::DrawPacket draw;draw.command_serial=++draw_serial_;draw.registers[0x104]=15;draw.registers[0x201]=1|(1<<16);
  auto bindings=BuildBindings(draw,e);if(!e.message.empty()) return {};
  bindings.textures[0][0]=source_id;bindings.texture_indices[0]=0;
  std::memcpy(bindings.shared_constants.data(),gamma.data(),gamma.size_bytes());
  uint32_t options[]{uint32_t(gamma_enabled),uint32_t(pass.formats[0]==VK_FORMAT_R8G8B8A8_SRGB || pass.formats[0]==VK_FORMAT_B8G8R8A8_SRGB)};
  std::memcpy(bindings.shared_constants.data()+1024,options,sizeof(options));
  // Linear clamp to edge prevents filtering beyond the frontbuffer at borders.
  draw.texture_fetch[0][0]=(2u<<10)|(2u<<13)|(2u<<16);draw.texture_fetch[0][3]=(1u<<19)|(1u<<21);
  result->descriptors=descriptors_.Prepare(bindings,draw.texture_fetch,resources,resources.CurrentSerial(),e);if(!result->descriptors) return {};
  result->pipeline=pipelines_.Acquire(draw,pass,vertex_,&pixel_,resources.CurrentSerial(),e);if(!result->pipeline) return {};
  e={};return result;
}
void FrontbufferCompositor::Record(VkCommandBuffer command,const CompositionDraw& draw) {
  c_.f.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,draw.pipeline->handle);
  c_.f.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines_.Layout(),0,4,draw.descriptors->sets.data(),uint32_t(draw.descriptors->dynamic_offsets.size()),draw.descriptors->dynamic_offsets.data());
  auto& box=draw.rectangle;VkViewport viewport{float(box.offset.x),float(box.offset.y),float(box.extent.width),float(box.extent.height),0,1};
  c_.f.vkCmdSetViewport(command,0,1,&viewport);c_.f.vkCmdSetScissor(command,0,1,&box);float blend[4]{};c_.f.vkCmdSetBlendConstants(command,blend);c_.f.vkCmdSetStencilReference(command,VK_STENCIL_FACE_FRONT_AND_BACK,0);c_.f.vkCmdDraw(command,3,1,0,0);
}
}
