#include "../../port/src/graphics/guest/captured_batch.h"
#include "../../port/src/graphics/guest/pm4_capture.h"
#include "../../port/src/graphics/guest/render_packet.h"
#include "../../port/src/native_renderer/pm4_mirror.h"
#include "test_main.h"
SR_TEST(packet_check_cadence_limits_debug_capture) {
  using superman_returns::graphics::guest::IsCaptureFrame;
  SR_CHECK(IsCaptureFrame(0,120));SR_CHECK(!IsCaptureFrame(119,120));
  SR_CHECK(IsCaptureFrame(120,120));SR_CHECK(IsCaptureFrame(7,1));
  SR_CHECK(IsCaptureFrame(7,0));
}
#include <bit>
#include <stdexcept>
using namespace superman_returns::graphics::guest;
SR_TEST(pm4_dependency_capture_preserves_caller_state_between_commands) {
  WorkBatch batch;
  // Type0 writes to VS c0.x, then PS c0.x in a later command.
  batch.bytes = {0, 0, 0x40, 0, 0x3f, 0x80, 0, 0,
                 0, 0, 0x44, 0, 0x40, 0, 0, 0};
  WorkCmd cmd;
  cmd.ring_bytes = 8;
  superman_returns::native::Pm4Mirror mirror;
  std::string error;
  auto no_dependencies = [](uint32_t, uint32_t) -> std::span<const uint8_t> {
    SR_CHECK(false); return {};
  };
  SR_CHECK(CapturePm4Dependencies(batch, cmd, no_dependencies, error, &mirror));
  SR_CHECK_EQ(mirror.reg(mirror.kAluConstantBase), 0x3f800000u);
  cmd.ring_offset = 8;
  SR_CHECK(CapturePm4Dependencies(batch, cmd, no_dependencies, error, &mirror));
  SR_CHECK_EQ(mirror.reg(mirror.kAluConstantBase), 0x3f800000u);
  SR_CHECK_EQ(mirror.reg(mirror.kAluConstantBase + 1024), 0x40000000u);
  SR_CHECK_EQ(mirror.packets, 2u);
  SR_CHECK(batch.ranges.empty());
}
SR_TEST(pm4_dependency_capture_owns_nested_sources) {
  WorkBatch batch;
  batch.bytes = {0xc0, 0x01, 0x3f, 0, 0, 0, 2, 0, 0, 0, 0, 4};
  WorkCmd cmd;
  cmd.ring_bytes = 12;
  std::vector<uint8_t> indirect = {0xc0, 0x02, 0x2f, 0, 0, 0, 1, 0,
                                   0,    0,    0,    0, 0, 0, 0, 1};
  std::vector<uint8_t> constant = {0x3f, 0x80, 0, 0};
  std::string error;
  SR_CHECK(CapturePm4Dependencies(
      batch, cmd,
      [&](uint32_t address, uint32_t length) -> std::span<const uint8_t> {
        if (address == 0xa0000200 && length == 16)
          return indirect;
        if (address == 0xa0000100 && length == 4)
          return constant;
        return {};
      },
      error));
  cmd.range_count = uint32_t(batch.ranges.size());
  indirect.clear();
  constant.clear();
  CapturedMemory memory;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, memory, error));
  superman_returns::native::Pm4Mirror mirror;
  mirror.ScanCopyUsing(batch.bytes.data(), cmd.ring_bytes,
                       [&](uint32_t address, uint32_t length) {
                         return memory.Read(address, length);
                       });
  SR_CHECK_EQ(mirror.reg(mirror.kAluConstantBase), 0x3f800000u);
}
// Same ring as pm4_dependency_capture_owns_nested_sources: an indirect buffer
// (16 bytes) that in turn loads a 4-byte ALU constant, so two guest reads.
SR_TEST(pm4_capture_probe_counts_reads_and_changes_nothing) {
  const std::vector<uint8_t> ring = {0xc0, 0x01, 0x3f, 0, 0, 0, 2, 0, 0, 0, 0, 4};
  const std::vector<uint8_t> indirect = {0xc0, 0x02, 0x2f, 0, 0, 0, 1, 0,
                                         0,    0,    0,    0, 0, 0, 0, 1};
  const std::vector<uint8_t> constant = {0x3f, 0x80, 0, 0};
  auto reader = [&](uint32_t address, uint32_t length) -> std::span<const uint8_t> {
    if (address == 0xa0000200 && length == 16) return indirect;
    if (address == 0xa0000100 && length == 4) return constant;
    return {};
  };
  WorkCmd cmd;
  cmd.ring_bytes = uint32_t(ring.size());

  WorkBatch plain;
  plain.bytes.assign(ring.begin(), ring.end());
  std::string plain_error;
  const bool plain_ok = CapturePm4Dependencies(plain, cmd, reader, plain_error);

  WorkBatch probed;
  probed.bytes.assign(ring.begin(), ring.end());
  std::string probed_error;
  Pm4CaptureProbe probe;
  const bool probed_ok =
      CapturePm4Dependencies(probed, cmd, reader, probed_error, nullptr, &probe);

  SR_CHECK(plain_ok);
  SR_CHECK_EQ(probed_ok, plain_ok);
  SR_CHECK(plain_error == probed_error);
  SR_CHECK(plain.bytes == probed.bytes);
  SR_CHECK_EQ(plain.ranges.size(), 2u);
  SR_CHECK_EQ(probed.ranges.size(), plain.ranges.size());
  for (size_t i = 0; i < plain.ranges.size(); ++i) {
    SR_CHECK_EQ(probed.ranges[i].address, plain.ranges[i].address);
    SR_CHECK_EQ(probed.ranges[i].length, plain.ranges[i].length);
    SR_CHECK_EQ(probed.ranges[i].offset, plain.ranges[i].offset);
  }
  SR_CHECK_EQ(probe.reads, 2u);
  SR_CHECK_EQ(probe.bytes, 20u);
  SR_CHECK(probe.copy_ns > 0 || probe.read_ns > 0 || probe.primary_ns > 0);

  // A failing dependency: same error and rollback with and without the probe;
  // only successful reads are counted.
  auto missing_constant = [&](uint32_t address, uint32_t length) -> std::span<const uint8_t> {
    if (address == 0xa0000200 && length == 16) return indirect;
    return {};
  };
  WorkBatch plain_fail, probed_fail;
  plain_fail.bytes.assign(ring.begin(), ring.end());
  probed_fail.bytes.assign(ring.begin(), ring.end());
  std::string plain_fail_error, probed_fail_error;
  Pm4CaptureProbe fail_probe;
  const bool plain_fail_ok =
      CapturePm4Dependencies(plain_fail, cmd, missing_constant, plain_fail_error);
  const bool probed_fail_ok = CapturePm4Dependencies(
      probed_fail, cmd, missing_constant, probed_fail_error, nullptr, &fail_probe);
  SR_CHECK(!plain_fail_ok);
  SR_CHECK_EQ(probed_fail_ok, plain_fail_ok);
  SR_CHECK(!plain_fail_error.empty());
  SR_CHECK(plain_fail_error == probed_fail_error);
  SR_CHECK(plain_fail.bytes == probed_fail.bytes);
  SR_CHECK_EQ(plain_fail.ranges.size(), probed_fail.ranges.size());
  SR_CHECK_EQ(fail_probe.reads, 1u);
  SR_CHECK_EQ(fail_probe.bytes, 16u);
}
SR_TEST(captured_memory_owns_bytes_after_source_reuse) {
  WorkBatch batch;
  batch.bytes = {1, 2, 3, 4, 5, 6};
  batch.ranges = {{0x1000, 4, 0}, {0x2000, 2, 4}};
  WorkCmd cmd;
  cmd.range_count = 2;
  CapturedMemory memory;
  std::string error;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, memory, error));
  batch.Clear();
  SR_CHECK_EQ(memory.Read(0x1000, 4)[2], 3);
  SR_CHECK_EQ(memory.Read(0x2000, 2)[1], 6);
}
SR_TEST(capture_range_overflow_is_rejected) {
  WorkBatch batch;
  batch.bytes = {1, 2, 3, 4};
  WorkCmd cmd;
  cmd.range_count = 1;
  CapturedMemory memory;
  std::string error;
  batch.ranges = {{UINT32_MAX, 4, 0}};
  SR_CHECK(!CapturedMemory::Capture(batch, cmd, memory, error));
  batch.ranges = {{0x1000, 4, UINT32_MAX}};
  SR_CHECK(!CapturedMemory::Capture(batch, cmd, memory, error));
  cmd.range_first = UINT32_MAX;
  SR_CHECK(!CapturedMemory::Capture(batch, cmd, memory, error));
}
SR_TEST(capture_overlap_uses_latest_range_without_live_fallback) {
  WorkBatch batch;
  batch.bytes = {1, 2, 3, 4, 9, 8, 7, 6};
  batch.ranges = {{0x1000, 4, 0}, {0x1000, 4, 4}};
  WorkCmd cmd;
  cmd.range_count = 2;
  CapturedMemory memory;
  std::string error;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, memory, error));
  SR_CHECK_EQ(memory.Read(0x1001, 2)[0], 8);
  bool rejected = false;
  try {
    memory.Read(0x1003, 2);
  } catch (const std::out_of_range &) {
    rejected = true;
  }
  SR_CHECK(rejected);
}
SR_TEST(capture_command_ranges_do_not_leak_other_draws) {
  WorkBatch batch;
  batch.bytes = {1, 2, 3, 4};
  batch.ranges = {{0x1000, 2, 0}, {0x1000, 2, 2}};
  WorkCmd cmd;
  cmd.range_count = 1;
  CapturedMemory first, second;
  std::string error;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, first, error));
  cmd.range_first = 1;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, second, error));
  SR_CHECK_EQ(first.Read(0x1000, 2)[0], 1);
  SR_CHECK_EQ(second.Read(0x1000, 2)[0], 3);
}
SR_TEST(packet_owns_constants_and_buffers_after_guest_mutation) {
  WorkBatch batch;
  batch.bytes = {1, 2, 3, 4};
  batch.ranges = {{0xA0001000, 4, 0}};
  StreamPlan stream;
  stream.stride = 4;
  stream.size = 4;
  stream.buffer = {};
  stream.buffer.address = 0x1000;
  stream.buffer.size = 4;
  stream.buffer.action = 2;
  stream.buffer.end = 4;
  batch.streams.push_back(stream);
  WorkCmd cmd;
  cmd.op = Op::kDraw;
  cmd.u[0] = 4;
  cmd.u[2] = 1;
  cmd.range_count = 1;
  cmd.stream_count = 1;
  auto shader = std::make_shared<ShaderCapture>();
  shader->hash = 0x1234;
  shader->vertex = true;
  shader->container = {1, 2, 3, 4};
  cmd.vertex_shader = shader;
  shader.reset();
  superman_returns::native::Pm4Mirror mirror;
  // Type0 VS c0.x=1 in guest byte order.
  uint8_t words[] = {0, 0, 0x40, 0, 0x3f, 0x80, 0, 0};
  mirror.ScanCopy(nullptr, words, sizeof(words));
  RenderPacket packet;
  std::string error;
  SR_CHECK(DecodeRenderPacket(batch, cmd, mirror, packet, error));
  batch.Clear();
  mirror = superman_returns::native::Pm4Mirror{};
  const auto &draw = std::get<DrawPacket>(packet);
  SR_CHECK_EQ(draw.constants.vs[0], 0x3f800000);
  SR_CHECK_EQ(draw.streams[0].update.bytes[2], 3);
  cmd.vertex_shader.reset();
  SR_CHECK_EQ(draw.vertex_shader->hash, 0x1234u);
  SR_CHECK_EQ(draw.vertex_shader->container[2], 3);
}
SR_TEST(strip_and_ushort2_metadata_survive_packet) {
  WorkBatch batch;
  batch.bytes = {0, 0, 0, 1, 0, 0xff, 0xff, 0xfe, 0, 0, 0, 2};
  batch.ranges = {{0xA0001000, 12, 0}};
  WorkCmd cmd;
  cmd.op = Op::kDrawIndexed;
  cmd.u[0] = 6;
  cmd.u[3] = 3;
  cmd.range_count = 1;
  cmd.has_index = true;
  cmd.index32 = true;
  cmd.index.address = 0x1000;
  cmd.index.size = 12;
  cmd.index.action = 2;
  cmd.index.end = 12;
  cmd.index.format = 2 | (2 << 2);
  cmd.index.reset_index = 0xfffffe;
  batch.bytes.resize(12 + 0x40, 0);
  // One POSITION0 declaration, stream0, USHORT2, offset0.
  batch.bytes[12 + 0x1b] = 1;
  batch.bytes[12 + 0x39] = 0x2c;
  batch.bytes[12 + 0x3a] = 0x22;
  batch.bytes[12 + 0x3b] = 0x59;
  batch.ranges.push_back({0x3000, 0x40, 12});
  cmd.range_count = 2;
  cmd.stream_count = 1;
  StreamPlan stream;
  stream.stride = 4;
  stream.buffer.decl = 0x3000;
  batch.streams.push_back(stream);
  superman_returns::native::Pm4Mirror mirror;
  RenderPacket packet;
  std::string error;
  SR_CHECK(DecodeRenderPacket(batch, cmd, mirror, packet, error));
  const auto &draw = std::get<DrawPacket>(packet);
  SR_CHECK(draw.indices.normalized ==
           std::vector<uint32_t>({1, UINT32_MAX, 2}));
    SR_CHECK(draw.primitive == Primitive::kTriangleStrip);
    SR_CHECK(draw.primitive_restart);
  SR_CHECK_EQ(draw.vertex_fetch[0].type, 0x2c2259);
  SR_CHECK_EQ(draw.vertex_fetch[0].stride, 4);
  VertexFetchMeta metadata;
  std::memcpy(&metadata, draw.constants.shared.data() + 512, sizeof(metadata));
  SR_CHECK_EQ(metadata.type, 0x2c2259);
  // Reused guest addresses and later partial captures must invalidate/override
  // cached declaration bytes without changing an already-owned packet.
  batch.bytes[12+0x3b]=0x5f;
  RenderPacket changed;
  SR_CHECK(DecodeRenderPacket(batch,cmd,mirror,changed,error));
  SR_CHECK_EQ(std::get<DrawPacket>(changed).vertex_fetch[0].type,0x2c225fu);
  auto patch=uint32_t(batch.bytes.size());
  batch.bytes.insert(batch.bytes.end(),{0,0x2c,0x22,0x59});
  batch.ranges.push_back({0x3038,4,patch});++cmd.range_count;
  SR_CHECK(DecodeRenderPacket(batch,cmd,mirror,changed,error));
  SR_CHECK_EQ(std::get<DrawPacket>(changed).vertex_fetch[0].type,0x2c2259u);
  SR_CHECK_EQ(draw.vertex_fetch[0].type,0x2c2259u);
}
SR_TEST(same_capture_decodes_identically_for_both_backends) {
  WorkBatch batch;
  WorkCmd cmd;
  cmd.op = Op::kSwap;
  cmd.u[0] = 0x12345678;
  cmd.u64 = 999;
  superman_returns::native::Pm4Mirror mirror;
  RenderPacket d3d, vulkan;
  std::string error;
  SR_CHECK(DecodeRenderPacket(batch, cmd, mirror, d3d, error));
  SR_CHECK(DecodeRenderPacket(batch, cmd, mirror, vulkan, error));
  SR_CHECK_EQ(std::get<SwapPacket>(d3d).frontbuffer,
              std::get<SwapPacket>(vulkan).frontbuffer);
  SR_CHECK_EQ(std::get<SwapPacket>(vulkan).guest_swap, 999);
  cmd.op=Op::kDrawInline;
  cmd.u[0]=4;cmd.u[1]=0x1000;cmd.u[2]=3;cmd.u[3]=4;
  batch.bytes={1,2,3,4,5,6,7,8,9,10,11,12};
  batch.ranges={{0x1000,12,0}};cmd.range_count=1;
  SR_CHECK(DecodeRenderPacket(batch,cmd,mirror,d3d,error));
  SR_CHECK(DecodeRenderPacket(batch,cmd,mirror,vulkan,error));
  auto& a=std::get<DrawPacket>(d3d);auto& b=std::get<DrawPacket>(vulkan);
  SR_CHECK(a.inline_data==b.inline_data);
  SR_CHECK(a.constants.vs==b.constants.vs);
  SR_CHECK(a.constants.ps==b.constants.ps);
  SR_CHECK(a.constants.shared==b.constants.shared);
  SR_CHECK(a.primitive==b.primitive);
  SR_CHECK_EQ(a.count,b.count);SR_CHECK_EQ(a.inline_stride,b.inline_stride);
}

SR_TEST(screen_shader_constants_preserve_pixel_centers_and_alpha_test) {
  DrawPacket draw;
  draw.has_device_state = true;
  draw.colors[0] = {1, false, {640, 360, 0, 0, 0, 0}};
  draw.viewport = {0, 0, 1280, 720, 0, 1};
  draw.registers[0x206] = 0;       // screen coordinates
  draw.registers[0x202] = 0x8 | 6; // alpha GEQUAL
  draw.registers[0x10e] = std::bit_cast<uint32_t>(0.5f);
  FinalizeDrawConstants(draw);
  auto f = [&](uint32_t word) {
    float v;
    std::memcpy(&v, draw.constants.shared.data() + word * 4, 4);
    return v;
  };
  SR_CHECK(f(73) == 1.0f / 640);
  SR_CHECK(f(74) == -1.0f / 360);
  SR_CHECK(f(108) == 2.0f / 640);
  SR_CHECK(f(109) == -2.0f / 360);
  SR_CHECK(f(110) == -1.0f);
  SR_CHECK(f(111) == 1.0f);
  SR_CHECK(f(75) == 0.5f);
  SR_CHECK(f(113) == 1.0f);
  uint32_t spec;
  std::memcpy(&spec, draw.constants.shared.data() + 112 * 4, 4);
  SR_CHECK_EQ(spec, 2u);
}

SR_TEST(indexed_quads_require_owned_expansion) {
  WorkBatch batch;
  WorkCmd cmd;
  cmd.op = Op::kDrawIndexed;
  cmd.u[0] = 13;
  cmd.u[3] = 4;
  cmd.has_index = true;
  RenderPacket out = SwapPacket{99, 7};
  std::string error;
  superman_returns::native::Pm4Mirror mirror;
  SR_CHECK(!DecodeRenderPacket(batch, cmd, mirror, out, error));
  SR_CHECK(std::holds_alternative<SwapPacket>(out));
  SR_CHECK_EQ(std::get<SwapPacket>(out).frontbuffer, 99u);
}

SR_TEST(resolve_packet_keeps_mip_slice_and_owned_regions) {
  WorkBatch batch;
  batch.bytes={0,0,0,1,0,0,0,2,0,0,0,9,0,0,0,10};
  batch.ranges={{0x1000,16,0}};
  WorkCmd cmd;cmd.op=Op::kResolve;cmd.u[1]=0x1000;
  cmd.u[6]=2;cmd.u[7]=5;cmd.range_count=1;
  superman_returns::native::Pm4Mirror mirror;
  RenderPacket out;std::string error;
  SR_CHECK(DecodeRenderPacket(batch,cmd,mirror,out,error));
  batch.Clear();
  const auto& resolve=std::get<ResolvePacket>(out);
  SR_CHECK_EQ(resolve.level,2u);SR_CHECK_EQ(resolve.slice,5u);
  SR_CHECK_EQ(resolve.source.left,1);SR_CHECK_EQ(resolve.source.bottom,10);
}
SR_TEST(captured_replay_rejects_unreadable_indirect_and_failed_capture) {
  WorkBatch batch;batch.bytes={0xc0,0x01,0x3f,0,0,0,2,0,0,0,0,4};WorkCmd cmd;cmd.op=Op::kRing;cmd.ring_bytes=12;
  superman_returns::native::Pm4Mirror mirror;RenderPacket packet;std::string error;
  SR_CHECK(!ReplayCapturedRenderPacket(batch,cmd,mirror,packet,error));SR_CHECK(!error.empty());
  cmd.pm4_capture_ok=false;SR_CHECK(!ReplayCapturedRenderPacket(batch,cmd,mirror,packet,error));SR_CHECK(error.find("capture")!=std::string::npos);
}
SR_TEST(captured_replay_checks_primary_bounds_and_decodes_without_live_memory) {
  WorkBatch batch;WorkCmd cmd;cmd.op=Op::kRing;cmd.pass=7;superman_returns::native::Pm4Mirror mirror;RenderPacket packet;std::string error;
  SR_CHECK(ReplayCapturedRenderPacket(batch,cmd,mirror,packet,error));SR_CHECK_EQ(std::get<PassPacket>(packet).pass,7);
  cmd.ring_offset=UINT32_MAX;cmd.ring_bytes=8;SR_CHECK(!ReplayCapturedRenderPacket(batch,cmd,mirror,packet,error));
}
SR_TEST(shader_roles_follow_owned_container_stage_when_device_slots_are_reversed) {
  WorkBatch batch;WorkCmd cmd;cmd.op=Op::kDraw;cmd.u[0]=4;
  auto vs=std::make_shared<ShaderCapture>(),ps=std::make_shared<ShaderCapture>();vs->vertex=true;vs->hash=17;ps->hash=29;
  cmd.vertex_shader=ps;cmd.pixel_shader=vs;
  superman_returns::native::Pm4Mirror mirror;RenderPacket packet;std::string error;
  SR_CHECK(DecodeRenderPacket(batch,cmd,mirror,packet,error));const auto& draw=std::get<DrawPacket>(packet);
  SR_CHECK(draw.vertex_shader==vs);SR_CHECK(draw.pixel_shader==ps);
}
SR_TEST(contiguous_capture_fast_path_rejects_partial_overlays) {
  WorkBatch batch;
  batch.bytes={1,2,3,4,9,8};
  batch.ranges={{0x1000,4,0},{0x1001,2,4}};
  WorkCmd cmd;cmd.range_count=2;
  CapturedMemory memory;std::string error;
  SR_CHECK(CapturedMemory::Capture(batch,cmd,memory,error));
  SR_CHECK(memory.ReadUnambiguous(0x1000,4).empty());
  SR_CHECK_EQ(memory.ReadUnambiguous(0x1001,2)[0],9);
  SR_CHECK_EQ(memory.Read(0x1000,4)[1],2);
}
SR_TEST(replayed_draw_keeps_latest_capture_and_pm4_constants_after_batch_reuse) {
  WorkBatch batch;
  // Primary PM4 writes VS c0.x; overlapping captures model a later guest write.
  batch.bytes = {0, 0, 0x40, 0, 0x3f, 0x80, 0, 0, 1, 2, 3, 4, 9, 8};
  batch.ranges = {{0x1000, 4, 8}, {0x1001, 2, 12}};
  WorkCmd cmd;
  cmd.op = Op::kDraw;
  cmd.u[0] = 4;
  cmd.ring_bytes = 8;
  cmd.range_count = 2;
  superman_returns::native::Pm4Mirror mirror;
  RenderPacket packet;
  std::string error;
  SR_CHECK(ReplayCapturedRenderPacket(batch, cmd, mirror, packet, error));
  batch.Clear();
  batch.bytes.resize(1024, 0xff);
  mirror = superman_returns::native::Pm4Mirror{};
  const auto& draw = std::get<DrawPacket>(packet);
  SR_CHECK_EQ(draw.constants.vs[0], 0x3f800000u);
  SR_CHECK_EQ(draw.memory.Read(0x1001, 2)[0], 9);
  SR_CHECK_EQ(draw.memory.Read(0x1000, 4)[1], 2);
}
