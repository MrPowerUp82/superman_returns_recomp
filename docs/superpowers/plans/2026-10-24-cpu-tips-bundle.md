# CPU tips bundle implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Assemble the four concrete renderer CPU tips from the private-fork report into one D3D12 candidate before further gameplay benchmarking.

**Architecture:** Reuse bounded PM4 reads, verified texture write tracking and the bounded SRV cache. Finish the mapped-upload audit (gamma readback and CPU assembly of shared constants), and key resolve resource reuse by complete allocation/interpretation identity. Expose one opt-in bundle while preserving the normal renderer defaults and existing fallbacks.

**Tech Stack:** C++20, D3D12, existing native tests and write-tracking SDK.

**Spec:** `docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md`, CPU milestone; user steering on 24/10: apply the tips together before testing.

## Constraints and rulings

- No more individual gameplay/FPS trials. Defer verification until all four paths and the bundle are assembled. The user's batching instruction overrides the usual per-change RED/GREEN order; do not claim a RED run for this increment.
- Preserve fences, resource ownership, byte order, versions, null descriptors and unavailable-tracking hash fallback.
- Gamma must be assembled/inspected in CPU memory; mapped upload destinations are write-only in normal rendering paths.
- A resolve identity includes physical base, format, full logical width/height, exact scale bits, layers, volume/depth/shadow interpretation. IDs are stable and never reused during a renderer lifetime; latest-address bindings remain separate.
- Bundle Off remains default. Bundle On selects texture tracking On, bounded ALU reads, CPU shared staging, complete resolve identity and at least 4096 retained buffer SRVs. An SDK without tracking support must report fallback rather than silently omit validation.
- Native PSO precompilation already exists and is enabled by default; do not claim it was absent. Full bypass of all original XDK routines and the four alleged thread limiters are separate unproven contracts, not implemented by toggling this bundle or patching 1/30 animation constants.

## Tasks

- [x] Finish mapped-upload normal-path audit; implement gamma CPU assembly and staging of shared constants without enabling the shared comparison cache.
- [x] Implement and integrate complete resolve identity, with diagnostic filenames independent of packed legacy keys.
- [x] Add the combined D3D12 cvar, log effective features and prepare a reproducible combined launch/benchmark command.
- [x] After all code is assembled, compile/test contracts (gamma flags/bytes, identity reuse/collisions/variants, combined option policy), build the game, and request independent review.
- [x] Only after the candidate is complete, consider one combined gameplay comparison, with scene/image limitations reported. Do not represent the full private fork as reproduced.

## Additional completed first-launch preparation

Existing PipelineCache had no embedded seed in the measured build. Added optional CNPS seed packer, CMake RCDATA 3 integration and SR_PIPELINE_SEED build option. Three Python tests passed; the PE resource matched the 376-record seed hash. The single final bundle run confirmed 376 embedded/0 local records before precompilation. No driver cache was deleted, and the original local pipeline cache was restored and hash-verified afterwards.

Result: all four concrete renderer CPU tips selected together, final run approximately22FPS; 60FPS, full XDK bypass and the alleged four thread limiters remain unachieved/unconfirmed. See `docs/d3d12-cpu-bundle-2026-10-24.md` for measured limits and next costs.
