#include "swapchain.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
namespace {
uint32_t creates = 0, destroys = 0;
VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
VkSharingMode sharing;
std::vector<uint32_t> families;
template <class T> T H() {
  static uintptr_t n = 100;
  return reinterpret_cast<T>(n++);
}
VkResult VKAPI_CALL Idle(VkDevice) { return VK_SUCCESS; }
VkResult VKAPI_CALL Caps(VkPhysicalDevice, VkSurfaceKHR,
                         VkSurfaceCapabilitiesKHR *c) {
  *c = {};
  c->currentExtent = {UINT32_MAX, UINT32_MAX};
  c->minImageExtent = {1, 1};
  c->maxImageExtent = {4096, 4096};
  c->minImageCount = 2;
  c->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  c->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Formats(VkPhysicalDevice, VkSurfaceKHR, uint32_t *n,
                            VkSurfaceFormatKHR *f) {
  if (f)
    f[0] = {format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  *n = 1;
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Modes(VkPhysicalDevice, VkSurfaceKHR, uint32_t *n,
                          VkPresentModeKHR *m) {
  if (m)
    m[0] = VK_PRESENT_MODE_FIFO_KHR;
  *n = 1;
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Create(VkDevice, const VkSwapchainCreateInfoKHR *ci,
                           const VkAllocationCallbacks *, VkSwapchainKHR *h) {
  sharing = ci->imageSharingMode;
  families.clear();
  if (ci->queueFamilyIndexCount)
    families.assign(ci->pQueueFamilyIndices,
                    ci->pQueueFamilyIndices + ci->queueFamilyIndexCount);
  *h = H<VkSwapchainKHR>();
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Images(VkDevice, VkSwapchainKHR, uint32_t *n, VkImage *p) {
  if (p)
    for (int i = 0; i < 3; ++i)
      p[i] = H<VkImage>();
  *n = 3;
  return VK_SUCCESS;
}
VkResult VKAPI_CALL View(VkDevice, const VkImageViewCreateInfo *,
                         const VkAllocationCallbacks *, VkImageView *h) {
  *h = H<VkImageView>();
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Pass(VkDevice, const VkRenderPassCreateInfo *ci,
                         const VkAllocationCallbacks *, VkRenderPass *h) {
  SR_CHECK_EQ(ci->pAttachments[0].format, format);
  ++creates;
  *h = H<VkRenderPass>();
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Buffer(VkDevice, const VkFramebufferCreateInfo *,
                           const VkAllocationCallbacks *, VkFramebuffer *h) {
  *h = H<VkFramebuffer>();
  return VK_SUCCESS;
}
void VKAPI_CALL Destroy(VkDevice, VkSwapchainKHR,
                        const VkAllocationCallbacks *) {}
void VKAPI_CALL DestroyView(VkDevice, VkImageView,
                            const VkAllocationCallbacks *) {}
void VKAPI_CALL DestroyBuffer(VkDevice, VkFramebuffer,
                              const VkAllocationCallbacks *) {}
void VKAPI_CALL DestroyPass(VkDevice, VkRenderPass,
                            const VkAllocationCallbacks *) {
  ++destroys;
}
Dispatch Fake() {
  Dispatch f;
  f.vkDeviceWaitIdle = Idle;
  f.vkGetPhysicalDeviceSurfaceCapabilitiesKHR = Caps;
  f.vkGetPhysicalDeviceSurfaceFormatsKHR = Formats;
  f.vkGetPhysicalDeviceSurfacePresentModesKHR = Modes;
  f.vkCreateSwapchainKHR = Create;
  f.vkGetSwapchainImagesKHR = Images;
  f.vkCreateImageView = View;
  f.vkCreateRenderPass = Pass;
  f.vkCreateFramebuffer = Buffer;
  f.vkDestroySwapchainKHR = Destroy;
  f.vkDestroyImageView = DestroyView;
  f.vkDestroyFramebuffer = DestroyBuffer;
  f.vkDestroyRenderPass = DestroyPass;
  return f;
}
} // namespace
SR_TEST(split_families_use_concurrent_sharing) {
  Context c(Fake());
  c.device = H<VkDevice>();
  c.graphics_family = 2;
  c.present_family = 5;
  Swapchain s;
  Error e;
  SR_CHECK(s.Recreate(c, {1280, 720}, true, e));
  SR_CHECK_EQ(sharing, VK_SHARING_MODE_CONCURRENT);
  SR_CHECK(families == std::vector<uint32_t>({2, 5}));
}
SR_TEST(recreate_rebuilds_format_dependents) {
  Context c(Fake());
  c.device = H<VkDevice>();
  Swapchain s;
  Error e;
  creates = destroys = 0;
  format = VK_FORMAT_B8G8R8A8_UNORM;
  SR_CHECK(s.Recreate(c, {1280, 720}, true, e));
  format = VK_FORMAT_R8G8B8A8_UNORM;
  SR_CHECK(s.Recreate(c, {960, 540}, true, e));
  SR_CHECK_EQ(creates, 2);
  SR_CHECK_EQ(destroys, 1);
  SR_CHECK_EQ(s.choice.format.format, format);
  SR_CHECK_EQ(s.generation, 2);
}
