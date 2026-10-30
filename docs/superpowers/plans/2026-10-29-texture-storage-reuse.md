# Texture Storage Reuse Implementation Plan

**Goal:** Share CPU texture backing only across sampler-only fetch variants.
**Spec:** `docs/superpowers/specs/2026-10-29-texture-storage-reuse-design.md`
**Architecture:** Conservative full-word storage key, D3D12 shared entries, Vulkan shared captures plus same-version resource aliases; exact per-draw fetches remain intact.

## Tasks

- [x] Test first: sampler-bit equivalence and every other bit distinct; disabled mode exact; malformed fetches conservative.
- [x] Add shared identity helper and environment override; final default On D3D12, Off Vulkan after repeated negative Vulkan A/B.
- [x] D3D12: normalized backing lookup with exact collision check; retain original binding and tracking semantics; count variant reuse.
- [x] Vulkan: normalize front capture-cache lookup, keep original fetches, alias only matching storage, version and snapshot through ResourceStore ownership.
- [x] Test actual decoder/ranges, distinct samplers, content/version changes and pending resource lifetime with GPU readback.
- [x] Build Release and run native, Vulkan, memory tracking suites; compare D3D12 pair and two alternating Vulkan pairs; document evidence/limits and keep Vulkan opt-in after repeated slowdown.

## Constraints and review

Preserve non-sampler bits and both mip addresses; do not infer equal content from guest address alone. Unknown/disabled path remains conservative. Sampler filters must remain different while backing shares. Never reuse a newer version for an older queued draw, mutate a resource used by an earlier draw, or turn a GPU resolve into a CPU texture. Keep prior changes and report unresolved measurements.

## Completed evidence

Native 604 checks/cases with no failed checks, CTest 5/5; Vulkan CPU CTest 1/1;
tracking CTest 4/4 (36 main-process tests including actual decoder equivalence).
GPU point/linear and immutable content versions passed inline/merged/global
On/Off; final merged/global also passed default Vulkan Off. Independent source
reviews found no production issues; the merged-upload test gap was closed.
D3D12 pair 33.0105 Off / 37.5581 On remains preliminary. Vulkan two pairs
25.2647/22.4662 and 26.8126/21.7659 Off/On regressed, hence opt-in by default.
Full report: `docs/texture-storage-reuse-2026-10-29.md`.
