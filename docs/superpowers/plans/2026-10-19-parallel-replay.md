# Parallel replay implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline for this continuation.

**Goal:** distribute captured draw decoding over CPU cores while preserving ordered Vulkan submission, native scale 1 and real gameplay frames.

**Architecture:** PM4 scanning remains serial. Publish a compact immutable snapshot containing only registers the decoder consumes and shared normalized VS/PS banks. Persistent workers decode contiguous portions of each frozen batch; the caller joins before publishing packets in original order. Never share the mutable mirror or read live guest memory from workers.

**Tech stack:** C++20, std::thread/condition_variable, existing captured-memory and draw-arena ownership.

## Constraints and decisions

- Retain the serial path, default enabled; opt in via SR_NATIVE_REPLAY_THREADS=2 or 4 for measurement.
- No Vulkan calls from these threads, no sorting draws or skipping work, no ABI changes.
- One caller per decoder; workers finish before batch ownership can be released or decoder destroyed.
- Preparation errors terminate the prepared prefix; decode errors are returned at their original position. Runtime stops at the first failed result and publishes no later packets.
- Snapshot fetch registers, bool/loop values and written bits, polygon offset and scissor state. Do not copy the roughly 90 KiB Pm4Mirror per command.
- Retain all current source changes and executable snapshots; no commit or publication in this continuation.

## Tasks / verification

- [x] RED: serial-vs-parallel ordered output, changing/reused PM4 banks, fetch written bits and device fallback, captured overlays and indices, retained payloads across calls, errors and teardown. Independently assert literal constants as well as path equivalence.
- [x] Implement PrepareReplayCommand / DecodePreparedRenderPacket with compact replay state; preserve serial direct decoding and failure publication semantics.
- [x] Implement persistent coarse batch decoder (caller plus up to three workers), exception-safe joins and ordered results.
- [x] Integrate opt-in runtime and meaningful wall/CPU profiling. Extend release benchmark with equivalent 1/2/4-thread workload and checksums, including varied banks.
- [x] Full native/Vulkan suites, GPU contract validation, independent review and game build. Measure game without concurrent CPU work; preserve results, including any lack of benefit.

## Review focus

Snapshot ownership after mirror mutations/reset; thread-local declaration cache; immutable capture ranges; worker exception/deadlock paths; no publication after the earliest error; profiler sums versus elapsed wall time; stable payload aliases after batch return; shutdown while caller waits.

## Ledger

2026-10-19: continuing the already authorized CPU optimization effort. Current baseline SHA 62E34402..., clean gameplay 10.06 idle / 10.64 moving, not deterministic A/B. Worker ~49 ms and Record ~73 ms overlap; parallel replay alone cannot prove 60 FPS. Inline execution in the existing codex branch retains the uncommitted integrated renderer state.

Ruling: add opt-in coarse batch limits 128/256 — the 32-command fixture did not amortize dispatch; preserve 4 MiB pressure and explicit swap flushes. Both knobs remain off by default. Independent follow-up review found no actionable ordering/lifetime issue.

Verification: 195 native and 108 Vulkan tests passed; native CTest 2/2 and Vulkan CTest 1/1. Global and portable GPU contracts passed with validation, plus async recorder and merged-pass proofs. No deterministic allocation/thread-creation failure injection or race sanitizer.

Outcome: Release varied-device benchmark cycle 44.4065 -> 36.7813 ms at 256/4, but clean gameplay advance serial 11.33 FPS versus parallel 11.25 FPS. No demonstrated FPS gain. 60 FPS remains pending; keep serial default and concentrate subsequent work on resource/binding preparation and frontend. Full results in docs/native-vulkan-parallel-replay-results-2026-10-19.md.
