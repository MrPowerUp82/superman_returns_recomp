#pragma once
#include <atomic>
#include <cstdint>

namespace superman_returns::graphics {
// Shared by standalone graphics libraries and the native renderer. No SDK,
// renderer instance or logging dependency; counters have process lifetime.
inline std::atomic<uint64_t> g_runtime_pipeline_creates{0};
inline std::atomic<uint64_t> g_diagnostic_pipeline_creates{0};
inline std::atomic<uint64_t> g_cache_audit_frames{0}, g_cache_audit_draws{0};
inline void RecordRuntimePipelineCreate() { ++g_runtime_pipeline_creates; }
inline uint64_t RuntimePipelineCreates() { return g_runtime_pipeline_creates.load(); }
inline void RecordDiagnosticPipelineCreate() { ++g_diagnostic_pipeline_creates; }
inline uint64_t DiagnosticPipelineCreates() { return g_diagnostic_pipeline_creates.load(); }
inline void RecordCacheAuditFrame() { ++g_cache_audit_frames; }
inline void RecordCacheAuditDraw() { ++g_cache_audit_draws; }
}
