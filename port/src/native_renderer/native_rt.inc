// Ray-traced shadows and reflections, renderer side (ported from
// Superman-Returns-PC; docs/rt-dlss-port.md). Included by native_renderer.cpp
// inside namespace superman_returns::native. The DXR scene, stream-output
// character capture and material table are in rt_shadows.cpp.
//
// Per frame: the first draw whose vertex shader carries the sun light rows
// (shadow_regs.inc) uploads the frame data (kRtFrameSrv) and builds the TLAS
// from the meshes and instances captured by earlier draws; the receivers
// (XenosRecomp patches 0017-0033) trace inline RayQuery against it.

namespace {
struct ShadowRegs {
  uint64_t hash;
  uint32_t u, v, z;
};
constexpr ShadowRegs kShadowRegs[] = {
#include "shadow_regs.inc"
};
struct ViewInverseReg {
  uint64_t hash;
  uint32_t reg;
};
constexpr ViewInverseReg kViewInverseRegs[] = {
#include "rt_view_inverse_regs.inc"
};
struct WorldRegs {
  uint64_t hash;
  uint32_t reg, skinned;
};
constexpr WorldRegs kWorldRegs[] = {
#include "rt_world_regs.inc"
};
uint64_t RtMix(uint64_t h, uint64_t v) {
  h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
  return h;
}
template <typename Instance>
uint64_t RtInstanceKey(const Instance& instance) {
  uint64_t k = instance.mesh;
  for (float v : instance.transform) k = RtMix(k, uint64_t(int64_t(std::lround(double(v) * 64.0))));
  return k;
}
struct AlbedoScalar {
  int32_t reg;  // pixel shader float constant register, -1 = value
  uint32_t comp;
  float value;
};
struct AlbedoRegs {
  uint64_t hash;
  uint32_t slot;
  int32_t interp;  // UV interpolant (TEXCOORDn), -1 = none
  uint32_t cu, cv;
  AlbedoScalar scale_u, scale_v, offset_u, offset_v;
};
constexpr AlbedoRegs kAlbedoRegs[] = {
#include "rt_albedo_regs.inc"
};
struct TintRegs {
  uint64_t hash;
  uint32_t mask_slot;
  int32_t interp;
  uint32_t cu, cv;
  AlbedoScalar scale_u, scale_v, offset_u, offset_v;
  uint32_t channels[3];
  uint32_t normalised;
  int32_t tint_regs[4];  // R_tint, G_tint, B_tint, tint; -1 = none
  int32_t col_slot;      // -1 = none
};
constexpr TintRegs kTintRegs[] = {
#include "rt_tint_regs.inc"
};
struct PaletteRegs {
  uint64_t hash;
  int32_t a_tint_reg, building_reg;
  uint32_t building_comps[4];  // gBuildingTint component for the R, G, B mask channels and the rest
  int32_t interp;
  uint32_t comp;
  float scale;
  uint32_t entries;
  float colours[8][3];
  int32_t tone_regs[3];  // gShadCol, ramp_col_exp, Kramp_lo_hi_Kao (-1 = none)
};
constexpr PaletteRegs kPaletteRegs[] = {
#include "rt_palette_regs.inc"
};
struct InstancedRegs {
  uint64_t hash;
  uint32_t data, transforms, per_instance;
};
constexpr InstancedRegs kInstancedRegs[] = {
#include "rt_instanced_regs.inc"
};
template <typename Table>
auto RtFind(const Table& table, uint64_t hash) -> decltype(&table[0]) {
  const auto it = std::lower_bound(std::begin(table), std::end(table), hash,
                                   [](const auto& e, uint64_t h) { return e.hash < h; });
  return it != std::end(table) && it->hash == hash ? &*it : nullptr;
}
const AlbedoRegs* RtAlbedoFor(uint64_t ps_hash) { return RtFind(kAlbedoRegs, ps_hash); }
const TintRegs* RtTintFor(uint64_t ps_hash) { return RtFind(kTintRegs, ps_hash); }
const PaletteRegs* RtPaletteFor(uint64_t ps_hash) { return RtFind(kPaletteRegs, ps_hash); }
// The interpolant a pixel shader's colour UV comes from (tint blend first).
int32_t RtUvInterp(uint64_t ps_hash) {
  if (const TintRegs* t = RtTintFor(ps_hash)) return t->interp;
  if (const AlbedoRegs* a = RtAlbedoFor(ps_hash)) return a->interp;
  return -1;
}
// The interpolant its building palette index comes from (-1: none).
int32_t RtPaletteInterp(uint64_t ps_hash) {
  const PaletteRegs* p = RtPaletteFor(ps_hash);
  return p && REXCVAR_GET(sr_rt_reflection_palette) ? p->interp : -1;
}
bool RtGeometryVs(uint64_t hash) { return RtFind(kWorldRegs, hash) || RtFind(kInstancedRegs, hash); }
// Position formats DXR builds from directly (the host vertex buffers hold
// little-endian elements, swapped on upload). Quantised formats the vertex
// shader rescales are left out.
DXGI_FORMAT RtPositionFormat(DXGI_FORMAT host) {
  switch (host) {
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
      return DXGI_FORMAT_R32G32B32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:
      return DXGI_FORMAT_UNKNOWN;
  }
}
}  // namespace

bool Renderer::RtAbOldSwap() const {
  const int64_t first = REXCVAR_GET(sr_rt_full_scene_hide_swap), period = REXCVAR_GET(sr_rt_ab_period);
  const int64_t swap = int64_t(swap_number_);
  if (first <= 0 || swap < first) return false;
  return period > 0 ? (swap - first) % period == 0 : swap == first;
}

bool Renderer::RtAbAll() const { return RtAbOldSwap() && REXCVAR_GET(sr_rt_ab_vs).empty(); }

bool Renderer::RtAbHidesVs(uint64_t vs) const {
  if (!(RtAbOldSwap() && REXCVAR_GET(sr_rt_ab_vs).empty())) return false;
  const std::string& list = REXCVAR_GET(sr_rt_ab_vs);
  if (list.empty()) return true;
  for (const char* p = list.c_str(); *p;) {
    char* end = nullptr;
    const unsigned long long h = std::strtoull(p, &end, 16);
    if (end == p) { ++p; continue; }
    if (h == vs) return true;
    p = end;
  }
  return false;
}

int32_t Renderer::RtDebugMode() const {
  const int32_t cycle = REXCVAR_GET(sr_rt_shadow_debug_cycle);
  if (cycle <= 0) return REXCVAR_GET(sr_rt_shadow_debug);
  const std::vector<int32_t> modes = RtDebugModes();
  return modes[(swap_number_ / uint64_t(cycle)) % modes.size()];
}

std::vector<int32_t> Renderer::RtDebugModes() const {
  std::vector<int32_t> modes;
  for (const char* p = REXCVAR_GET(sr_rt_debug_modes).c_str(); *p;) {
    char* end = nullptr;
    const long v = std::strtol(p, &end, 10);
    if (end == p) { ++p; continue; }
    modes.push_back(int32_t(v));
    p = end;
  }
  if (modes.empty()) modes.push_back(0);
  return modes;
}

// The frame data buffer exists from the first draw: every translated receiver
// reads its switches (all zero = the game's own shadows and sky reflections).
bool Renderer::EnsureRtFrameBuffer() {
  if (rt_frame_buffer_) return true;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = kRtFrameBytes;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  rt_frame_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, rt_frame_state_, nullptr,
                                              IID_PPV_ARGS(&rt_frame_buffer_)))) {
    return false;
  }
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = kRtFrameBytes / 4;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device_->CreateShaderResourceView(
      rt_frame_buffer_.Get(), &srv,
      provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(), kRtFrameSrv));
  // Zero it once (a new default-heap buffer's content is undefined).
  UploadAlloc up;
  if (Upload(kRtFrameBytes, 256, up)) {
    std::memset(up.cpu, 0, kRtFrameBytes);
    command_list_->CopyBufferRegion(rt_frame_buffer_.Get(), 0, up.resource, up.offset, kRtFrameBytes);
  }
  Transition(rt_frame_buffer_.Get(), rt_frame_state_,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  return true;
}

void Renderer::UpdateRtFrame(uint8_t* base) {
  (void)base;
  if (packet_sink_) return;  // the Vulkan capture path has no ray tracing
  if (!EnsureRtFrameBuffer()) return;
  // The camera position: the translation column of gViewInverse, from any
  // vertex shader that declares it (the receivers too, before the upload below).
  if (const ViewInverseReg* vit = RtFind(kViewInverseRegs, current_vs_hash_); vit && vit->reg + 3 < 256) {
    const float* c = reinterpret_cast<const float*>(mirror_.regs() + Pm4Mirror::kAluConstantBase) + vit->reg * 4;
    if (std::isfinite(c[3]) && std::isfinite(c[7]) && std::isfinite(c[11])) {
      rt_camera_[0] = c[3];
      rt_camera_[1] = c[7];
      rt_camera_[2] = c[11];
      rt_camera_valid_ = true;
    }
  }
  const ShadowRegs* it = RtFind(kShadowRegs, current_vs_hash_);
  if (!it) return;
  const float* c = reinterpret_cast<const float*>(mirror_.regs() + Pm4Mirror::kAluConstantBase);
  float m[16] = {};
  for (int k = 0; k < 4; ++k) {
    m[k] = c[it->u * 4 + k];
    m[4 + k] = c[it->v * 4 + k];
    m[8 + k] = c[it->z * 4 + k];
  }
  m[15] = 1.0f;
  // Once a frame the scene is built; a second view of the same frame (co-op
  // split screen: P2's own cascades) only re-uploads the frame data with its
  // light rows, so its receivers reconstruct their world positions right.
  const bool rebuild = rt_frame_swap_ != swap_number_;
  if (!rebuild) {
    float diff = 0.0f;
    for (int k = 0; k < 12; ++k) diff = std::max(diff, std::abs(m[k] - rt_view_rows_[k]));
    if (diff <= 1e-4f) return;
  }
  float inv[16];
  if (!Invert4x4(m, inv)) return;
  std::memcpy(rt_view_rows_, m, sizeof(rt_view_rows_));
  if (rebuild) std::memcpy(rt_frame_rows_, m, sizeof(rt_frame_rows_));
  // Towards the sun: light-space depth grows away from the light (row Z).
  float sun[3] = {-m[8], -m[9], -m[10]};
  const float len = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
  if (len <= 0.0f) return;
  for (float& v : sun) v /= len;
  if (rebuild) std::memcpy(rt_frame_inv_, inv, sizeof(rt_frame_inv_));
  const bool scene = RtSceneOn();
  if (scene && rebuild) {
    // Anchor of the city instance window: the first cascade's centre.
    float anchor[3];
    for (int r = 0; r < 3; ++r) anchor[r] = inv[r * 4] * 0.5f + inv[r * 4 + 1] * 0.5f + inv[r * 4 + 2] * 0.5f + inv[r * 4 + 3];
    RtBuildSkinnedBlas();
    RtBuildCityTlas(anchor);
  } else if (!scene && rebuild && RtAvailable()) {
    EnsureRtScene(inv);
  }
  UploadAlloc up;
  if (!Upload(kRtFrameBytes, 256, up)) return;
  uint32_t* data = reinterpret_cast<uint32_t*>(up.cpu);
  std::memset(data, 0, kRtFrameBytes);
  std::memcpy(data, inv, 48);
  std::memcpy(&data[12], sun, 12);
  const int32_t debug_mode = RtDebugMode();
  data[16] = uint32_t(debug_mode);
  data[17] = 1;
  const bool ready = scene ? rt_city_ready_ : rt_scene_ready_;
  data[18] = ready ? 1u : 0u;
  const float bias = float(REXCVAR_GET(sr_rt_shadow_bias));
  std::memcpy(&data[19], &bias, 4);
  // Byte 80: the receivers' shadow map fetches read the ray result.
  data[20] = RtShadowsOn() && REXCVAR_GET(sr_rt_shadows_replace) && (debug_mode == 0 || debug_mode == 5) ? 1u : 0u;
  // Softness (menu row 1): Low = the real sun's disc, High = a wider one.
  const int32_t rays = RtHighSoftness() ? std::max(REXCVAR_GET(sr_rt_shadow_rays), 16)
                                        : std::min(REXCVAR_GET(sr_rt_shadow_rays), 8);
  data[21] = uint32_t(rays);
  const double sun_angle = REXCVAR_GET(sr_rt_sun_angle) * (RtHighSoftness() ? 2.0 : 1.0);
  const float spread = float(std::tan(sun_angle * 0.5 * 3.14159265358979 / 180.0));
  std::memcpy(&data[22], &spread, 4);
  // Reflections in the game view and in their own debug views (6 seen colour, 7 hit path, 8).
  data[23] = RtReflectionsOn() && (debug_mode == 0 || (debug_mode >= 6 && debug_mode <= 8)) ? 1u : 0u;
  data[24] = REXCVAR_GET(sr_rt_reflection_detail) && !RtAbAll() ? 1u : 0u;
  // Bytes 112..143: the city's lighting tone (shadow colour, ramp strength,
  // ramp colour, valid) for reflection hits.
  std::memcpy(&data[28], rt_tone_, sizeof(rt_tone_));
  // Bytes 148..159: the camera position; byte 100: the reflection bump weight
  // (sr_rt_reflection_bump; on the A/B swap the game's bumped direction).
  if (rt_camera_valid_) {
    std::memcpy(&data[37], rt_camera_, 12);
    const float bump = RtAbAll() ? 1.0f : float(REXCVAR_GET(sr_rt_reflection_bump));
    std::memcpy(&data[25], &bump, 4);  // byte 100
  }
  data[36] = rt_skin_read_;  // which character buffer the reflection hits read (kRtSkinSrv0 - n)
  Transition(rt_frame_buffer_.Get(), rt_frame_state_, D3D12_RESOURCE_STATE_COPY_DEST);
  command_list_->CopyBufferRegion(rt_frame_buffer_.Get(), 0, up.resource, up.offset, kRtFrameBytes);
  Transition(rt_frame_buffer_.Get(), rt_frame_state_,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  rt_frame_swap_ = swap_number_;
  if (scene && ready && rebuild) {
    ++rt_applied_frames_;
    rt_last_applied_swap_ = swap_number_;
  }
  static int logged = 0;
  if (logged++ < 3 || swap_number_ == uint64_t(REXCVAR_GET(sr_rt_dump_obj_swap))) {
    REXLOG_INFO("native: rt frame swap {} light rows ({:.4f} {:.4f} {:.4f} {:.4f}) ({:.4f} {:.4f} {:.4f} {:.4f}) "
                "({:.4f} {:.4f} {:.4f} {:.4f}) from vs {:016X}; scene {} ready {}", swap_number_, m[0], m[1], m[2],
                m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], current_vs_hash_, scene, ready);
  }
}

void Renderer::RtCaptureDraw(uint8_t* base, const WorkBatch& batch, const WorkCmd& cmd,
                             const BufferEntry* ib, uint32_t start_index, uint32_t index_count,
                             int32_t base_vertex, bool strip) {
  if (!RtSceneOn()) return;
  static uint64_t calls = 0, no_receiver = 0, no_world = 0, no_pos = 0, bad_args = 0, last_diag = 0;
  ++calls;
  if (swap_number_ >= last_diag + 600) {
    last_diag = swap_number_;
    REXLOG_INFO("native: rt capture diag: indexed triangle draws {}, not receiver {}, no world {}, "
                "no position {}, bad args {}, skinned {}, format {}, captured {} (full scene {}, strip meshes {}), "
                "budget {}, meshes {}, instances {}",
                calls, no_receiver, no_world, no_pos, bad_args, rt_stats_.skip_skinned, rt_stats_.skip_format,
                rt_stats_.captured, rt_stats_.full_scene, rt_stats_.strip_meshes, rt_stats_.skip_budget,
                rt_meshes_.size(), rt_city_instances_.size());
  }
  if (!ib || !ib->gpu_address || base_vertex < 0 || index_count < 3) { ++bad_args; return; }
  // Stream output writes the draw's triangles as a list: 3 vertices each. A
  // strip (the characters) has length - 2 per run between restart indices.
  auto so_vertices = [&]() {
    uint32_t vertices = index_count - index_count % 3;
    if (strip) {
      const uint32_t isize = cmd.index32 ? 4 : 2, cut = cmd.index32 ? 0xFFFFFFFFu : 0xFFFFu;
      const uint8_t* q = GuestPtr(base, 0xA0000000u + cmd.index.address + start_index * isize, index_count * isize);
      uint32_t triangles = 0, run = 0;
      for (uint32_t i = 0; i <= index_count; ++i) {
        const uint32_t index = i == index_count ? cut
                               : isize == 4 ? (uint32_t(q[4 * i]) << 24) | (uint32_t(q[4 * i + 1]) << 16) |
                                                  (uint32_t(q[4 * i + 2]) << 8) | q[4 * i + 3]
                                            : (uint32_t(q[2 * i]) << 8) | q[2 * i + 1];
        if (index == cut || index == cmd.index.reset_index) {
          triangles += run > 2 ? run - 2 : 0;
          run = 0;
        } else {
          ++run;
        }
      }
      vertices = triangles * 3;
    }
    return vertices;
  };
  if (current_vs_hash_ == kRtCapeVs) {
    RtCaptureCape(base, true, start_index, index_count, base_vertex, so_vertices());
    return;
  }
  const ShadowRegs* sit = RtFind(kShadowRegs, current_vs_hash_);
  const WorldRegs* wit = RtFind(kWorldRegs, current_vs_hash_);
  // Static world geometry that does not receive the sun shadow (cars and props
  // with damage effects, far lit buildings, foliage) joins the scene too with
  // sr_rt_full_scene, except the water surface itself and camera-facing sprites.
  const bool receiver = sit != nullptr;
  const bool full_scene = REXCVAR_GET(sr_rt_full_scene) && wit && !wit->skinned &&
                          current_vs_hash_ != 0x31FE1648D1734C70ull && current_vs_hash_ != 0x0EAF824686027895ull;
  if (!receiver && full_scene) ++rt_stats_.full_scene;
  if (!receiver && !full_scene) {
    ++no_receiver;
    return;
  }
  if (!wit) { ++no_world; return; }
  if (wit->skinned) {
    if (sit) RtCaptureSkinned(base, sit->u, sit->v, sit->z, start_index, index_count, base_vertex, so_vertices());
    return;
  }
  // Static strips: their indices are expanded to a triangle list for the BLAS
  // (with sr_rt_full_scene; otherwise static geometry is triangle lists only).
  if (strip && !REXCVAR_GET(sr_rt_full_scene)) return;
  // Position element and its stream.
  const StreamPlan* pos_stream = nullptr;
  uint32_t pos_offset = 0;
  DXGI_FORMAT pos_format = DXGI_FORMAT_UNKNOWN;
  for (uint32_t i = 0; i < cmd.stream_count && !pos_stream; ++i) {
    const StreamPlan& sp = batch.streams[cmd.stream_first + i];
    for (const auto& a : ReadVertexDeclaration(base, sp.buffer.decl)) {
      if (a.stream != sp.stream || a.usage != 0 || a.index != 0) continue;
      pos_stream = &sp;
      pos_offset = a.offset;
      pos_format = RtPositionFormat(MapDeclType(a.type).format);
      break;
    }
  }
  if (!pos_stream) { ++no_pos; return; }
  if (pos_format == DXGI_FORMAT_UNKNOWN) { ++rt_stats_.skip_format; return; }
  ++rt_stats_.captured;
  uint64_t key = RtMix(pos_stream->buffer.key, pos_stream->offset);
  key = RtMix(key, (uint64_t(pos_stream->stride) << 32) | pos_offset);
  key = RtMix(key, cmd.index.key);
  key = RtMix(key, (uint64_t(start_index) << 32) | index_count);
  key = RtMix(key, uint64_t(uint32_t(base_vertex)));
  if (strip) key = RtMix(key, 0x5354524950ull);
  // World matrix rows (the vertex shader's gWorldXform, dot products with the
  // position), the same world space the receivers' light rows use.
  const float* c = reinterpret_cast<const float*>(mirror_.regs() + Pm4Mirror::kAluConstantBase);
  RtInstance instance;
  instance.mesh = key;
  instance.vs = current_vs_hash_;
  instance.source = !receiver ? 8 : current_vs_hash_ == 0xC7C0E381A3C9B79Eull ? 2 : 1;
  std::memcpy(instance.transform, c + wit->reg * 4, sizeof(instance.transform));
  const uint64_t ikey = RtInstanceKey(instance);
  auto mit = rt_meshes_.find(key);
  if (mit == rt_meshes_.end()) {
    if (rt_builds_swap_ != swap_number_) {
      rt_builds_swap_ = swap_number_;
      rt_builds_this_frame_ = 0;
    }
    if (rt_builds_this_frame_ >= uint32_t(REXCVAR_GET(sr_rt_builds_per_frame))) { ++rt_stats_.skip_budget; return; }
    auto vb_it = buffers_.find(pos_stream->buffer.key);  // already uploaded by BindVertexStreams
    if (vb_it == buffers_.end() || !vb_it->second.gpu_address) return;
    const BufferEntry* vb = &vb_it->second;
    const uint32_t stride = pos_stream->stride;
    const uint64_t first = uint64_t(base_vertex) * stride;
    if (!stride || first + pos_offset >= pos_stream->size) return;
    const bool index32 = cmd.index32;
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexBuffer.StartAddress = vb->gpu_address + pos_stream->offset + first + pos_offset;
    geometry.Triangles.VertexBuffer.StrideInBytes = stride;
    geometry.Triangles.VertexFormat = pos_format;
    geometry.Triangles.VertexCount = UINT((pos_stream->size - first - pos_offset) / stride);
    geometry.Triangles.IndexBuffer = ib->gpu_address + uint64_t(start_index) * (index32 ? 4 : 2);
    geometry.Triangles.IndexFormat = index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    geometry.Triangles.IndexCount = index_count - index_count % 3;
    if (strip) {
      // The strip as a list in the order stream output writes it (odd triangles
      // i+1, i, i+2, restart index ends a run), so primitive n is SO triangle n.
      const uint32_t isize = index32 ? 4 : 2, cut = index32 ? 0xFFFFFFFFu : 0xFFFFu;
      const uint8_t* q = GuestPtr(base, 0xA0000000u + cmd.index.address + start_index * isize, index_count * isize);
      std::vector<uint32_t> list;
      list.reserve(size_t(index_count) * 3);
      uint32_t run[2] = {}, n = 0;
      for (uint32_t i = 0; i < index_count; ++i) {
        const uint32_t index = isize == 4 ? (uint32_t(q[4 * i]) << 24) | (uint32_t(q[4 * i + 1]) << 16) |
                                                (uint32_t(q[4 * i + 2]) << 8) | q[4 * i + 3]
                                          : (uint32_t(q[2 * i]) << 8) | q[2 * i + 1];
        if (index == cut || index == cmd.index.reset_index) { n = 0; continue; }
        if (n >= 2) {
          const bool odd = (n - 2) & 1;
          list.push_back(odd ? run[1] : run[0]);
          list.push_back(odd ? run[0] : run[1]);
          list.push_back(index);
        }
        run[0] = run[1];
        run[1] = index;
        if (n < 2) run[n] = index;  // first two vertices of a run
        ++n;
      }
      if (list.empty()) return;
      UploadAlloc up;
      if (!Upload(list.size() * 4, 16, up)) return;
      std::memcpy(up.cpu, list.data(), list.size() * 4);
      geometry.Triangles.IndexBuffer = up.gpu;
      geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
      geometry.Triangles.IndexCount = UINT(list.size());
      ++rt_stats_.strip_meshes;
    }
    RtMesh mesh;
    if (!RtBuildBlas(geometry, mesh)) return;
    mesh.triangles = geometry.Triangles.IndexCount / 3;
    ++rt_builds_this_frame_;
    ++rt_stats_.new_meshes;
    mit = rt_meshes_.emplace(key, std::move(mesh)).first;
  }
  if (REXCVAR_GET(sr_rt_dump_obj_swap) > 0 && swap_number_ == uint64_t(REXCVAR_GET(sr_rt_dump_obj_swap)) &&
      pos_format == DXGI_FORMAT_R32G32B32_FLOAT) {
    const uint32_t isize = cmd.index32 ? 4 : 2;
    std::vector<uint32_t> idx(index_count - index_count % 3);
    for (uint32_t i = 0; i < idx.size(); ++i) {
      const uint8_t* q = GuestPtr(base, 0xA0000000u + cmd.index.address + (start_index + i) * isize, isize);
      idx[i] = isize == 4 ? (uint32_t(q[0]) << 24) | (uint32_t(q[1]) << 16) | (uint32_t(q[2]) << 8) | q[3]
                          : (uint32_t(q[0]) << 8) | q[1];
    }
    RtDumpObj(base, pos_stream->buffer.address + pos_stream->offset + uint32_t(base_vertex) * pos_stream->stride +
                        pos_offset,
              pos_stream->stride, idx, instance.transform, ikey);
  }
  // Reflections: the mesh's texture coordinates, from the first draw whose
  // pixel shader has a colour texture (the shadow pass draws it first without).
  if (RtReflectionsOn())
    RtCaptureUv(mit->second, base, RtUvInterp(current_ps_hash_), RtPaletteInterp(current_ps_hash_), true,
                index_count, start_index, base_vertex, strip ? so_vertices() : 0);
  RtAddInstance(instance);
}

// The building palettes (kPaletteRegs, 8 colours each) after the material
// entries: reflection hits look up palette p entry n at kRtMaterialBytes + (p * 8 + n) * 16.
void Renderer::RtUploadPalettes() {
  if (!rt_material_buffer_) return;
  float colours[16 * 8][4] = {};
  const size_t count = std::min<size_t>(std::size(kPaletteRegs), 16);
  for (size_t p = 0; p < count; ++p) {
    for (int n = 0; n < 8; ++n) std::memcpy(colours[p * 8 + n], kPaletteRegs[p].colours[n], 12);
  }
  UploadAlloc up;
  if (!Upload(sizeof(colours), 256, up)) return;
  std::memcpy(up.cpu, colours, sizeof(colours));
  Transition(rt_material_buffer_.Get(), rt_material_state_, D3D12_RESOURCE_STATE_COPY_DEST);
  command_list_->CopyBufferRegion(rt_material_buffer_.Get(), kRtMaterialBytes, up.resource, up.offset, sizeof(colours));
  Transition(rt_material_buffer_.Get(), rt_material_state_,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

// Debug export (sr_rt_dump_obj_swap): a mesh's vertices from guest memory (big
// endian floats, the first at vertex0), transformed with the instance matrix,
// appended to sr_native_dump_dir/rt_city.obj.
void Renderer::RtDumpObj(uint8_t* base, uint32_t vertex0, uint32_t stride, const std::vector<uint32_t>& idx,
                         const float t[12], uint64_t instance_key) {
  static std::FILE* obj = nullptr;
  static uint32_t written_vertices = 0, written_meshes = 0;
  static std::unordered_set<uint64_t> written;
  if (!written.insert(instance_key).second) return;
  if (!obj) {
    const std::string path = REXCVAR_GET(sr_native_dump_dir) + "/rt_city.obj";
    obj = std::fopen(path.c_str(), "w");
  }
  if (!obj || written_vertices >= 4000000 || idx.empty()) return;
  const auto [lo_it, hi_it] = std::minmax_element(idx.begin(), idx.end());
  const uint32_t lo = *lo_it, hi = *hi_it;
  if (hi - lo >= 200000) return;
  for (uint32_t v = lo; v <= hi; ++v) {
    float p[3];
    for (int k = 0; k < 3; ++k) {
      const uint8_t* q = GuestPtr(base, 0xA0000000u + vertex0 + v * stride + 4 * k, 4);
      const uint32_t w = (uint32_t(q[0]) << 24) | (uint32_t(q[1]) << 16) | (uint32_t(q[2]) << 8) | q[3];
      std::memcpy(&p[k], &w, 4);
    }
    std::fprintf(obj, "v %.3f %.3f %.3f\n", t[0] * p[0] + t[1] * p[1] + t[2] * p[2] + t[3],
                 t[4] * p[0] + t[5] * p[1] + t[6] * p[2] + t[7], t[8] * p[0] + t[9] * p[1] + t[10] * p[2] + t[11]);
  }
  std::fprintf(obj, "g mesh_%u_vs_%016llX\n", written_meshes++, (unsigned long long)current_vs_hash_);
  for (size_t i = 0; i + 2 < idx.size(); i += 3)
    std::fprintf(obj, "f %u %u %u\n", written_vertices + idx[i] - lo + 1, written_vertices + idx[i + 1] - lo + 1,
                 written_vertices + idx[i + 2] - lo + 1);
  written_vertices += hi - lo + 1;
  std::fflush(obj);
}

// The material of the current draw for reflection hits, from its pixel
// shader's tables (tint blend with palette, or the colour texture alone);
// texture coordinates only when captured_uv_interp (the interpolant captured
// with the mesh) is the one the pixel shader uses. False: no colour texture.
bool Renderer::RtDrawMaterial(int32_t captured_uv_interp, RtMaterial& in) {
  const float* pc = reinterpret_cast<const float*>(mirror_.regs() + Pm4Mirror::kAluConstantBase) + 1024;
  auto value = [pc](const AlbedoScalar& sc) { return sc.reg < 0 ? sc.value : pc[sc.reg * 4 + sc.comp]; };
  // A bound 2D texture of this draw: SRV index | 0x8000 for PWL gamma (0 = none).
  auto texture = [this](int32_t slot) -> uint32_t {
    if (slot < 0 || slot >= 16) return 0;
    const uint32_t srv = rt_tex2d_[slot], index = srv & 0x7FFFu;
    return index > 2 ? index | (srv & 0x80000000u ? 0x8000u : 0u) : 0;
  };
  bool found = false;
  if (const TintRegs* t = RtTintFor(current_ps_hash_); t && texture(int32_t(t->mask_slot))) {
    // Building shaders: mask * tints (* tint * colour texture), all at the mask's UV.
    in.mask = texture(int32_t(t->mask_slot));
    in.mask_gen = SrvGeneration(in.mask & 0x7FFFu);
    in.mask_sampler = rt_samp_[t->mask_slot];
    in.tint_flags = t->channels[0] | (t->channels[1] << 2) | (t->channels[2] << 4) | (t->normalised << 6) |
                    (t->tint_regs[3] >= 0 ? 1u << 7 : 0u);
    for (int k = 0; k < 4; ++k) {
      if (t->tint_regs[k] >= 0) std::memcpy(in.tints[k], pc + t->tint_regs[k] * 4, 16);
    }
    if (const uint32_t col = texture(t->col_slot)) {
      in.albedo = col;
      in.albedo_gen = SrvGeneration(col & 0x7FFFu);
      in.sampler = rt_samp_[t->col_slot];
      in.tint_flags |= 1u << 8;
    }
    if (const PaletteRegs* pal = RtPaletteFor(current_ps_hash_)) {
      // The rest weight's tint, the building weights in R, G, B, rest order, the palette.
      in.tint_flags |= 1u << 10;
      std::memcpy(in.a_tint, pc + pal->a_tint_reg * 4, 16);
      for (int k = 0; k < 4; ++k) in.tints[3][k] = pc[pal->building_reg * 4 + pal->building_comps[k]];
      in.palette = int32_t(pal - std::begin(kPaletteRegs));
      in.palette_interp = pal->interp;
      in.palette_comp = pal->comp;
      in.palette_scale = pal->scale;
      // The city's lighting tone for reflection hits (rt_tone_, frame bytes 112..143).
      if (pal->tone_regs[0] >= 0 && pal->tone_regs[1] >= 0 && pal->tone_regs[2] >= 0) {
        std::memcpy(rt_tone_, pc + pal->tone_regs[0] * 4, 12);
        rt_tone_[3] = pc[pal->tone_regs[2] * 4];
        std::memcpy(rt_tone_ + 4, pc + pal->tone_regs[1] * 4, 12);
        rt_tone_[7] = 1.0f;
      }
    }
    if (t->interp >= 0 && captured_uv_interp == t->interp) {
      in.uv_interp = t->interp;
      in.uv_comps = t->cu | (t->cv << 2);
      in.uv[0] = value(t->scale_u);
      in.uv[1] = value(t->scale_v);
      in.uv[2] = value(t->offset_u);
      in.uv[3] = value(t->offset_v);
    }
    found = true;
  } else if (const AlbedoRegs* ait = RtAlbedoFor(current_ps_hash_); ait && texture(int32_t(ait->slot))) {
    // Other materials: the colour texture alone.
    in.albedo = texture(int32_t(ait->slot));
    // Its alpha test (leaves, fences): the ray tests the texture's alpha the same way.
    const uint32_t cc = mirror_.regs()[rex::graphics::XE_GPU_REG_RB_COLORCONTROL];
    in.alpha_ref = -1.0f;
    if (REXCVAR_GET(sr_rt_alpha_test) && (cc & 8) && (cc & 7) != 7 && (cc & 7) != 0)
      std::memcpy(&in.alpha_ref, &mirror_.regs()[rex::graphics::XE_GPU_REG_RB_ALPHA_REF], 4);
    in.albedo_gen = SrvGeneration(in.albedo & 0x7FFFu);
    in.sampler = rt_samp_[ait->slot];
    if (ait->interp >= 0 && captured_uv_interp == ait->interp) {
      in.uv_interp = ait->interp;
      in.uv_comps = ait->cu | (ait->cv << 2);
      in.uv[0] = value(ait->scale_u);
      in.uv[1] = value(ait->scale_v);
      in.uv[2] = value(ait->offset_u);
      in.uv[3] = value(ait->offset_v);
    }
    found = true;
  }
  return found;
}

// The UV interpolant of the current draw's pixel shader (-1: none).
int32_t Renderer::RtDrawUvInterp() const { return RtUvInterp(current_ps_hash_); }

// A TLAS instance of a captured mesh with its world matrix. The same mesh and
// matrix again keeps when it was first seen (static objects accumulate age;
// moving ones get a new key every frame).
void Renderer::RtAddInstance(const RtInstance& instance) {
  auto mit = rt_meshes_.find(instance.mesh);
  if (mit == rt_meshes_.end()) return;
  mit->second.last_swap = swap_number_;
  auto [iit, inserted] = rt_city_instances_.try_emplace(RtInstanceKey(instance), instance);
  if (inserted) {
    iit->second.first_swap = swap_number_;
    iit->second.xform = RtXformKey(instance.transform);
    rt_young_instances_.emplace_back(iit->first, swap_number_);
  }
  iit->second.last_swap = swap_number_;
  // A copy a shadow receiver also draws is not only full-scene geometry.
  if ((iit->second.source & 8) && !(instance.source & 8)) iit->second.source = instance.source;
  // Draws without a colour texture (the shadow pass, which also draws what is
  // outside the view) leave the material the copy or the mesh already has.
  RtMaterial in;
  if (RtDrawMaterial(mit->second.uv_interp, in)) {
    iit->second.material = in;
    mit->second.material = in;
  }
  static uint64_t last_log = 0;
  if (swap_number_ >= last_log + 600) {
    last_log = swap_number_;
    REXLOG_INFO("native: rt city: {} meshes, {} instances; captured {}, new {}, skipped format {} "
                "skinned {} budget {}; instanced draws {} copies {} skipped {}", rt_meshes_.size(),
                rt_city_instances_.size(), rt_stats_.captured, rt_stats_.new_meshes,
                rt_stats_.skip_format, rt_stats_.skip_skinned, rt_stats_.skip_budget,
                rt_stats_.instanced_draws, rt_stats_.instanced_copies, rt_stats_.skip_instanced);
    REXLOG_INFO("native: rt reflections: {} meshes with texture coordinates, {} of {} pool vertices",
                rt_stats_.uv_meshes, rt_uv_cursor_, (192ull << 20) / 32);
  }
}

// Instanced city geometry (the instance_transforms vertex shaders, e.g.
// 1C89B1A64AA3110B): the draw is non-indexed, vertex id v belongs to copy
// i = floor((v + 0.5) / N) with N = instance_data.x vertices per copy, the
// mesh vertex is POSITION1[v - i*N + instance_data.y] and copy i's transform
// is in instance_transforms + i * per_instance: per_instance 2 = translation
// c0.xyz and a yaw c1 (x' = c0.x + x*c1.x + z*c1.z, y' = c0.y + y,
// z' = c0.z + x*c1.y + z*c1.x), 4 = world = x*c0 + y*c1 + z*c2 + c3.
void Renderer::RtCaptureInstanced(uint8_t* base, const WorkBatch& batch, const WorkCmd& cmd,
                                  uint32_t start_vertex, uint32_t vertex_count) {
  if (!RtSceneOn()) return;
  const InstancedRegs* iit = RtFind(kInstancedRegs, current_vs_hash_);
  if (!iit) return;
  if (!RtFind(kShadowRegs, current_vs_hash_)) return;  // only the sun shadow receivers
  ++rt_stats_.instanced_draws;
  const float* c = reinterpret_cast<const float*>(mirror_.regs() + Pm4Mirror::kAluConstantBase);
  const float* data = c + iit->data * 4;
  const uint32_t per_copy = std::isfinite(data[0]) && data[0] >= 3.0f ? uint32_t(data[0]) : 0;
  const uint32_t first_local = std::isfinite(data[1]) && data[1] >= 0.0f ? uint32_t(data[1]) : 0;
  auto skip = [&](const char* why) {
    ++rt_stats_.skip_instanced;
    static int logged = 0;
    if (logged++ < 12)
      REXLOG_INFO("native: rt instanced skip ({}): vs {:016X} instance_data ({} {} {} {}) start {} vertices {}",
                  why, current_vs_hash_, data[0], data[1], data[2], data[3], start_vertex, vertex_count);
  };
  if (!per_copy || per_copy % 3) return skip("vertices per copy");
  // POSITION0 (the vertices) and POSITION1 (the mesh's vertex indices).
  const StreamPlan* streams[2] = {};
  uint32_t offsets[2] = {};
  DXGI_FORMAT formats[2] = {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN};
  for (uint32_t i = 0; i < cmd.stream_count; ++i) {
    const StreamPlan& sp = batch.streams[cmd.stream_first + i];
    for (const auto& a : ReadVertexDeclaration(base, sp.buffer.decl)) {
      if (a.stream != sp.stream || a.usage != 0 || a.index > 1 || streams[a.index]) continue;
      streams[a.index] = &sp;
      offsets[a.index] = a.offset;
      formats[a.index] = MapDeclType(a.type).format;
    }
  }
  const DXGI_FORMAT pos_format = streams[0] ? RtPositionFormat(formats[0]) : DXGI_FORMAT_UNKNOWN;
  const bool index_uint = formats[1] == DXGI_FORMAT_R32_UINT;
  if (pos_format == DXGI_FORMAT_UNKNOWN || !streams[1] ||
      (!index_uint && formats[1] != DXGI_FORMAT_R32_FLOAT && formats[1] != DXGI_FORMAT_R32G32_FLOAT &&
       formats[1] != DXGI_FORMAT_R32G32B32_FLOAT && formats[1] != DXGI_FORMAT_R32G32B32A32_FLOAT)) {
    return skip("formats");
  }
  const StreamPlan& ps = *streams[0];
  const StreamPlan& xs = *streams[1];
  uint64_t key = RtMix(ps.buffer.key, ps.offset);
  key = RtMix(key, (uint64_t(ps.stride) << 32) | offsets[0]);
  key = RtMix(key, RtMix(xs.buffer.key, xs.offset));
  key = RtMix(key, (uint64_t(first_local) << 32) | per_copy);
  // Copies past sr_rt_instance_range stay out of the ray tracing (their shadows
  // and reflections are not seen that far, their cost is).
  const float rt_range = float(REXCVAR_GET(sr_rt_instance_range));
  auto in_rt_range = [&](uint32_t copy) {
    const uint32_t reg = iit->transforms + copy * iit->per_instance;
    if (reg + iit->per_instance > 256) return false;
    const float* t = c + reg * 4;
    const float* o = iit->per_instance == 2 ? t : t + 12;
    const float dx = o[0] - rt_camera_[0], dy = o[1] - rt_camera_[1], dz = o[2] - rt_camera_[2];
    return !(dx * dx + dy * dy + dz * dz > rt_range * rt_range);
  };
  if (rt_range > 0.0f && rt_camera_valid_) {
    const uint32_t first_copy = uint32_t((double(start_vertex) + 0.5) / per_copy);
    const uint32_t last_copy = uint32_t((double(start_vertex) + vertex_count - 1 + 0.5) / per_copy);
    bool any = false;
    for (uint32_t i = first_copy; i <= last_copy && !any; ++i) any = in_rt_range(i);
    if (!any) return;
  }
  const bool fresh = !rt_meshes_.count(key);
  const bool dump = REXCVAR_GET(sr_rt_dump_obj_swap) > 0 &&
                    swap_number_ == uint64_t(REXCVAR_GET(sr_rt_dump_obj_swap)) &&
                    pos_format == DXGI_FORMAT_R32G32B32_FLOAT;
  if (fresh) {
    if (rt_builds_swap_ != swap_number_) {
      rt_builds_swap_ = swap_number_;
      rt_builds_this_frame_ = 0;
    }
    if (rt_builds_this_frame_ >= uint32_t(REXCVAR_GET(sr_rt_builds_per_frame))) { ++rt_stats_.skip_budget; return; }
  }
  if (!ps.stride) return;
  const uint32_t vertex_total = (ps.size - offsets[0]) / ps.stride;
  // Mesh indices: guest memory, big-endian uint32 or float.
  std::vector<uint32_t> indices;
  if (fresh || dump) {
    indices.resize(per_copy);
    for (uint32_t i = 0; i < per_copy; ++i) {
      const uint64_t at = uint64_t(first_local + i) * xs.stride + offsets[1];
      if (!xs.stride || at + 4 > xs.size) return skip("index stream size");
      const uint8_t* q = GuestPtr(base, 0xA0000000u + xs.buffer.address + xs.offset + uint32_t(at), 4);
      const uint32_t w = (uint32_t(q[0]) << 24) | (uint32_t(q[1]) << 16) | (uint32_t(q[2]) << 8) | q[3];
      float f;
      if (index_uint) f = float(w);
      else std::memcpy(&f, &w, 4);
      if (!(f >= 0.0f) || f >= float(vertex_total)) return skip("index out of range");
      indices[i] = uint32_t(f);
    }
  }
  if (fresh) {
    auto vb_it = buffers_.find(ps.buffer.key);  // already uploaded by BindVertexStreams
    if (vb_it == buffers_.end() || !vb_it->second.gpu_address) return;
    RtMesh mesh;
    if (!RtMakeIndexBuffer(indices, mesh)) return;
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexBuffer.StartAddress = vb_it->second.gpu_address + ps.offset + offsets[0];
    geometry.Triangles.VertexBuffer.StrideInBytes = ps.stride;
    geometry.Triangles.VertexFormat = pos_format;
    geometry.Triangles.VertexCount = vertex_total;
    geometry.Triangles.IndexBuffer = mesh.indices->GetGPUVirtualAddress();
    geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    geometry.Triangles.IndexCount = per_copy;
    if (!RtBuildBlas(geometry, mesh)) return;
    mesh.triangles = per_copy / 3;
    ++rt_builds_this_frame_;
    ++rt_stats_.new_meshes;
    rt_meshes_.emplace(key, std::move(mesh));
  }
  // Copies covered by this draw (SV_VertexID starts at start_vertex).
  const uint32_t first = uint32_t((double(start_vertex) + 0.5) / per_copy);
  const uint32_t last = uint32_t((double(start_vertex) + vertex_count - 1 + 0.5) / per_copy);
  // Reflections: the mesh's texture coordinates from the first whole copy in
  // this draw (its vertices in order are the BLAS triangles' corners).
  if (RtReflectionsOn()) {
    const uint32_t whole = (start_vertex + per_copy - 1) / per_copy;
    auto mesh_it = rt_meshes_.find(key);
    if (mesh_it != rt_meshes_.end() && uint64_t(whole + 1) * per_copy <= uint64_t(start_vertex) + vertex_count)
      RtCaptureUv(mesh_it->second, base, RtUvInterp(current_ps_hash_), RtPaletteInterp(current_ps_hash_), false,
                  per_copy, whole * per_copy, 0);
  }
  for (uint32_t i = first; i <= last; ++i) {
    const uint32_t reg = iit->transforms + i * iit->per_instance;
    if (reg + iit->per_instance > 256) break;
    const float* t = c + reg * 4;
    RtInstance instance;
    instance.mesh = key;
    instance.source = 4;
    float* m = instance.transform;  // 3x4 rows
    if (iit->per_instance == 2) {
      const float* r = t + 4;
      const float rows[12] = {r[0], 0.0f, r[2], t[0], 0.0f, 1.0f, 0.0f, t[1], r[1], 0.0f, r[0], t[2]};
      std::memcpy(m, rows, sizeof(rows));
    } else {
      for (int k = 0; k < 3; ++k) {
        for (int col = 0; col < 4; ++col) m[k * 4 + col] = t[col * 4 + k];
      }
    }
    bool finite = true;
    for (int k = 0; k < 12; ++k) finite = finite && std::isfinite(m[k]);
    if (!finite) continue;
    ++rt_stats_.instanced_copies;
    if (rt_range > 0.0f && rt_camera_valid_ && !in_rt_range(i)) continue;
    if (dump) RtDumpObj(base, ps.buffer.address + ps.offset + offsets[0], ps.stride, indices, m, RtInstanceKey(instance));
    RtAddInstance(instance);
  }
}

// Graphics Options rows 0-3: RT shadows, softness, character shadows, RT
// reflections. Applied once a frame uploaded the ray-traced scene.
void Renderer::ReportRtFeatures(graphics::GraphicsBackendReport& report) {
  const bool capable = RtAvailable();
  const char* reason = !ray_tracing_capable_ ? "DXR 1.1 / shader model 6.5 is unavailable on this device"
                       : packet_sink_       ? "Ray tracing needs the Direct3D 12 renderer"
                                            : "Sun shadows and reflections traced in the game's own shaders";
  for (size_t i = 0; i < 4; ++i) report.features[i] = {capable, capable, reason};
  const bool applied = capable && rt_applied_frames_ && swap_number_ <= rt_last_applied_swap_ + 2;
  report.applied[0] = applied && RtShadowsOn() ? 1 : 0;
  report.applied[1] = report.applied[0] ? graphics_frame_request_.requested[1] : 0;
  report.applied[2] = applied && RtCharactersOn() && rt_skin_ready_ ? 1 : 0;
  report.applied[3] = applied && RtReflectionsOn() ? 1 : 0;
}
