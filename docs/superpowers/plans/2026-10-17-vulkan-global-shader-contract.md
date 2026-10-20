# Vulkan global shader contract implementation plan

**Goal:** Add an explicit offline compiler profile for the global resource ABI
validated by the native 60 FPS investigation. Keep the default compiler and
runtime on their existing ABI until the renderer owns the new resources.

**Architecture:** Adapt the portable common header in memory, preserving shader
entry points and varyings. Give the profile a distinct ABI, backend, cache
directory and compile arguments. Cache the actual adapted header digest.

**Scope:** The user has already authorized and repeatedly requested continuation
of the structural Vulkan work. Execute inline; no additional approval gate.
Evidence and limitations are recorded in
`docs/native-vulkan-global-contract-2026-10-17.md`.

- [x] Write cache/profile tests covering separation, corrupt metadata, header
  fingerprints and failure on an unsupported common header. Observe failure.
- [x] Add `tools/shaders/vulkan_global_contract.py` for the header adaptation,
  resource layout and feature requirements; add an explicit `global` compiler
  profile in `compile_vulkan.py`, retaining portable defaults.
- [x] Run shader unit tests and compile the real local corpus through this
  supported profile; compare interfaces and run `spirv-val`.
- [x] Archive the GPU readbacks, lifetime controls and their limitations.

**Review focus:** No portable cache entry can answer a global request. A cache
hit must have the requested backend and ABI. Unknown profiles fail before tool
execution. Input common and existing DXIL tools remain untouched. This change
does not activate global shaders in the game or claim 60 FPS.

## Execution record

- Continue in the existing authorized checkout to retain its prepared toolchain
  and all preceding native changes; created `codex/vulkan-global-contract`.
  Existing edits were preserved; no worktree duplication, reset or commit.
- Tests first: the missing profile/header module failed; implementation made
  all tests green. The global validator was added after its missing-function
  test failed. Full Python suite: 102 passed.
- Independent review: one P2 feature-declaration issue. Added the regression
  assertion, observed failure, added the core dynamic storage indexing feature,
  and reran the suite (102 passed) and both real corpus profiles (241/241 each).
- Review scope excluded actual game integration/FPS and runtime slot ownership;
  these are explicitly subsequent work, not claims of this offline change.
  Allocated descriptor counts are intentionally absent from shader metadata:
  the future heap chooses them, and the runtime must check physical limits.
- GPU proof with the product header: all48 readbacks and two pending-frame
  readbacks passed, Khronos validation active/errors0. Four negative controls
  failed at their intended pixel assertions. An invalid earlier event gate was
  replaced with a timeline semaphore and its failed log retained.
- Evidence snapshot: `artifacts/native-renderer-performance/global_contract_2026-10-17/`;
  includes a SHA256 manifest and explicitly requires the prepared checkout.
