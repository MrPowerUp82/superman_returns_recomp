#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "context.h"
#include <windows.h>
namespace superman_returns::graphics::vulkan {
struct WindowEvents {
  VkExtent2D extent;
  bool close_requested;
};
class Win32Window {
public:
  Win32Window() = default;
  ~Win32Window();
  Win32Window(const Win32Window &) = delete;
  Win32Window &operator=(const Win32Window &) = delete;
  bool Open(uint32_t, uint32_t, Error &);
  VkSurfaceKHR CreateSurface(Context &, Error &);
  WindowEvents PumpEvents();
  void Resize(uint32_t, uint32_t);
  void Minimize();
  void Restore();
  void WaitEvents();

private:
  HWND window_ = nullptr;
  bool closed_ = false;
  static LRESULT CALLBACK Procedure(HWND, UINT, WPARAM, LPARAM);
};
} // namespace superman_returns::graphics::vulkan
