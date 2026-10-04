#include "game_renderer.h"
#include "resolve.h"
#include "../guest/primitive_expansion.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <exception>
namespace superman_returns::graphics::vulkan {
namespace {
constexpr guest::ResourceId MissingVertex=UINT64_MAX-4;
bool Fail(Error& e,const char* message) {e={"Game renderer",VK_ERROR_INITIALIZATION_FAILED,message};return false;}
}
bool GameRenderer::Initialize(const std::filesystem::path& path,Error& e) {return descriptors_.Initialize(e) && pipelines_.Initialize(descriptors_.Layouts(),path,e);}
std::shared_ptr<TextureResource> GameRenderer::SelectFrontbuffer(const guest::SwapPacket& swap,Error& e) const {
  if(!swap.frontbuffer || swap.frontbuffer>UINT32_MAX-0x23u) {Fail(e,"Invalid captured frontbuffer object");return {};}
  try {
    auto bytes=swap.memory.Read(uint32_t(swap.frontbuffer)+0x20,4);uint32_t fetch1=0;
    for(auto b:bytes) fetch1=(fetch1<<8)|b;
    std::array<uint32_t,6> fetch{};fetch[1]=fetch1;auto base=ResolvedPhysicalBase(fetch);
    auto found=resolved_latest_.find(base);
    if(found==resolved_latest_.end() || !found->second.image) {Fail(e,"Frontbuffer has no completed native resolve");return {};}
    e={};return found->second.image;
  } catch(const std::exception& ex) {e={"Captured frontbuffer",VK_ERROR_INITIALIZATION_FAILED,ex.what()};return {};}
}
bool GameRenderer::BeginSubmission(VkCommandBuffer command,uint64_t serial,Error& e) {
  if(!resources_.BeginSubmission(command,serial,e) || !targets_.BeginSubmission(command,serial,e)) return false;
  command_=command;serial_=serial;
  if(!dummies_) {float missing[]{0,0,0,1};if(!resources_.CreateDummies(e) || !resources_.UploadBuffer(MissingVertex,std::as_bytes(std::span(missing)),1,e)) return false;dummies_=true;}
  e={};return true;
}
void GameRenderer::Retire(uint64_t serial) {
  for(auto it=transient_buffers_.begin();it!=transient_buffers_.end() && it->first<=serial;) {for(auto id:it->second) resources_.ForgetBuffer(id);it=transient_buffers_.erase(it);}
  depth_resolver_.Retire(serial);descriptors_.Retire(serial);pipelines_.Retire(serial);targets_.Retire(serial);resources_.Retire(serial);if(serial>=serial_) command_=VK_NULL_HANDLE;
}
bool GameRenderer::Upload(const guest::BufferUpdate& update,bool indices,uint64_t version,Error& e) {
  auto& p=update.plan;if(!p.key || !p.size || p.action>2 || p.begin>p.end || p.end>p.size) return Fail(e,"Invalid guest buffer update");
  if(!p.action) return bool(resources_.Buffer(p.key,e));
  uint32_t width=indices?((p.format&3)==2?4:2):1;
  if(indices && (p.size%width || p.begin%width || p.end%width)) return Fail(e,"Invalid index update alignment");
  uint64_t size=indices?uint64_t(p.size)/width*4:p.size;
  if(size>512*1024*1024) return Fail(e,"Buffer update exceeds capture limit");
  auto found=cpu_buffers_.find(p.key);
  if(p.action==1 && (found==cpu_buffers_.end() || found->second.size()!=size)) return Fail(e,"Partial buffer update has no captured base version");
  auto& bytes=cpu_buffers_[p.key];if(p.action==2) bytes.assign(size,0);
  auto source=indices?std::as_bytes(std::span(update.normalized)):std::as_bytes(std::span(update.bytes));
  size_t begin=indices?size_t(p.begin)/width*4:p.begin,end=indices?size_t(p.end)/width*4:p.end;
  if(source.size()!=end-begin) return Fail(e,"Buffer update payload length differs from range");
  std::memcpy(bytes.data()+begin,source.data(),source.size());return resources_.UploadBuffer(p.key,std::as_bytes(std::span(bytes)),version,e);
}
bool GameRenderer::Record(const guest::RenderPacket& packet,VkCommandBuffer command,Error& e) {
  if(command!=command_ || !serial_) return Fail(e,"Packet command buffer differs from active submission");
  try {
    if(auto* draw=std::get_if<guest::DrawPacket>(&packet)) {bool ok=Draw(*draw,command,e);if(!ok && e.result!=VK_NOT_READY) ++stats_.failed;return ok;}
    if(auto* clear=std::get_if<guest::ClearPacket>(&packet)) {if(!targets_.Clear(*clear,e)) return false;++stats_.clears;return true;}
    if(auto* resolve=std::get_if<guest::ResolvePacket>(&packet)) {
      auto plan=PlanResolve(*resolve,targets_,e);if(!e.message.empty()) return false;
      if(resolve->destination) plan.destination=TextureResourceId(resolve->destination_fetch);
      guest::ResourceId raw_id=0;
      if(!plan.empty && plan.operation==ResolveOperation::kDepthConversion) {
        if(!depth_ready_) {if(!depth_resolver_.Initialize(e)) return false;depth_ready_=true;}
        auto depth_fetch=resolve->destination_fetch;depth_fetch[1]=(depth_fetch[1]&~63u)|23u;plan.destination=TextureResourceId(depth_fetch);
        auto raw_fetch=resolve->destination_fetch;raw_fetch[1]=(raw_fetch[1]&~63u)|6u;
        raw_id=TextureResourceId(raw_fetch);
        if(!depth_resolver_.Record(command,plan,raw_id,targets_,resources_,state_,e)) return false;
      } else if(!RecordColorResolve(c_,command,plan,targets_,resources_,state_,e)) return false;
      if(!plan.empty) {
        auto& latest=resolved_latest_[plan.physical_base];latest.image=resources_.Texture(plan.destination,e);if(!latest.image) return false;
        latest.raw=raw_id?resources_.Texture(raw_id,e):nullptr;if(raw_id && !latest.raw) return false;
        if(resolve->has_copy_draw) latest.swap=resolve->copy_dest_swap;
        latest.generation=++resolve_generation_;
      }
      if(plan.clear_color || plan.clear_depth) {
        guest::ClearPacket clear{};clear.colors[0]=resolve->clear_color_surface;clear.depth_surface=resolve->clear_depth_surface;
        clear.flags=(plan.clear_color?1u:0u)|(plan.clear_depth?0x30u:0u);clear.color=resolve->clear_color;clear.depth=resolve->clear_depth;clear.stencil=resolve->clear_stencil;
        if(resolve->has_source_rect) clear.rects={resolve->source};
        if(!targets_.Clear(clear,e)) return false;++stats_.clears;
      }
      e={};return true;
    }
    if(auto* pass=std::get_if<guest::PassPacket>(&packet)) {
      if(pass->operation==guest::Op::kEndTiling) tiling_={};
      if(pass->operation==guest::Op::kBeginTiling) {
        tiling_={};for(auto& rect:pass->rects) {tiling_.width=std::max(tiling_.width,uint32_t(std::max(0,rect.right)));tiling_.height=std::max(tiling_.height,uint32_t(std::max(0,rect.bottom)));}
        guest::ClearPacket clear{};clear.colors[0]=pass->color_surface;clear.depth_surface=pass->depth_surface;
        clear.flags=(pass->clear_color?1u:0u)|(pass->depth_surface.id?0x30u:0u);
        clear.color=pass->color;clear.depth=std::clamp(pass->depth,0.0f,1.0f);clear.stencil=pass->stencil;
        if(tiling_.width && tiling_.height) {
          for(auto* surface:{&clear.colors[0],&clear.depth_surface}) if(surface->id) {surface->geometry.width=tiling_.width;surface->geometry.height=tiling_.height;}
        }
        if(clear.flags && !targets_.Clear(clear,e)) return false;
      }
      e={};return true;
    }
    return Fail(e,"Resolve/presentation consumer is not integrated yet");
  } catch(const std::exception& ex) {e={"Game render packet",VK_ERROR_INITIALIZATION_FAILED,ex.what()};++stats_.failed;return false;}
}
bool GameRenderer::Draw(const guest::DrawPacket& draw,VkCommandBuffer command,Error& e) {
  if(draw.primitive==guest::Primitive::kRectangles && draw.inline_vertices) {
    guest::DrawPacket expanded=draw;std::string error;
    if(!guest::ExpandRectangles(std::as_bytes(std::span(draw.inline_data)),draw.count,draw.inline_stride,draw.attributes,expanded.inline_data,error)) {e={"Rectangle expansion",VK_ERROR_INITIALIZATION_FAILED,error};return false;}
    expanded.primitive=guest::Primitive::kTriangles;expanded.count=uint32_t(expanded.inline_data.size()/draw.inline_stride);expanded.first=0;return Draw(expanded,command,e);
  }
  if(draw.primitive==guest::Primitive::kQuads && !draw.indexed) {
    guest::DrawPacket expanded=draw;uint64_t end=uint64_t(draw.first)+draw.count;
    if(end>UINT32_MAX || draw.count>16*1024*1024) return Fail(e,"Quad expansion range invalid");
    for(uint32_t q=0;q<draw.count/4;++q) {uint32_t b=draw.first+q*4;expanded.expanded_indices.insert(expanded.expanded_indices.end(),{b,b+1,b+2,b,b+2,b+3});}
    expanded.primitive=guest::Primitive::kTriangles;expanded.indexed=true;expanded.first=0;expanded.count=uint32_t(expanded.expanded_indices.size());return Draw(expanded,command,e);
  }
  // Consume cache updates even while shaders compile. Later action0/partial
  // packets depend on the first owned version and cannot read guest memory.
  if(draw.inline_vertices) {auto id=InlineBufferBase|draw.command_serial;if(!resources_.UploadBuffer(id,std::as_bytes(std::span(draw.inline_data)),draw.command_serial,e)) return false;transient_buffers_[serial_].push_back(id);}
  else for(auto& stream:draw.streams) if(!Upload(stream.update,false,draw.command_serial,e)) return false;
  if(draw.indexed && draw.indices.plan.key && !Upload(draw.indices,true,draw.command_serial,e)) return false;
  if(draw.indexed && !draw.indices.plan.key && draw.expanded_indices.empty() && draw.count) return Fail(e,"Indexed draw has no captured indices");
  auto index_id=draw.indices.plan.key;
  if(!draw.expanded_indices.empty()) {index_id=ExpandedIndexBufferBase|draw.command_serial;if(!resources_.UploadBuffer(index_id,std::as_bytes(std::span(draw.expanded_indices)),draw.command_serial,e)) return false;transient_buffers_[serial_].push_back(index_id);}
  if(!draw.count) {e={};return true;}
  if(!draw.vertex_shader || !shaders_) return Fail(e,"Captured vertex shader/lookup absent");
  auto vs=shaders_(*draw.vertex_shader);shaders::ShaderResult ps{shaders::ShaderPoll::ready,{},{}};
  if(draw.pixel_shader) ps=shaders_(*draw.pixel_shader);
  if(vs.status==shaders::ShaderPoll::failed || ps.status==shaders::ShaderPoll::failed) {e={"Game shader",VK_ERROR_INITIALIZATION_FAILED,vs.status==shaders::ShaderPoll::failed?vs.diagnostic:ps.diagnostic};return false;}
  if(vs.status==shaders::ShaderPoll::pending || ps.status==shaders::ShaderPoll::pending) {++stats_.pending;e={"Game shader",VK_NOT_READY,"Captured shader compilation pending"};return false;}
  if(!vs.artifact || (draw.pixel_shader && !ps.artifact)) return Fail(e,"Ready shader has no owned artifact");
  auto plan=PlanAttachments(draw,e,draw.tiling_active?tiling_:VkExtent2D{});if(!e.message.empty()) return false;
  auto pass=targets_.PreparePass(plan,e);if(!pass) return false;
  for(auto& [destination,alias]:targets_.Aliases(*pass)) {
    if(!depth_ready_) {if(!depth_resolver_.Initialize(e)) return false;depth_ready_=true;}
    if(!depth_resolver_.RecordAlias(command,destination,alias,targets_,resources_,state_,e,alias_options_)) return false;
  }
  for(uint32_t slot=0;slot<32;++slot) if((draw.texture_fetch[slot][0]&3)==2) {
    auto id=TextureResourceId(draw.texture_fetch[slot]);Error lookup;
    auto current=resources_.Texture(id,lookup);
    if(auto resolved=resolved_latest_.find(ResolvedPhysicalBase(draw.texture_fetch[slot]));resolved!=resolved_latest_.end()) {
      auto& latest=resolved->second;auto selected=((draw.texture_fetch[slot][1]&63)==6 && latest.raw)?latest.raw:latest.image;
      const auto version=std::pair{resolved->first,latest.generation};
      if(!current || !resolve_views_.contains(id) || resolve_views_[id]!=version) {
        auto mapping=PlanResolvedSwizzle(draw.texture_fetch[slot],selected->format,selected==latest.raw?false:latest.swap);
        if(!resources_.BindTextureView(id,selected,mapping,e)) return false;resolve_views_[id]=version;current=resources_.Texture(id,e);
      }
    }
    if(current && current->state==&state_) {
      if(!state_.Transition(command,current->handle,{VK_IMAGE_ASPECT_COLOR_BIT,0,current->mips,0,current->layers},ImageUsage::Sampled(),e)) return false;
      continue;
    }
    auto capture=draw.textures[slot];if(!capture) return Fail(e,"Bound texture needs a captured upload or resolved image");
    if(current && current->version==capture->version) continue;
    guest::LinearTexture texture;std::string reason;if(!decoder_ || !decoder_(*capture,texture,reason)) {e={"Game texture",VK_ERROR_FORMAT_NOT_SUPPORTED,reason.empty()?"Captured texture decoder unavailable":reason};return false;}
    if(!resources_.UploadTexture(id,texture,capture->version,e)) return false;
  }
  auto bindings=BuildBindings(draw,e);if(!e.message.empty()) return false;
  auto descriptors=descriptors_.Prepare(bindings,draw.texture_fetch,resources_,serial_,e);if(!descriptors) return false;
  auto pipeline=pipelines_.Acquire(draw,*pass,*vs.artifact,ps.artifact.get(),serial_,e);if(!pipeline) {e.message+="; VS="+std::to_string(draw.vertex_shader->hash)+", PS="+std::to_string(draw.pixel_shader?draw.pixel_shader->hash:0)+", RT0="+std::to_string(pass->formats[0])+", DS="+std::to_string(pass->depth_format);return false;}
  struct Binding {uint32_t slot;std::shared_ptr<BufferResource> resource;VkDeviceSize offset;};std::vector<Binding> vertices;
  if(draw.inline_vertices) {auto b=resources_.Buffer(InlineBufferBase|draw.command_serial,e);if(!b) return false;vertices.push_back({0,b,0});}
  else for(auto& stream:draw.streams) {auto b=resources_.Buffer(stream.update.plan.key,e);if(!b || stream.offset>b->size) return Fail(e,"Vertex stream offset exceeds buffer");vertices.push_back({stream.stream,b,stream.offset});}
  auto missing=resources_.Buffer(MissingVertex,e);if(!missing) return false;vertices.push_back({31,missing,0});
  std::shared_ptr<BufferResource> indices;if(draw.indexed) {indices=resources_.Buffer(index_id,e);if(!indices || uint64_t(draw.first+uint64_t(draw.count))*4>indices->size) {e={"Index draw",VK_ERROR_INITIALIZATION_FAILED,"Draw exceeds captured buffer: first="+std::to_string(draw.first)+", count="+std::to_string(draw.count)+", bytes="+std::to_string(indices?indices->size:0)+", plan="+std::to_string(draw.indices.plan.size)+", format="+std::to_string(draw.indices.plan.format)+", action="+std::to_string(draw.indices.plan.action)+", serial="+std::to_string(draw.command_serial)};return false;}}
  VkViewport viewport{draw.viewport.x,draw.viewport.y,draw.viewport.width,draw.viewport.height,draw.viewport.min_depth,draw.viewport.max_depth};
  if(viewport.width<=0 || viewport.height<=0) viewport={0,0,float(pass->extent.width),float(pass->extent.height),0,1};
  auto x0=std::clamp(draw.scissor.left,0,int32_t(pass->extent.width)),y0=std::clamp(draw.scissor.top,0,int32_t(pass->extent.height));
  auto x1=std::clamp(draw.scissor.right,x0,int32_t(pass->extent.width)),y1=std::clamp(draw.scissor.bottom,y0,int32_t(pass->extent.height));
  if(x0==x1 || y0==y1) {e={};return true;}
  VkRect2D scissor{{x0,y0},{uint32_t(x1-x0),uint32_t(y1-y0)}};float blend[4];for(uint32_t i=0;i<4;++i) blend[i]=std::bit_cast<float>(draw.registers[0x105+i]);
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass->render_pass;begin.framebuffer=pass->framebuffer;begin.renderArea={{0,0},pass->extent};
  c_.f.vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);c_.f.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->handle);
  c_.f.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines_.Layout(),0,4,descriptors->sets.data(),0,nullptr);
  for(auto& b:vertices) if(std::any_of(pipeline->bindings.begin(),pipeline->bindings.end(),[&](const auto& binding){return binding.binding==b.slot;})) c_.f.vkCmdBindVertexBuffers(command,b.slot,1,&b.resource->handle,&b.offset);
  c_.f.vkCmdSetViewport(command,0,1,&viewport);c_.f.vkCmdSetScissor(command,0,1,&scissor);c_.f.vkCmdSetBlendConstants(command,blend);c_.f.vkCmdSetStencilReference(command,VK_STENCIL_FACE_FRONT_AND_BACK,draw.registers[0x10d]&255);
  if(draw.indexed) {c_.f.vkCmdBindIndexBuffer(command,indices->handle,0,VK_INDEX_TYPE_UINT32);c_.f.vkCmdDrawIndexed(command,draw.count,1,draw.first,draw.base_vertex,0);}else c_.f.vkCmdDraw(command,draw.count,1,draw.first,0);
  c_.f.vkCmdEndRenderPass(command);targets_.MarkWritten(*pass);++stats_.draws;e={};return true;
}
}
