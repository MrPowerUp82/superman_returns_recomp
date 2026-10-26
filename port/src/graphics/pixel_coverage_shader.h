#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace superman_returns::graphics {

// Capture-only specialization of original PS 559F/2203/E6C7/3EF6/E83A. This says nothing
// about motion correspondence, blending, material identity or scene coverage.
bool IsPixelCoverageShader(uint64_t ps_hash);
bool IsPixelCoverageShaderPair(uint64_t vs_hash, uint64_t ps_hash);

// Adds uint SV_Target1 and a b4,space4 ID constant AFTER original fragment
// discard/alpha logic. Caller owns an R32_UINT MRT, frame/background policy,
// matching PSO/root binding and original RT0/depth/stencil/sample state.
// The private MRT is UNBOUND for unsupported PS paths: positive IDs retain the
// LAST AUDITED writer, not the nearest writer across ALL original draws. Do
// not erase known audited coverage merely because an unsupported draw occurs.
// Temporal replay filters coverageId>0 through this map; id0 legacy opaque or
// unverified conservative invalidators bypass it. Retain EQUAL depth testing
// and original draw order. Unknown invalidators can overinvalidate alpha holes
// but must never qualify unknown motion. The retained map alone is not proof
// that later unsupported writers preserved the content: conservative unknown
// invalidators remain required. Missing quad/inline coverage is known partial
// incompleteness and keeps DLSS Off until separately qualified by the caller.
// Bounded structural/emitter checks are not a replacement for HLSL compilation.
// CRLF is normalized to LF; lone CR/control bytes and ambiguous shapes reject.
// Rejection clears output and supplies error; success clears error. Source may
// alias output. Supply distinct output/error objects.
bool SpecializePixelCoverageShader(uint64_t ps_hash, std::string_view source,
                                   std::string& output, std::string& error);

// Version, validation anchors and actual emitted patch bytes. Fingerprint this
// in a separate cache domain; ordinary PS DXIL is never the specialized PS.
std::string_view PixelCoverageShaderCacheIdentity();

}  // namespace superman_returns::graphics
