#include "win32_surface.h"
namespace superman_returns::graphics::vulkan {
Win32Window::~Win32Window() {
  if (window_)
    DestroyWindow(window_);
}
LRESULT CALLBACK Win32Window::Procedure(HWND window, UINT message, WPARAM w,
                                        LPARAM l) {
  auto *self =
      reinterpret_cast<Win32Window *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<Win32Window *>(
        reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (self && (message == WM_CLOSE || message == WM_DESTROY)) {
    self->closed_ = true;
    return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
bool Win32Window::Open(uint32_t width, uint32_t height, Error &e) {
  auto instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = Procedure;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.lpszClassName = L"SRVulkanFoundation";
  if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    e = {"RegisterClass", VK_ERROR_INITIALIZATION_FAILED,
         "Unable to register Vulkan test window"};
    return false;
  }
  RECT rect{0, 0, LONG(width), LONG(height)};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  window_ = CreateWindowExW(
      0, wc.lpszClassName, L"Superman Returns — Vulkan M1 (triangle test)",
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
      rect.bottom - rect.top, nullptr, nullptr, instance, this);
  if (!window_) {
    e = {"CreateWindow", VK_ERROR_INITIALIZATION_FAILED,
         "Unable to create Vulkan test window"};
    return false;
  }
  ShowWindow(window_, SW_SHOW);
  return true;
}
VkSurfaceKHR Win32Window::CreateSurface(Context &c, Error &e) {
  auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
      c.Proc()(c.instance, "vkCreateWin32SurfaceKHR"));
  if (!create) {
    e = {"Win32 surface", VK_ERROR_EXTENSION_NOT_PRESENT,
         "Win32 surface entry point unavailable"};
    return VK_NULL_HANDLE;
  }
  VkWin32SurfaceCreateInfoKHR ci{
      VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
  ci.hinstance = GetModuleHandleW(nullptr);
  ci.hwnd = window_;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!Check(create(c.instance, &ci, nullptr, &surface), "CreateWin32Surface",
             e))
    return VK_NULL_HANDLE;
  return surface;
}
WindowEvents Win32Window::PumpEvents() {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT)
      closed_ = true;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  RECT rect{};
  GetClientRect(window_, &rect);
  return {IsIconic(window_)
              ? VkExtent2D{}
              : VkExtent2D{uint32_t(rect.right), uint32_t(rect.bottom)},
          closed_};
}
void Win32Window::Resize(uint32_t width, uint32_t height) {
  RECT rect{0, 0, LONG(width), LONG(height)};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  SetWindowPos(window_, nullptr, 0, 0, rect.right - rect.left,
               rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
}
void Win32Window::Minimize() { ShowWindow(window_, SW_MINIMIZE); }
void Win32Window::Restore() { ShowWindow(window_, SW_RESTORE); }
void Win32Window::WaitEvents() {
  MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
}
} // namespace superman_returns::graphics::vulkan
