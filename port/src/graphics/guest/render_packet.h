#pragma once
#include "captured_batch.h"
#include "constant_snapshot.h"
#include "render_state.h"
#include "vertex_layout.h"
#include <array>
#include <variant>
namespace superman_returns::native {
class Pm4Mirror;
}
namespace superman_returns::graphics::guest {
using ResourceId = uint64_t;
struct BufferUpdate {
  BufferPlan plan;
  std::vector<uint8_t> bytes;
  std::vector<uint32_t> normalized;
};
struct VertexStream {
  uint32_t stream = 0, offset = 0, size = 0, stride = 0;
  BufferUpdate update;
};
struct SurfaceDesc {
  ResourceId id = 0;
  bool depth = false;
  SurfaceGeometry geometry{};
};
struct DrawPacket {
  int pass = 0;
  Primitive primitive = Primitive::kTriangles;
  bool indexed = false, inline_vertices = false;
  bool primitive_restart = false;
  int32_t depth_bias=0;float slope_bias=0;
  int32_t base_vertex = 0;
  uint32_t first = 0, count = 0, inline_stride = 0;
  ConstantSnapshot constants;
  CapturedMemory memory;
  std::vector<VertexStream> streams;
  std::vector<VertexAttribute> attributes;
  std::array<VertexFetchMeta, 224> vertex_fetch{};
  BufferUpdate indices;
  std::vector<uint32_t> expanded_indices;
  std::vector<uint8_t> inline_data;
  Viewport viewport;
  Rect scissor{0, 0, 16384, 16384};
  bool tiling_active = false;
  std::array<uint32_t, 0x400> registers{}, mirrored_registers{};
  std::array<std::array<uint32_t, 6>, 32> texture_fetch{};
  std::array<SurfaceDesc, 4> colors{};
  SurfaceDesc depth;
  ResourceId shader_a = 0, shader_b = 0;
  std::shared_ptr<const ShaderCapture> vertex_shader, pixel_shader;
  std::array<std::shared_ptr<const TextureCapture>, 32> textures;
  std::vector<std::pair<uint32_t, std::string>> texture_errors;
  uint64_t command_serial = 0;
  bool has_device_state = false;
};
struct ClearPacket {
  uint32_t flags;
  std::array<float, 4> color;
  float depth;
  uint32_t stencil;
  std::vector<Rect> rects;
  CapturedMemory memory;
  std::array<SurfaceDesc,4> colors{};
  SurfaceDesc depth_surface;
};
struct ResolvePacket {
  uint64_t command_serial = 0;
  uint32_t flags, destination;
  bool has_source_rect=false,has_destination_point=false,tiling_active=false;
  bool has_copy_draw=false,copy_dest_swap=false;
  Rect source{};
  std::array<int32_t, 2> destination_point{};
  std::array<float, 4> clear_color{};
  float clear_depth = 0;
  uint32_t clear_stencil = 0;
  uint32_t level=0,slice=0;
  SurfaceDesc source_surface;
  SurfaceDesc clear_color_surface,clear_depth_surface;
  std::array<uint32_t,6> destination_fetch{};
  CapturedMemory memory;
};
struct SwapPacket {
  ResourceId frontbuffer;
  uint64_t guest_swap;
  CapturedMemory memory;
  std::shared_ptr<const std::array<uint32_t,256>> gamma;
  bool gamma_enabled=false;
};
struct PassPacket {
  Op operation;
  int pass;
  std::vector<Rect> rects;
  std::array<float, 4> color{};
  float depth = 0;
  uint32_t stencil = 0;
  bool clear_color = false;
  SurfaceDesc color_surface, depth_surface;
};
using RenderPacket = std::variant<DrawPacket, ClearPacket, ResolvePacket,
                                  SwapPacket, PassPacket>;
bool DecodeRenderPacket(const WorkBatch &, const WorkCmd &,
                        const native::Pm4Mirror &, RenderPacket &,
                        std::string &);
bool ReplayCapturedRenderPacket(const WorkBatch&,const WorkCmd&,native::Pm4Mirror&,RenderPacket&,std::string&);
// Original scale1/MSAA1 shader ABI state. Descriptor indices are populated by
// the backend after binding resources; these semantic constants are common.
void FinalizeDrawConstants(DrawPacket &);
} // namespace superman_returns::graphics::guest
