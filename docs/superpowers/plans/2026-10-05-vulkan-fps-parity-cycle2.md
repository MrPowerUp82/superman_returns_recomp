# Vulkan FPS Parity Cycle 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Achieve parity with D3D12 (≥ 29 FPS) on Vulkan by eliminating texture hashing overhead and dynamic heap allocations during command recording.

**Architecture:** Initialize `texture_watch_` for the Vulkan provider to enable fast physical memory write tracking. Replace `GameRenderer::Draw` dynamic containers (`std::vector`) with `std::array`. Convert transient buffer allocations into a flat linear allocator returning value structs. Shadow-cache Vulkan pipeline state commands (viewport, scissor, blend, stencil) to avoid redundant driver calls.

**Tech Stack:** C++, Vulkan 1.2+

**Spec:** `docs/superpowers/specs/2026-10-05-vulkan-fps-parity-cycle2-design.md`

## Global Constraints

- Manter a equivalência visual estrita (imagem idêntica ao D3D12).
- Aprovação nos testes nativos e de GPU (`native_tests`).
- Não quebrar ou alterar a inicialização do D3D12.

## Review Focus

- Modifying texture page without the watcher catching it, causing missing texture updates.
  Test: Run manual `SR_VULKAN_PROFILE=1` bench and assert visual equivalence and textures ms timing drop.
- Running out of pre-allocated ring buffer space during a frame with many draws.
  Test: Added assertions in `ResourceStore::UploadTransient` to fail safely if the linear allocator exceeds bounds.
- Pipeline state changes skipping necessary updates because the shadow cache wasn't invalidated properly (e.g., between frames or pipelines).
  Test: `GameRenderer::BeginSubmission` resets the state cache ensuring next pass updates state.
- Descriptor draw pooling reusing an object that is still in-flight on the GPU.
  Test: Pool reuse uses `completed_serial` which is tracked by the GPU fence in `DescriptorStore::Retire`.
- D3D12 initialization being affected by changes to `EnsureSystemWatchers` call logic.
  Test: Run D3D12 `tools\bench.ps1` to verify it still hooks memory watcher and runs normally without crashes.

---

### Task 1: Habilitar `texture_watch_` no Vulkan

**Files:**
- Modify: `port/src/native_renderer/native_renderer.h`
- Modify: `port/src/native_renderer/native_renderer.cpp`
- Modify: `port/src/native_renderer/native_graphics_system.cpp`

**Interfaces:**
- Produces: `void Renderer::EnsureSystemWatchers()`

- [ ] **Step 1: Write the manual test check**

There is no unit test for this step. Instead, run `tools\bench.ps1` with `SR_VULKAN_PROFILE=1` after implementation to verify `textures` phase takes < 2 ms.

- [ ] **Step 2: Add `EnsureSystemWatchers` declaration in `native_renderer.h`**

```cpp
  void EnsureSystemWatchers();
```

- [ ] **Step 3: Extract initialization logic in `native_renderer.cpp`**

Move the following block from `Renderer::EnsureInitialized` into `void Renderer::EnsureSystemWatchers()`:
```cpp
  if (REXCVAR_GET(sr_native_texture_watch) && !texture_watch_) {
    page_write_seq_ = std::make_unique<std::atomic<uint32_t>[]>(0x20000);
    for (uint32_t p = 0; p < 0x20000; ++p) page_write_seq_[p].store(0);
    REX_KERNEL_MEMORY()->RegisterPhysicalMemoryInvalidationCallback(&Renderer::OnPhysicalWrite,
                                                                     this);
    texture_watch_ = true;
  }
```
And add a call to `EnsureSystemWatchers();` inside `Renderer::EnsureInitialized()` where the block used to be.

- [ ] **Step 4: Call `EnsureSystemWatchers()` on Vulkan init**

In `port/src/native_renderer/native_graphics_system.cpp`, inside the Vulkan initialization branch, immediately after `provider_=graphics::vulkan::CreateNativeVulkanProvider(std::move(config));`, add:
```cpp
  Renderer::Get().EnsureSystemWatchers();
```

- [ ] **Step 5: Run D3D12 and Vulkan verification**

Run: `.\tools\bench.ps1` (with Vulkan and D3D12).
Expected: D3D12 doesn't regress. Vulkan shows improved `textures` timings and no visual artifacts.

- [ ] **Step 6: Commit**

```bash
git add port/src/native_renderer/native_renderer.h port/src/native_renderer/native_renderer.cpp port/src/native_renderer/native_graphics_system.cpp
git commit -m "feat(vulkan): enable texture_watch_ for fast texture tracking"
```

### Task 2: Gravação Zero-Allocation (Containers e Constant Buffers)

**Files:**
- Modify: `port/src/graphics/vulkan/game_renderer.cpp`
- Modify: `port/src/graphics/vulkan/resources.h`
- Modify: `port/src/graphics/vulkan/resources.cpp`
- Modify: `port/src/graphics/vulkan/descriptor_sets.h`
- Modify: `port/src/graphics/vulkan/descriptor_sets.cpp`

**Interfaces:**
- Produces: `struct TransientBuffer { VkBuffer handle; VkDeviceSize offset; };`
- Consumes: `TransientBuffer ResourceStore::UploadTransient(...)`

- [ ] **Step 1: Write `TransientBuffer` and update `ResourceStore` signature**

In `resources.h`, define `struct TransientBuffer { VkBuffer handle; VkDeviceSize offset; };`.
Change `std::shared_ptr<BufferResource> UploadTransient(guest::ResourceId, std::span<const std::byte>, VkDeviceSize reserve, Error&);` to `TransientBuffer UploadTransient(...)`.

- [ ] **Step 2: Implement Ring/Linear Allocator for `UploadTransient`**

In `resources.cpp`, change `UploadTransient` to use a flat array of mapped memory (e.g. advance an offset inside a large pre-allocated staging buffer, returning the binding parameters) instead of `std::make_shared<BufferResource>`. Fail if out of memory. Maintain lifetime properly using the existing serial fences.

- [ ] **Step 3: Abolish `std::make_shared<DescriptorDraw>`**

In `descriptor_sets.h`, remove `std::vector<std::shared_ptr<void>> resources;` from `DescriptorDraw`. Change `std::shared_ptr<DescriptorDraw> Prepare(...)` to `DescriptorDraw* Prepare(...)`.
In `descriptor_sets.cpp`, maintain a `std::vector<DescriptorDraw>` pool in `DescriptorStore` tied to the submission serial, and return pointers into it instead of allocating via `make_shared`. Handle retirement in `DescriptorStore::Retire`.

- [ ] **Step 4: Remove `std::vector` allocations in `GameRenderer::Draw`**

In `game_renderer.cpp`, change `std::vector<Binding> vertices;` inside `GameRenderer::Draw` to a stack-allocated array:
```cpp
struct Binding {uint32_t slot; TransientBuffer resource; VkDeviceSize offset;};
std::array<Binding, 32> vertices;
size_t vertex_count = 0;
```
Replace `vertices.push_back(...)` with `vertices[vertex_count++] = ...`.
Adjust `resources_.Buffer(...)` calls or transient uploads to handle the new `TransientBuffer` struct properties (e.g., `.handle` instead of `->handle`).

- [ ] **Step 5: Run tests**

Run: `pytest tests/vulkan/test_descriptors.cpp -v`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add port/src/graphics/vulkan/game_renderer.cpp port/src/graphics/vulkan/resources.* port/src/graphics/vulkan/descriptor_sets.*
git commit -m "perf(vulkan): replace dynamic allocations with transient linear allocator and stack arrays"
```

### Task 3: Shadow-caching do estado atual do pipeline

**Files:**
- Modify: `port/src/graphics/vulkan/game_renderer.h`
- Modify: `port/src/graphics/vulkan/game_renderer.cpp`

**Interfaces:**
- Consumes: `GameRenderer` state.

- [ ] **Step 1: Add shadow state variables to `GameRenderer`**

In `game_renderer.h`, under private members of `GameRenderer`, add:
```cpp
  VkViewport shadow_viewport_{};
  VkRect2D shadow_scissor_{};
  float shadow_blend_[4]{};
  uint32_t shadow_stencil_ = ~0u;
  bool shadow_valid_ = false;
```

- [ ] **Step 2: Invalidate cache on pass open**

In `game_renderer.cpp`, inside `GameRenderer::BeginSubmission` or wherever a new command buffer or pass is started, set `shadow_valid_ = false;`.

- [ ] **Step 3: Check and update shadow cache in `Draw`**

In `GameRenderer::Draw`, before calling `vkCmdSet*`, conditionally call them only if state changed:
```cpp
  if (!shadow_valid_ || memcmp(&shadow_viewport_, &viewport, sizeof(VkViewport)) != 0) {
    c_.f.vkCmdSetViewport(command, 0, 1, &viewport);
    shadow_viewport_ = viewport;
  }
  if (!shadow_valid_ || memcmp(&shadow_scissor_, &scissor, sizeof(VkRect2D)) != 0) {
    c_.f.vkCmdSetScissor(command, 0, 1, &scissor);
    shadow_scissor_ = scissor;
  }
  if (!shadow_valid_ || memcmp(shadow_blend_, blend, sizeof(float) * 4) != 0) {
    c_.f.vkCmdSetBlendConstants(command, blend);
    memcpy(shadow_blend_, blend, sizeof(float) * 4);
  }
  uint32_t stencil = draw.registers[0x10d] & 255;
  if (!shadow_valid_ || shadow_stencil_ != stencil) {
    c_.f.vkCmdSetStencilReference(command, VK_STENCIL_FACE_FRONT_AND_BACK, stencil);
    shadow_stencil_ = stencil;
  }
  shadow_valid_ = true;
```

- [ ] **Step 4: Run tests**

Run: `pytest tests/vulkan/ -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add port/src/graphics/vulkan/game_renderer.*
git commit -m "perf(vulkan): shadow-cache pipeline dynamic state"
```
