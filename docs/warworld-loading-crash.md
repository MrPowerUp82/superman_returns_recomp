# Warworld -> Metropolis loading crash (2026-10-26)

The October 30 crash **during the Mongul fight** has a different dump signature
in the XMA audio path; allocations succeeded. See
[Mongul XMA crash](mongul-xma-crash-2026-10-30.md). The transition fix below
remains in place.

The supplied `game.log` contains the same failure in two sessions:

| API | Time | Failure |
| --- | --- | --- |
| D3D12 | 21:36:27.086 | Physical allocation fails; a 0x0DFE54F2-byte read targets guest zero; guest reads 0x0000000C. |
| Vulkan | 21:48:19.370 | Same allocation, read size, and null-pointer failure. |

This is guest physical memory fragmentation during the level transition. The GPU
continues presenting after the guest failure. Missing optional cinematic AST
probes precede the failure, but do not establish its cause.

The size is present in the local `metropol.AST` index at offset `0x0EA4A8F3`,
immediately before `METROPOLISWORLDFLOW_RENDER`: little-endian `0x0DFE54F2`
(234,771,698 bytes, approximately 224 MiB). This identifies the level graphics
section being loaded; it is not evidence of a malformed movie size.

The diagnostic D3D12 startup reproduced a 299 MiB physical arena allocation:

```
size=12B00000 caller=82167FD0 result=ED1A6000
free_bytes=203059200 largest_free_bytes=203059200
callers=82167FD0 8218654C 8218D050 82149914 82468EA8
```

That leaves about 193.7 MiB at startup. This initial observation alone does not
explain the crash: the temporary boot arena is later released.

## Confirmed fragmentation and correction

The subsequent user reproduction in
`artifacts/warworld_diagnostic/logs/warworld_diagnostic.log` supplies both a
successful initial load and the failed reload:

| Event | Request | Total free after request | Largest free range |
| --- | --- | --- | --- |
| 22:48:19 initial Metropolis load | 0x0DFF0000 | 226,693,120 bytes | 226,557,952 bytes |
| 22:59:36 reload after Warworld | 0x0DFF0000 | 445,489,152 bytes | 189,194,240 bytes |

The failed request is about 224 MiB. About 425 MiB are free, but the largest
contiguous range is only about 180 MiB. The immediately following read identifies
`DATA\metropol.AST`, offset 7,786,496, with a null buffer and the expected section
length. Allocation callers include `82467F94 825FA3F0 82184024 826F01B8`.

`guest_physical_allocation.cpp` places unbounded allocations of at least 16 MiB
at the low end of the existing SDK physical heaps. This includes the temporary
boot arena and large streamed sections. Small allocations retain the runtime's
normal high-to-low search, so long-lived small buffers do not split the space
needed by subsequent large sections.

The change retains SDK page rounding, requested alignment, cache/access
protection, physical alias mapping and ordinary release/invalidation. The first
64 KiB of physical addresses are excluded to avoid a null GPU address. Fixed
physical addresses, small requests and unsupported protection flags use the
original XDK wrapper. The game's large-page disabling flag at `0x82976A94` is
preserved. A failed low-to-high allocation falls through to the original wrapper
for its normal failure/last-error behavior. `SR_LARGE_PHYSICAL_BOTTOM_UP=0`
restores the previous allocation policy for comparison.

`port/src/guest_memory_diagnostics.cpp` observes the original XDK allocation and
ReadFile wrappers. Allocations of at least 16 MiB and allocation failures log
request parameters and approximate physical free space. Large or null-buffer
reads log their source file, handle, position, and guest callers. The wrappers
keep the original I/O behavior. The allocation hook applies the policy above.
Free-space observations can change concurrently; largest free range is reported
before alignment.

The diagnostic executable and `testar_warworld.cmd` are staged locally in
`artifacts/warworld_diagnostic/`. The script uses the installed game data and
normal save directory, writes a separate diagnostic log, and accepts overrides
such as `--sr_native_api=d3d12`. It does not replace the installed executable.

`testar_correcao_warworld.cmd` launches the updated executable with a separate
`logs/warworld_fixed.log`. The original diagnostic log and the previous executable
(`superman_returns_before_physical_fix.exe`) are preserved.

## Validation

- The real stock SDK allocator reproduces the old failure in an automated
  boot-arena / small allocations / level load-unload-reload sequence, despite
  more than 400 MiB being free. The same test passes with the correction.
- Regression coverage also checks twenty load/unload cycles with a resident
  objective section and accumulating small buffers; 4 KiB, 64 KiB and 16 MiB
  pages; requested alignment; physical aliases; read-only/write-combined/no-cache
  protection; normal release; and no leaked reservation on allocation failure.
- The distribution executable compiles and links with D3D12 and Vulkan enabled.
  An independent read-only review found no actionable correctness issues.
- D3D12 and Vulkan runtime smoke loads the 224 MiB Metropolis section into
  `A1000000`, logs `allocation policy=bottom_up`, and continues presenting
  guest frames without a guest access violation or allocation failure.

The user confirmed that the previously failing mission-ending transition works
with the updated executable (API not specified in that confirmation). The
validated executable was installed in the normal game directory. Its SHA-256:
`44883ED4504133C71A01A97A04348C3DBB8F234C40371E4B09184ABD68676742`.
The previous installed executable is retained in `backup_warworld/` beside the
game. This validates the reported transition, not every gameplay scene.

Build the standalone regression suite with CMake using the installed SDK:

```
cmake -S tests/guest_memory -B build/guest-memory-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH=.tools/rexglue-sdk/win-amd64
cmake --build build/guest-memory-tests
ctest --test-dir build/guest-memory-tests --output-on-failure
```
