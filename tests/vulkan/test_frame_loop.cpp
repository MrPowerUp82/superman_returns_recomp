#include "frame_loop.h"
#include <stdexcept>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
namespace {
std::vector<std::string> calls;
std::vector<VkSemaphore> present_semaphores;
int image = 0;
bool pass_open=false;
std::mutex* probed = nullptr;
std::vector<std::string> lock_states;
// Outra thread só consegue travar o mutex quando a thread do teste não o segura.
bool Held() {
  if (!probed) return false;
  bool acquired = false;
  std::thread([&] { acquired = probed->try_lock(); if (acquired) probed->unlock(); }).join();
  return !acquired;
}
void NoteLock(const char* where) { if (probed) lock_states.push_back(std::string(where) + (Held() ? ":held" : ":free")); }
VkResult acquire_result = VK_SUCCESS, submit_result = VK_SUCCESS, present_result = VK_SUCCESS;
uintptr_t next = 10;
template <class T> T Handle() { return reinterpret_cast<T>(next++); }
VkResult VKAPI_CALL Idle(VkDevice) {
  calls.push_back("idle");
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Pool(VkDevice, const VkCommandPoolCreateInfo *,
                         const VkAllocationCallbacks *, VkCommandPool *o) {
  *o = Handle<VkCommandPool>();
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Allocate(VkDevice, const VkCommandBufferAllocateInfo *,
                             VkCommandBuffer *o) {
  *o = Handle<VkCommandBuffer>();
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Semaphore(VkDevice, const VkSemaphoreCreateInfo *,
                              const VkAllocationCallbacks *, VkSemaphore *o) {
  *o = Handle<VkSemaphore>();
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Fence(VkDevice, const VkFenceCreateInfo *,
                          const VkAllocationCallbacks *, VkFence *o) {
  *o = Handle<VkFence>();
  return VK_SUCCESS;
}
void VKAPI_CALL FreePool(VkDevice, VkCommandPool,
                         const VkAllocationCallbacks *) {}
void VKAPI_CALL FreeSemaphore(VkDevice, VkSemaphore,
                              const VkAllocationCallbacks *) {}
void VKAPI_CALL FreeFence(VkDevice, VkFence, const VkAllocationCallbacks *) {}
VkResult VKAPI_CALL Wait(VkDevice, uint32_t, const VkFence *, VkBool32,
                         uint64_t) {
  calls.push_back("wait");NoteLock("wait");
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Acquire(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore,
                            VkFence, uint32_t *i) {
  calls.push_back("acquire");NoteLock("acquire");
  *i = image;
  return acquire_result;
}
VkResult VKAPI_CALL Reset(VkDevice, uint32_t, const VkFence *) {
  calls.push_back("reset");
  return VK_SUCCESS;
}
VkResult VKAPI_CALL ResetPool(VkDevice, VkCommandPool,
                              VkCommandPoolResetFlags) {
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Begin(VkCommandBuffer, const VkCommandBufferBeginInfo *) {
  calls.push_back("record");
  return VK_SUCCESS;
}
VkResult VKAPI_CALL End(VkCommandBuffer) { return VK_SUCCESS; }
void VKAPI_CALL BeginPass(VkCommandBuffer, const VkRenderPassBeginInfo *,
                          VkSubpassContents) {pass_open=true;}
void VKAPI_CALL EndPass(VkCommandBuffer) {pass_open=false;}
VkResult VKAPI_CALL Submit(VkQueue, uint32_t, const VkSubmitInfo *, VkFence) {
  calls.push_back("submit");NoteLock("submit");
  return submit_result;
}
VkResult VKAPI_CALL Present(VkQueue, const VkPresentInfoKHR *p) {
  calls.push_back("present");NoteLock("present");
  present_semaphores.push_back(*p->pWaitSemaphores);
  return present_result;
}
Dispatch Functions() {
  Dispatch f;
  f.vkDeviceWaitIdle = Idle;
  f.vkCreateCommandPool = Pool;
  f.vkAllocateCommandBuffers = Allocate;
  f.vkCreateSemaphore = Semaphore;
  f.vkCreateFence = Fence;
  f.vkDestroyCommandPool = FreePool;
  f.vkDestroySemaphore = FreeSemaphore;
  f.vkDestroyFence = FreeFence;
  f.vkWaitForFences = Wait;
  f.vkAcquireNextImageKHR = Acquire;
  f.vkResetFences = Reset;
  f.vkResetCommandPool = ResetPool;
  f.vkBeginCommandBuffer = Begin;
  f.vkEndCommandBuffer = End;
  f.vkQueueSubmit = Submit;
  f.vkQueuePresentKHR = Present;
  f.vkCmdBeginRenderPass = BeginPass;
  f.vkCmdEndRenderPass = EndPass;
  return f;
}
struct Fixture {
  Context c{Functions()};
  Swapchain s;
  FrameLoop loop;
  Error e;
  Fixture() {
    c.device = Handle<VkDevice>();
    s.handle = Handle<VkSwapchainKHR>();
    s.images.resize(3);
    s.framebuffers.resize(3);
    s.choice.extent = {1280, 720};
    calls.clear();
    present_semaphores.clear();
    acquire_result = submit_result = present_result = VK_SUCCESS;
    probed = nullptr;lock_states.clear();
    SR_CHECK(loop.Initialize(c, s, e));
    calls.clear();
  }
  FrameOutcome Draw() {
    return loop.Draw(c, s, [](auto, auto) {}, e);
  }
};
} // namespace
SR_TEST(zero_extent_does_not_acquire) {
  Fixture f;
  f.s.choice.extent = {0, 0};
  SR_CHECK(f.Draw() == FrameOutcome::kSuspended);
  SR_CHECK(calls.empty());
}
SR_TEST(out_of_date_acquire_does_not_reset_fence) {
  Fixture f;
  acquire_result = VK_ERROR_OUT_OF_DATE_KHR;
  SR_CHECK(f.Draw() == FrameOutcome::kRecreate);
  SR_CHECK(calls == std::vector<std::string>({"wait", "acquire"}));
}
SR_TEST(suboptimal_image_is_submitted_then_recreated) {
  Fixture f;
  acquire_result = VK_SUBOPTIMAL_KHR;
  SR_CHECK(f.Draw() == FrameOutcome::kRecreate);
  SR_CHECK(calls == std::vector<std::string>({"wait", "acquire", "record",
                                              "reset", "submit", "present"}));
}
SR_TEST(present_semaphore_follows_image_index) {
  Fixture f;
  image = 2;
  SR_CHECK(f.Draw() == FrameOutcome::kPresented);
  image = 0;
  SR_CHECK(f.Draw() == FrameOutcome::kPresented);
  image = 2;
  SR_CHECK(f.Draw() == FrameOutcome::kPresented);
  SR_CHECK(present_semaphores[0] == present_semaphores[2]);
  SR_CHECK(present_semaphores[0] != present_semaphores[1]);
}
SR_TEST(submit_failure_stops_instead_of_waiting_unsignaled_fence) {
  Fixture f;
  submit_result = VK_ERROR_DEVICE_LOST;
  SR_CHECK(f.Draw() == FrameOutcome::kFailed);
  calls.clear();
  SR_CHECK(f.Draw() == FrameOutcome::kFailed);
  SR_CHECK(calls.empty());
}
SR_TEST(device_lost_present_is_not_masked_by_suboptimal_acquire) {
  Fixture f;
  acquire_result=VK_SUBOPTIMAL_KHR;
  present_result=VK_ERROR_DEVICE_LOST;
  SR_CHECK(f.Draw()==FrameOutcome::kFailed);
  calls.clear();
  SR_CHECK(f.Draw()==FrameOutcome::kFailed);
  SR_CHECK(calls.empty());
}
SR_TEST(game_prepare_precedes_present_pass_and_resources_retire_after_fence) {
  Fixture f;std::vector<uint64_t> prepared,retired;
  FrameWork work;
  work.prepare=[&](VkCommandBuffer,uint64_t serial,Error&) {SR_CHECK(!pass_open);prepared.push_back(serial);return true;};
  work.paint=[&](VkCommandBuffer,uint32_t) {SR_CHECK(pass_open);};
  work.retire=[&](uint64_t serial) {retired.push_back(serial);};
  image=0;SR_CHECK(f.loop.DrawGame(f.c,f.s,work,f.e)==FrameOutcome::kPresented);
  image=1;SR_CHECK(f.loop.DrawGame(f.c,f.s,work,f.e)==FrameOutcome::kPresented);SR_CHECK(retired.empty());
  image=2;SR_CHECK(f.loop.DrawGame(f.c,f.s,work,f.e)==FrameOutcome::kPresented);
  SR_CHECK(prepared==std::vector<uint64_t>({1,2,3}));SR_CHECK(retired==std::vector<uint64_t>({1}));
  SR_CHECK(f.loop.Retire(f.c,f.e));SR_CHECK(retired==std::vector<uint64_t>({1,3,2}));
}
SR_TEST(game_prepare_failure_submits_owned_partial_commands_and_stops) {
  Fixture f;FrameWork work;bool painted=false;
  work.prepare=[](VkCommandBuffer,uint64_t,Error& e) {e={"game prepare",VK_ERROR_FORMAT_NOT_SUPPORTED,"Unsupported guest format"};return false;};
  work.paint=[&](VkCommandBuffer,uint32_t) {painted=true;};
  SR_CHECK(f.loop.DrawGame(f.c,f.s,work,f.e)==FrameOutcome::kFailed);SR_CHECK(!painted);
  SR_CHECK(f.e.operation=="game prepare");SR_CHECK(std::find(calls.begin(),calls.end(),"submit")!=calls.end());
  calls.clear();SR_CHECK(f.loop.DrawGame(f.c,f.s,work,f.e)==FrameOutcome::kFailed);SR_CHECK(calls.empty());
}
SR_TEST(game_callback_exception_finishes_submission_and_stops) {
  for(bool during_paint:{false,true}) {
    Fixture f;FrameWork work;
    if(during_paint) work.paint=[](VkCommandBuffer,uint32_t){throw std::runtime_error("UI paint failed");};
    else work.prepare=[](VkCommandBuffer,uint64_t,Error&)->bool{throw std::runtime_error("Guest prepare failed");};
    SR_CHECK(f.loop.DrawGame(f.c,f.s,work,f.e)==FrameOutcome::kFailed);
    SR_CHECK(!pass_open);SR_CHECK(!f.e.message.empty());SR_CHECK(std::find(calls.begin(),calls.end(),"submit")!=calls.end());
    calls.clear();SR_CHECK(f.loop.DrawGame(f.c,f.s,work,f.e)==FrameOutcome::kFailed);SR_CHECK(calls.empty());
  }
}

SR_TEST(game_draw_waits_and_acquires_outside_the_queue_mutex_and_submits_under_it) {
  Fixture f;
  std::mutex queue;
  probed = &queue;
  FrameWork work;
  work.queue_mutex = &queue;
  work.prepare = [&](VkCommandBuffer, uint64_t, Error&) { NoteLock("prepare"); return true; };
  work.retire = [&](uint64_t) { NoteLock("retire"); };
  for (int i = 0; i < 3; ++i) {
    image = i;
    SR_CHECK(f.loop.DrawGame(f.c, f.s, work, f.e) == FrameOutcome::kPresented);
  }
  // O terceiro quadro reusa o slot 0: espera o fence, faz o retire do serial 1 sob lock e só então faz o acquire.
  SR_CHECK(lock_states == std::vector<std::string>({
      "wait:free", "acquire:free", "prepare:held", "submit:held", "present:held",
      "wait:free", "acquire:free", "prepare:held", "submit:held", "present:held",
      "wait:free", "retire:held", "acquire:free", "prepare:held", "submit:held", "present:held"}));
  probed = nullptr;
}
