#include "game_frame.h"
#include <chrono>
#include <cstdlib>
#include <thread>
namespace superman_returns::graphics::vulkan {
GameFrame::GameFrame(Context& c,std::mutex& mutex,ShaderLookup shaders,TextureDecoder decoder):c_(c),queue_mutex_(mutex),shaders_(std::move(shaders)),renderer_(c,shaders_,std::move(decoder)) {}
GameFrame::~GameFrame() {
  std::lock_guard lock(queue_mutex_);snapshots_.clear();
  if(submitted_) {c_.f.vkDeviceWaitIdle(c_.device);renderer_.Retire(serial_);}
  if(fence_) c_.f.vkDestroyFence(c_.device,fence_,nullptr);
  if(pool_) c_.f.vkDestroyCommandPool(c_.device,pool_,nullptr);
}
bool GameFrame::Initialize(const std::filesystem::path& cache,Error& e) {
  if(!renderer_.Initialize(cache,e)) return false;
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=c_.graphics_family;
  if(!Check(c_.f.vkCreateCommandPool(c_.device,&pool,nullptr,&pool_),"Game command pool",e)) return false;
  VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};command.commandPool=pool_;command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command.commandBufferCount=2;
  VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
  std::array<VkCommandBuffer,2> commands{};
  if(!Check(c_.f.vkAllocateCommandBuffers(c_.device,&command,commands.data()),"Game command buffer",e)) return false;
  upload_=commands[0];command_=commands[1];
  return Check(c_.f.vkCreateFence(c_.device,&fence,nullptr,&fence_),"Game fence",e);
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
  const auto started=std::chrono::steady_clock::now();
  if(!WaitShaders(e)) {failed_=true;return false;}
  const auto shaders_ready=std::chrono::steady_clock::now();
  if(!WaitFence(e)) {failed_=true;return false;}
  const auto fence_ready=std::chrono::steady_clock::now();
  // This worker owns its command pool, descriptors, pipelines and mutable game
  // targets. Presentation reads immutable mailbox snapshots; ImageState also
  // protects cross-thread snapshot registration/retirement. CPU recording must
  // not hold the host queue mutex and block the UI's paint/event loop.
  const auto record_started=std::chrono::steady_clock::now();
  auto fail=[&] {failed_=true;return false;};
  if(!Check(c_.f.vkResetCommandPool(c_.device,pool_,0),"Reset game pool",e)) return fail();
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  // Uploads record into upload_, submitted ahead of command_ in one batch.
  if(!Check(c_.f.vkBeginCommandBuffer(upload_,&begin),"Begin game uploads",e) || !Check(c_.f.vkBeginCommandBuffer(command_,&begin),"Begin game frame",e) || !renderer_.BeginSubmission(command_,++serial_,e,upload_)) return fail();
  // Diagnostics: SR_VULKAN_DUMP_FRAME=N [SR_VULKAN_DUMP_DIR=dir] dumps frame N.
  static const uint64_t dump_frame=[] {const char* v=std::getenv("SR_VULKAN_DUMP_FRAME");return v?std::strtoull(v,nullptr,10):0ull;}();
  static const char* dump_trigger=std::getenv("SR_VULKAN_DUMP_TRIGGER");
  std::error_code trigger_error;
  const bool dump=(dump_frame && serial_==dump_frame) || (dump_trigger && std::filesystem::remove(dump_trigger,trigger_error));
  if(dump) renderer_.BeginDump();
  for(size_t i=0;i<packets_.size();++i) if(!renderer_.Record(packets_[i],command_,e)) {e.message+=" (frame packet "+std::to_string(i)+", kind="+std::to_string(packets_[i].index())+")";return fail();}
  if(dump && !renderer_.DumpTargets(command_,e)) return fail();
  renderer_.FinishSubmission();
  auto source=renderer_.SelectFrontbuffer(std::get<guest::SwapPacket>(packet),e);if(!source) return fail();
  // A mailbox owns a snapshot, never an image that the next guest frame mutates.
  // The previous submission has retired, so a snapshot held only here is free.
  constexpr guest::ResourceId snapshot_id=UINT64_MAX-16;
  auto& resources=renderer_.Resources();
  for(auto& snapshot:snapshots_) if(snapshot.use_count()==1 && snapshot->format==source->format && snapshot->extent.width==source->extent.width && snapshot->extent.height==source->extent.height) {output=snapshot;break;}
  if(!output) {
    resources.ForgetTexture(snapshot_id);
    output=resources.ResolveTexture(snapshot_id,source->format,source->extent,1,1,false,renderer_.Images(),e);if(!output) return fail();
    resources.ForgetTexture(snapshot_id);
    std::erase_if(snapshots_,[](auto& s){return s.use_count()==1;});
    if(snapshots_.size()<4) snapshots_.push_back(output);
  }
  VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  if(!renderer_.Images().Transition(command_,source->handle,range,ImageUsage::TransferSource(),e) || !renderer_.Images().Transition(command_,output->handle,range,ImageUsage::TransferDestination(),e)) return fail();
  VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent=source->extent;
  c_.f.vkCmdCopyImage(command_,source->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output->handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  if(!renderer_.Images().Transition(command_,source->handle,range,ImageUsage::Sampled(),e) || !renderer_.Images().Transition(command_,output->handle,range,ImageUsage::Sampled(),e)) return fail();
  if(!Check(c_.f.vkEndCommandBuffer(upload_),"End game uploads",e) || !Check(c_.f.vkEndCommandBuffer(command_),"End game frame",e) || !Check(c_.f.vkResetFences(c_.device,1,&fence_),"Reset game fence",e)) return fail();
  const std::array<VkCommandBuffer,2> commands{upload_,command_};
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=2;submit.pCommandBuffers=commands.data();
  const auto submit_started=std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point queue_ready;
  {
    std::lock_guard lock(queue_mutex_);
    queue_ready=std::chrono::steady_clock::now();
    if(!Check(c_.f.vkQueueSubmit(c_.graphics_queue,1,&submit,fence_),"Submit game frame",e)) return fail();
  }
  submitted_=true;
  if(dump) {
    const char* dir=std::getenv("SR_VULKAN_DUMP_DIR");
    if(!WaitFence(e) || !renderer_.WriteDump(dir?dir:"vk_dump",e)) return fail();
    c_.Log("Dumped native Vulkan frame "+std::to_string(serial_)+" to "+std::string(dir?dir:"vk_dump"));
  }
  if(serial_==1 || std::chrono::steady_clock::now()-cache_checkpoint_>=std::chrono::seconds(5)) {
    Error cache_error;
    if(!renderer_.CheckpointCache(cache_error)) c_.Log(cache_error.operation+": "+cache_error.message);
    cache_checkpoint_=std::chrono::steady_clock::now();
  }
  const auto finished=std::chrono::steady_clock::now();
  auto ms=[](auto a,auto b) {return std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count();};
  if(serial_==1 || serial_%120==0 || ms(started,finished)>100) {
    auto stats=renderer_.Stats();c_.Log("Submitted native Vulkan frame="+std::to_string(serial_)+", draws="+std::to_string(stats.draws)+", pending="+std::to_string(stats.pending)+", failed="+std::to_string(stats.failed)+", shaders_ms="+std::to_string(ms(started,shaders_ready))+", fence_ms="+std::to_string(ms(shaders_ready,fence_ready))+", queue_ms="+std::to_string(ms(submit_started,queue_ready))+", record_submit_ms="+std::to_string(ms(record_started,submit_started)+ms(queue_ready,finished))+", packets="+std::to_string(packets_.size()));
  }
  packets_.clear();return true;
}
}
