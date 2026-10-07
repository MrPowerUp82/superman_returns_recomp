// Compare allocation traffic rather than wall-clock thresholds: replaying PM4
// must not allocate a second copy of the same captured guest bytes.
#include "../../port/src/graphics/guest/render_packet.h"
#include "../../port/src/native_renderer/pm4_mirror.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

static thread_local size_t allocated_bytes = 0;
static thread_local bool counting = false;
void* operator new(size_t size) {
  if (counting) allocated_bytes += size;
  if (auto p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }

int main() {
  using namespace superman_returns::graphics::guest;
  WorkBatch batch;
  WorkCmd cmd;
  cmd.op = Op::kDraw;
  cmd.u[0] = 4;
  cmd.range_count = 32;
  // A real Type0 PM4 write to VS c0.x precedes the captured data.
  cmd.ring_bytes = 8;
  batch.bytes = {0, 0, 0x40, 0, 0x3f, 0x80, 0, 0};
  batch.bytes.resize(8 + 32 * 2048, 7);
  for (uint32_t i = 0; i < cmd.range_count; ++i)
    batch.ranges.push_back({0x1000 + i * 2048, 2048, 8 + i * 2048});
  superman_returns::native::Pm4Mirror mirror;
  std::string error;
  auto measure = [&](bool replay) {
    allocated_bytes = 0;
    auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 1000; ++i) {
      RenderPacket packet;
      counting = true;
      bool ok = replay ? ReplayCapturedRenderPacket(batch, cmd, mirror, packet, error)
                       : DecodeRenderPacket(batch, cmd, mirror, packet, error);
      counting = false;
      if (!ok) { std::fprintf(stderr, "%s\n", error.c_str()); std::exit(2); }
    }
    auto us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - start).count() / 1000;
    auto bytes = allocated_bytes / 1000;
    std::printf("%s: %zu allocated bytes/command, %.3f us/command\n",
                replay ? "replay" : "decode", bytes, us);
    return bytes;
  };
  auto decode = measure(false), replay = measure(true);
  if (replay > decode) {
    std::fprintf(stderr, "Replay exceeds the allocation budget of one decode\n");
    return 1;
  }
}
