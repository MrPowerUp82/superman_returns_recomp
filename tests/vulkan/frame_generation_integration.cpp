#include "frame_generation.h"
#include "image_state.h"
#include "platform/win32_surface.h"
#include "lsfg-vk-common/vulkan/command_buffer.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
void Require(bool ok,const Error& e) {if(!ok) throw std::runtime_error(e.operation+": "+e.message);}
void Require(VkResult r,const char* what) {if(r!=VK_SUCCESS) throw std::runtime_error(std::string(what)+": "+std::to_string(r));}
int main(int argc,char** argv) {
  try {
    if(argc!=2) throw std::runtime_error("Pass local Lossless.dll path");
    Win32Window window;Context c;Error e;
    const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    Require(c.CreateInstance(extensions,true,e),e);Require(window.Open(256,144,e,false),e);
    if(!c.validation_active) throw std::runtime_error("Validation layer required; set VK_LAYER_PATH");
    auto surface=window.CreateSurface(c,e);Require(bool(surface),e);
    VkPhysicalDeviceFeatures features{};features.shaderSampledImageArrayDynamicIndexing=features.shaderStorageBufferArrayDynamicIndexing=features.independentBlend=features.shaderClipDistance=features.shaderCullDistance=features.robustBufferAccess=VK_TRUE;
    Require(c.OpenDevice(surface,"",e,&features,true,true),e);
    {
    ImageState state(c.f);ResourceStore resources(c);
    FrameGenerationConfig cfg{true,std::filesystem::path(argv[1]),true};
    FrameGenerationSession session(c,cfg);Require(session.Initialize(e),e);
    if(!session.active()) throw std::runtime_error("Frame generation did not activate");
    std::cout<<"frame generation initialized on borrowed device\n";
    FrameGenerationSession off(c,{});Require(off.Initialize(e),e);
    if(off.active()) throw std::runtime_error("Disabled frame generation became active");
    FrameGenerationConfig bad{true,"missing-Lossless.dll",true};
    FrameGenerationSession missing(c,bad);
    if(missing.Initialize(e) || e.message.empty()) throw std::runtime_error("Missing DLL did not fail clearly");
    VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=c.graphics_family;
    Require(c.f.vkCreateCommandPool(c.device,&pi,nullptr,&pool),"pool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    Require(c.f.vkAllocateCommandBuffers(c.device,&ai,&command),"command");
    {
      auto fi=vk::initVulkanInstanceFuncs(c.instance,c.Proc(),true);
      auto df=vk::initVulkanDeviceFuncs(fi,c.device,true);
      vk::Vulkan borrowed(c.instance,c.device,c.physical,c.graphics_family,c.graphics_queue,false,fi,df);
      vk::CommandBuffer view(command);unsigned guards=0;
      auto Reject=[&](auto call){try {call();}catch(const std::logic_error&){++guards;}};
      Reject([&]{view.begin(borrowed);});Reject([&]{view.end(borrowed);});Reject([&]{view.submit(borrowed);});
      Reject([&]{view.submit(borrowed,{},VK_NULL_HANDLE,0,{},VK_NULL_HANDLE,0);});
      if(guards!=4)throw std::runtime_error("Borrowed command buffer allowed ownership operations");
    }
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};Require(c.f.vkCreateFence(c.device,&fi,nullptr,&fence),"fence");
    struct Cleanup {Context& c;VkCommandPool pool;VkFence fence;~Cleanup(){c.f.vkDeviceWaitIdle(c.device);c.f.vkDestroyFence(c.device,fence,nullptr);c.f.vkDestroyCommandPool(c.device,pool,nullptr);}} cleanup{c,pool,fence};
    std::vector<uint8_t> previous;std::shared_ptr<TextureResource> held_generated;
    for(uint64_t frame=1;frame<=8;++frame) {
      if(frame==4) session.ResetAfterRetire();
      Require(c.f.vkResetCommandPool(c.device,pool,0),"reset pool");
      VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Require(c.f.vkBeginCommandBuffer(command,&bi),"begin");
      Require(resources.BeginSubmission(command,frame,e),e);
      VkExtent2D extent=frame>=4?VkExtent2D{128,128}:VkExtent2D{256,144};
      auto source=resources.ResolveTexture(77,VK_FORMAT_R8G8B8A8_UNORM,{extent.width,extent.height,1},1,1,false,state,e);Require(bool(source),e);
      VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      Require(state.Transition(command,source->handle,range,ImageUsage::TransferDestination(),e),e);
      guest::LinearTexture pattern;pattern.width=extent.width;pattern.height=extent.height;pattern.format=guest::LinearFormat::kRGBA8Unorm;pattern.levels={{extent.width,extent.height,extent.width*4,extent.height,0}};
      pattern.data.resize(size_t(extent.width)*extent.height*4);
      for(uint32_t y=0;y<extent.height;++y)for(uint32_t x=0;x<extent.width;++x) {
        const size_t p=(size_t(y)*extent.width+x)*4;
        const bool box=x>=uint32_t(frame*12) && x<uint32_t(frame*12+48) && y>=32 && y<96;
        pattern.data[p]=box?240:20;pattern.data[p+1]=box?100:40;pattern.data[p+2]=box?30:80;pattern.data[p+3]=255;
      }
      Require(resources.UploadTexture(78,pattern,frame,e),e);auto uploaded=resources.Texture(78,e);Require(bool(uploaded),e);
      VkImageMemoryBarrier upload_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};upload_barrier.image=uploaded->handle;upload_barrier.subresourceRange=range;upload_barrier.srcQueueFamilyIndex=upload_barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
      upload_barrier.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;upload_barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;upload_barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;upload_barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
      c.f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&upload_barrier);
      VkImageCopy image_copy{};image_copy.srcSubresource=image_copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};image_copy.extent={extent.width,extent.height,1};
      c.f.vkCmdCopyImage(command,uploaded->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,source->handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&image_copy);
      Require(state.Transition(command,source->handle,range,ImageUsage::Sampled(),e),e);
      FrameGenerationInput input{};input.source_serial=frame;input.image=source;input.extent=extent;input.aspect_x=extent.width;input.aspect_y=extent.height;
      input.gamma_enabled=true;
      input.force_warmup=frame==6; // odd phase: pause warmup must retain parity
      for(uint32_t i=0;i<256;++i) {const auto value=((255-i)*1023+127)/255;input.gamma[i]=(value<<20)|(value<<10)|value;}
      auto canonical_expected=pattern.data;
      for(size_t p=0;p<canonical_expected.size();++p)if(p%4!=3)canonical_expected[p]=255-canonical_expected[p];
      Require(session.Prepare(input,e),e);
      FrameGenerationOutput output;Require(session.Record(command,frame,input,state,output,e),e);
      if(output.original!=source || bool(output.generated)!=(frame==2 || frame==3 || frame==5 || frame>=7)) throw std::runtime_error("Original/warmup contract failed");
      auto ReadImage=[&](const std::shared_ptr<TextureResource>& image) {
        auto buffer=resources.ReadbackBuffer(uint64_t(extent.width)*extent.height*4,e);Require(bool(buffer),e);
        Require(image->state->Transition(command,image->handle,range,ImageUsage::TransferSource(),e),e);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={extent.width,extent.height,1};
        c.f.vkCmdCopyImageToBuffer(command,image->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer->handle,1,&copy);
        Require(image->state->Transition(command,image->handle,range,ImageUsage::Sampled(),e),e);
        return buffer;
      };
      auto original_readback=ReadImage(output.original);auto generated_readback=output.generated?ReadImage(output.generated):nullptr;
      Require(c.f.vkEndCommandBuffer(command),"end");Require(c.f.vkResetFences(c.device,1,&fence),"reset fence");
      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
      Require(c.f.vkQueueSubmit(c.graphics_queue,1,&submit,fence),"submit");Require(c.f.vkWaitForFences(c.device,1,&fence,VK_TRUE,30000000000ull),"wait");
      std::vector<uint8_t> original_pixels;Require(resources.Readback(original_readback,original_pixels,e),e);
      if(original_pixels!=pattern.data)throw std::runtime_error("FG changed original pixels");
      if(generated_readback) {
        std::vector<uint8_t> pixels;Require(resources.Readback(generated_readback,pixels,e),e);
        std::cout<<"background "<<frame<<": "<<int(pixels[0])<<','<<int(pixels[1])<<','<<int(pixels[2])<<" expected "<<int(canonical_expected[0])<<','<<int(canonical_expected[1])<<','<<int(canonical_expected[2])<<'\n';
        if(pixels==canonical_expected || pixels==previous)throw std::runtime_error("Generated frame repeated an input");
        // Multiple FP32 filter passes can round an 8-bit static color by a few
        // levels; missing/double inversion differs by over 90 levels here.
        for(size_t p=0;p<3;++p)if(std::abs(int(pixels[p])-int(canonical_expected[p]))>3)throw std::runtime_error("Canonical gamma was missing or applied twice");
        const auto changed=std::count_if(pixels.begin(),pixels.end(),[&,p=size_t{0}](uint8_t value)mutable{return value!=canonical_expected[p++];});
        std::cout<<"intermediate distinct: changed_channels="<<changed<<'\n';held_generated=output.generated;
      }
      previous=canonical_expected;
      session.Retire(frame);resources.Retire(frame);
    }
    const auto stats=session.GpuStats();
    if(stats.samples!=5 || stats.warmups!=3 || !stats.total_ns)throw std::runtime_error("GPU timestamp accounting failed");
    std::cout<<"FG GPU average_ms="<<double(stats.total_ns)/double(stats.samples)/1000000.0<<'\n';
    }
    std::cout<<"borrowed-device generation and resize passed; validation_errors="<<c.validation_errors.load()<<'\n';
    return c.validation_errors.load()?1:0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
