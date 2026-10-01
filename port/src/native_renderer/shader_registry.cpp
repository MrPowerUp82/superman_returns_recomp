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

// Kit: hash [c, c + virtual + physical) exactly like the corpus. Returns
// false (logged once) when the header is not a shader container.
bool HashContainer(uint8_t* base, uint32_t container, GuestShaderInfo& info) {
  ShaderContainerHeader header;
  // Guest addresses above 0x82000000 still leave > 32 MB readable in the
  // 4 GB guest space; the size limit is in the parser.
  if (!container || !ParseShaderContainerHeader(base + container, 0x100000000ull - container,
                                                header)) {
    LogCaptureAnomalyOnce("CreateShader r3 is not a shader container", container);
    return false;
  }
  info.is_vertex = header.is_vertex;
  info.container_hash = XXH3_64bits(base + container, header.total_size());
  return true;
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

// XDK shader creators (called by the game's effect/material loader): r3 =
// shader container, returns the new D3D shader object in r3.
void OnCreateShader(PPCContext& ctx, uint8_t* base, void (*original)(PPCContext&, uint8_t*),
                    const char* role) {
  uint32_t container = ctx.r3.u32;
  GuestShaderInfo info;
  bool valid = HashContainer(base, container, info);
  uint32_t shader_obj = 0;
  if (!valid && HashContainer(base, ctx.r4.u32, info)) {
    container = ctx.r4.u32;
    shader_obj = ctx.r3.u32;
    valid = true;
  }
  original(ctx, base);
  if (!shader_obj) shader_obj = ctx.r3.u32;
  NoteHookCall(role, container, shader_obj);
  if (!valid || !shader_obj) return;
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
