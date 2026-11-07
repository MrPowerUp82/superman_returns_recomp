// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/pipeline_cache.cpp (Conan native renderer).
// The kit ships no license file at that revision; parts derived from Xenia /
// ReXGlue keep their BSD license. Changes for Superman Returns are listed in
// docs/native-port-plan.md section 2.
//
#include "pipeline_cache.h"
#include "../graphics/pipeline_creation_audit.h"
#include "../graphics/d3d12/post_vertex_clip_capture.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <rex/filesystem.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/hash.h>

REXCVAR_DEFINE_BOOL(sr_native_debug_no_embedded_pipelines, false, "Superman Returns Native",
                    "Debug: ignore the embedded pipeline base (worst case for the async path)");

namespace superman_returns::native {

namespace {

constexpr char kMagic[4] = {'C', 'N', 'P', 'S'};
// Bump when PsoRecord or the renderer's pipeline key changes.
constexpr uint32_t kVersion = 2;
constexpr size_t kLegacyRecordSize = offsetof(PsoRecord, pixel_variant);
static_assert(kLegacyRecordSize == 696, "CNPS v1 ABI changed; add an explicit migration");

struct FileHeader {
  char magic[4];
  uint32_t version;
  uint32_t record_size;
  uint32_t reserved;
};

bool ValidHeader(const FileHeader& h) {
  return std::memcmp(h.magic, kMagic, 4) == 0 && h.reserved == 0 &&
      ((h.version == kVersion && h.record_size == sizeof(PsoRecord)) ||
       (h.version == 1 && h.record_size == kLegacyRecordSize));
}

bool ValidRecord(const PsoRecord& r) {
  return r.pixel_variant <= 3 && r.pipeline_kind <= 1 &&
      (!r.pipeline_kind || (!r.pixel_variant && !r.ps_hash)) &&
      r.element_count <= PsoRecord::kMaxElements &&
      r.num_render_targets <= 8 && r.sample_count > 0 && r.sample_count <= 16;
}

void ReadRecords(const uint8_t* data, size_t bytes, uint32_t stride, std::vector<PsoRecord>& out) {
  const size_t count = bytes / stride;
  if (bytes % stride) REXLOG_WARN("native cache audit: truncated CNPS tail ignored");
  for (size_t i = 0; i < count; ++i) {
    PsoRecord record{};
    std::memcpy(&record, data + i * stride, stride);
    if (ValidRecord(record)) out.push_back(record);
    else REXLOG_WARN("native cache audit: invalid CNPS record ignored index={}", i);
  }
}

// Records embedded in superman_returns.exe (RCDATA 3): same format as the local file.
void LoadEmbedded(std::vector<PsoRecord>& out) {
  HMODULE module = GetModuleHandleW(nullptr);
  HRSRC res = FindResourceW(module, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(10));  // RT_RCDATA
  if (!res) return;
  HGLOBAL handle = LoadResource(module, res);
  const uint8_t* data = handle ? static_cast<const uint8_t*>(LockResource(handle)) : nullptr;
  DWORD size = SizeofResource(module, res);
  if (!data || size < sizeof(FileHeader)) return;
  FileHeader h;
  std::memcpy(&h, data, sizeof(h));
  if (!ValidHeader(h)) return;
  ReadRecords(data + sizeof(FileHeader), size - sizeof(FileHeader), h.record_size, out);
}

void LoadFile(const std::filesystem::path& path, std::vector<PsoRecord>& out) {
  std::FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return;
  FileHeader h{};
  if (std::fread(&h, sizeof(h), 1, f) == 1 && ValidHeader(h)) {
    PsoRecord r{};
    while (std::fread(&r, h.record_size, 1, f) == 1) {
      if (ValidRecord(r)) out.push_back(r);
      else REXLOG_WARN("native cache audit: invalid local CNPS record ignored");
      r = {};
    }
  }
  std::fclose(f);
}

}  // namespace

void PipelineCache::RecordRuntimeCreate() { graphics::RecordRuntimePipelineCreate(); }
uint64_t PipelineCache::RuntimeCreates() { return graphics::RuntimePipelineCreates(); }
void PipelineCache::RecordDiagnosticCreate() { graphics::RecordDiagnosticPipelineCreate(); }
uint64_t PipelineCache::DiagnosticCreates() { return graphics::DiagnosticPipelineCreates(); }

D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineCache::MakeDesc(
    const PsoRecord& r, ID3D12RootSignature* root_signature, const void* vs, size_t vs_size,
    const void* ps, size_t ps_size, SemanticName semantic,
    std::vector<D3D12_INPUT_ELEMENT_DESC>& elements) {
  elements.clear();
  for (uint32_t i = 0; i < std::min(r.element_count, PsoRecord::kMaxElements); ++i) {
    const auto& e = r.elements[i];
    D3D12_INPUT_ELEMENT_DESC d{};
    d.SemanticName = semantic(e.usage);
    d.SemanticIndex = e.usage_index;
    d.Format = DXGI_FORMAT(e.format);
    d.InputSlot = e.slot;
    d.AlignedByteOffset = e.offset;
    d.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    elements.push_back(d);
  }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
  desc.pRootSignature = root_signature;
  desc.VS = {vs, vs_size};
  if (ps) desc.PS = {ps, ps_size};
  desc.InputLayout = {elements.data(), uint32_t(elements.size())};
  desc.BlendState = r.blend;
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState = r.raster;
  desc.DepthStencilState = r.depth_stencil;
  desc.DSVFormat = r.dsv_format;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE(r.topology_type);
  desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE(r.strip_cut);
  desc.NumRenderTargets = r.num_render_targets;
  for (uint32_t i = 0; i < 8; ++i) desc.RTVFormats[i] = r.rtv_formats[i];
  desc.SampleDesc.Count = r.sample_count;
  if (r.pipeline_kind == 1) {
    desc.GS = graphics::d3d12::PostVertexClipCapture::GeometryShader();
    static const D3D12_SO_DECLARATION_ENTRY output{0,"TEXCOORD",15,0,4,0};
    static const uint32_t stride = 16;
    desc.StreamOutput = {&output,1,&stride,1,D3D12_SO_NO_RASTERIZED_STREAM};
  }
  return desc;
}

uint64_t PipelineCache::DescHash(const PsoRecord& r) {
  // Field by field: the D3D12 structs contain padding bytes.
  std::vector<uint32_t> v;
  v.reserve(160);
  auto add64 = [&](uint64_t x) {
    v.push_back(uint32_t(x));
    v.push_back(uint32_t(x >> 32));
  };
  add64(r.vs_hash);
  add64(r.ps_hash);
  v.push_back(r.pixel_variant);
  v.push_back(r.pipeline_kind);
  v.push_back(uint32_t(r.blend.AlphaToCoverageEnable));
  v.push_back(uint32_t(r.blend.IndependentBlendEnable));
  for (const auto& t : r.blend.RenderTarget) {
    v.insert(v.end(), {uint32_t(t.BlendEnable), uint32_t(t.LogicOpEnable), uint32_t(t.SrcBlend),
                       uint32_t(t.DestBlend), uint32_t(t.BlendOp), uint32_t(t.SrcBlendAlpha),
                       uint32_t(t.DestBlendAlpha), uint32_t(t.BlendOpAlpha), uint32_t(t.LogicOp),
                       uint32_t(t.RenderTargetWriteMask)});
  }
  const auto& ra = r.raster;
  uint32_t slope, clamp;
  std::memcpy(&slope, &ra.SlopeScaledDepthBias, 4);
  std::memcpy(&clamp, &ra.DepthBiasClamp, 4);
  v.insert(v.end(), {uint32_t(ra.FillMode), uint32_t(ra.CullMode), uint32_t(ra.FrontCounterClockwise),
                     uint32_t(ra.DepthBias), clamp, slope, uint32_t(ra.DepthClipEnable),
                     uint32_t(ra.MultisampleEnable), uint32_t(ra.AntialiasedLineEnable),
                     uint32_t(ra.ForcedSampleCount), uint32_t(ra.ConservativeRaster)});
  const auto& ds = r.depth_stencil;
  auto face = [&](const D3D12_DEPTH_STENCILOP_DESC& f) {
    v.insert(v.end(), {uint32_t(f.StencilFailOp), uint32_t(f.StencilDepthFailOp),
                       uint32_t(f.StencilPassOp), uint32_t(f.StencilFunc)});
  };
  v.insert(v.end(), {uint32_t(ds.DepthEnable), uint32_t(ds.DepthWriteMask), uint32_t(ds.DepthFunc),
                     uint32_t(ds.StencilEnable), uint32_t(ds.StencilReadMask),
                     uint32_t(ds.StencilWriteMask)});
  face(ds.FrontFace);
  face(ds.BackFace);
  for (DXGI_FORMAT f : r.rtv_formats) v.push_back(uint32_t(f));
  v.insert(v.end(), {uint32_t(r.dsv_format), r.num_render_targets, r.sample_count, r.topology_type,
                     r.strip_cut, r.element_count});
  for (uint32_t i = 0; i < std::min(r.element_count, PsoRecord::kMaxElements); ++i) {
    const auto& e = r.elements[i];
    v.insert(v.end(), {uint32_t(e.usage) | (uint32_t(e.usage_index) << 8) | (uint32_t(e.slot) << 16),
                       e.format, e.offset});
  }
  return XXH3_64bits(v.data(), v.size() * sizeof(uint32_t));
}

void PipelineCache::Start(ID3D12Device* device, ID3D12RootSignature* root_signature,
                          ShaderLookup shaders, SemanticName semantic, uint32_t msaa_samples,
                          bool foliage_a2c, VariantLookup variants_lookup) {
  device_ = device;
  root_signature_ = root_signature;
  shaders_ = shaders;
  semantic_ = semantic;
  variants_ = variants_lookup;
  path_ = rex::filesystem::GetExecutableFolder() / "superman_returns_pipelines.bin";

  std::vector<PsoRecord> records;
  if (!REXCVAR_GET(sr_native_debug_no_embedded_pipelines)) LoadEmbedded(records);
  size_t embedded = records.size();
  LoadFile(path_, records);
  for (const auto& record : records) persisted_desc_.insert(DescHash(record));
  size_t variants = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto enqueue = [&](const PsoRecord& r) {
      // Every loaded key is already persisted, even when another key has the
      // same D3D12 description. Otherwise Record() appends those aliases to
      // the local cache again on every launch.
      known_.insert(r.key);
      auto alias = [&](uint64_t canonical) {
        if (ambiguous_keys_.count(r.key)) return;
        const auto previous = aliases_.find(r.key);
        if (previous != aliases_.end() && previous->second != canonical) {
          aliases_.erase(previous);
          ambiguous_keys_.insert(r.key);
        } else {
          aliases_[r.key] = canonical;
        }
      };
      uint64_t dh = DescHash(r);
      auto existing = by_desc_.find(dh);
      if (existing != by_desc_.end()) {
        alias(existing->second);
        return false;
      }
      // A persisted renderer key may describe different host settings. Keep
      // every description and use a private canonical key on collision.
      PsoRecord canonical = r;
      while (scheduled_.count(canonical.key)) ++canonical.key;
      alias(canonical.key);
      by_desc_[dh] = canonical.key;
      scheduled_.insert(canonical.key);
      queue_.push_back(canonical);
      return true;
    };
    // Settings variants of records captured on multisampled targets: the same
    // draw at the current MSAA sample count, with and without alpha to
    // coverage (foliage antialiasing converts opaque alpha-tested draws). They
    // carry a synthetic key (never produced by the renderer) and are found by
    // description (TakeByDesc). Originals of another sample count are only
    // converted: the renderer cannot request them with these settings.
    std::vector<PsoRecord> generated;
    for (const PsoRecord& r : records) {
      if (r.sample_count <= 1) {
        generated.push_back(r);
        continue;
      }
      PsoRecord v = r;
      v.sample_count = msaa_samples;
      if (msaa_samples <= 1) v.blend.AlphaToCoverageEnable = FALSE;
      bool opaque = r.ps_hash && r.num_render_targets && !r.blend.RenderTarget[0].BlendEnable;
      if (r.sample_count == msaa_samples) generated.push_back(r);
      PsoRecord base_variant = v;
      base_variant.key = DescHash(v) | 1;  // synthetic (odd) key
      generated.push_back(base_variant);
      if (msaa_samples > 1 && opaque) {
        PsoRecord a2c = v;
        a2c.blend.AlphaToCoverageEnable = foliage_a2c ? TRUE : FALSE;
        a2c.key = DescHash(a2c) | 1;
        generated.push_back(a2c);
      }
    }
    std::unordered_set<uint64_t> original;
    for (const PsoRecord& r : records) original.insert(r.key);
    for (const PsoRecord& r : generated) {
      if (enqueue(r) && !original.count(r.key)) ++variants;
    }
  }
  // Start a fresh local file when missing or from another version.
  {
    std::FILE* f = _wfopen(path_.c_str(), L"rb");
    FileHeader h{};
    bool valid = f && std::fread(&h, sizeof(h), 1, f) == 1 && ValidHeader(h);
    if (f) std::fclose(f);
    if (valid && h.version == 1) {
      // Keep the owner's v1 cache before replacing it; never append v2 records
      // to an old ABI. A failed backup/migration disables writes for this run.
      std::error_code ec;
      auto backup = path_;
      backup += L".v1.bak";
      if (!std::filesystem::exists(backup, ec))
        std::filesystem::copy_file(path_, backup, std::filesystem::copy_options::none, ec);
      auto pending = path_;
      pending += L".v2.pending";
      bool migrated = !ec;
      if (migrated) {
        std::FILE* w = _wfopen(pending.c_str(), L"wb");
        FileHeader nh{{'C', 'N', 'P', 'S'}, kVersion, uint32_t(sizeof(PsoRecord)), 0};
        const size_t local = records.size() - embedded;
        migrated = w && std::fwrite(&nh, sizeof(nh), 1, w) == 1;
        if (migrated && local)
          migrated = std::fwrite(records.data() + embedded, sizeof(PsoRecord), local, w) == local;
        if (w && std::fclose(w) != 0) migrated = false;
        if (migrated) migrated = MoveFileExW(pending.c_str(), path_.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
      }
      if (!migrated) {
        REXLOG_WARN("native cache audit: CNPS v1 migration failed; writes disabled");
        path_.clear();
      } else {
        REXLOG_INFO("native cache audit: migrated CNPS v1 local cache; backup retained");
      }
    } else if (!valid) {
      if (std::FILE* w = _wfopen(path_.c_str(), L"wb")) {
        FileHeader nh{};
        std::memcpy(nh.magic, kMagic, 4);
        nh.version = kVersion;
        nh.record_size = sizeof(PsoRecord);
        std::fwrite(&nh, sizeof(nh), 1, w);
        std::fclose(w);
      }
    } else {
      // An interrupted append may leave a partial record. Drop only that
      // incomplete tail before the background writer appends new records.
      std::error_code ec;
      const auto bytes = std::filesystem::file_size(path_, ec);
      if (!ec) {
        const auto complete = sizeof(FileHeader) +
            ((bytes - sizeof(FileHeader)) / sizeof(PsoRecord)) * sizeof(PsoRecord);
        if (bytes != complete) {
          std::filesystem::resize_file(path_, complete, ec);
          if (ec) REXLOG_WARN("native: could not repair incomplete pipeline cache tail");
        }
      }
    }
  }
  REXLOG_INFO("native: pipeline cache: {} pipelines to precompile ({} embedded, {} local, {} "
              "settings variants for {}x MSAA{})",
              queue_.size(), embedded, records.size() - embedded, variants, msaa_samples,
              foliage_a2c ? " + foliage A2C" : "");
  // File writes must not stall the game's render worker when a new PSO appears.
  save_thread_ = std::thread([this] { SaveWorker(); });
  start_time_ = std::chrono::steady_clock::now();
  // Leave cores for the game: a few background threads at low priority.
  unsigned threads = std::clamp(std::thread::hardware_concurrency() / 4, 1u, 4u);
  for (unsigned i = 0; i < threads; ++i) threads_.emplace_back([this] { Worker(); });
}

void PipelineCache::Worker() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  SetThreadDescription(GetCurrentThread(), L"Native PSO precompile");
  std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
  while (true) {
    PsoRecord r;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      auto skip_done = [&] {
        // Skip what the renderer already created or someone else compiles.
        while (next_ < queue_.size() &&
               (ready_.count(queue_[next_].key) || failed_.count(queue_[next_].key) ||
                pending_.count(queue_[next_].key))) {
          ++next_;
        }
      };
      skip_done();
      if (next_ >= queue_.size() && pending_.empty() && !startup_logged_) {
        startup_logged_ = true;
        double wall =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
        REXLOG_INFO("native: pipeline cache: {} pipelines precompiled in {:.1f} s ({:.0f} ms of "
                    "driver compile time)",
                    compiled_, wall, compile_ms_);
        REXLOG_INFO("native cache audit: startup queued={} attempted={} ready={} "
                    "shader_missing={} driver_failed={}",
                    queue_.size(), attempted_, compiled_, shader_missing_, driver_failed_);
      }
      // Idle workers stay alive for pipelines requested during play.
      work_cv_.wait(lock, [&] {
        skip_done();
        return stop_ || !urgent_.empty() || next_ < queue_.size();
      });
      if (stop_) break;
      if (!urgent_.empty()) {
        r = urgent_.front();
        urgent_.pop_front();
      } else {
        r = queue_[next_++];
      }
      pending_.insert(r.key);
    }
    const void *vs = nullptr, *ps = nullptr;
    size_t vs_size = 0, ps_size = 0;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    const bool have_shaders = shaders_(r.vs_hash, true, &vs, &vs_size) &&
        (!r.ps_hash || (r.pixel_variant ?
            variants_ && variants_(r.ps_hash, r.pixel_variant, &ps, &ps_size) :
            shaders_(r.ps_hash, false, &ps, &ps_size)));
    if (have_shaders) {
      auto t0 = std::chrono::steady_clock::now();
      D3D12_GRAPHICS_PIPELINE_STATE_DESC desc =
          MakeDesc(r, root_signature_, vs, vs_size, ps, ps_size, semantic_, elements);
      if (FAILED(device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)))) pso.Reset();
      double ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      std::lock_guard<std::mutex> lock(mutex_);
      compile_ms_ += ms;
      ++attempted_;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_.erase(r.key);
      scheduled_.erase(r.key);
      if (pso) {
        ready_[r.key] = pso;
        ++compiled_;
      } else {
        failed_.insert(r.key);
        if (have_shaders) ++driver_failed_;
        else ++shader_missing_;
        REXLOG_WARN("native cache audit: precompile_failed key={:016X} vs={:016X} "
                    "ps={:016X} reason={}", r.key, r.vs_hash, r.ps_hash,
                    have_shaders ? "driver" : "shader_missing");
      }
    }
    cv_.notify_all();
  }
}

void PipelineCache::CompileAsync(const PsoRecord& record) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    known_.insert(record.key);
    const uint64_t dh = DescHash(record);
    auto description = by_desc_.find(dh);
    uint64_t key = description == by_desc_.end() ? record.key : description->second;
    if (description == by_desc_.end()) {
      while (scheduled_.count(key) || ready_.count(key) || failed_.count(key)) ++key;
      by_desc_[dh] = key;
    }
    aliases_[record.key] = key;
    if (scheduled_.count(key) || failed_.count(key)) return;
    auto it = ready_.find(key);
    if (it != ready_.end() && it->second) return;
    if (!urgent_keys_.insert(key).second) return;
    PsoRecord canonical = record;
    canonical.key = key;
    scheduled_.insert(key);
    urgent_.push_back(canonical);
  }
  work_cv_.notify_one();
}

bool PipelineCache::WaitForStartup(uint32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(mutex_);
  const bool finished = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
      [&] { return stop_ || scheduled_.empty(); });
  return finished && !stop_ && scheduled_.empty() && failed_.empty();
}

PipelineCache::Poll PipelineCache::TryTake(uint64_t key,
                                           Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto alias = aliases_.find(key);
  if (alias != aliases_.end()) key = alias->second;
  if (failed_.count(key) || stop_) return Poll::kFailed;
  auto it = ready_.find(key);
  if (it == ready_.end() || !it->second) return Poll::kPending;
  out = it->second;
  return Poll::kReady;
}

bool PipelineCache::Take(uint64_t key, Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto alias = aliases_.find(key);
  if (alias == aliases_.end()) {
    ++key_misses_;
    return false;
  }
  key = alias->second;
  if (scheduled_.count(key)) ++waited_;
  // A known queued record is just as reusable as an in-flight one. Waiting
  // avoids duplicate synchronous creation on the renderer thread.
  cv_.wait(lock, [&] { return stop_ || !scheduled_.count(key); });
  auto it = ready_.find(key);
  if (stop_ || it == ready_.end() || !it->second) {
    ++unavailable_;
    REXLOG_WARN("native cache audit: unavailable key={:016X} reason={}",
                key, stop_ ? "stopping" : "precompile_failed");
    return false;
  }
  out = it->second;
  ++key_hits_;
  return true;
}

bool PipelineCache::TakeByDesc(const PsoRecord& record,
                               Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
  std::unique_lock<std::mutex> lock(mutex_);
  const auto description = DescHash(record);
  auto it = by_desc_.find(description);
  if (it == by_desc_.end()) {
    ++desc_misses_;
    REXLOG_INFO("native cache audit: unknown_desc key={:016X} desc={:016X} "
                "vs={:016X} ps={:016X}", record.key, description, record.vs_hash, record.ps_hash);
    return false;
  }
  const uint64_t key = it->second;
  if (scheduled_.count(key)) ++waited_;
  cv_.wait(lock, [&] { return stop_ || !scheduled_.count(key); });
  const auto ready = ready_.find(key);
  if (stop_ || ready == ready_.end() || !ready->second) {
    ++unavailable_;
    return false;
  }
  out = ready->second;
  ++desc_hits_;
  return true;
}

void PipelineCache::Record(const PsoRecord& record) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Start is skipped when pipeline caching is disabled.
    if (path_.empty()) return;
    known_.insert(record.key);
    if (!persisted_desc_.insert(DescHash(record)).second) return;
    save_queue_.push_back(record);
  }
  save_cv_.notify_one();
}

void PipelineCache::SaveWorker() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  SetThreadDescription(GetCurrentThread(), L"Native PSO cache writer");
  std::vector<PsoRecord> batch;
  while (true) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      save_cv_.wait(lock, [&] { return stop_ || !save_queue_.empty(); });
      if (save_queue_.empty() && stop_) break;
      batch.swap(save_queue_);
    }
    // Drain the whole batch per open/close. Closing flushes records so the
    // next launch can load them even after a normal renderer shutdown.
    std::FILE* f = _wfopen(path_.c_str(), L"ab");
    if (!f) {
      REXLOG_WARN("native: failed to open pipeline cache for {} new records", batch.size());
    } else {
      const size_t written = std::fwrite(batch.data(), sizeof(PsoRecord), batch.size(), f);
      const int closed = std::fclose(f);
      if (written != batch.size() || closed != 0)
        REXLOG_WARN("native: failed to persist all pipeline records ({} of {} written)",
                    written, batch.size());
    }
    batch.clear();
  }
}

PipelineCache::~PipelineCache() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  work_cv_.notify_all();
  cv_.notify_all();
  save_cv_.notify_all();
  for (std::thread& t : threads_) {
    if (t.joinable()) t.join();
  }
  if (save_thread_.joinable()) save_thread_.join();
  REXLOG_INFO("native cache audit: totals key_hits={} desc_hits={} key_misses={} "
              "desc_misses={} unavailable={} waits={} ready={} shader_missing={} driver_failed={}",
              key_hits_, desc_hits_, key_misses_, desc_misses_, unavailable_, waited_,
              compiled_, shader_missing_, driver_failed_);
}

}  // namespace superman_returns::native
