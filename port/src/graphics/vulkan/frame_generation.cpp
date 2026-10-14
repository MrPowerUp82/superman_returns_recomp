// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_generation.h"
#include "composition.h"
#include "image_state.h"
#include "lsfg-vk-backend/lsfgvk.hpp"
#include "lsfg-vk-common/vulkan/command_buffer.hpp"
#include <algorithm>
#include <stdexcept>
namespace superman_returns::graphics::vulkan {
namespace {
constexpr VkImageSubresourceRange kColor{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
bool Fail(Error& e,const char* message,VkResult result=VK_ERROR_INITIALIZATION_FAILED) {e={"Vulkan frame generation",result,message};return false;}
VkImageMemoryBarrier Barrier(VkImage image,VkImageLayout before,VkImageLayout after,VkAccessFlags from,VkAccessFlags to) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=before;b.newLayout=after;
  b.srcAccessMask=from;b.dstAccessMask=to;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange=kColor;return b;
}
void ImageBarrier(Context& c,VkCommandBuffer command,const VkImageMemoryBarrier& barrier) {
  c.f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
}
void Copy(Context& c,VkCommandBuffer command,VkImage source,VkImage dest,VkExtent2D size) {
  VkImageCopy region{};region.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.dstSubresource=region.srcSubresource;region.extent={size.width,size.height,1};
  c.f.vkCmdCopyImage(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dest,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
}
}
struct FrameGenerationSession::Impl {
  Context& c;FrameGenerationConfig config;
  std::shared_ptr<ImageState> state;
  ResourceStore resources;FrontbufferCompositor compositor;
  std::unique_ptr<lsfgvk::backend::Instance> backend;
  lsfgvk::backend::Context* context=nullptr;
  VkExtent2D extent{};VkFormat input_format=VK_FORMAT_UNDEFINED;
  VkRenderPass render_pass=VK_NULL_HANDLE;VkQueryPool queries=VK_NULL_HANDLE;
  uint64_t timestamp_mask=~0ull,phase=0,last_source=0;bool history=false,dummies=false;
  std::array<bool,2> source_initialized{};
  struct Target {
    Context* c;std::shared_ptr<TextureResource> image;VkFramebuffer framebuffer=VK_NULL_HANDLE;uint64_t held_serial=0;
    ~Target(){if(framebuffer)c->f.vkDestroyFramebuffer(c->device,framebuffer,nullptr);}
  };
  std::vector<std::shared_ptr<Target>> targets;
  std::vector<std::shared_ptr<TextureResource>> generated;
  SubmissionResources pending;
  struct Query {uint64_t serial=0;bool warmup=false;};std::array<Query,8> samples{};
  Impl(Context& owner,FrameGenerationConfig cfg):c(owner),config(std::move(cfg)),state(std::make_shared<ImageState>(c.f)),resources(c),compositor(c) {
    lsfgvk::backend::BorrowedDevice device{c.instance,c.physical,c.device,c.graphics_family,c.graphics_queue,c.Proc(),std::nullopt};
    backend=std::make_unique<lsfgvk::backend::Instance>(device,config.dll,false);
    Error e;if(!compositor.Initialize(e)) throw std::runtime_error(e.operation+": "+e.message);
  }
  ~Impl() {
    // Shutdown/reset only, never in the per-pair path.
    c.f.vkDeviceWaitIdle(c.device);pending.Retire(UINT64_MAX);compositor.Retire(UINT64_MAX);resources.Retire(UINT64_MAX);
    targets.clear();generated.clear();
    if(render_pass)c.f.vkDestroyRenderPass(c.device,render_pass,nullptr);
    if(queries)c.f.vkDestroyQueryPool(c.device,queries,nullptr);
  }
  bool Setup(const FrameGenerationInput& input,Error& e) {
    if(context) return true;
    extent=input.extent;input_format=input.image->format;
    // The backend needs all seven flow pyramid levels to have nonzero extents.
    if(std::min(extent.width,extent.height)<128) return Fail(e,"FG requires an extent of at least 128x128");
    const float flow=std::min(4.0f,float(std::min(extent.width,extent.height))/128.0f);
    context=&backend->openLocalContext(extent.width,extent.height,false,flow,config.performance,1);
    VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R8G8B8A8_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass{};
    subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=1;rp.pAttachments=&attachment;rp.subpassCount=1;rp.pSubpasses=&subpass;
    if(!Check(c.f.vkCreateRenderPass(c.device,&rp,nullptr,&render_pass),"FG canonical pass",e)) return false;
    uint32_t count=0;c.f.vkGetPhysicalDeviceQueueFamilyProperties(c.physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count);c.f.vkGetPhysicalDeviceQueueFamilyProperties(c.physical,&count,families.data());
    const uint32_t bits=c.graphics_family<count?families[c.graphics_family].timestampValidBits:0;
    if(bits) {
      timestamp_mask=bits>=64?~0ull:(1ull<<bits)-1;
      VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qp.queryType=VK_QUERY_TYPE_TIMESTAMP;qp.queryCount=uint32_t(samples.size()*2);
      if(!Check(c.f.vkCreateQueryPool(c.device,&qp,nullptr,&queries),"FG timestamp pool",e)) return false;
    }
    c.Log("Frame generation ready: Vulkan same-device FP32 "+std::to_string(extent.width)+"x"+std::to_string(extent.height)+", 2x");
    return true;
  }
  std::shared_ptr<TextureResource> NewImage(VkCommandBuffer,uint64_t, bool attachment,Error& e) {
    constexpr guest::ResourceId id=UINT64_MAX-40;
    resources.ForgetTexture(id);
    auto image=resources.ResolveTexture(id,VK_FORMAT_R8G8B8A8_UNORM,{extent.width,extent.height,1},1,1,false,*state,e,attachment?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:0);
    resources.ForgetTexture(id);if(image)image->state_owner=state;return image;
  }
  std::shared_ptr<Target> TargetFor(VkCommandBuffer command,uint64_t serial,Error& e) {
    for(auto& target:targets) if(target.use_count()==1 && target->image.use_count()==1) {pending.Hold(serial,target);return target;}
    if(targets.size()>=4) {Fail(e,"No retired canonical FG slot");return {};}
    auto target=std::make_shared<Target>();target->c=&c;target->image=NewImage(command,serial,true,e);if(!target->image) return {};
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=render_pass;fb.attachmentCount=1;fb.pAttachments=&target->image->view;fb.width=extent.width;fb.height=extent.height;fb.layers=1;
    if(!Check(c.f.vkCreateFramebuffer(c.device,&fb,nullptr,&target->framebuffer),"FG canonical framebuffer",e)) return {};
    targets.push_back(target);pending.Hold(serial,target);return target;
  }
  std::shared_ptr<TextureResource> GeneratedFor(VkCommandBuffer command,uint64_t serial,Error& e) {
    for(auto& image:generated) if(image.use_count()==1) {resources.Hold(image);return image;}
    if(generated.size()>=4) {Fail(e,"No retired generated FG slot");return {};}
    auto image=NewImage(command,serial,false,e);if(image)generated.push_back(image);return image;
  }
  bool Record(VkCommandBuffer command,uint64_t serial,const FrameGenerationInput& input,ImageState& source_state,FrameGenerationOutput& output,Error& e) {
    if(!input.source_serial || !input.image || input.image->state!=&source_state || !input.extent.width || !input.extent.height) return Fail(e,"Invalid source frame/state/extent");
    if(input.source_serial<last_source) return Fail(e,"Out-of-order source frame");
    output={input.image,{}};
    if(input.source_serial==last_source) return true;
    if(context && (extent.width!=input.extent.width || extent.height!=input.extent.height || input_format!=input.image->format)) return Fail(e,"Retire and reset FG before changing extent/format",VK_ERROR_OUT_OF_DATE_KHR);
    if(!Setup(input,e) || !resources.BeginSubmission(command,serial,e)) return false;
    if(!dummies) {if(!resources.CreateDummies(e))return false;dummies=true;}
    const bool warmup=!history || input.force_warmup;
    auto& query=samples[serial%samples.size()];
    if(queries) {
      if(query.serial) return Fail(e,"FG timestamp slot is still pending");
      const uint32_t first=uint32_t((serial%samples.size())*2);
      c.f.vkCmdResetQueryPool(command,queries,first,2);c.f.vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,first);
    }
    auto target=TargetFor(command,serial,e);if(!target)return false;
    TargetPass pass;pass.owns_handles=false;pass.render_pass=render_pass;pass.framebuffer=target->framebuffer;pass.extent=extent;pass.color_count=1;pass.formats[0]=VK_FORMAT_R8G8B8A8_UNORM;
    auto composition=compositor.Prepare(command,pass,input.image,resources,source_state,input.gamma,input.gamma_enabled,input.aspect_x,input.aspect_y,e);if(!composition)return false;
    if(!state->Transition(command,target->image->handle,kColor,ImageUsage::ColorAttachment(),e))return false;
    VkClearValue black{};black.color.float32[3]=1;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=render_pass;begin.framebuffer=target->framebuffer;begin.renderArea.extent=extent;begin.clearValueCount=1;begin.pClearValues=&black;
    c.f.vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);compositor.Record(command,*composition);c.f.vkCmdEndRenderPass(command);
    if(!state->Transition(command,target->image->handle,kColor,ImageUsage::TransferSource(),e)) return false;
    for(size_t i=0;i<(warmup?2u:1u);++i) {
      const size_t slot=warmup?i:phase%2;const auto source=backend->sourceImage(*context,slot);
      ImageBarrier(c,command,Barrier(source,source_initialized[slot]?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,source_initialized[slot]?(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT):0,VK_ACCESS_TRANSFER_WRITE_BIT));
      Copy(c,command,target->image->handle,source,extent);
      ImageBarrier(c,command,Barrier(source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT));source_initialized[slot]=true;
    }
    ::vk::CommandBuffer view(command);
    for(size_t i=0;i<(warmup?4u:1u);++i) {backend->recordFrame(*context,view);++phase;}
    if(!warmup) {
      auto image=GeneratedFor(command,serial,e);if(!image)return false;
      const auto source=backend->destinationImage(*context,0);
      ImageBarrier(c,command,Barrier(source,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT));
      if(!state->Transition(command,image->handle,kColor,ImageUsage::TransferDestination(),e))return false;
      Copy(c,command,source,image->handle,extent);
      ImageBarrier(c,command,Barrier(source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT));
      if(!state->Transition(command,image->handle,kColor,ImageUsage::Sampled(),e))return false;
      output.generated=image;
    }
    if(queries) {c.f.vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,uint32_t((serial%samples.size())*2+1));query={serial,warmup};}
    history=true;last_source=input.source_serial;e={};return true;
  }
  void Retire(uint64_t serial,FrameGenerationGpuStats& stats) {
    for(size_t i=0;i<samples.size();++i) if(samples[i].serial && samples[i].serial<=serial) {
      uint64_t ticks[2]{};
      if(c.f.vkGetQueryPoolResults(c.device,queries,uint32_t(i*2),2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS) {
        auto ns=uint64_t(double((ticks[1]-ticks[0])&timestamp_mask)*c.properties.limits.timestampPeriod);
        if(samples[i].warmup) {++stats.warmups;stats.warmup_ns+=ns;}else {++stats.samples;stats.total_ns+=ns;}
      }
      samples[i]={};
    }
    pending.Retire(serial);compositor.Retire(serial);resources.Retire(serial);
  }
};
FrameGenerationSession::FrameGenerationSession(Context& c,FrameGenerationConfig cfg):c_(c),config_(std::move(cfg)) {}
FrameGenerationSession::~FrameGenerationSession()=default;
bool FrameGenerationSession::Initialize(Error& e) {
  e={};if(!config_.enabled || impl_)return true;
  if(!c_.timeline_enabled)return Fail(e,"Timeline semaphores were not enabled",VK_ERROR_FEATURE_NOT_PRESENT);
  if(config_.dll.empty() || !std::filesystem::is_regular_file(config_.dll))return Fail(e,"Local Lossless.dll was not found");
  try {impl_=std::make_unique<Impl>(c_,config_);return true;}
  catch(const std::exception& ex) {e={"Vulkan FG initialization",VK_ERROR_INITIALIZATION_FAILED,ex.what()};return false;}
}
bool FrameGenerationSession::Record(VkCommandBuffer command,uint64_t serial,const FrameGenerationInput& input,ImageState& state,FrameGenerationOutput& output,Error& e) {
  if(!impl_)return Fail(e,"Frame generation is not active");
  try {return impl_->Record(command,serial,input,state,output,e);}
  catch(const std::exception& ex) {e={"Vulkan FG recording",VK_ERROR_INITIALIZATION_FAILED,ex.what()};return false;}
}
bool FrameGenerationSession::Prepare(const FrameGenerationInput& input,Error& e) {
  if(!impl_)return Fail(e,"Frame generation is not active");
  if(!input.image || !input.extent.width || !input.extent.height)return Fail(e,"Missing source frame or extent");
  if(NeedsReset(input.extent,input.image->format))return Fail(e,"Retire and reset FG before preparing a new extent/format",VK_ERROR_OUT_OF_DATE_KHR);
  try {return impl_->Setup(input,e);}
  catch(const std::exception& ex) {e={"Vulkan FG setup",VK_ERROR_INITIALIZATION_FAILED,ex.what()};return false;}
}
void FrameGenerationSession::Retire(uint64_t serial){if(impl_)impl_->Retire(serial,stats_);}
void FrameGenerationSession::ResetAfterRetire(){impl_.reset();Error e;if(!Initialize(e))c_.Log(e.operation+": "+e.message);}
bool FrameGenerationSession::NeedsReset(VkExtent2D size,VkFormat format) const {return impl_ && impl_->context && (impl_->extent.width!=size.width || impl_->extent.height!=size.height || impl_->input_format!=format);}
bool FrameGenerationSession::active() const {return bool(impl_);}
}
