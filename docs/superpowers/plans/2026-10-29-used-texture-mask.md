# Used Texture Mask Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans with a bounded decoder worker and fresh final review. Track evidence below.

**Goal:** Skip preparation of textures not referenced by the active shader pair.
**Architecture:** Analyze original container microcode once, publish conservative immutable usage, use the union per draw, and retain full processing on unknown/diagnostic paths. Do not change shader binary formats.
**Tech Stack:** C++20, native D3D12/Vulkan, CMake/Clang, local shader corpus.
**Spec:** `docs/superpowers/specs/2026-10-29-used-texture-mask-design.md`

## Global Constraints

- Preserve all earlier uncommitted changes and work in the existing checkout.
- Unknown/malformed/unsupported shader usage falls back to all slots.
- Known empty usage is distinct from unknown; include all control-flow branches.
- Preserve host variants, diagnostics, immutable captures and resource lifetime.
- No automatic commit, push or dependency installation.

## Review Focus

- A PS-only mask must never hide a VS texture fetch.
- Unknown usage must never become an empty mask.
- Reused shader objects and recorded commands must retain their own mask.
- Re-enabling a previously unused slot must validate current contents/resolves.
- Filtering must preserve null bindings and extra textures of host variants.

## Task 1: Conservative usage decoder

Files: `port/src/graphics/guest/shader_texture_usage.h`, `tests/native/test_shader_texture_usage.cpp`.

Interface: `ShaderTextureUsage { uint32_t mask; bool known; }`; `AnalyzeShaderTextureUsage(std::span<const uint8_t> container)` returns known only on complete supported microcode analysis.

- [x] Add failing tests for supported fetches/branches and conservative rejection.
- [x] Implement bounded container/microcode analysis with source attribution.
- [x] Compile focused tests and compare all corpus masks with TFETCH metadata.

## Task 2: Immutable publication and renderer integration

Files: shader capture/registry, native renderer, neutral replay, native/Vulkan tests and CMake.

- [x] Add failing tests for union, absent PS versus unknown PS, disabled mode, and captured replay.
- [x] Publish usage once in registered captures; integrate a restart control and per-draw mask.
- [x] Skip unused D3D12 slots with null descriptors; filter neutral capture and replay consistently.
- [x] Preserve all slots for special host variants and diagnostics, and add skip counters.
- [x] Run native and Vulkan regressions, build Release, compare On/Off.

## Task 3: Review and results

- [x] Independent review, fix concrete findings and verify final diff.
- [x] Record tests, actual benchmark results and image-validation limitations.

## Execution ledger

User authorized starting the recommended first candidate. Reuse the existing checkout to preserve its installed SDK, game data and prior changes. Decoder work is independent of renderer plumbing; parent owns plumbing/CMake and final integration.


Final ledger: decoder RED 49 failed checks -> GREEN 21 cases; corpus 241/241 exact, ASan+UBSan 1,558,689 calls without diagnostics. Policy RED missing header; replay RED 3 failed checks -> GREEN. 600 native tests, CTest 5/5; 108 Vulkan tests; tracking CTest4/4; Release build passed. Frozen Vulkan pixel comparison and resolve-slot reactivation passed. Independent review found no actionable defect; final guards conservatively bypass diagnostics and capture mask with shader identity. Three D3D12 pairs and one Vulkan pair ran, with an extreme 22-second tracking-query stall in one On run; no stable FPS benefit claimed. Results/limitations: docs/used-texture-mask-2026-10-29.md. No commit or push.
