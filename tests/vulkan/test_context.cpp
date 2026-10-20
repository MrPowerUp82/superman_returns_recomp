#include "context.h"
#include "test_main.h"
#include <cstring>
#include <stdexcept>
using namespace superman_returns::graphics::vulkan;
static std::vector<std::string> events;
static uint32_t version = VK_API_VERSION_1_1;
static bool ext = true;
static bool debug_available = false;
static bool timeline_available=false,timeline_supported=true;
static bool global_available=false,global_supported=true;
static bool robust_global_supported=true,robust_seen=false;
static PFN_vkDebugUtilsMessengerCallbackEXT debug_callback;
static void *debug_user;
static VkResult VKAPI_CALL Version(uint32_t *v) {
  *v = version;
  return VK_SUCCESS;
}
static VkResult VKAPI_CALL Extensions(const char *, uint32_t *n,
                                      VkExtensionProperties *p) {
  if (!p)
    *n = debug_available ? 2 : (ext ? 1 : 0);
  else {
    std::strcpy(p[0].extensionName, VK_KHR_SURFACE_EXTENSION_NAME);
    if (debug_available)
      std::strcpy(p[1].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  return VK_SUCCESS;
}
static VkResult VKAPI_CALL Layers(uint32_t *n, VkLayerProperties *p) {
  *n = debug_available ? 1 : 0;
  if (p && debug_available)
    std::strcpy(p[0].layerName, "VK_LAYER_KHRONOS_validation");
  return VK_SUCCESS;
}
static VkResult VKAPI_CALL Create(const VkInstanceCreateInfo *ci,
                                  const VkAllocationCallbacks *,
                                  VkInstance *i) {
  if (ci->pNext) {
    auto *d =
        static_cast<const VkDebugUtilsMessengerCreateInfoEXT *>(ci->pNext);
    debug_callback = d->pfnUserCallback;
    debug_user = d->pUserData;
  }
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
static VkResult VKAPI_CALL OneDevice(VkInstance, uint32_t *n,
                                     VkPhysicalDevice *p) {
  *n = 1;
  if (p)
    *p = reinterpret_cast<VkPhysicalDevice>(3);
  return VK_SUCCESS;
}
static void VKAPI_CALL DeviceProperties(VkPhysicalDevice,
                                        VkPhysicalDeviceProperties2 *p) {
  p->properties.apiVersion = VK_API_VERSION_1_1;
  p->properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
  for(auto* next=static_cast<VkBaseOutStructure*>(p->pNext);next;next=next->pNext)
    if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES)
      reinterpret_cast<VkPhysicalDeviceDescriptorIndexingProperties*>(next)->robustBufferAccessUpdateAfterBind=robust_global_supported;
}
static VkResult VKAPI_CALL DeviceExtensions(VkPhysicalDevice, const char *,
                                            uint32_t *n,
                                            VkExtensionProperties *p) {
  *n = 1+uint32_t(timeline_available)+uint32_t(global_available);
  if (p)
    std::strcpy(p->extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  if(p && timeline_available) std::strcpy(p[1].extensionName,VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
  if(p && global_available) std::strcpy(p[1+uint32_t(timeline_available)].extensionName,VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
  return VK_SUCCESS;
}
static void VKAPI_CALL DeviceFamilies(VkPhysicalDevice, uint32_t *n,
                                      VkQueueFamilyProperties *p) {
  *n = 1;
  if (p) {
    *p = {};
    p->queueFlags = VK_QUEUE_GRAPHICS_BIT;
    p->queueCount = 1;
  }
}
static VkResult VKAPI_CALL DevicePresent(VkPhysicalDevice, uint32_t,
                                         VkSurfaceKHR, VkBool32 *p) {
  *p = VK_TRUE;
  return VK_SUCCESS;
}
static const VkPhysicalDeviceFeatures *requested_features_seen;
static VkResult VKAPI_CALL FailedDevice(VkPhysicalDevice,
                                        const VkDeviceCreateInfo *ci,
                                        const VkAllocationCallbacks *,
                                        VkDevice *) {
  requested_features_seen = ci->pEnabledFeatures;
  events.push_back("create-device-failed");
  return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
static int feature_queries=0;static bool timeline_seen=false;
static void VKAPI_CALL TimelineFeatures(VkPhysicalDevice,VkPhysicalDeviceFeatures2* features) {
  ++feature_queries;
  auto* timeline=static_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(features->pNext);
  timeline->timelineSemaphore=timeline_supported;
}
static VkResult VKAPI_CALL TimelineDevice(VkPhysicalDevice,const VkDeviceCreateInfo* info,const VkAllocationCallbacks*,VkDevice*) {
  timeline_seen=info->pNext && static_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(info->pNext)->timelineSemaphore;
  return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
SR_TEST(optional_fg_timeline_does_not_change_the_off_device) {
  for(bool requested:{false,true}) for(bool available:{false,true}) for(bool supported:{false,true}) {
    timeline_available=available;timeline_supported=supported;feature_queries=0;timeline_seen=false;
    auto f=Fake();f.vkEnumeratePhysicalDevices=OneDevice;f.vkGetPhysicalDeviceProperties2=DeviceProperties;
    f.vkEnumerateDeviceExtensionProperties=DeviceExtensions;f.vkGetPhysicalDeviceQueueFamilyProperties=DeviceFamilies;
    f.vkGetPhysicalDeviceSurfaceSupportKHR=DevicePresent;f.vkGetPhysicalDeviceFeatures2=TimelineFeatures;f.vkCreateDevice=TimelineDevice;
    Context c(f);Error error;SR_CHECK(c.CreateInstance({},false,error));
    SR_CHECK(!c.OpenDevice(reinterpret_cast<VkSurfaceKHR>(2),"",error,nullptr,false,requested));
    SR_CHECK_EQ(timeline_seen,requested && available && supported);
    if(!requested) SR_CHECK_EQ(feature_queries,0);
    SR_CHECK(error.result==VK_ERROR_OUT_OF_DEVICE_MEMORY);
  }
  timeline_available=false;
}
static bool global_seen=false,global_timeline_seen=false;
static void VKAPI_CALL GlobalFeatures(VkPhysicalDevice,VkPhysicalDeviceFeatures2* features) {
  for(auto* next=static_cast<VkBaseOutStructure*>(features->pNext);next;next=next->pNext) {
    if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES)
      reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(next)->timelineSemaphore=VK_TRUE;
    if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES) {
      features->features.shaderStorageBufferArrayDynamicIndexing=global_supported;
      auto& q=*reinterpret_cast<VkPhysicalDeviceDescriptorIndexingFeatures*>(next);
      q.runtimeDescriptorArray=q.shaderSampledImageArrayNonUniformIndexing=q.shaderStorageBufferArrayNonUniformIndexing=
        q.descriptorBindingPartiallyBound=q.descriptorBindingSampledImageUpdateAfterBind=q.descriptorBindingStorageBufferUpdateAfterBind=global_supported;
    }
  }
}
static VkResult VKAPI_CALL GlobalDevice(VkPhysicalDevice,const VkDeviceCreateInfo* info,const VkAllocationCallbacks*,VkDevice*) {
  robust_seen=info->pEnabledFeatures && info->pEnabledFeatures->robustBufferAccess;
  for(auto* next=static_cast<const VkBaseInStructure*>(info->pNext);next;next=next->pNext) {
    if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES)global_timeline_seen=true;
    if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES) {
      const auto& q=*reinterpret_cast<const VkPhysicalDeviceDescriptorIndexingFeatures*>(next);
      global_seen=q.runtimeDescriptorArray && q.shaderSampledImageArrayNonUniformIndexing && q.shaderStorageBufferArrayNonUniformIndexing &&
        q.descriptorBindingPartiallyBound && q.descriptorBindingSampledImageUpdateAfterBind && q.descriptorBindingStorageBufferUpdateAfterBind;
      SR_CHECK(!q.descriptorBindingStorageImageUpdateAfterBind && !q.shaderUniformBufferArrayNonUniformIndexing);
      SR_CHECK(info->pEnabledFeatures && info->pEnabledFeatures->shaderStorageBufferArrayDynamicIndexing);
    }
  }
  return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
SR_TEST(global_descriptors_are_explicit_require_support_and_preserve_timeline_chain) {
  for(bool requested:{false,true})for(bool available:{false,true})for(bool supported:{false,true})for(bool timeline:{false,true}) {
    global_available=available;global_supported=supported;timeline_available=timeline;
    global_seen=global_timeline_seen=false;
    auto f=Fake();f.vkEnumeratePhysicalDevices=OneDevice;f.vkGetPhysicalDeviceProperties2=DeviceProperties;
    f.vkEnumerateDeviceExtensionProperties=DeviceExtensions;f.vkGetPhysicalDeviceQueueFamilyProperties=DeviceFamilies;
    f.vkGetPhysicalDeviceSurfaceSupportKHR=DevicePresent;f.vkGetPhysicalDeviceFeatures2=GlobalFeatures;f.vkCreateDevice=GlobalDevice;
    Context c(f);Error error;SR_CHECK(c.CreateInstance({},false,error));
    SR_CHECK(!c.OpenDevice(reinterpret_cast<VkSurfaceKHR>(2),"",error,nullptr,false,timeline,requested));
    if(requested && (!available || !supported))SR_CHECK(error.result!=VK_ERROR_OUT_OF_DEVICE_MEMORY);
    else {SR_CHECK(error.result==VK_ERROR_OUT_OF_DEVICE_MEMORY);SR_CHECK_EQ(global_seen,requested);SR_CHECK_EQ(global_timeline_seen,timeline);}
  }
  global_available=timeline_available=false;
}
SR_TEST(global_descriptors_preserve_robustness_or_reject_an_incompatible_device) {
  for(bool supported:{false,true}) {
    global_available=global_supported=true;robust_global_supported=supported;robust_seen=false;
    auto f=Fake();f.vkEnumeratePhysicalDevices=OneDevice;f.vkGetPhysicalDeviceProperties2=DeviceProperties;
    f.vkEnumerateDeviceExtensionProperties=DeviceExtensions;f.vkGetPhysicalDeviceQueueFamilyProperties=DeviceFamilies;
    f.vkGetPhysicalDeviceSurfaceSupportKHR=DevicePresent;f.vkGetPhysicalDeviceFeatures2=GlobalFeatures;f.vkCreateDevice=GlobalDevice;
    Context c(f);Error error;SR_CHECK(c.CreateInstance({},false,error));VkPhysicalDeviceFeatures core{};core.robustBufferAccess=VK_TRUE;
    SR_CHECK(!c.OpenDevice(reinterpret_cast<VkSurfaceKHR>(2),"",error,&core,false,false,true));
    if(supported){SR_CHECK(error.result==VK_ERROR_OUT_OF_DEVICE_MEMORY);SR_CHECK(robust_seen);}
    else SR_CHECK(error.result==VK_ERROR_FEATURE_NOT_PRESENT);
  }
  global_available=false;robust_global_supported=true;
}
SR_TEST(partial_device_failure_cleans_surface_and_instance) {
  events.clear();
  {
    auto f = Fake();
    f.vkEnumeratePhysicalDevices = OneDevice;
    f.vkGetPhysicalDeviceProperties2 = DeviceProperties;
    f.vkEnumerateDeviceExtensionProperties = DeviceExtensions;
    f.vkGetPhysicalDeviceQueueFamilyProperties = DeviceFamilies;
    f.vkGetPhysicalDeviceSurfaceSupportKHR = DevicePresent;
    f.vkCreateDevice = FailedDevice;
    Context c(f);
    Error e;
    SR_CHECK(c.CreateInstance({}, false, e));
    SR_CHECK(!c.OpenDevice(reinterpret_cast<VkSurfaceKHR>(2), "", e));
    SR_CHECK(e.operation == "CreateDevice");
    SR_CHECK_EQ(e.result, VK_ERROR_OUT_OF_DEVICE_MEMORY);
    SR_CHECK(requested_features_seen == nullptr);
  }
  SR_CHECK(events == std::vector<std::string>({"create", "create-device-failed",
                                               "surface", "destroy"}));
}
SR_TEST(required_mirror_clamp_extension_is_not_silently_ignored) {
  events.clear();auto f=Fake();
  f.vkEnumeratePhysicalDevices=OneDevice;f.vkGetPhysicalDeviceProperties2=DeviceProperties;
  f.vkEnumerateDeviceExtensionProperties=DeviceExtensions;f.vkGetPhysicalDeviceQueueFamilyProperties=DeviceFamilies;
  f.vkGetPhysicalDeviceSurfaceSupportKHR=DevicePresent;f.vkCreateDevice=FailedDevice;
  Context c(f);Error e;SR_CHECK(c.CreateInstance({},false,e));
  SR_CHECK(!c.OpenDevice(reinterpret_cast<VkSurfaceKHR>(2),"",e,nullptr,true));
  SR_CHECK(e.operation=="Device extension");SR_CHECK(e.result==VK_ERROR_EXTENSION_NOT_PRESENT);
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
static VkResult VKAPI_CALL
CreateDebug(VkInstance, const VkDebugUtilsMessengerCreateInfoEXT *,
            const VkAllocationCallbacks *, VkDebugUtilsMessengerEXT *h) {
  *h = reinterpret_cast<VkDebugUtilsMessengerEXT>(4);
  return VK_SUCCESS;
}
static void VKAPI_CALL DestroyDebug(VkInstance, VkDebugUtilsMessengerEXT,
                                    const VkAllocationCallbacks *) {}
SR_TEST(validation_callback_contains_logger_exceptions) {
  auto f = Fake();
  f.vkCreateDebugUtilsMessengerEXT = CreateDebug;
  f.vkDestroyDebugUtilsMessengerEXT = DestroyDebug;
  Context c(f);
  Error e;
  debug_available = true;
  bool created = c.CreateInstance({}, true, e);
  debug_available = false;
  SR_CHECK(created);
  c.logger = [](auto &) { throw std::runtime_error("logger failed"); };
  VkDebugUtilsMessengerCallbackDataEXT data{
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT};
  data.pMessage = "test validation error";
  bool escaped = false;
  try {
    debug_callback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &data,
                   debug_user);
  } catch (...) {
    escaped = true;
  }
  SR_CHECK(!escaped);
  SR_CHECK_EQ(c.validation_errors.load(), 1);
}

SR_TEST(optional_device_features_are_explicit) {
  auto f = Fake();
  f.vkEnumeratePhysicalDevices = OneDevice;
  f.vkGetPhysicalDeviceProperties2 = DeviceProperties;
  f.vkEnumerateDeviceExtensionProperties = DeviceExtensions;
  f.vkGetPhysicalDeviceQueueFamilyProperties = DeviceFamilies;
  f.vkGetPhysicalDeviceSurfaceSupportKHR = DevicePresent;
  f.vkCreateDevice = FailedDevice;
  Context c(f);
  Error e;
  SR_CHECK(c.CreateInstance({}, false, e));
  VkPhysicalDeviceFeatures features{};
  features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
  SR_CHECK(!c.OpenDevice(reinterpret_cast<VkSurfaceKHR>(2), "", e, &features));
  SR_CHECK(requested_features_seen == &features);
}
