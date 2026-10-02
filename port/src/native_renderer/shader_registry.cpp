// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/shader_registry.cpp (Conan native renderer).
// The kit ships no license file at that revision; parts derived from Xenia /
// ReXGlue keep their BSD license. Changes for Superman Returns are listed in
// docs/native-port-plan.md section 2.
//
// Changes: hook addresses from game_profile.h (built only once confirmed),
// header validation (shader_container.h) and an optional dump of every
// container the game creates (sr_native_dump_shader_dir), which feeds
// tools/shaders/extract_shaders.py --dump-dir when the game data stores its
// shaders compressed.
#include "shader_registry.h"

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <rex/cvar.h>
#include <rex/hash.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "game_profile.h"
#include "native_bridge.h"
#include "shader_container.h"

REXCVAR_DEFINE_STRING(sr_native_dump_shader_dir, "", "Superman Returns Native",
                      "Write every shader container seen by the XDK CreateShader hooks to "
                      "<dir>/<hash>.<vs|ps>.bin (input for tools/shaders/extract_shaders.py)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace superman_returns::native {

namespace {
std::mutex g_mutex;
std::unordered_map<uint32_t, GuestShaderInfo> g_shaders;

// Kit: hash [c, c + virtual + physical) exactly like the corpus.
bool TryHashContainer(uint8_t* base, uint32_t container, GuestShaderInfo& info) {
  if (!container) return false;
  ShaderContainerHeader header;
  if (!ParseShaderContainerHeader(base + container, 0x100000000ull - container, header)) {
    return false;
  }
  info.is_vertex = header.is_vertex;
  info.container_hash = XXH3_64bits(base + container, header.total_size());
  return true;
}

// In standard XDK (Conan): the argument is the container pointer directly.
// In Superman Returns XDK 2.0.3529 LTCG: r4 is a descriptor holding MemStream
// structs; stream 1 at +20 (+0 buffer, +4 size) holds the compiled shader container.
uint32_t ResolveContainerAddress(uint8_t* base, uint32_t arg, GuestShaderInfo& info) {
  if (!arg) return 0;
  if (TryHashContainer(base, arg, info)) return arg;
  if (arg <= 0xFFFFFFFFu - 24) {
    const uint32_t stream1 = LoadBigEndian32(base + arg + 20);
    if (stream1 && stream1 <= 0xFFFFFFFFu - 64) {
      const uint32_t w0 = LoadBigEndian32(base + stream1);
      if ((w0 & 0xFFFFFF00u) == 0x102A1100u) {
        REXLOG_INFO("CONTAINER {:08X}:", stream1);
        for (uint32_t i = 0; i < 16; i += 4) {
          REXLOG_INFO("  +{:02X}: {:08X} {:08X} {:08X} {:08X}",
                      i * 4,
                      LoadBigEndian32(base + stream1 + i * 4),
                      LoadBigEndian32(base + stream1 + (i + 1) * 4),
                      LoadBigEndian32(base + stream1 + (i + 2) * 4),
                      LoadBigEndian32(base + stream1 + (i + 3) * 4));
        }
      }
      if (TryHashContainer(base, stream1, info)) return stream1;
    }
    const uint32_t stream0 = LoadBigEndian32(base + arg);
    if (stream0 && stream0 <= 0xFFFFFFFFu - 16) {
      if (TryHashContainer(base, stream0, info)) return stream0;
    }
  }
  return 0;
}

void DumpContainer(uint8_t* base, uint32_t container, const GuestShaderInfo& info) {
  static const std::string dir = REXCVAR_GET(sr_native_dump_shader_dir);
  if (dir.empty()) return;
  static std::mutex mutex;
  static std::unordered_set<uint64_t> written;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!written.insert(info.container_hash).second) return;
  }
  ShaderContainerHeader header;
  ParseShaderContainerHeader(base + container, 0x100000000ull - container, header);
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  char name[64];
  std::snprintf(name, sizeof(name), "%016llX.%s.bin", (unsigned long long)info.container_hash,
                info.is_vertex ? "vs" : "ps");
  const auto path = std::filesystem::path(dir) / name;
  if (std::FILE* f = std::fopen(path.string().c_str(), "wb")) {
    std::fwrite(base + container, 1, header.total_size(), f);
    std::fclose(f);
  }
}

// XDK shader creators: called by the game's effect/material loader.
// Returns the new D3D shader object in r3.
void OnCreateShader(PPCContext& ctx, uint8_t* base, void (*original)(PPCContext&, uint8_t*),
                    const char* role) {
  const uint32_t r3_in = ctx.r3.u32;
  const uint32_t r4_in = ctx.r4.u32;

  original(ctx, base);
  const uint32_t shader_obj = ctx.r3.u32;

  GuestShaderInfo info;
  uint32_t container = ResolveContainerAddress(base, r4_in, info);
  if (!container) container = ResolveContainerAddress(base, r3_in, info);

  if (r4_in && r4_in <= 0xFFFFFFFFu - 24) {
    const uint32_t stream1 = LoadBigEndian32(base + r4_in + 20);
    if (stream1 && stream1 <= 0xFFFFFFFFu - 16) {
      REXLOG_INFO("OnCreateShader POST role={} r3_out={:08X} stream1={:08X}: {:08X} {:08X} {:08X} {:08X}",
                  role, shader_obj, stream1,
                  LoadBigEndian32(base + stream1),
                  LoadBigEndian32(base + stream1 + 4),
                  LoadBigEndian32(base + stream1 + 8),
                  LoadBigEndian32(base + stream1 + 12));
    }
  }

  NoteHookCall(role, container, shader_obj);
  if (!container || !shader_obj) {
    if (shader_obj) {
      LogCaptureAnomalyOnce("CreateShader container not found", r4_in ? r4_in : r3_in);
    }
    return;
  }
  DumpContainer(base, container, info);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_shaders[shader_obj] = info;
}
}  // namespace

const GuestShaderInfo* LookupGuestShader(uint32_t guest_object) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_shaders.find(guest_object);
  return it == g_shaders.end() ? nullptr : &it->second;
}

// XDK 2.0.3529 LTCG: containers are embedded inside the shader objects at
// kVSContainerOffset / kPSContainerOffset (confirmed by disassembly).
// We try both offsets for each object so the caller does not need to know
// which type it is holding.
void TryRegisterInlineShaders(uint8_t* base, uint32_t vs_obj, uint32_t ps_obj) {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (uint32_t obj : {vs_obj, ps_obj}) {
    if (!obj || obj > 0xFFFFFF00u) continue;
    if (g_shaders.count(obj)) continue;  // already registered

    GuestShaderInfo info;
    // Try VS offset first, then PS offset.
    uint32_t container = 0;
    if (obj + kVSContainerOffset < 0xFFFFFF00u &&
        TryHashContainer(base, obj + kVSContainerOffset, info)) {
      container = obj + kVSContainerOffset;
    } else if (obj + kPSContainerOffset < 0xFFFFFF00u &&
               TryHashContainer(base, obj + kPSContainerOffset, info)) {
      container = obj + kPSContainerOffset;
    }

    if (container) {
      DumpContainer(base, container, info);
      // Temporary: also dump unconditionally for hash cross-check.
      {
        char tmp[256];
        std::snprintf(tmp, sizeof(tmp), "logs\\inline_%016llX.%s.bin",
                      (unsigned long long)info.container_hash,
                      info.is_vertex ? "vs" : "ps");
        ShaderContainerHeader hdr2;
        ParseShaderContainerHeader(base + container, 0x100000000ull - container, hdr2);
        if (std::FILE* f = std::fopen(tmp, "wb")) {
          std::fwrite(base + container, 1, hdr2.total_size(), f);
          std::fclose(f);
        }
      }
      g_shaders[obj] = info;
      REXLOG_INFO("TryRegisterInline: obj={:08X} container={:08X} hash={:016X} {}",
                  obj, container, info.container_hash, info.is_vertex ? "VS" : "PS");
    }
  }
}

}  // namespace superman_returns::native

#define SR_SHADER_CREATE_HOOK_(addr, role)                                             \
  REX_EXTERN(__imp__sub_##addr);                                                       \
  extern "C" REX_FUNC(sub_##addr) {                                                    \
    superman_returns::native::OnCreateShader(                                          \
        ctx, base, [](PPCContext& c, uint8_t* b) { __imp__sub_##addr(c, b); }, role);  \
  }
#define SR_SHADER_CREATE_HOOK(addr, role) SR_SHADER_CREATE_HOOK_(addr, role)

#if SR_HOOK_ENABLED(CREATE_SHADER_A)
SR_SHADER_CREATE_HOOK(SR_ADDR_CREATE_SHADER_A, "CreateShaderA")
#endif
#if SR_HOOK_ENABLED(CREATE_SHADER_B)
SR_SHADER_CREATE_HOOK(SR_ADDR_CREATE_SHADER_B, "CreateShaderB")
#endif
