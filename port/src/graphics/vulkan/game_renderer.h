#pragma once
#include "game_pipeline.h"
#include "depth_resolve.h"
#include "../guest/texture_capture.h"
namespace superman_returns::graphics::vulkan {
using ShaderLookup=std::function<shaders::ShaderResult(const guest::ShaderCapture&)>;
using TextureDecoder=std::function<bool(const guest::TextureCapture&,guest::LinearTexture&,std::string&)>;
struct GameRenderStats {uint64_t draws=0,pending=0,failed=0,clears=0;};
class GameRenderer {
public:
  GameRenderer(Context& c,ShaderLookup shaders,TextureDecoder decoder={},AliasOptions aliases={}):c_(c),shaders_(std::move(shaders)),decoder_(std::move(decoder)),state_(c.f),resources_(c),targets_(c,state_),descriptors_(c),pipelines_(c),depth_resolver_(c),alias_options_(aliases) {}
  bool Initialize(const std::filesystem::path& driver_cache,Error&);
  bool BeginSubmission(VkCommandBuffer,uint64_t serial,Error&);
  bool Record(const guest::RenderPacket&,VkCommandBuffer,Error&);
  // Selection alone does not submit or acknowledge a guest presentation.
  std::shared_ptr<TextureResource> SelectFrontbuffer(const guest::SwapPacket&,Error&) const;
  void Retire(uint64_t serial);
  TargetStore& Targets() {return targets_;}
  ImageState& Images() {return state_;}
  ResourceStore& Resources() {return resources_;}
  const GameRenderStats& Stats() const {return stats_;}
  bool CheckpointCache(Error& e) {return pipelines_.CheckpointCache(e);}
private:
  bool Draw(const guest::DrawPacket&,VkCommandBuffer,Error&);
  bool Upload(const guest::BufferUpdate&,bool indices,uint64_t version,Error&);
  Context& c_;ShaderLookup shaders_;TextureDecoder decoder_;ImageState state_;ResourceStore resources_;TargetStore targets_;DescriptorStore descriptors_;GamePipelineStore pipelines_;
  DepthResolver depth_resolver_;bool depth_ready_=false;
  AliasOptions alias_options_;
  std::map<guest::ResourceId,std::vector<uint8_t>> cpu_buffers_;
  struct ResolvedBinding {std::shared_ptr<TextureResource> image,raw;bool swap=false;uint64_t generation=0;};
  std::map<uint32_t,ResolvedBinding> resolved_latest_;
  std::map<guest::ResourceId,std::pair<uint32_t,uint64_t>> resolve_views_;
  uint64_t resolve_generation_=0;
  VkCommandBuffer command_=VK_NULL_HANDLE;uint64_t serial_=0;bool dummies_=false;
  GameRenderStats stats_;
  std::map<uint64_t,std::vector<guest::ResourceId>> transient_buffers_;
  VkExtent2D tiling_{};
};
}
