#include "sr_graphics_system.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/graphics_system.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/logging.h>

namespace {

class SrGraphicsSystem;

class SrCommandProcessor final : public rex::graphics::d3d12::D3D12CommandProcessor {
 public:
  SrCommandProcessor(SrGraphicsSystem* graphics_system,
                     rex::system::KernelState* kernel_state);

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

  std::ofstream trace_;
  uint32_t start_frame_ = 0;
  uint32_t max_frames_ = 0;
  std::filesystem::path trigger_path_;
  bool trace_started_ = true;
  uint64_t skip_vs_hash_ = 0;
  uint64_t skip_ps_hash_ = 0;
  int32_t skip_edram_mode_ = -1;
  uint64_t skipped_draws_ = 0;
  uint64_t frame_ = 0;
  uint64_t event_ = 0;
};

class SrGraphicsSystem final : public rex::graphics::d3d12::D3D12GraphicsSystem {
 protected:
  std::unique_ptr<rex::graphics::CommandProcessor> CreateCommandProcessor() override {
    return std::make_unique<SrCommandProcessor>(this, kernel_state_);
  }
};

uint64_t ParseShaderHash(const std::string& value) {
  if (value.size() != 16) return 0;
  uint64_t hash = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(),
                                           hash, 16);
  return error == std::errc{} && end == value.data() + value.size() ? hash : 0;
}

SrCommandProcessor::SrCommandProcessor(
    SrGraphicsSystem* graphics_system, rex::system::KernelState* kernel_state)
    : D3D12CommandProcessor(graphics_system, kernel_state) {
  const std::string path = rex::cvar::Query<std::string>("sr_gpu_trace_path");
  max_frames_ = static_cast<uint32_t>(
      std::clamp(rex::cvar::Query<int32_t>("sr_gpu_trace_frames"), 1, 10000));
  start_frame_ = static_cast<uint32_t>(
      std::clamp(rex::cvar::Query<int32_t>("sr_gpu_trace_start_frame"), 0, 1000000));
  trigger_path_ = rex::cvar::Query<std::string>("sr_gpu_trace_trigger_path");
  trace_started_ = trigger_path_.empty();
  skip_vs_hash_ = ParseShaderHash(
      rex::cvar::Query<std::string>("sr_gpu_skip_vs_hash"));
  skip_ps_hash_ = ParseShaderHash(
      rex::cvar::Query<std::string>("sr_gpu_skip_ps_hash"));
  skip_edram_mode_ = rex::cvar::Query<int32_t>("sr_gpu_skip_edram_mode");
  if (skip_vs_hash_ && skip_ps_hash_) {
    REXLOG_WARN("sr renderer: diagnostic draw skip enabled for VS {:016X}, PS {:016X}, mode {}",
                skip_vs_hash_, skip_ps_hash_, skip_edram_mode_);
  } else if (skip_vs_hash_ || skip_ps_hash_) {
    REXLOG_ERROR("sr renderer: both skip shader hashes are required; skipping disabled");
    skip_vs_hash_ = skip_ps_hash_ = 0;
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
  const auto* vs = active_vertex_shader();
  const auto* ps = active_pixel_shader();
  const uint32_t mode = register_file_->values[rex::graphics::XE_GPU_REG_RB_MODECONTROL] & 7;
  if (skip_vs_hash_ && vs && ps && vs->ucode_data_hash() == skip_vs_hash_ &&
      ps->ucode_data_hash() == skip_ps_hash_ &&
      (skip_edram_mode_ < 0 || mode == static_cast<uint32_t>(skip_edram_mode_))) {
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

bool SrCommandProcessor::IssueCopy() {
  const bool accepted = D3D12CommandProcessor::IssueCopy();
  Trace("copy", 0, 0, 0, accepted);
  return accepted;
}

void SrCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                   uint32_t frontbuffer_width,
                                   uint32_t frontbuffer_height) {
  D3D12CommandProcessor::IssueSwap(frontbuffer_ptr, frontbuffer_width,
                                   frontbuffer_height);
  Trace("swap", 0, 0, 0, true);
  ++frame_;
  if (!trace_started_ && std::filesystem::exists(trigger_path_)) {
    trace_started_ = true;
    start_frame_ = static_cast<uint32_t>(frame_);
    REXLOG_INFO("sr renderer: trigger found; recording from frame {}", start_frame_);
  }
  if (trace_started_ && frame_ == static_cast<uint64_t>(start_frame_) + max_frames_) {
    trace_.flush();
    REXLOG_INFO("sr renderer: finished tracing {} frames ({} events, {} draws skipped)",
                max_frames_, event_, skipped_draws_);
  }
  if (skip_vs_hash_ && frame_ % 120 == 0) {
    REXLOG_INFO("sr renderer: skipped {} draws through frame {}",
                skipped_draws_, frame_);
  }
}

}  // namespace

std::unique_ptr<rex::system::IGraphicsSystem> CreateSrTraceGraphicsSystem() {
  return std::make_unique<SrGraphicsSystem>();
}
