#include "immediate.h"
#include "immediate_vs.h"
#include "immediate_ps.h"
#include <cstring>
namespace superman_returns::graphics::vulkan {
bool ImmediateRenderer::Initialize(Error& e) {
  vertex_.words.assign(std::begin(immediate_vs),std::end(immediate_vs));vertex_.inputs={{0,"float2"},{11,"float4"},{13,"float2"}};vertex_.outputs={{0,"float4"},{1,"float2"}};
  pixel_.stage=shaders::ShaderStage::kPixel;pixel_.words.assign(std::begin(immediate_ps),std::end(immediate_ps));pixel_.inputs=vertex_.outputs;
  vertex_.requirements.storage_buffers=1;vertex_.requirements.descriptor_sets=1;vertex_.requirements.vertex_attributes=14;vertex_.requirements.vertex_output_components=6;
  pixel_.requirements.sampled_images=32;pixel_.requirements.samplers=32;pixel_.requirements.storage_buffers=1;pixel_.requirements.descriptor_sets=3;pixel_.requirements.fragment_input_components=6;
  return descriptors_.Initialize(e) && pipelines_.Initialize(descriptors_.Layouts(),{},e);
}
bool ImmediateRenderer::SetBatch(ResourceStore& resources,std::span<const UIVertex> vertices,std::span<const uint16_t> indices,Error& e) {
  vertices_.reset();indices_.reset();vertex_count_=index_count_=0;
  if(vertices.empty() || vertices.size()>1048576 || indices.size()>4194304) {e={"UI batch",VK_ERROR_INITIALIZATION_FAILED,"Invalid UI vertex/index count"};return false;}
  constexpr guest::ResourceId vertex_id=UINT64_MAX-20,index_id=UINT64_MAX-21;
  if(!resources.UploadHostBuffer(vertex_id,std::as_bytes(vertices),++batch_,e)) return false;vertices_=resources.Buffer(vertex_id,e);resources.ForgetBuffer(vertex_id);
  if(!indices.empty()) {if(!resources.UploadHostBuffer(index_id,std::as_bytes(indices),batch_,e)) return false;indices_=resources.Buffer(index_id,e);resources.ForgetBuffer(index_id);}
  cpu_indices_.assign(indices.begin(),indices.end());vertex_count_=uint32_t(vertices.size());index_count_=uint32_t(indices.size());e={};return true;
}
bool ImmediateRenderer::Draw(VkCommandBuffer command,const TargetPass& pass,ResourceStore& resources,float width,float height,const UIDraw& input,Error& e) {
  if(!vertices_ || width<=0 || height<=0 || !input.count) {e={"UI draw",VK_ERROR_INITIALIZATION_FAILED,"UI batch/coordinate space is invalid"};return false;}
  uint64_t limit=indices_?index_count_:vertex_count_;if(uint64_t(input.first)+input.count>limit) {e={"UI draw",VK_ERROR_INITIALIZATION_FAILED,"UI draw exceeds owned batch"};return false;}
  if(indices_) for(uint32_t i=input.first;i<input.first+input.count;++i) if(int64_t(cpu_indices_[i])+input.base_vertex<0 || int64_t(cpu_indices_[i])+input.base_vertex>=vertex_count_) {e={"UI draw",VK_ERROR_INITIALIZATION_FAILED,"UI index/base vertex exceeds owned vertices"};return false;}
  guest::DrawPacket draw;draw.inline_vertices=true;draw.inline_stride=sizeof(UIVertex);draw.attributes={{0,0,0x2c23a5,0,0},{0,8,0x2c23a5,5,0},{0,16,0x1a2086,10,0}};
  draw.primitive=input.lines?guest::Primitive::kLines:guest::Primitive::kTriangles;draw.registers[0x104]=15;draw.registers[0x201]=6|(7<<8)|(1<<16)|(7<<24);
  auto bindings=BuildBindings(draw,e);if(!e.message.empty()) return false;
  bindings.vertex_buffers.fill(DummyBuffer);
  struct Options {float width,height;uint32_t texture,srgb;} options{width,height,uint32_t(input.texture!=0),uint32_t(pass.formats[0]==VK_FORMAT_R8G8B8A8_SRGB || pass.formats[0]==VK_FORMAT_B8G8R8A8_SRGB)};
  std::memcpy(bindings.constants.shared.data(),&options,sizeof(options));
  if(input.texture) {bindings.textures[0][0]=input.texture;bindings.texture_indices[0]=0;}
  uint32_t clamp=input.repeated?0:2;draw.texture_fetch[0][0]=(clamp<<10)|(clamp<<13)|(clamp<<16);draw.texture_fetch[0][3]=input.linear?((1u<<19)|(1u<<21)):0;
  auto descriptor=descriptors_.Prepare(bindings,draw.texture_fetch,resources,resources.CurrentSerial(),e);if(!descriptor) return false;
  auto pipeline=pipelines_.Acquire(draw,pass,vertex_,&pixel_,resources.CurrentSerial(),e);if(!pipeline) return false;
  c_.f.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->handle);c_.f.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines_.Layout(),0,4,descriptor->sets.data(),0,nullptr);
  VkViewport viewport{0,0,float(pass.extent.width),float(pass.extent.height),0,1};c_.f.vkCmdSetViewport(command,0,1,&viewport);c_.f.vkCmdSetScissor(command,0,1,&input.scissor);float blend[4]{};c_.f.vkCmdSetBlendConstants(command,blend);c_.f.vkCmdSetStencilReference(command,VK_STENCIL_FACE_FRONT_AND_BACK,0);
  VkDeviceSize offset=0;c_.f.vkCmdBindVertexBuffers(command,0,1,&vertices_->handle,&offset);
  if(indices_) {c_.f.vkCmdBindIndexBuffer(command,indices_->handle,0,VK_INDEX_TYPE_UINT16);c_.f.vkCmdDrawIndexed(command,input.count,1,input.first,input.base_vertex,0);}else c_.f.vkCmdDraw(command,input.count,1,input.first,0);
  e={};return true;
}
}
