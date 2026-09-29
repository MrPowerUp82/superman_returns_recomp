#include "sr_graphics_system.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/graphics_system.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/presenter.h>

#include "post_effects.h"

namespace {

class SrGraphicsSystem;

struct SkipRule {
  uint64_t vs = 0;           // 0 = any
  uint64_t ps = 0;           // 0 = any
  int32_t mode = -1;         // RB_MODECONTROL edram mode, -1 = any
  int32_t prim = -1;         // xenos primitive type, -1 = any
  int64_t surface = -1;      // RB_SURFACE_INFO value, -1 = any
  bool copy = false;         // matches resolves (IssueCopy) instead of draws
};

std::atomic<SrXenosSwapObserver> g_swap_observer{nullptr};

class SrCommandProcessor;
std::atomic<SrCommandProcessor*> g_active_processor{nullptr};

class SrCommandProcessor final : public rex::graphics::d3d12::D3D12CommandProcessor {
 public:
  // record = false: no CSV trace, probe or skip rules (A/B mode).
  // skip_post_effects: sr_post_effects=false (post_effects.h).
  SrCommandProcessor(SrGraphicsSystem* graphics_system,
                     rex::system::KernelState* kernel_state, bool record,
                     bool skip_post_effects);
  ~SrCommandProcessor() override;
  bool GammaRamp256(uint32_t* out) const {
    const auto* table = gamma_ramp_256_entry_table();
    for (uint32_t i = 0; i < 256; ++i) out[i] = table[i].value;
    return true;
  }

 protected:
  bool IssueDraw(rex::graphics::xenos::PrimitiveType primitive_type,
                 uint32_t index_count, IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;
  bool IssueCopy() override;
  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

 private:
  void Trace(const char* kind, uint32_t primitive_type, uint32_t count,
             uint32_t index_format, bool accepted);
  void ProbeDraw(rex::graphics::xenos::PrimitiveType primitive_type,
                 uint32_t index_count, const IndexBufferInfo* index_buffer_info);
  void DumpAbOutput(uint64_t swap_number);
  bool SkipPostEffectDraw(uint32_t primitive_type);
  bool SkipPostEffectCopy();

  SrGraphicsSystem* sr_graphics_system_;
  bool skip_post_effects_;
  superman_returns::post_effects::Filter post_effects_;

  std::ofstream trace_;
  std::ofstream pass_probe_;
  uint32_t start_frame_ = 0;
  uint32_t max_frames_ = 0;
  std::filesystem::path trigger_path_;
  bool trace_started_ = true;
  bool MatchesSkipRule(bool copy, uint32_t primitive_type) const;
  std::vector<SkipRule> skip_rules_;
  uint64_t skipped_copies_ = 0;
  uint64_t probe_vs_hash_ = 0;
  uint64_t probe_ps_hash_ = 0;
  uint64_t skipped_draws_ = 0;
  uint64_t frame_ = 0;
  uint64_t event_ = 0;
};

class SrGraphicsSystem final : public rex::graphics::d3d12::D3D12GraphicsSystem {
 public:
  SrGraphicsSystem(bool record, bool skip_post_effects)
      : record_(record), skip_post_effects_(skip_post_effects) {}
  rex::ui::Presenter* sr_presenter() const { return presenter(); }

 protected:
  std::unique_ptr<rex::graphics::CommandProcessor> CreateCommandProcessor() override {
    return std::make_unique<SrCommandProcessor>(this, kernel_state_, record_,
                                                skip_post_effects_);
  }

 private:
  bool record_;
  bool skip_post_effects_;
};

uint64_t ParseShaderHash(const std::string& value) {
  if (value.size() != 16) return 0;
  uint64_t hash = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(),
                                           hash, 16);
  return error == std::errc{} && end == value.data() + value.size() ? hash : 0;
}

uint64_t HashRegisterWords(const uint32_t* words, size_t count) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < count; ++i) {
    hash ^= words[i];
    hash *= 0x100000001B3ull;
  }
  return hash;
}

// One diagnostic skip rule: every field that is set must match. Parsed from
// sr_gpu_skip_rules, e.g. "ps=BE398F0A17FF758A;prim=13,mode=4;copy=1".
SkipRule ParseSkipRule(std::string_view text) {
  SkipRule rule;
  while (!text.empty()) {
    const size_t comma = text.find(',');
    const std::string_view term = text.substr(0, comma);
    text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    const size_t eq = term.find('=');
    if (eq == std::string_view::npos) continue;
    const std::string_view key = term.substr(0, eq);
    const std::string value(term.substr(eq + 1));
    if (key == "vs") rule.vs = ParseShaderHash(value);
    else if (key == "ps") rule.ps = ParseShaderHash(value);
    else if (key == "mode") rule.mode = std::stoi(value);
    else if (key == "prim") rule.prim = std::stoi(value);
    else if (key == "surface") rule.surface = std::stoll(value);
    else if (key == "copy") rule.copy = value == "1";
  }
  return rule;
}

std::vector<SkipRule> ParseSkipRules(std::string_view text) {
  std::vector<SkipRule> rules;
  while (!text.empty()) {
    const size_t semi = text.find(';');
    const std::string_view part = text.substr(0, semi);
    text = semi == std::string_view::npos ? std::string_view{} : text.substr(semi + 1);
    if (!part.empty()) rules.push_back(ParseSkipRule(part));
  }
  return rules;
}

SrCommandProcessor::~SrCommandProcessor() {
  SrCommandProcessor* self = this;
  g_active_processor.compare_exchange_strong(self, nullptr);
}

SrCommandProcessor::SrCommandProcessor(
    SrGraphicsSystem* graphics_system, rex::system::KernelState* kernel_state, bool record,
    bool skip_post_effects)
    : D3D12CommandProcessor(graphics_system, kernel_state),
      sr_graphics_system_(graphics_system),
      skip_post_effects_(skip_post_effects) {
  g_active_processor.store(this);
  if (skip_post_effects_) {
    REXLOG_INFO("sr renderer: post effects off, skipping {} bloom/light-ray passes derived "
                "from the gameplay trace (post_effects.h)",
                superman_returns::post_effects::kPassCount);
    if (rex::cvar::Query<int32_t>("sr_render_scale") != 100) {
      REXLOG_WARN("sr renderer: sr_post_effects=false only matches the 1280x720 surfaces; "
                  "with sr_render_scale != 100 the post effects will probably stay on");
    }
  }
  if (!record) return;
  const std::string path = rex::cvar::Query<std::string>("sr_gpu_trace_path");
  max_frames_ = static_cast<uint32_t>(
      std::clamp(rex::cvar::Query<int32_t>("sr_gpu_trace_frames"), 1, 10000));
  start_frame_ = static_cast<uint32_t>(
      std::clamp(rex::cvar::Query<int32_t>("sr_gpu_trace_start_frame"), 0, 1000000));
  trigger_path_ = rex::cvar::Query<std::string>("sr_gpu_trace_trigger_path");
  trace_started_ = trigger_path_.empty();
  skip_rules_ = ParseSkipRules(rex::cvar::Query<std::string>("sr_gpu_skip_rules"));
  // Legacy single-pair options map onto one rule.
  const uint64_t legacy_vs = ParseShaderHash(rex::cvar::Query<std::string>("sr_gpu_skip_vs_hash"));
  const uint64_t legacy_ps = ParseShaderHash(rex::cvar::Query<std::string>("sr_gpu_skip_ps_hash"));
  if (legacy_ps) {
    skip_rules_.push_back({legacy_vs, legacy_ps,
                           rex::cvar::Query<int32_t>("sr_gpu_skip_edram_mode")});
  }
  probe_vs_hash_ = ParseShaderHash(
      rex::cvar::Query<std::string>("sr_gpu_probe_vs_hash"));
  probe_ps_hash_ = ParseShaderHash(
      rex::cvar::Query<std::string>("sr_gpu_probe_ps_hash"));
  for (const SkipRule& r : skip_rules_) {
    REXLOG_WARN("sr renderer: diagnostic skip rule vs={:016X} ps={:016X} mode={} prim={} "
                "surface={} copy={}", r.vs, r.ps, r.mode, r.prim, r.surface, r.copy);
  }
  trace_.open(path, std::ios::out | std::ios::trunc);
  if (!trace_) {
    REXLOG_ERROR("sr renderer: cannot open GPU trace at '{}'", path);
    return;
  }
  trace_ << "frame,event,kind,accepted,primitive,index_count,index_format,vs_hash,ps_hash,"
            "rb_mode,rb_surface,rb_color,rb_depth,rb_color_mask,rb_copy\n";
  if (trace_started_) {
    REXLOG_INFO("sr renderer: recording {} guest frames starting at {} to '{}'",
                max_frames_, start_frame_, path);
  } else {
    REXLOG_INFO("sr renderer: waiting for '{}' before recording {} frames to '{}'",
                trigger_path_.string(), max_frames_, path);
  }
  const std::string probe_path = rex::cvar::Query<std::string>("sr_gpu_pass_probe_path");
  if (!probe_path.empty()) {
    if (!probe_vs_hash_ || !probe_ps_hash_) {
      REXLOG_ERROR("sr renderer: pass probe requires both shader hashes");
    } else {
      pass_probe_.open(probe_path, std::ios::out | std::ios::trunc);
      if (!pass_probe_) {
        REXLOG_ERROR("sr renderer: cannot open pass probe at '{}'", probe_path);
      } else {
        pass_probe_ << "frame,event,primitive,index_count,index_base,index_length,"
                       "vf0_addr,vf0_size,c0_3_hash,c4_10_hash,rb_color_info,"
                       "rb_depth_info,rb_blend0,rb_depth_control\n";
        REXLOG_INFO("sr renderer: probing VS {:016X}, PS {:016X} to '{}'",
                    probe_vs_hash_, probe_ps_hash_, probe_path);
      }
    }
  }
}

void SrCommandProcessor::ProbeDraw(
    rex::graphics::xenos::PrimitiveType primitive_type, uint32_t index_count,
    const IndexBufferInfo* index_buffer_info) {
  if (!pass_probe_ || !trace_started_ || frame_ < start_frame_ ||
      frame_ >= static_cast<uint64_t>(start_frame_) + max_frames_)
    return;
  const auto* vs = active_vertex_shader();
  const auto* ps = active_pixel_shader();
  if (!vs || !ps || vs->ucode_data_hash() != probe_vs_hash_ ||
      ps->ucode_data_hash() != probe_ps_hash_)
    return;
  const auto& regs = *register_file_;
  const auto vf0 = regs.GetVertexFetch(0);
  const auto* constants = &regs[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_000_X];
  pass_probe_ << frame_ << ',' << event_ << ',' << static_cast<uint32_t>(primitive_type)
              << ',' << index_count << ','
              << (index_buffer_info ? index_buffer_info->guest_base : 0) << ','
              << (index_buffer_info ? index_buffer_info->length : 0) << ','
              << (static_cast<uint64_t>(vf0.address) << 2) << ','
              << (static_cast<uint64_t>(vf0.size) << 2) << ','
              << std::hex << std::setfill('0') << std::setw(16)
              << HashRegisterWords(constants, 16) << ',' << std::setw(16)
              << HashRegisterWords(constants + 16, 28) << std::dec << ','
              << regs[rex::graphics::XE_GPU_REG_RB_COLOR_INFO] << ','
              << regs[rex::graphics::XE_GPU_REG_RB_DEPTH_INFO] << ','
              << regs[rex::graphics::XE_GPU_REG_RB_BLENDCONTROL0] << ','
              << regs[rex::graphics::XE_GPU_REG_RB_DEPTHCONTROL] << '\n';
}

void SrCommandProcessor::Trace(const char* kind, uint32_t primitive_type,
                               uint32_t count, uint32_t index_format,
                               bool accepted) {
  if (!trace_ || !trace_started_ || frame_ < start_frame_ ||
      frame_ >= static_cast<uint64_t>(start_frame_) + max_frames_)
    return;
  const auto* vs = active_vertex_shader();
  const auto* ps = active_pixel_shader();
  const auto& regs = *register_file_;
  trace_ << frame_ << ',' << event_++ << ',' << kind << ',' << accepted << ','
         << primitive_type << ','
         << count << ',' << index_format << ',' << std::hex << std::setfill('0')
         << std::setw(16) << (vs ? vs->ucode_data_hash() : 0) << ','
         << std::setw(16) << (ps ? ps->ucode_data_hash() : 0) << std::dec << ','
         << regs[rex::graphics::XE_GPU_REG_RB_MODECONTROL] << ','
         << regs[rex::graphics::XE_GPU_REG_RB_SURFACE_INFO] << ','
         << regs[rex::graphics::XE_GPU_REG_RB_COLORCONTROL] << ','
         << regs[rex::graphics::XE_GPU_REG_RB_DEPTHCONTROL] << ','
         << regs[rex::graphics::XE_GPU_REG_RB_COLOR_MASK] << ','
         << regs[rex::graphics::XE_GPU_REG_RB_COPY_CONTROL] << '\n';
}

bool SrCommandProcessor::IssueDraw(
    rex::graphics::xenos::PrimitiveType primitive_type, uint32_t index_count,
    IndexBufferInfo* index_buffer_info, bool major_mode_explicit) {
  ProbeDraw(primitive_type, index_count, index_buffer_info);
  if (SkipPostEffectDraw(static_cast<uint32_t>(primitive_type)) ||
      MatchesSkipRule(false, static_cast<uint32_t>(primitive_type))) {
    ++skipped_draws_;
    Trace("skipped", static_cast<uint32_t>(primitive_type), index_count,
          index_buffer_info ? static_cast<uint32_t>(index_buffer_info->format) : 0,
          false);
    return true;
  }
  const bool accepted = D3D12CommandProcessor::IssueDraw(
      primitive_type, index_count, index_buffer_info, major_mode_explicit);
  Trace("draw", static_cast<uint32_t>(primitive_type), index_count,
        index_buffer_info ? static_cast<uint32_t>(index_buffer_info->format) : 0,
        accepted);
  return accepted;
}

bool SrCommandProcessor::MatchesSkipRule(bool copy, uint32_t primitive_type) const {
  if (skip_rules_.empty()) return false;
  const auto& regs = *register_file_;
  const uint32_t mode = regs.values[rex::graphics::XE_GPU_REG_RB_MODECONTROL] & 7;
  const uint32_t surface = regs.values[rex::graphics::XE_GPU_REG_RB_SURFACE_INFO];
  const auto* vs = active_vertex_shader();
  const auto* ps = active_pixel_shader();
  for (const SkipRule& r : skip_rules_) {
    if (r.copy != copy) continue;
    if (r.mode >= 0 && static_cast<uint32_t>(r.mode) != mode) continue;
    if (r.surface >= 0 && static_cast<uint32_t>(r.surface) != surface) continue;
    if (!copy) {
      if (r.prim >= 0 && static_cast<uint32_t>(r.prim) != primitive_type) continue;
      if (r.vs && (!vs || vs->ucode_data_hash() != r.vs)) continue;
      if (r.ps && (!ps || ps->ucode_data_hash() != r.ps)) continue;
    }
    return true;
  }
  return false;
}

// sr_post_effects=false. Every draw must reach the filter, so this runs before
// the diagnostic skip rules; the resolve draws (mode 6) only pass through.
bool SrCommandProcessor::SkipPostEffectDraw(uint32_t primitive_type) {
  if (!skip_post_effects_) return false;
  const auto& regs = *register_file_;
  const auto* vs = active_vertex_shader();
  const auto* ps = active_pixel_shader();
  return post_effects_.SkipDraw(primitive_type,
                                regs.values[rex::graphics::XE_GPU_REG_RB_MODECONTROL] & 7,
                                vs ? vs->ucode_data_hash() : 0,
                                ps ? ps->ucode_data_hash() : 0,
                                regs.values[rex::graphics::XE_GPU_REG_RB_SURFACE_INFO]);
}

bool SrCommandProcessor::SkipPostEffectCopy() {
  if (!skip_post_effects_) return false;
  const auto* ps = active_pixel_shader();
  return post_effects_.SkipCopy(
      ps ? ps->ucode_data_hash() : 0,
      register_file_->values[rex::graphics::XE_GPU_REG_RB_SURFACE_INFO]);
}

bool SrCommandProcessor::IssueCopy() {
  if (SkipPostEffectCopy() || MatchesSkipRule(true, 0)) {
    ++skipped_copies_;
    Trace("skipped_copy", 0, 0, 0, false);
    return true;
  }
  const bool accepted = D3D12CommandProcessor::IssueCopy();
  Trace("copy", 0, 0, 0, accepted);
  return accepted;
}

void SrCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                   uint32_t frontbuffer_width,
                                   uint32_t frontbuffer_height) {
  D3D12CommandProcessor::IssueSwap(frontbuffer_ptr, frontbuffer_width,
                                   frontbuffer_height);
  if (SrXenosSwapObserver observer = g_swap_observer.load()) {
    observer(frame_ + 1, register_file_->values, rex::graphics::RegisterFile::kRegisterCount);
    DumpAbOutput(frame_ + 1);
  }
  Trace("swap", 0, 0, 0, true);
  ++frame_;
  if (!trace_started_ && std::filesystem::exists(trigger_path_)) {
    trace_started_ = true;
    start_frame_ = static_cast<uint32_t>(frame_);
    REXLOG_INFO("sr renderer: trigger found; recording from frame {}", start_frame_);
  }
  if (trace_started_ && frame_ == static_cast<uint64_t>(start_frame_) + max_frames_) {
    trace_.flush();
    pass_probe_.flush();
    REXLOG_INFO("sr renderer: finished tracing {} frames ({} events, {} draws skipped)",
                max_frames_, event_, skipped_draws_);
  }
  if (!skip_rules_.empty() && frame_ % 120 == 0) {
    REXLOG_INFO("sr renderer: skipped {} draws, {} copies through frame {}",
                skipped_draws_, skipped_copies_, frame_);
  }
  if (skip_post_effects_ && frame_ % 600 == 0) {
    REXLOG_INFO("sr renderer: post effects off: skipped {} passes, {} resolves through frame {}",
                post_effects_.skipped_draws(), post_effects_.skipped_copies(), frame_);
  }
}

// A/B reference image: the presented Xenos output at the swaps the native
// renderer dumps too (sr_native_ab_swaps, comma-separated).
void SrCommandProcessor::DumpAbOutput(uint64_t swap_number) {
  static const std::vector<uint64_t> swaps = [] {
    std::vector<uint64_t> v;
    const std::string text = rex::cvar::Query<std::string>("sr_native_ab_swaps");
    std::string_view list = text;
    while (!list.empty()) {
      const size_t comma = list.find(',');
      uint64_t n = 0;
      std::from_chars(list.data(), list.data() + std::min(comma, list.size()), n);
      if (n) v.push_back(n);
      list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
    }
    return v;
  }();
  if (std::find(swaps.begin(), swaps.end(), swap_number) == swaps.end()) return;
  const std::string dir = rex::cvar::Query<std::string>("sr_native_dump_dir");
  rex::ui::Presenter* presenter = sr_graphics_system_->sr_presenter();
  rex::ui::RawImage image;
  if (dir.empty() || !presenter || !presenter->CaptureGuestOutput(image)) {
    REXLOG_WARN("sr renderer: could not capture the Xenos output at swap {}", swap_number);
    return;
  }
  char name[64];
  std::snprintf(name, sizeof(name), "/s%06llu_xenos_output.raw", (unsigned long long)swap_number);
  if (std::FILE* f = std::fopen((dir + name).c_str(), "wb")) {
    // DXGI_FORMAT_R8G8B8A8_UNORM (28): RawImage rows are R8 G8 B8 X8.
    const uint32_t header[4] = {image.width, image.height, 28u, uint32_t(image.stride)};
    std::fwrite(header, 4, 4, f);
    std::fwrite(image.data.data(), 1, image.data.size(), f);
    std::fclose(f);
    REXLOG_INFO("sr renderer: A/B Xenos output of swap {} -> {}{}", swap_number, dir, name);
  }
}

}  // namespace

// Query<bool> would read a missing cvar as false (= off); compare the text so
// anything but an explicit "false" keeps the stock behavior.
static bool SrPostEffectsDisabled() {
  return rex::cvar::GetFlagByName("sr_post_effects") == "false";
}

std::unique_ptr<rex::system::IGraphicsSystem> CreateSrTraceGraphicsSystem() {
  return std::make_unique<SrGraphicsSystem>(true, SrPostEffectsDisabled());
}

std::unique_ptr<rex::system::IGraphicsSystem> CreateSrAbGraphicsSystem() {
  return std::make_unique<SrGraphicsSystem>(false, false);
}

void SetSrXenosSwapObserver(SrXenosSwapObserver observer) { g_swap_observer.store(observer); }

bool GetSrXenosGammaRamp256(uint32_t* out_entries) {
  SrCommandProcessor* processor = g_active_processor.load();
  return processor && out_entries && processor->GammaRamp256(out_entries);
}

std::unique_ptr<rex::system::IGraphicsSystem> CreateSrXenosGraphicsSystem() {
  // sr_post_effects=true (default) keeps the stock backend untouched.
  if (SrPostEffectsDisabled()) return std::make_unique<SrGraphicsSystem>(false, true);
  return std::make_unique<rex::graphics::d3d12::D3D12GraphicsSystem>();
}
