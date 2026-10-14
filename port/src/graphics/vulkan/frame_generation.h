// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "resources.h"
#include <filesystem>
#include <memory>
namespace superman_returns::graphics::vulkan {
struct FrameGenerationConfig {bool enabled=false;std::filesystem::path dll;bool performance=true;};
struct FrameGenerationInput {
  uint64_t source_serial=0;
  std::shared_ptr<TextureResource> image;
  std::array<uint32_t,256> gamma{};
  bool gamma_enabled=false,force_warmup=false;
  uint32_t aspect_x=1280,aspect_y=720;
  VkExtent2D extent{};
};
struct FrameGenerationOutput {
  // Original remains the unmodified guest snapshot; compose its gamma normally.
  std::shared_ptr<TextureResource> original,generated;
};
struct FrameGenerationGpuStats {uint64_t samples=0,total_ns=0,warmups=0,warmup_ns=0;};
class FrameGenerationSession {
public:
  FrameGenerationSession(Context&,FrameGenerationConfig);
  ~FrameGenerationSession();
  bool Initialize(Error&);
  // Setup only, before acquiring/recording a presentation frame. Failure here
  // can safely disable FG and retain normal Vulkan presentation.
  bool Prepare(const FrameGenerationInput&,Error&);
  bool Record(VkCommandBuffer,uint64_t presentation_serial,const FrameGenerationInput&,ImageState& source_state,FrameGenerationOutput&,Error&);
  void Retire(uint64_t);
  void ResetAfterRetire();
  bool NeedsReset(VkExtent2D,VkFormat) const;
  bool active() const;
  FrameGenerationGpuStats GpuStats() const {return stats_;}
private:
  struct Impl;Context& c_;FrameGenerationConfig config_;
  std::unique_ptr<Impl> impl_;FrameGenerationGpuStats stats_;
};
}
