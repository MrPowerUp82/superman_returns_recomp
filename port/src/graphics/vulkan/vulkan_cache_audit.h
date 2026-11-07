#pragma once
#include <atomic>
#include <cstdint>

namespace superman_returns::graphics::vulkan {
// Process-lifetime atomics: benchmark callbacks never dereference a Host/renderer
// or race with mutable stores. Attempts include failures, utility stores too.
struct VulkanCacheAudit {
  inline static std::atomic<uint64_t> frames{0},draws{0};
  inline static std::atomic<uint64_t> pipeline_attempts{0},pipeline_failures{0};
  inline static std::atomic<uint64_t> startup_attempts{0},startup_failures{0};
  inline static std::atomic<uint64_t> pipeline_hits{0},description_misses{0};
  inline static std::atomic<uint64_t> shader_library_hits{0},shader_memory_hits{0},shader_misses{0};
  inline static std::atomic<uint64_t> shader_jobs{0},shader_process_failures{0};
  inline static std::atomic<uint64_t> shader_active_jobs{0};
  inline static std::atomic<uint64_t> shader_compiler_invocations{0},shader_disk_hits{0},shader_unknown_audits{0};
  inline static std::atomic<uint64_t> shader_not_ready{0};
  inline static std::atomic<bool> opaque_frame_generation{false};
};
}
