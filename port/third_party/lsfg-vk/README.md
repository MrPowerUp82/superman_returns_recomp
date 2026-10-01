# Vendored LSFG-VK backend

Source: https://github.com/NaGaa95/NetherSX2_nx
Revision: f084dc1038c8c67cdff0fa9ad109bce3efe58f22
Subtree: third_party/lsfg-vk
License: GPL-3.0-or-later (see LICENSE.md and source SPDX headers).

Contains source code and open-source embedded replacement shaders from that
repository. Does not contain Lossless.dll or shaders extracted from that DLL.

Local changes:
- Load the Windows Vulkan runtime from System32 on demand.
- Request Win32 external-memory/semaphore extensions on Windows.
- Select the Vulkan physical device by the exact D3D12 adapter LUID.
- Use the Windows temporary directory for the compute pipeline cache.
- Avoid Win32 CreateSemaphore macro collisions in the Vulkan dispatch table.
- Expose the command-buffer handle for host integration.
- Borrow compute devices without requiring Vulkan swapchain entry points;
  presentation will remain owned by D3D12.

The Linux file-descriptor transport is not used by the Windows bridge. That
bridge imports D3D12 resources and fences with Win32 handles separately, then
copies GPU images to/from the backend's local context.
