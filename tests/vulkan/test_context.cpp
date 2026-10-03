#include "context.h"
#include "test_main.h"
#include <cstring>
using namespace superman_returns::graphics::vulkan;
static std::vector<std::string> events;
static uint32_t version = VK_API_VERSION_1_1;
static bool ext = true;
static VkResult VKAPI_CALL Version(uint32_t *v) {
  *v = version;
  return VK_SUCCESS;
}
static VkResult VKAPI_CALL Extensions(const char *, uint32_t *n,
                                      VkExtensionProperties *p) {
  if (!p)
    *n = ext ? 1 : 0;
  else
    std::strcpy(p[0].extensionName, VK_KHR_SURFACE_EXTENSION_NAME);
  return VK_SUCCESS;
}
static VkResult VKAPI_CALL Layers(uint32_t *n, VkLayerProperties *) {
  *n = 0;
  return VK_SUCCESS;
}
static VkResult VKAPI_CALL Create(const VkInstanceCreateInfo *,
                                  const VkAllocationCallbacks *,
                                  VkInstance *i) {
  *i = reinterpret_cast<VkInstance>(1);
  events.push_back("create");
  return VK_SUCCESS;
}
static void VKAPI_CALL Destroy(VkInstance, const VkAllocationCallbacks *) {
  events.push_back("destroy");
}
static void VKAPI_CALL DestroySurface(VkInstance, VkSurfaceKHR,
                                      const VkAllocationCallbacks *) {
  events.push_back("surface");
}
static VkResult VKAPI_CALL Devices(VkInstance, uint32_t *, VkPhysicalDevice *) {
  return VK_ERROR_INITIALIZATION_FAILED;
}
static Dispatch Fake() {
  Dispatch f;
  f.vkEnumerateInstanceVersion = Version;
  f.vkEnumerateInstanceExtensionProperties = Extensions;
  f.vkEnumerateInstanceLayerProperties = Layers;
  f.vkCreateInstance = Create;
  f.vkDestroyInstance = Destroy;
  f.vkDestroySurfaceKHR = DestroySurface;
  f.vkEnumeratePhysicalDevices = Devices;
  return f;
}
SR_TEST(missing_loader_reports_error) {
  Loader l;
  Error e;
  SR_CHECK(!l.Open(e, "does-not-exist-vulkan-loader.dll"));
  SR_CHECK(!e.message.empty());
}
SR_TEST(unsupported_instance_version_reports_error) {
  version = VK_API_VERSION_1_0;
  Context c(Fake());
  Error e;
  SR_CHECK(!c.CreateInstance({}, false, e));
  version = VK_API_VERSION_1_1;
}
SR_TEST(required_surface_extension_is_checked) {
  ext = false;
  Context c(Fake());
  Error e;
  const char *requested = VK_KHR_SURFACE_EXTENSION_NAME;
  SR_CHECK(!c.CreateInstance({&requested, 1}, false, e));
  ext = true;
}
SR_TEST(optional_validation_absence_is_logged) {
  Context c(Fake());
  std::string log;
  c.logger = [&](auto &s) { log += s; };
  Error e;
  SR_CHECK(c.CreateInstance({}, true, e));
  SR_CHECK(!c.validation_active);
  SR_CHECK(log.find("unavailable") != std::string::npos);
}
SR_TEST(partial_device_failure_cleans_surface_and_instance) {
  events.clear();
  {
    Context c(Fake());
    Error e;
    SR_CHECK(c.CreateInstance({}, false, e));
    SR_CHECK(!c.OpenDevice(reinterpret_cast<VkSurfaceKHR>(2), "", e));
  }
  SR_CHECK(events ==
           std::vector<std::string>({"create", "surface", "destroy"}));
}
SR_TEST(context_destroys_each_created_handle_once) {
  events.clear();
  {
    Context c(Fake());
    Error e;
    SR_CHECK(c.CreateInstance({}, false, e));
  }
  SR_CHECK(events == std::vector<std::string>({"create", "destroy"}));
}
static VkResult VKAPI_CALL Lost(VkDevice) {
  events.push_back("wait-lost");
  return VK_ERROR_DEVICE_LOST;
}
static void VKAPI_CALL DestroyDevice(VkDevice, const VkAllocationCallbacks *) {
  events.push_back("device");
}
SR_TEST(device_lost_context_cleanup_is_finite) {
  events.clear();
  {
    auto f = Fake();
    f.vkDeviceWaitIdle = Lost;
    f.vkDestroyDevice = DestroyDevice;
    Context c(f);
    c.device = reinterpret_cast<VkDevice>(3);
  }
  SR_CHECK(events == std::vector<std::string>({"wait-lost", "device"}));
}
