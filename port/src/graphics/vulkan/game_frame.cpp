#include "game_frame.h"
#include <chrono>
#include <thread>
namespace superman_returns::graphics::vulkan {
GameFrame::GameFrame(Context& c,std::mutex& mutex,ShaderLookup shaders,TextureDecoder decoder):c_(c),queue_mutex_(mutex),shaders_(std::move(shaders)),renderer_(c,shaders_,std::move(decoder)) {}
GameFrame::~GameFrame() {
  std::lock_guard lock(queue_mutex_);
  if(submitted_) {c_.f.vkDeviceWaitIdle(c_.device);renderer_.Retire(serial_);}
  if(fence_) c_.f.vkDestroyFence(c_.device,fence_,nullptr);
  if(pool_) c_.f.vkDestroyCommandPool(c_.device,pool_,nullptr);
}
bool GameFrame::Initialize(const std::filesystem::path& cache,Error& e) {
  if(!renderer_.Initialize(cache,e)) return false;
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=c_.graphics_family;
  if(!Check(c_.f.vkCreateCommandPool(c_.device,&pool,nullptr,&pool_),"Game command pool",e)) return false;
  VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};command.commandPool=pool_;command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command.commandBufferCount=1;
  VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
  return Check(c_.f.vkAllocateCommandBuffers(c_.device,&command,&command_),"Game command buffer",e) && Check(c_.f.vkCreateFence(c_.device,&fence,nullptr,&fence_),"Game fence",e);
}
bool GameFrame::WaitFence(Error& e) {
  if(!submitted_) return true;
  // The queue lock isn't held across waits, so UI paints can be submitted.
  for(;;) {
    auto result=c_.f.vkWaitForFences(c_.device,1,&fence_,VK_TRUE,25000000);
    if(result==VK_SUCCESS) {std::lock_guard lock(queue_mutex_);renderer_.Retire(serial_);submitted_=false;return true;}
    if(result!=VK_TIMEOUT) return Check(result,"Game frame completion",e);
    if(cancelled_) {e={"Game frame",VK_ERROR_INITIALIZATION_FAILED,"Rendering cancelled"};return false;}
  }
}
bool GameFrame::WaitShaders(Error& e) {
  std::vector<std::shared_ptr<const guest::ShaderCapture>> captures;
  for(auto& packet:packets_) if(auto* draw=std::get_if<guest::DrawPacket>(&packet);draw && draw->count) {
    if(!draw->vertex_shader) {e={"Game shader",VK_ERROR_INITIALIZATION_FAILED,"Draw has no captured vertex shader"};return false;}
    for(auto& shader:{draw->vertex_shader,draw->pixel_shader}) if(shader && std::find(captures.begin(),captures.end(),shader)==captures.end()) captures.push_back(shader);
  }
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(10);
  auto next_log=std::chrono::steady_clock::now();
  for(;;) {
    bool ready=true;
    for(auto& capture:captures) {auto result=shaders_(*capture);if(result.status==shaders::ShaderPoll::failed) {e={"Game shader",VK_ERROR_INITIALIZATION_FAILED,result.diagnostic};return false;}ready&=result.status==shaders::ShaderPoll::ready;}
    if(ready) return true;
    if(compilation_progress) compilation_progress();
    if(std::chrono::steady_clock::now()>=next_log) {c_.Log("Preparing native Vulkan frame: waiting for "+std::to_string(captures.size())+" owned shader containers");next_log=std::chrono::steady_clock::now()+std::chrono::seconds(5);}
    if(cancelled_ || std::chrono::steady_clock::now()>=deadline) {e={"Game shader",VK_TIMEOUT,cancelled_?"Rendering cancelled":"Frame shader compilation timed out"};return false;}
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
bool GameFrame::Enqueue(guest::RenderPacket&& packet,std::shared_ptr<TextureResource>& output,Error& e) {
  output.reset();e={};
  if(failed_ || cancelled_) {e={"Game frame",VK_ERROR_INITIALIZATION_FAILED,"Renderer stopped"};return false;}
  if(!std::holds_alternative<guest::SwapPacket>(packet)) {
    if(packets_.size()>=262144) {failed_=true;e={"Game frame",VK_ERROR_OUT_OF_HOST_MEMORY,"Frame packet limit exceeded"};return false;}
    packets_.push_back(std::move(packet));return true;
  }
  if(!WaitShaders(e) || !WaitFence(e)) {failed_=true;return false;}
  std::lock_guard lock(queue_mutex_);
  auto fail=[&] {failed_=true;return false;};
  if(!Check(c_.f.vkResetCommandPool(c_.device,pool_,0),"Reset game pool",e)) return fail();
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if(!Check(c_.f.vkBeginCommandBuffer(command_,&begin),"Begin game frame",e) || !renderer_.BeginSubmission(command_,++serial_,e)) return fail();
  for(size_t i=0;i<packets_.size();++i) if(!renderer_.Record(packets_[i],command_,e)) {e.message+=" (frame packet "+std::to_string(i)+", kind="+std::to_string(packets_[i].index())+")";return fail();}
  auto source=renderer_.SelectFrontbuffer(std::get<guest::SwapPacket>(packet),e);if(!source) return fail();
  // A mailbox owns a snapshot, never an image that the next guest frame mutates.
  constexpr guest::ResourceId snapshot_id=UINT64_MAX-16;
  auto& resources=renderer_.Resources();resources.ForgetTexture(snapshot_id);
  output=resources.ResolveTexture(snapshot_id,source->format,source->extent,1,1,false,renderer_.Images(),e);if(!output) return fail();
  VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  if(!renderer_.Images().Transition(command_,source->handle,range,ImageUsage::TransferSource(),e) || !renderer_.Images().Transition(command_,output->handle,range,ImageUsage::TransferDestination(),e)) return fail();
  VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent=source->extent;
  c_.f.vkCmdCopyImage(command_,source->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output->handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  if(!renderer_.Images().Transition(command_,source->handle,range,ImageUsage::Sampled(),e) || !renderer_.Images().Transition(command_,output->handle,range,ImageUsage::Sampled(),e)) return fail();
  if(!Check(c_.f.vkEndCommandBuffer(command_),"End game frame",e) || !Check(c_.f.vkResetFences(c_.device,1,&fence_),"Reset game fence",e)) return fail();
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command_;
  if(!Check(c_.f.vkQueueSubmit(c_.graphics_queue,1,&submit,fence_),"Submit game frame",e)) return fail();
  submitted_=true;packets_.clear();return true;
}
}
