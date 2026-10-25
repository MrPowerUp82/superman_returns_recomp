# Static unchanged-world shader dependencies — 2026-10-25

## Scope and result

`static_world_shader_dependencies.h` accepts four source-audited rigid
families and preserves the existing two instanced families. This extension
adds `C5E8551EFDE01162` and `C63F6F22679609F0` to both existing overloads.
Only this document, the existing helper, and its existing native test source
are edited. No renderer, history, CMake, shared build, game, or commit changes
are part of this work.

The reported OPAQUE depth-writer counts (73 C5E8 draws, 40 C63F draws) identify
candidate shader families only. They do not establish actor identity,
terrain, material, camera, pass semantics, static resource contents, or
runtime correctness. Neither new source has a skin/animated branch.

**API need:** the returned 16 clip words are the matrix **before shared XY
fixups**, not the complete final clip dependency set. Actual uploaded
`g_ScreenXform.xyzw` and `g_HalfPixelOffset.xy` must be supplied/tracked by
integration, or independently proved invariant in its capture domain.
This API cannot receive those six float words. Consumers needing complete
final clip correspondence must reject when that external proof is missing;
helper success alone is insufficient. No mirror fallback or guessed values
are added. Final Z and W in both new sources have no extra post-WVP input.

## Audited local artifacts

Paths below are relative to `artifacts/shaders/hlsl/`; line numbers are
one-based in the complete `<hash>.vs.hlsl` files. Complete-file SHA-256:

```text
4F17209D364D5B51  316A81ECAC0233CD10AF8584B968C14FCDCEF01DFBF8B56D98DA244FC7B29D53
18B4F69400B7F0AF  FAE9ED130A1D1C800D8AE42737418C31AF81AADF90F0CE4A943CCAB680506BB9
29C1ACD9F4373197  994E401FD9F5F6E24E87A44E8C3DC6B71FC73D0C727F23769FECBBD4AC695C2A
C79CC997C897773E  B3B1758AA27107630355F0F43824A9D7DAB1F5DA1EC4874777519D151B7AF582
C5E8551EFDE01162  3E1ABC7982454DD6EEA572C3B282A8B92E9F6702641453AF2FF004D36FA62623
C63F6F22679609F0  E2135E02AAD73F8E7E666A412E383825E322DCB18F619BAE9F6D9FF95F0A448C
```

The hash allowlist is a source contract, not a runtime source-file checksum.
Keep source replacements disabled or audit the equivalent world AND final
clip chain of the actual loaded shader before using it.

## Backward audit of C5E8551EFDE01162

| Sink / dependency | Exact source evidence |
| --- | --- |
| Exported world | 996: `oTexCoord2.xyzw = max(r0.xyzw, r0.xyzw)` |
| World row dots | 951, 958, 964, 971 assign `r0.w/z/y/x` from `gWorldXform(3/2/1/0)` dotted with `r6.zxyw` |
| World aliases | 588, 595: `gWorldXform(i) = CONST_REL(1 + i)`; world bank is c1–4 |
| Local position | 886–887: `r6.xyz = srVertexInput(0, iPosition0).xyz`; `r6.w = 1.0` |
| Clip row dots | 894, 898, 902, 906 assign `oPos.x/y/z/w` from `gWorldViewProjectionXform(0/1/2/3)` and the same local `r6.zxyw` |
| Clip aliases | 594: `gWorldViewProjectionXform(i) = CONST_REL(5 + i)`; object-to-clip bank is c5–8 |
| Final clip | 1056–1059: shared screen transform of XY, then shared half-pixel XY addition, both scaled by clip W; no further Z/W assignment |

The source reads local XYZ once and never rewrites `r6` before either chain.
The paired `.zxyw` swizzles preserve canonical row-dot XYZW ordering.
World `r0.xyz` is exported as TEXCOORD2 before shading later overwrites `r0`.
All four world rows, including their translation W components and the
fourth row, are retained conservatively. Final Z is the c7 row-dot and final
W the c8 row-dot. These are object-to-clip WVP, not a world-to-clip camera.

`Kramp_lo_hi_Kao` is c0 (589); its reads at 952, 965, 972 and their subsequent
scalar chain feed shading, not `r6`, the exported world value, or `oPos`.
The inverse-transpose c9–12 (593; 911–946), inverse-view c13–16
(592; 977–984), light constants (988, 1013), and shadow rows (1000–1008)
also do not feed either position sink. c0 is therefore excluded even if
nonfinite; c4 is included as the fourth world row. There is no dynamic
relative palette address, boolean selector, or runtime loop in the main body.

## Backward audit of C63F6F22679609F0

| Sink / dependency | Exact source evidence |
| --- | --- |
| Exported world | 998: `oTexCoord1.xyzw = max(r0.xyzw, r0.xyzw)` |
| World row dots | 946, 951, 956, 961 assign `r0.w/z/y/x` from `gWorldXform(3/2/1/0)` dotted with `r1.zxyw` |
| World aliases | 588, 595: `gWorldXform(i) = CONST_REL(0 + i)`; world bank is c0–3 |
| Local position | 881–882: `r1.xyz = srVertexInput(0, iPosition0).xyz`; `r1.w = 1.0` |
| Clip row dots | 889, 893, 897, 901 assign `oPos.x/y/z/w` from `gWorldViewProjectionXform(0/1/2/3)` and local `r1.zxyw` |
| Clip aliases | 593: `gWorldViewProjectionXform(i) = CONST_REL(8 + i)`; object-to-clip bank is c8–11 |
| Final clip | 1068–1071: shared screen transform then half-pixel addition of XY only; no further Z/W assignment |

Local `r1` is unchanged through both matrix chains. Its overwrite beginning
at 994 occurs after both chains; it cannot change `r0` before export or the
already written `oPos`. World `r0` is not overwritten until 1029. Final Z is
the c10 row-dot, W the c11 row-dot. World-view c4–7 (594; 977–989),
inverse-transpose c12–15 (592; 906–941), inverse-view c16–19
(591; 993, 1016, 1023), light and shadow constants feed other varyings only.
Normals, tangent, texture coordinates and color do not deform position.
The main body has no boolean, loop or skin/palette dependency.

## Shared inputs and final clip boundary

Both complete files define `CONAN_RECOMP` at line 1. At 383–389,
`srVertexInput(0, iPosition0)` consults `g_VertexFetch(0).w`: type `0x2C2259`
restores unsigned scaled 16-bit XY by rounding `value.xy * 65535`, returns
Z=0, W=1; other types return the input. This is a real shared fetch dependency
for world AND clip. The parent's canonical fetch/declaration/resource
signature must cover the actual uploaded metadata and input bytes; this
helper does not infer a format from the hash. `g_VertexFetchArr` starts at
shared c32 / byte 512 (143, 148; byte-address definition 95).

For D3D12, SharedConstants is b2, space4 (601–604). The definitions at
138–145 place half-pixel XY at c18.yz (bytes 292, 296) and screen XYZW at
c27 (bytes 432–444). The shared byte-address aliases at 93 and 97 agree.
The final shader operations, in order, are:

```text
clip.xy = clip.xy * screen.xy + screen.zw * clip.w
clip.xy += half_pixel.xy * clip.w
```

There is no Z remap, depth bias, deformation, or boolean-conditioned clip
rewrite in either audited main body. Nevertheless, the six shared float
words are outside the VS float bank, and are not encoded in `clip_rows` or
`position_words`. A future shared-input API or existing external proof must
use the values actually bound for this draw; reading a possibly different
mirror state is not proof. Unproven/nonfinite shared fixups or fetch metadata
must keep the consuming final-clip path unverified. No new API taking
conditional rigid bool/loop state is needed for these two branchless shaders.

## Preserved source domains

The older rigid world/clip contracts remain unchanged:

| Source | Local position | World / export | Object-to-clip | Shared XY fixups |
| --- | --- | --- | --- | --- |
| 4F17209D364D5B51 | 879–880 | 944–959 c0–3; 996 TEXCOORD1 | 887–899 c8–11 | 1066–1069 |
| 18B4F69400B7F0AF | 873–874 | 917–932 c0–3; 969 TEXCOORD1 | 880–892 c8–11 | 1028–1031 |

The older aliases at 590–593 establish world c0–3, world-view c4–7,
WVP c8–11 and inverse-view c12 onward. No accepted rigid main body reads
`BOOL_BIT`, `g_LoopConstant`, or a palette; their declarations are not reads.

29C1/C79C remain accepted only by the verified-range overload. In both files,
587 and 589 alias world-to-clip c0–3 and relative instance rows starting
at c100. At 901–916, the per-instance row pair builds internal world position
in `r1.yzx`; 920–932 projects it. TEXCOORD1 is lighting, not exported world.
Their final shared XY fixups are 29C1 1011–1014 and C79C 1006–1009.
The existing helper's precise-fdiv address envelope, c4.xy remap, two-row
instance addressing, reachable XYZ and lighting-W exclusion remain intact.
No copied range algorithm or wider temporal-history allowlist is introduced.

## API and bounded representation

Both existing signatures are preserved: 1024 VS float words, four VS boolean
words, 32 VS loop words, and result; the range overload additionally takes
`normalized_vertex_first` and exclusive `normalized_vertex_end`.

| Result field | Meaning |
| --- | --- |
| `position_words` | Existing fixed 1200-word array, value-initialized; hash this array, not the padded struct |
| `[0..15]` rigid | Exact four world rows in bank order, normalized to signature offset zero: c1–4 for C5E8, c0–3 otherwise |
| `[16..17]` | Low/high source-hash words; distinct shader domains remain distinct |
| `[18]` | Existing schema **2**, unchanged for prior families |
| `[19]` | C5E8: 2; other rigid: 1; instanced: UINT32_MAX (no exported world varying) |
| `[20..655]` instanced | Existing 636-word audited range signature; no new range algorithm |
| Other position words | Zero; all rigid position data fit the first 20 words |
| `clip_rows` | Exact 16 pre-shared-fixup matrix words, separate from world equality |
| `clip_first_row` | C5E8: 5; other rigid: 8; instanced: 0 |
| `clip_matrix_kind` | Rigid: `kObjectToClip`; instanced: `kWorldToClip` |

All accepted position dependencies remain bounded by the existing
1200-word storage (also the 640-word compact budget for rigid signatures);
no struct enlargement or schema migration is needed. Old family bits and
both overloads' behavior remain unchanged. Instanced envelopes use the
frontend's verified, normalized SV_VertexID interval `[first,end)` (main
command u6/u7 for indexed draws), after base-vertex/restart normalization.
Empty, inverted, or 24-bit-mask-wrapping instanced envelopes are rejected.
Rigid shaders do not consume the initialized SV_VertexID, so the range
arguments are ignored when dispatching them to the no-range overload.

Unknown hashes and any nonfinite read world/clip word return false without
changing any result field. Unread shading values, including NaNs, and all
boolean/loop words are ignored. Success preserves signed zero, subnormals,
and exact finite bits; singular matrices are allowed. There is no matrix
inversion or inferred shared camera. Finite inputs do not guarantee finite
shader output; consumers must still validate captured positions.

Unchanged-world comparison uses `position_words`, not whole-struct equality:
clip-only changes must not invalidate the world signature. Combine it with
canonical mesh/fetch/topology/order, actual bound buffers, resource and upload
versions, and the capture domain (`SourceShapeInputs` integration).
Shader hashes do not establish moving/deforming identity or correspondence
across different world signatures. Dynamic generations remain unverified.

## Rejection and integration handoff

Other hashes remain fail-closed, including 9A62/008A (different dynamic
fetch/remapping), and 32EA/4063/7704/9B2F/C687/C209/E57F/EC4D
(skin/animated or separately unaudited chains). E57F changes projected Z
after its WVP dots (1338–1362); it cannot borrow this rigid proof.
The existing 2C45 range helper remains separate and is not newly accepted
by the no-range API. Future conditional rigid support requires actual
uploaded selectors and all reachable inputs, not assumed mirror values.

`WorldPositionTexcoord` in `post_vertex_geometry.h` currently returns
UINT32_MAX for BOTH newly accepted hashes. This helper returns the audited
TEXCOORD index, but no external capture allowlist or geometry HUD is edited.
The parent must integrate C5E8→2 and C63F→1 where appropriate, preserve the
shared-fixup/fetch proof gates, and obtain fresh GPU evidence. In particular,
never submit rigid c0–3 or c5–8/c8–11 as a verified common camera matrix.
Parent review remains the acceptance gate for integration.

## Isolated validation

Outputs live only under `%TEMP%/sr-static-world-extension-20261025-isolated`.
The existing registered 14-test source is expanded with independent literal
bank/TEXCOORD expectations for all four rigid families. The checks cover
all 16 world words, all 16 clip words including Z/W, zero signature tail,
all six rigid source-domain pairs with identical matrix values, unread
shading and unused boolean/loop banks, exact finite bits and immutable inputs,
384 nonfinite rigid shader/word/value combinations with atomic failure,
and rigid overload equivalence. Existing instanced range, precise-division
boundary, reachable XYZ/light-W exclusion and rejection checks are retained.

TDD observed before helper edits: **14 tests, 22 failed checks**, exclusively
from missing support for the two new hashes. After implementing the audited
bank dispatch: **14 tests, 0 failed checks**, Clang 23.1.2, C++20,
`-Wall -Wextra -Werror`. The later source-domain and rigid-range assertions
exercise the same accepted behavior.

From the repository root, standalone reproduction:

```powershell
$testDir = Join-Path $env:TEMP 'sr-static-world-extension-20261025-isolated'
New-Item -ItemType Directory -Force -Path $testDir | Out-Null
& ./.tools/clang+llvm-23.1.2-x86_64-pc-windows-msvc/bin/clang++.exe `
  -std=c++20 -Wall -Wextra -Werror `
  tests/native/main.cpp tests/native/test_static_world_shader_dependencies.cpp `
  -o "$testDir/static_world_extension_tests.exe"
if ($LASTEXITCODE -ne 0) { throw 'Standalone compile failed' }
& "$testDir/static_world_extension_tests.exe"
if ($LASTEXITCODE -ne 0) { throw 'Standalone tests failed' }
```

The full existing `tests/native` CMake project was also configured and built
with Ninja/Clang in that directory's `native/` subdirectory, Release mode.
CTest: **3/3 passed** (`sr_native_tests`, `sr_dlss_contract_tests`,
`sr_capture_budget`); the native harness reports **378 tests, 0 failed checks**.
The shared project and existing build directories were untouched. The broader
build emits unrelated existing `getenv` deprecation / missing aggregate-field
initializer warnings; the standalone extension build passes with `-Werror`.

An independent read-only review of the helper, tests, document and both new
HLSL artifacts found **no Critical or Important issues**. It independently
confirmed the bank offsets, world exports, full clip Z/W chains, source
fingerprints and external shared-input requirements. Parent review remains
required before renderer integration.

No gameplay or GPU validation has been performed for this extension.
