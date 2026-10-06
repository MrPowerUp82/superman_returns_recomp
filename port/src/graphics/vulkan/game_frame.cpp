#include "game_frame.h"
#include <chrono>
#include <cstdlib>
#include <thread>
#include <unordered_map>
namespace superman_returns::graphics::vulkan {
namespace {
// The service hashes and compares the whole container under a mutex on every
// lookup (~6000 per frame). Captures are long-lived registry objects, so final
// results are memoized per capture; pointer, hash and size must all match.
ShaderLookup MemoizeFinal(ShaderLookup lookup) {
  struct Entry {uint64_t hash;size_t size;shaders::ShaderResult result;};
  auto cache=std::make_shared<std::unordered_map<const guest::ShaderCapture*,Entry>>();
  return [lookup=std::move(lookup),cache](const guest::ShaderCapture& capture) {
    if(auto found=cache->find(&capture);found!=cache->end() && found->second.hash==capture.hash && found->second.size==capture.container.size()) return found->second.result;
    auto result=lookup(capture);
    if(result.status!=shaders::ShaderPoll::pending) (*cache)[&capture]=Entry{capture.hash,capture.container.size(),result};
    return result;
  };
}
}
GameFrame::GameFrame(Context& c,std::mutex& mutex,ShaderLookup shaders,TextureDecoder decoder):c_(c),queue_mutex_(mutex),shaders_(shaders?MemoizeFinal(std::move(shaders)):ShaderLookup{}),renderer_(c,shaders_,std::move(decoder)) {
  const char* profile=std::getenv("SR_VULKAN_PROFILE");profiling_=profile && *profile && *profile!='0';renderer_.EnableProfile(profiling_);
}
GameFrame::~GameFrame() {
  StopAsync();
  std::lock_guard lock(queue_mutex_);snapshots_.clear();
  if(slot_submitted_[0] || slot_submitted_[1]) {c_.f.vkDeviceWaitIdle(c_.device);renderer_.Retire(serial_);}
  for(auto fence:fences_) if(fence) c_.f.vkDestroyFence(c_.device,fence,nullptr);
  for(auto pool:pools_) if(pool) c_.f.vkDestroyCommandPool(c_.device,pool,nullptr);
}
bool GameFrame::Initialize(const std::filesystem::path& cache,Error& e) {
  if(!renderer_.Initialize(cache,e)) return false;
  for(size_t slot=0;slot<kSlots;++slot) {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=c_.graphics_family;
    if(!Check(c_.f.vkCreateCommandPool(c_.device,&pool,nullptr,&pools_[slot]),"Game command pool",e)) return false;
    VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};command.commandPool=pools_[slot];command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command.commandBufferCount=2;
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
    std::array<VkCommandBuffer,2> commands{};
    if(!Check(c_.f.vkAllocateCommandBuffers(c_.device,&command,commands.data()),"Game command buffer",e)) return false;
    uploads_[slot]=commands[0];commands_[slot]=commands[1];
    if(!Check(c_.f.vkCreateFence(c_.device,&fence,nullptr,&fences_[slot]),"Game fence",e)) return false;
  }
  return true;
}
bool GameFrame::WaitSlot(size_t slot,Error& e) {
  if(!slot_submitted_[slot]) return true;
  // The queue lock isn't held across waits, so UI paints can be submitted.
  // The queue completes in order, so retiring this serial retires older ones.
  for(;;) {
    auto result=c_.f.vkWaitForFences(c_.device,1,&fences_[slot],VK_TRUE,25000000);
    if(result==VK_SUCCESS) {std::lock_guard lock(queue_mutex_);renderer_.Retire(slot_serial_[slot]);slot_submitted_[slot]=false;return true;}
    if(result!=VK_TIMEOUT) return Check(result,"Game frame completion",e);
    if(cancelled_) {e={"Game frame",VK_ERROR_INITIALIZATION_FAILED,"Rendering cancelled"};return false;}
  }
}
void GameFrame::Cancel() {
  cancelled_.store(true);{std::lock_guard lock(job_mutex_);}job_cv_.notify_all();
}
void GameFrame::StartAsync(OutputCallback output) {
  std::lock_guard lock(job_mutex_);if(recorder_.joinable()) return;
  output_=std::move(output);stop_=false;recorder_=std::thread(&GameFrame::RecorderMain,this);
}
void GameFrame::StopAsync() {
  {std::lock_guard lock(job_mutex_);if(!recorder_.joinable()) return;stop_=true;}
  job_cv_.notify_all();recorder_.join();
  std::lock_guard lock(job_mutex_);output_={};
}
void GameFrame::RecorderMain() {
  for(;;) {
    {std::unique_lock lock(job_mutex_);job_cv_.wait(lock,[&]{return stop_ || job_pending_;});if(!job_pending_) return;}
    // job_pending_ stays set while recording: the producer never touches the job.
    std::shared_ptr<TextureResource> output;Error e;
    bool ok=RecordFrame(job_packets_,*job_swap_,output,e);
    if(ok && output && output_) output_(output,*job_swap_);
    std::lock_guard lock(job_mutex_);
    if(!ok) {async_error_=e;failed_=true;}
    job_packets_.clear();job_swap_.reset();job_pending_=false;job_cv_.notify_all();
  }
}
bool GameFrame::WaitShaders(const std::vector<guest::RenderPacket>& packets,Error& e) {
  std::vector<std::shared_ptr<const guest::ShaderCapture>> captures;
  for(auto& packet:packets) if(auto* draw=std::get_if<guest::DrawPacket>(&packet);draw && draw->count) {
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
  if(recorder_.joinable()) {
    std::unique_lock lock(job_mutex_);
    job_cv_.wait(lock,[&]{return !job_pending_ || cancelled_ || stop_;});
    if(failed_) {e=async_error_.message.empty()?Error{"Game frame",VK_ERROR_INITIALIZATION_FAILED,"Renderer stopped"}:async_error_;return false;}
    if(cancelled_ || stop_) {e={"Game frame",VK_ERROR_INITIALIZATION_FAILED,"Rendering cancelled"};return false;}
    job_packets_.swap(packets_);packets_.clear();job_swap_=std::move(std::get<guest::SwapPacket>(packet));job_pending_=true;
    lock.unlock();job_cv_.notify_all();return true;
  }
  return RecordFrame(packets_,std::get<guest::SwapPacket>(packet),output,e);
}
bool GameFrame::RecordFrame(std::vector<guest::RenderPacket>& packets_,const guest::SwapPacket& swap,std::shared_ptr<TextureResource>& output,Error& e) {
  const auto started=std::chrono::steady_clock::now();
  if(!WaitShaders(packets_,e)) {failed_=true;return false;}
  const auto shaders_ready=std::chrono::steady_clock::now();
  // Wait only for the frame that last used this slot (N-2).
  const size_t slot=(serial_+1)%kSlots;
  if(!WaitSlot(slot,e)) {failed_=true;return false;}
  pool_=pools_[slot];command_=commands_[slot];upload_=uploads_[slot];fence_=fences_[slot];
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
  auto source=renderer_.SelectFrontbuffer(swap,e);if(!source) return fail();
  // A mailbox owns a snapshot, never an image that the next guest frame mutates.
  // In-flight submissions keep their snapshot referenced, so a snapshot held
  // only by this pool is free to overwrite.
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
  slot_submitted_[slot]=true;slot_serial_[slot]=serial_;
  // Loading frames stage hundreds of MB; keeping two of them in flight can
  // exhaust a shared-memory iGPU. Finish such a frame before recording more.
  if(kSlots>1 && renderer_.Resources().SubmissionBytes()>(64ull<<20) && !WaitSlot(slot,e)) return fail();
  if(dump) {
    const char* dir=std::getenv("SR_VULKAN_DUMP_DIR");
    if(!WaitSlot(slot,e) || !renderer_.WriteDump(dir?dir:"vk_dump",e)) return fail();
    c_.Log("Dumped native Vulkan frame "+std::to_string(serial_)+" to "+std::string(dir?dir:"vk_dump"));
  }
  if(serial_==1 || std::chrono::steady_clock::now()-cache_checkpoint_>=std::chrono::seconds(5)) {
    Error cache_error;
    if(!renderer_.CheckpointCache(cache_error)) c_.Log(cache_error.operation+": "+cache_error.message);
    cache_checkpoint_=std::chrono::steady_clock::now();
  }
  const auto finished=std::chrono::steady_clock::now();
  auto ms=[](auto a,auto b) {return std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count();};
  if(profiling_) {
    auto us=[](auto a,auto b) {return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(b-a).count());};
    auto& t=timing_;++t.frames;t.packets+=packets_.size();
    if(last_swap_.time_since_epoch().count()) t.interval_us+=us(last_swap_,started);
    t.shaders_us+=us(started,shaders_ready);t.fence_us+=us(shaders_ready,fence_ready);t.record_us+=us(record_started,submit_started);
    t.queue_us+=us(submit_started,queue_ready);t.tail_us+=us(queue_ready,finished);last_swap_=finished;
    if(t.frames==120) {
      auto p=renderer_.TakeProfile();auto cache=renderer_.TakeDescriptorStats();auto avg=[&](uint64_t v) {return std::to_string(v/1000/t.frames)+"."+std::to_string(v/100/t.frames%10);};
      auto avg_ns=[&](uint64_t v) {return std::to_string(v/1000000/t.frames)+"."+std::to_string(v/100000/t.frames%10);};
      c_.Log("Vulkan profile (ms/frame over 120): interval="+avg(t.interval_us)+" shaders="+avg(t.shaders_us)+" fence="+avg(t.fence_us)+" record="+avg(t.record_us)+" queue="+avg(t.queue_us)+" tail="+avg(t.tail_us)
        +" | uploads="+avg_ns(p.ns[RecordProfile::kUploads])+" targets="+avg_ns(p.ns[RecordProfile::kTargets])+" textures="+avg_ns(p.ns[RecordProfile::kTextures])+" bindings="+avg_ns(p.ns[RecordProfile::kBindings])
        +" descriptors="+avg_ns(p.ns[RecordProfile::kDescriptors])+" pipeline="+avg_ns(p.ns[RecordProfile::kPipeline])+" commands="+avg_ns(p.ns[RecordProfile::kCommands])+" resolves="+avg_ns(p.ns[RecordProfile::kResolves])+" clears="+avg_ns(p.ns[RecordProfile::kClears])
        +" | draws="+std::to_string(p.draws/t.frames)+" packets="+std::to_string(t.packets/t.frames)+" buffer_uploads="+std::to_string(p.buffer_uploads/t.frames)+" buffer_kb="+std::to_string(p.buffer_bytes/1024/t.frames)
        +" texture_uploads="+std::to_string(p.texture_uploads/t.frames)+" texture_kb="+std::to_string(p.texture_bytes/1024/t.frames)
        +" | descriptor_cache hits="+std::to_string(cache.hits/t.frames)+" misses="+std::to_string(cache.misses/t.frames)+" evicted="+std::to_string(cache.evicted/t.frames)+" entries="+std::to_string(cache.entries)
        +" | live buffers="+std::to_string(LiveMemory().buffers.load())+" ("+std::to_string(LiveMemory().buffer_bytes.load()>>20)+" MB) images="+std::to_string(LiveMemory().images.load())+" ("+std::to_string(LiveMemory().image_bytes.load()>>20)+" MB)");
      t={};
    }
  }
  if(serial_==1 || serial_%120==0 || ms(started,finished)>100) {
    auto stats=renderer_.Stats();c_.Log("Submitted native Vulkan frame="+std::to_string(serial_)+", draws="+std::to_string(stats.draws)+", pending="+std::to_string(stats.pending)+", failed="+std::to_string(stats.failed)+", shaders_ms="+std::to_string(ms(started,shaders_ready))+", fence_ms="+std::to_string(ms(shaders_ready,fence_ready))+", queue_ms="+std::to_string(ms(submit_started,queue_ready))+", record_submit_ms="+std::to_string(ms(record_started,submit_started)+ms(queue_ready,finished))+", packets="+std::to_string(packets_.size())+", live_buffers="+std::to_string(LiveMemory().buffers.load())+", live_mb="+std::to_string((LiveMemory().buffer_bytes.load()+LiveMemory().image_bytes.load())>>20));
  }
  packets_.clear();return true;
}
}
