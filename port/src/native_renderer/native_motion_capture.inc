// Included inside native_renderer.cpp's namespace to share its audited IA helpers.
// Opt-in two-frame experiment. This does not claim gameplay DLSS readiness.
void Renderer::ReportObservedClothDraw(const WorkBatch& batch,const WorkCmd& cmd) {
  if(!motion_capture_active_ || motion_scene_finished_ || motion_cloth_reports_>=2 ||
     current_vs_hash_!=0xA5232A3184AA617Cull || cmd.observed_render.render!=0x82301510) return;
  ++motion_cloth_reports_;const auto& c=cmd.observed_render.cloth;
  REXLOG_INFO("native observed cloth route: phase={} serial={} owner={:08X} generation={} checked={} topology_generation={} caller={:08X} vertices={} quads={} ring={} index32={} expected_ib={:08X}/{} actual_ib={:08X}/{} streams={} identity_verified=false",
      motion_capture_phase_,cmd.command_serial,cmd.observed_render.token.address,cmd.observed_render.token.generation,
      c.source_route_observed,c.topology_generation,c.caller,c.vertices,c.quads,c.ring,c.index32,
      c.indices.physical,c.indices.bytes,cmd.index.address,cmd.index.size,cmd.stream_count);
  if(uint64_t(cmd.stream_first)+cmd.stream_count>batch.streams.size()) return;
  for(uint32_t i=0;i<cmd.stream_count;++i) {
    const auto& stream=batch.streams[cmd.stream_first+i];
    if(stream.stream>=4)continue;
    const auto& expected=c.streams[stream.stream];
    REXLOG_INFO("native observed cloth stream: phase={} serial={} role={} expected_object={:08X} expected_physical={:08X} expected_bytes={} actual_physical={:08X} actual_bytes={} offset={} stride={} view_bytes={}",
        motion_capture_phase_,cmd.command_serial,stream.stream,expected.object,expected.physical,expected.bytes,
        stream.buffer.address,stream.buffer.size,stream.offset,stream.stride,stream.size);
  }
}

void Renderer::CaptureMotionClipDraw(uint8_t* base,uint32_t dev,D3D12_PRIMITIVE_TOPOLOGY topology,
    bool indexed,uint32_t count,uint32_t first,int32_t base_vertex,
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE cut,const WorkBatch* batch,const WorkCmd* cmd) {
  if(!motion_capture_active_ || motion_scene_finished_ || !motion_clips_ || !current_raster_pipeline_ ||
     SkipGpuDraw() || !bound_rts_[0] || !bound_ds_ || bound_rts_[0]->guest_format!=12 ||
     bound_rts_[0]->width!=kOutputWidth || bound_rts_[0]->height!=kOutputHeight ||
     bound_samples_!=1 || !current_motion_raster_.full_viewport ||
     (topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST && topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)) return;
  if(!graphics::MatchesRetainedMotionCapturePair(
      {reinterpret_cast<uintptr_t>(motion_captured_color_.Get()),reinterpret_cast<uintptr_t>(motion_captured_depth_.Get())},
      {reinterpret_cast<uintptr_t>(bound_rts_[0]->resource.Get()),reinterpret_cast<uintptr_t>(bound_ds_->resource.Get())})) return;
  const uint32_t vertices=graphics::PostVertexCapacity(count,topology==D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  if(!vertices) return;
  if(cmd && cmd->observed_render.token.generation && motion_owner_reports_[current_vs_hash_==0xC687EB65D635C5D7ull?1:0]<16 &&
     graphics::IsObservedSkinnedMotionShader(current_vs_hash_)) {
    ++motion_owner_reports_[current_vs_hash_==0xC687EB65D635C5D7ull?1:0];
    const auto& owner=cmd->observed_render;
    REXLOG_INFO("native instance worker provenance: phase={} frame={} serial={} vs={:016X} owner={:08X} generation={} render={:08X} vtable={:08X} packet={:08X} packet_word16={:08X} motion_identity_verified=false",
        motion_capture_phase_,frame_count_+1,cmd->command_serial,current_vs_hash_,owner.token.address,
        owner.token.generation,owner.render,owner.vtable,owner.packet,owner.packet_word16);
    const auto& part=owner.part;
    REXLOG_INFO("native observed part route: phase={} serial={} checked={} model={:08X} sections={:08X} origin={} lod_slice={} group={} attachment={} stream_offset={} submesh={} actor_palette={:08X} part_identity_verified=false",
        motion_capture_phase_,cmd->command_serial,part.source_route_observed,part.model,part.sections,
        part.origin,part.lod_slice,part.group,part.attachment,part.stream_offset,part.submesh,part.actor_palette);
    graphics::ObservedSkinnedMotionInputs inputs;
    if(cb_vs_version_==mirror_.vs_version && cb_vs_gpu_ &&
       graphics::CaptureObservedSkinnedMotionInputs(current_vs_hash_,owner,mirror_.FloatConstants(false),motion_uploaded_vs_bools_,inputs))
      REXLOG_INFO("native observed skin inputs: phase={} serial={} bank_bool_signature={:016X} uploaded_bool0={:08X} submesh_identity_verified=false",
          motion_capture_phase_,cmd->command_serial,XXH3_64bits(inputs.position_words.data(),sizeof(inputs.position_words)),motion_uploaded_vs_bools_[0]);
  }
  const uint32_t declaration=Load32(base,dev+kDevVertexDecl);
  const auto& attributes=ReadVertexDeclaration(base,declaration);
  const uint64_t declaration_hash=XXH3_64bits(attributes.data(),attributes.size()*sizeof(attributes[0]));
  const uint64_t pipeline_parts[]{current_vs_hash_,declaration_hash,uint64_t(cut)};
  const uint64_t pipeline_key=XXH3_64bits(pipeline_parts,sizeof(pipeline_parts));
  auto found=motion_clip_pipelines_.find(pipeline_key);
  if(found==motion_clip_pipelines_.end()) {
    const auto* vs=LoadShader(current_vs_hash_,true);if(!vs) return;
    const auto inputs=VertexShaderInputs(*vs);
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    for(const auto& a:attributes) {
      const auto format=MapDeclType(a.type);if(format.format==DXGI_FORMAT_UNKNOWN) continue;
      if(!inputs.empty() && std::find(inputs.begin(),inputs.end(),
          std::make_pair(std::string(UsageSemantic(a.usage)),a.index))==inputs.end()) continue;
      D3D12_INPUT_ELEMENT_DESC e{};e.SemanticName=UsageSemantic(a.usage);e.SemanticIndex=a.index;
      e.Format=format.format;e.InputSlot=a.stream;e.AlignedByteOffset=a.offset;
      e.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;elements.push_back(e);
    }
    for(const auto& input:VertexShaderInputs(*vs,true)) {
      if(std::any_of(elements.begin(),elements.end(),[&](const auto& e){return input.first==e.SemanticName && input.second==e.SemanticIndex;})) continue;
      uint32_t usage=0;while(usage<14 && input.first!=UsageSemantic(usage)) ++usage;if(usage==14) continue;
      D3D12_INPUT_ELEMENT_DESC e{};e.SemanticName=UsageSemantic(usage);e.SemanticIndex=input.second;
      e.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;e.InputSlot=16;e.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
      elements.push_back(e);
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root_signature_.Get();
    desc.VS={vs->data(),vs->size()};desc.GS=graphics::d3d12::PostVertexClipCapture::GeometryShader();
    const D3D12_SO_DECLARATION_ENTRY so{0,"TEXCOORD",15,0,4,0};const uint32_t stride=16;
    desc.StreamOutput={&so,1,&stride,1,D3D12_SO_NO_RASTERIZED_STREAM};
    desc.InputLayout={elements.data(),UINT(elements.size())};
    desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    desc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    desc.DepthStencilState.FrontFace=desc.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;desc.IBStripCutValue=cut;
    desc.SampleDesc.Count=1;desc.SampleMask=UINT_MAX;
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr=device_->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pipeline));
    if(FAILED(hr)) REXLOG_WARN("native motion clip pipeline failed: vs={:016X} hr={:08X}",current_vs_hash_,uint32_t(hr));
    found=motion_clip_pipelines_.emplace(pipeline_key,std::move(pipeline)).first;
  }
  if(!found->second) return;
  const auto range=motion_clips_->Reserve(vertices);if(!range) return;
  graphics::TemporalGeometrySample sample;
  sample.range={frame_index_,range->offset,range->vertices};
  const auto& words=mirror_.FloatConstants(false);
  std::array<uint32_t,636> world_dependencies{};
  const uint64_t nonindexed_end=uint64_t(first)+count;
  const bool vertex_range_valid=indexed ?
      cmd && cmd->u[7]>cmd->u[6] && cmd->u[7]<=0x1000000u :
      count && nonindexed_end<=0x1000000u;
  const uint32_t vertex_first=indexed && cmd ? cmd->u[6] : first;
  const uint32_t vertex_last=indexed && cmd ? cmd->u[7]-1 : uint32_t(nonindexed_end-1);
  std::array<uint32_t,4> vertex_bools{};
  std::array<uint32_t,32> vertex_loops{};
  for(uint32_t i=0;i<vertex_bools.size();++i) vertex_bools[i]=mirror_.reg(Pm4Mirror::kBoolConstantBase+i);
  for(uint32_t i=0;i<vertex_loops.size();++i) vertex_loops[i]=mirror_.reg(Pm4Mirror::kLoopConstantBase+i);
  graphics::StaticWorldShaderDependencies other_dependencies;
  std::array<uint32_t,16> projection_words{};
  bool shared_world_camera=false,dependencies_valid=false;
  uint64_t position_signature=0;
  graphics::ObservedSkinnedMotionInputs observed_skin;
  graphics::ObservedSkinnedPartKey observed_part;
  const bool observed_skin_inputs=REXCVAR_GET(sr_native_debug_observed_skin_motion) && cmd &&
      motion_coverage_draw_active_ && current_motion_raster_.coverage_id &&
      graphics::IsPixelCoverageShaderPair(current_vs_hash_,current_ps_hash_) &&
      graphics::CaptureObservedSkinnedPartKey(cmd->observed_render,observed_part) &&
      graphics::CaptureObservedSkinnedMotionInputs(current_vs_hash_,cmd->observed_render,
          words,motion_uploaded_vs_bools_,observed_skin);
  if(observed_skin_inputs && vertex_range_valid) {
    projection_words=observed_skin.clip_rows;
    dependencies_valid=true; // Object-to-clip, not a shared camera.
    position_signature=XXH3_64bits(observed_skin.position_words.data(),sizeof(observed_skin.position_words));
  } else if(current_vs_hash_==0x2C45BFFE76D77398ull && vertex_range_valid &&
     graphics::CaptureStaticWorldDependenciesInVertexRange(current_vs_hash_,words,vertex_first,vertex_last,world_dependencies)) {
    std::copy_n(words.begin(),16,projection_words.begin());
    shared_world_camera=true;dependencies_valid=true;
    position_signature=XXH3_64bits(world_dependencies.data(),sizeof(world_dependencies));
  } else if(vertex_range_valid && graphics::CaptureStaticWorldShaderDependencies(
      current_vs_hash_,words,vertex_bools,vertex_loops,vertex_first,vertex_last+1,other_dependencies)) {
    projection_words=other_dependencies.clip_rows;
    shared_world_camera=other_dependencies.clip_matrix_kind==graphics::StaticWorldClipMatrixKind::kWorldToClip;
    dependencies_valid=true;
    position_signature=XXH3_64bits(other_dependencies.position_words.data(),sizeof(other_dependencies.position_words));
  }
  for(uint32_t word:projection_words) if(!std::isfinite(std::bit_cast<float>(word))) dependencies_valid=false;
  if(current_motion_raster_.opaque && REXCVAR_GET(sr_native_pm4_mirror) &&
     cb_vs_version_==mirror_.vs_version && cb_vs_gpu_ && !ScreenSpaceDraw(base,dev) &&
     REXCVAR_GET(sr_native_shader_dir).empty() && batch && cmd && cmd->stream_count &&
     cmd->stream_count<=16 && uint64_t(cmd->stream_first)+cmd->stream_count<=batch->streams.size() &&
     dependencies_valid) {
    if(shared_world_camera) {
      if(motion_camera_valid_ && projection_words!=motion_camera_words_) motion_camera_conflict_=true;
      if(!motion_camera_valid_) {motion_camera_words_=projection_words;motion_camera_valid_=true;}
    }
    std::vector<uint64_t> identity{current_vs_hash_,declaration_hash,uint64_t(topology),uint64_t(cut),
        uint64_t(indexed),count,first,uint32_t(base_vertex)};
    bool versioned=true;
    std::vector<graphics::TemporalFetchResource> fetch_resources;
    for(uint32_t i=0;i<cmd->stream_count;++i) {
      const auto& stream=batch->streams[cmd->stream_first+i];const auto entry=buffers_.find(stream.buffer.key);
      if(entry==buffers_.end() || !entry->second.resource || !entry->second.resource_generation || !entry->second.content_version) {versioned=false;break;}
      identity.insert(identity.end(),{stream.buffer.key,entry->second.resource_generation,entry->second.content_version,
          stream.stream,stream.offset,stream.size,stream.stride});
      fetch_resources.push_back({entry->second.raw_srv,stream.buffer.key,
          entry->second.resource_generation,entry->second.content_version});
    }
    if(cmd->has_index) {
      const auto entry=buffers_.find(cmd->index.key);
      if(entry==buffers_.end() || !entry->second.resource_generation || !entry->second.content_version) versioned=false;
      else identity.insert(identity.end(),{cmd->index.key,entry->second.resource_generation,entry->second.content_version,cmd->index_size,uint64_t(cmd->index32)});
    }
    graphics::TemporalFetchSignature fetch_signature{};
    if(versioned && graphics::CaptureTemporalVertexFetch(vertex_fetch_,fetch_resources,fetch_signature)) {
      const uint64_t buffers_signature=XXH3_64bits(identity.data(),identity.size()*sizeof(identity[0]));
      const uint64_t fetch_hash=XXH3_64bits(fetch_signature.data(),sizeof(fetch_signature));
      identity.push_back(fetch_hash);
      sample.mesh=XXH3_64bits(identity.data(),identity.size()*sizeof(identity[0]));
      const uint64_t domain[]{bound_rts_[0]->edram_base,bound_rts_[0]->width,bound_rts_[0]->height,
          uint64_t(HostPx(bound_rts_[0]->width,bound_rts_[0]->scale)),uint64_t(HostPx(bound_rts_[0]->height,bound_rts_[0]->scale))};
      sample.camera_domain=XXH3_64bits(domain,sizeof(domain));
      sample.geometry_inputs=position_signature;
      const auto clip_inputs=graphics::CaptureTemporalProjectionInputs(projection_words,motion_clip_fixup_);
      sample.projection_inputs=XXH3_64bits(clip_inputs.data(),sizeof(clip_inputs));
      sample.proof=graphics::GeometryHistoryProof::kStaticWorldInputs;
      if(observed_skin_inputs) {
        sample.proof=graphics::GeometryHistoryProof::kVerifiedInstance;
        sample.instance=XXH3_64bits(observed_part.instance_words.data(),sizeof(observed_part.instance_words));
        sample.instance_generation=observed_part.generation;
      }
      if(motion_signature_reports_++<12) {
        REXLOG_INFO("native motion input proof: phase={} mesh={:016X} buffers={:016X} fetch={:016X} world={:016X} domain={:016X} count={} first={} base={}",
            motion_capture_phase_,sample.mesh,buffers_signature,fetch_hash,sample.geometry_inputs,sample.camera_domain,count,first,base_vertex);
        // Record actual inputs before considering another correspondence rule.
        // Bounded to twelve candidate draws per phase, opt-in capture only.
        if(current_vs_hash_==0x2C45BFFE76D77398ull && !REXCVAR_GET(sr_native_dump_dir).empty()) {
          std::error_code error;
          const auto folder=std::filesystem::u8path(REXCVAR_GET(sr_native_dump_dir));
          std::filesystem::create_directories(folder,error);
          std::ofstream diagnostic(folder/fmt::format("motion_inputs_p{}_mesh{:016X}.bin",motion_capture_phase_,sample.mesh),std::ios::binary);
          const uint32_t header[]{0x4D564450,1,vertex_first,vertex_last};
          diagnostic.write(reinterpret_cast<const char*>(header),sizeof(header));
          diagnostic.write(reinterpret_cast<const char*>(words.data()),1024*sizeof(uint32_t));
          diagnostic.write(reinterpret_cast<const char*>(world_dependencies.data()),sizeof(world_dependencies));
        }
      }
    }
  }
  const auto registered=motion_history_.Add(sample);if(!registered) return;
  if(!motion_captured_depth_) {
    // Own the actual scene resources; resolves may invalidate OM binding caches
    // before the depth mutation which terminates this capture epoch.
    motion_captured_depth_=bound_ds_->resource;motion_captured_color_=bound_rts_[0]->resource;
    motion_captured_color_edram_=bound_rts_[0]->edram_base;
    motion_captured_color_width_=bound_rts_[0]->width;
    motion_captured_color_height_=bound_rts_[0]->height;
    motion_captured_color_scale_=bound_rts_[0]->scale;
  }
  motion_rasters_.push_back(current_motion_raster_);
  command_list_->SetPipelineState(found->second.Get());command_list_->SOSetTargets(0,1,&range->view);
  if(indexed) command_list_->DrawIndexedInstanced(count,1,first,base_vertex,0);
  else command_list_->DrawInstanced(count,1,first,0);
  command_list_->SOSetTargets(0,0,nullptr);command_list_->SetPipelineState(current_raster_pipeline_);
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::MotionCoverageView() const {
  auto view=motion_coverage_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
  view.ptr+=SIZE_T(frame_index_)*device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  return view;
}

void Renderer::ConfigureMotionCoverage(HostSurface* color,HostSurface* depth,
    uint64_t vs,uint64_t ps,bool single_color) {
  motion_coverage_draw_active_=false;motion_coverage_draw_id_=0;motion_coverage_shader_=nullptr;
  const bool paired=graphics::IsPixelCoverageShaderPair(vs,ps);
  if(!paired || motion_coverage_failed_shaders_.contains(ps) || !motion_capture_active_ || motion_scene_finished_ || !single_color || !color || !depth ||
     color->guest_format!=12 || color->width!=kOutputWidth || color->height!=kOutputHeight ||
     color->samples!=1 || depth->samples!=1 || !REXCVAR_GET(sr_native_shader_dir).empty() ||
     motion_coverage_next_id_>=4096) return;
  const auto dimensions=color->resource->GetDesc(),depth_dimensions=depth->resource->GetDesc();
  if(dimensions.Width!=depth_dimensions.Width || dimensions.Height!=depth_dimensions.Height) return;
  auto shader=motion_coverage_shaders_.find(ps);
  if(shader==motion_coverage_shaders_.end()) {
    std::vector<uint8_t> bytecode;
    if(ShaderTranslator::Get().FetchPixelCoverageVariant(ps,bytecode)!=TranslateState::kReady || bytecode.empty()) return;
    shader=motion_coverage_shaders_.emplace(ps,std::move(bytecode)).first;
  }
  if(!motion_coverage_rtv_heap_) {
    D3D12_DESCRIPTOR_HEAP_DESC descriptors{};descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    descriptors.NumDescriptors=3;
    if(FAILED(device_->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&motion_coverage_rtv_heap_)))) return;
  }
  auto& owner=motion_coverage_[frame_index_];
  if(!owner || owner->GetDesc().Width!=dimensions.Width || owner->GetDesc().Height!=dimensions.Height) {
    D3D12_RESOURCE_DESC texture=dimensions;texture.Format=DXGI_FORMAT_R32_UINT;
    texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};clear.Format=texture.Format;
    if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,
        D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(&owner)))) return;
    motion_coverage_state_[frame_index_]=D3D12_RESOURCE_STATE_RENDER_TARGET;
    device_->CreateRenderTargetView(owner.Get(),nullptr,MotionCoverageView());
  }
  Transition(owner.Get(),motion_coverage_state_[frame_index_],D3D12_RESOURCE_STATE_RENDER_TARGET);
  if(!motion_coverage_cleared_) {
    const float zero[4]{};command_list_->ClearRenderTargetView(MotionCoverageView(),zero,0,nullptr);
    motion_coverage_cleared_=true;
  }
  motion_coverage_shader_=&shader->second;
  motion_coverage_draw_id_=++motion_coverage_next_id_;motion_coverage_draw_active_=true;
}

void Renderer::BeginMotionCapture() {
  ReportMotionReadbacks(frame_index_);
  motion_capture_active_=false;motion_scene_finished_=false;
  motion_jitter_draw_active_=motion_jitter_applied_=motion_jitter_ready_=false;motion_frame_jitter_={};
  motion_captured_depth_.Reset();motion_captured_color_.Reset();
  motion_coverage_draw_active_=false;motion_coverage_bound_=false;motion_coverage_cleared_=false;
  motion_coverage_next_id_=motion_coverage_draw_id_=0;motion_coverage_shader_=nullptr;
  if(motion_capture_done_ || packet_sink_) return;
  const auto& trigger=REXCVAR_GET(sr_native_motion_capture_trigger_path);
  std::error_code error;
  if(!motion_capture_phase_ && (trigger.empty() || !std::filesystem::exists(std::filesystem::u8path(trigger),error) || error)) return;
  if(!motion_clips_) {
    auto clips=std::make_unique<graphics::d3d12::PostVertexClipCapture>();
    if(!clips->Initialize(device_)) {REXLOG_ERROR("native motion: clip allocation failed");motion_capture_done_=true;return;}
    motion_clips_=std::move(clips);
  }
  if(!motion_clips_->Begin(command_list_.Get(),frame_index_)) {motion_capture_done_=true;return;}
  ++motion_capture_phase_;motion_capture_active_=true;motion_signature_reports_=0;motion_owner_reports_={};motion_cloth_reports_=0;
  if(motion_capture_phase_==1) {motion_previous_jitter_={};ArmInstanceProbe();}
  motion_jitter_ready_=graphics::MakeSceneRasterJitter(motion_capture_phase_-1,
      HostPx(kOutputWidth,render_scale_),HostPx(kOutputHeight,render_scale_),
      REXCVAR_GET(sr_native_debug_motion_jitter),motion_frame_jitter_);
  motion_camera_valid_=motion_camera_conflict_=false;motion_rasters_.clear();
  motion_history_.Begin(frame_count_+1,HostPx(kOutputWidth,render_scale_),HostPx(kOutputHeight,render_scale_),motion_previous_camera_bad_);
}

void Renderer::MaybeFinishMotionBeforeDepthWrite(HostSurface* depth,bool writes_depth,const char* reason) {
  const graphics::MotionDepthBoundaryState state{motion_capture_active_,motion_scene_finished_,
      !motion_history_.samples().empty(),reinterpret_cast<uintptr_t>(motion_captured_depth_.Get())};
  if(!graphics::ShouldFreezeMotionDepthBeforeWrite(state,
      depth?reinterpret_cast<uintptr_t>(depth->resource.Get()):0,writes_depth)) return;
  REXLOG_INFO("native motion depth boundary: phase={} host_frame={} reason={} depth={:X} before_mutation=true",
      motion_capture_phase_,frame_count_+1,reason,reinterpret_cast<uint64_t>(depth->resource.Get()));
  FinishMotionScene(depth);
}

void Renderer::FinishMotionScene(HostSurface* depth) {
  using namespace graphics::d3d12;
  static_assert(TemporalMotion::kMaxVertices >= PostVertexClipCapture::kBytes/16/3*3,
                "The motion consumer must cover the bounded clip capture arena");
  if(!depth || depth->resource.Get()!=motion_captured_depth_.Get() || !motion_clips_ ||
     motion_scene_finished_ || motion_history_.samples().empty()) return;
  motion_scene_finished_=true;motion_clips_->Finish(command_list_.Get());motion_history_.Seal();
  uint32_t matched=0,observed_skin_matches=0,unverified=0,missing=0,ambiguous=0;uint64_t vertex_total=0;
  struct Family {uint32_t draws=0,opaque=0,matches=0,stencil=0,no_depth_write=0,blend=0,alpha_test=0;};
  std::unordered_map<uint64_t,Family> families;
  std::vector<MotionGeometry> geometry;
  const auto dimensions=depth->resource->GetDesc();
  REXLOG_INFO("native motion scene snapshot source: phase={} host_frame={} prepare_observations={} color_edram={} color={}x{} scale={} color_resource={:X} depth_edram={} depth={}x{} depth_resource={:X} host_depth={}x{}",
      motion_capture_phase_,frame_count_+1,surface_sequence_.total_draws(),
      motion_captured_color_edram_,motion_captured_color_width_,
      motion_captured_color_height_,motion_captured_color_scale_,
      reinterpret_cast<uint64_t>(motion_captured_color_.Get()),
      depth->edram_base,depth->width,depth->height,reinterpret_cast<uint64_t>(depth->resource.Get()),
      dimensions.Width,dimensions.Height);
  for(size_t i=0;i<motion_history_.samples().size();++i) {
    const auto& sample=motion_history_.samples()[i];const auto match=motion_history_.Resolve(i);
    vertex_total+=sample.range.vertices;
    matched+=match.status==graphics::GeometryHistoryStatus::kMatch;
    unverified+=match.status==graphics::GeometryHistoryStatus::kUnverifiedIdentity;
    ambiguous+=match.status==graphics::GeometryHistoryStatus::kAmbiguous;
    missing+=match.status==graphics::GeometryHistoryStatus::kMissing || match.status==graphics::GeometryHistoryStatus::kNoPreviousFrame;
    MotionGeometry draw;
    draw.currentClip=motion_clips_->Buffer(sample.range.slot);draw.current_offset=sample.range.offset;
    draw.vertex_count=sample.range.vertices;
    draw.correspondence_valid=match.status==graphics::GeometryHistoryStatus::kMatch &&
        !motion_camera_conflict_ && !motion_previous_camera_bad_ && !motion_clips_->dropped() && !motion_history_.dropped();
    if(draw.correspondence_valid) {
      observed_skin_matches+=sample.proof==graphics::GeometryHistoryProof::kVerifiedInstance;
      draw.previousClip=motion_clips_->Buffer(match.previous.slot);draw.previous_offset=match.previous.offset;
    }
    const auto& raster=motion_rasters_[i];
    draw.coverage_id=raster.coverage_id;
    auto& family=families[raster.shader];++family.draws;family.opaque+=raster.opaque;
    family.matches+=match.status==graphics::GeometryHistoryStatus::kMatch;
    family.stencil+=raster.stencil_enabled;
    family.no_depth_write+=!raster.z_write;
    family.blend+=(raster.blend_control&0x1fff)!=1;
    family.alpha_test+=(raster.color_control&8) && (raster.color_control&7)!=7;
    draw.cull_mode=raster.cull;draw.front_counter_clockwise=raster.front_ccw;
    draw.depth_bias=raster.depth_bias;draw.slope_scaled_depth_bias=raster.slope_bias;
    draw.scissor_enabled=true;
    draw.scissor={std::clamp<LONG>(raster.scissor.left,0,LONG(dimensions.Width)),
        std::clamp<LONG>(raster.scissor.top,0,LONG(dimensions.Height)),
        std::clamp<LONG>(raster.scissor.right,0,LONG(dimensions.Width)),
        std::clamp<LONG>(raster.scissor.bottom,0,LONG(dimensions.Height))};
    draw.scissor.right=std::max(draw.scissor.left,draw.scissor.right);
    draw.scissor.bottom=std::max(draw.scissor.top,draw.scissor.bottom);
    geometry.push_back(draw);
  }
  REXLOG_INFO("native motion clip history: phase={} frame={} guest_swap={} draws={} vertices={} matched={} unverified={} missing={} ambiguous={} camera_conflict={} dropped={} (partial position proof; unsupported objects/deformation unverified)",
      motion_capture_phase_,frame_count_+1,swap_number_,geometry.size(),vertex_total,matched,unverified,missing,ambiguous,
      motion_camera_conflict_,motion_clips_->dropped()+motion_history_.dropped());
  for(const auto& [shader,family]:families)
    REXLOG_INFO("native motion family: phase={} vs={:016X} draws={} position_eligible={} matches={} stencil={} no_z_write={} blend={} alpha_test={} (coverage unqualified)",
        motion_capture_phase_,shader,family.draws,family.opaque,family.matches,family.stencil,family.no_depth_write,family.blend,family.alpha_test);
  if(motion_capture_phase_!=2) return;
  if(!temporal_motion_) {
    auto motion=std::make_unique<TemporalMotion>();
    if(FAILED(motion->Initialize(device_))) {REXLOG_ERROR("native motion initialization failed: {}",motion->LastError());return;}
    temporal_motion_=std::move(motion);
  }
  // The guest may change its depth again for UI/post effects later in this
  // list. Freeze a private, immutable snapshot for the motion module's fence.
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  HRESULT hr=device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&dimensions,
      D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&motion_depth_snapshot_));
  if(FAILED(hr)) {REXLOG_ERROR("native motion depth allocation failed {:08X}",uint32_t(hr));return;}
  const auto source_state=depth->state;
  Transition(depth->resource.Get(),depth->state,D3D12_RESOURCE_STATE_COPY_SOURCE);
  command_list_->CopyResource(motion_depth_snapshot_.Get(),depth->resource.Get());
  Transition(depth->resource.Get(),depth->state,source_state);
  auto snapshot_state=D3D12_RESOURCE_STATE_COPY_DEST;
  Transition(motion_depth_snapshot_.Get(),snapshot_state,D3D12_RESOURCE_STATE_DEPTH_READ);
  TemporalMotionInputs input;
  input.depth=motion_depth_snapshot_.Get();input.width=uint32_t(dimensions.Width);input.height=dimensions.Height;
  input.current_jitter[0]=motion_jitter_applied_?motion_frame_jitter_.pixel_x:0;
  input.current_jitter[1]=motion_jitter_applied_?motion_frame_jitter_.pixel_y:0;
  input.previous_jitter[0]=motion_previous_jitter_[0];input.previous_jitter[1]=motion_previous_jitter_[1];
  if(motion_coverage_cleared_) {
    Transition(motion_coverage_[frame_index_].Get(),motion_coverage_state_[frame_index_],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    input.coverage=motion_coverage_[frame_index_].Get();
    QueueMotionReadback(input.coverage,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,"audited_pixel_owners");
  }
  hr=temporal_motion_->BeginFrame(frame_index_);
  if(SUCCEEDED(hr)) hr=temporal_motion_->Render(command_list_.Get(),input,geometry);
  frame_state_bound_=false;bound_rt_count_=0;bound_ds_=nullptr;
  std::memset(bound_rts_,0,sizeof(bound_rts_));
  if(FAILED(hr)) {REXLOG_ERROR("native motion render failed {:08X}: {}",uint32_t(hr),temporal_motion_->LastError());return;}
  const auto outputs=temporal_motion_->Outputs();
  // Preserve the actual scene inputs for a deterministic depth/EQUAL replay.
  // Sky-only coverage must be diagnosed using real clips and depth, rather than
  // loosening equality or interpreting a synthetic fixture as gameplay proof.
  if(!REXCVAR_GET(sr_native_dump_dir).empty()) {
    const auto directory=std::filesystem::u8path(REXCVAR_GET(sr_native_dump_dir));
    std::error_code error;std::filesystem::create_directories(directory,error);
    std::ofstream manifest(directory/fmt::format("motion_geometry_s{}.csv",swap_number_));
    manifest<<"draw,offset,vertices,vs,valid,cull,front_ccw,depth_bias,slope_bias,left,top,right,bottom\n";
    uint32_t used=0;
    for(size_t i=0;i<geometry.size();++i) {
      const auto& g=geometry[i];
      used=uint32_t(std::max<uint64_t>(used,g.current_offset+uint64_t(g.vertex_count)*16));
      manifest<<i<<','<<g.current_offset<<','<<g.vertex_count<<','<<std::hex<<motion_rasters_[i].shader<<std::dec
          <<','<<g.correspondence_valid<<','<<uint32_t(g.cull_mode)<<','<<g.front_counter_clockwise
          <<','<<g.depth_bias<<','<<g.slope_scaled_depth_bias<<','<<g.scissor.left<<','<<g.scissor.top
          <<','<<g.scissor.right<<','<<g.scissor.bottom<<'\n';
    }
    manifest.close();
    QueueMotionClipReadback(motion_clips_->Buffer(frame_index_),used);
    QueueMotionReadback(motion_depth_snapshot_.Get(),D3D12_RESOURCE_STATE_DEPTH_READ,"scene_depth_snapshot");
  }
  QueueMotionReadback(outputs.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET,"motion_velocity");
  QueueMotionReadback(outputs.validity,D3D12_RESOURCE_STATE_RENDER_TARGET,"motion_validity");
  // Diagnostic upper bound only: isolate how much candidate coverage is later
  // invalidated by unverified geometry. This is NEVER an input to DLSS/readiness.
  // The production experiment above still includes every unknown invalidator.
  std::vector<MotionGeometry> static_only;
  for(const auto& draw:geometry) if(draw.correspondence_valid) static_only.push_back(draw);
  if(!motion_static_candidate_) {
    auto candidate=std::make_unique<TemporalMotion>();
    if(SUCCEEDED(candidate->Initialize(device_))) motion_static_candidate_=std::move(candidate);
  }
  if(motion_static_candidate_) {
    HRESULT candidate_hr=motion_static_candidate_->BeginFrame(frame_index_);
    if(SUCCEEDED(candidate_hr)) candidate_hr=motion_static_candidate_->Render(command_list_.Get(),input,static_only);
    if(SUCCEEDED(candidate_hr)) {
      const auto candidate=motion_static_candidate_->Outputs();
      QueueMotionReadback(candidate.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET,"candidate_matched_only_motion_velocity");
      QueueMotionReadback(candidate.validity,D3D12_RESOURCE_STATE_RENDER_TARGET,"candidate_matched_only_motion_validity");
      REXLOG_INFO("native motion candidate-only GPU recorded: draws={} unknown_geometry_omitted=true DLSS_input=false (diagnostic upper bound, not verified scene coverage)",static_only.size());
    } else REXLOG_WARN("native motion candidate-only render failed {:08X}: {}",uint32_t(candidate_hr),motion_static_candidate_->LastError());
  }
  // Attribute actual valid pixels to C209 without mistaking a later static
  // writer for a skinned pixel. Preserve every other draw as a zero-validity
  // invalidator in original order, with the SAME final depth/coverage tests.
  if(observed_skin_matches && REXCVAR_GET(sr_native_debug_observed_skin_motion)) {
    if(!motion_observed_skin_attribution_) {
      auto candidate=std::make_unique<TemporalMotion>();
      if(SUCCEEDED(candidate->Initialize(device_))) motion_observed_skin_attribution_=std::move(candidate);
    }
    if(motion_observed_skin_attribution_) {
      auto attribution=geometry;
      for(size_t i=0;i<attribution.size();++i)
        if(!graphics::IsObservedSkinnedMotionShader(motion_rasters_[i].shader) ||
           motion_history_.samples()[i].proof!=graphics::GeometryHistoryProof::kVerifiedInstance)
          attribution[i].correspondence_valid=false;
      HRESULT skin_hr=motion_observed_skin_attribution_->BeginFrame(frame_index_);
      if(SUCCEEDED(skin_hr)) skin_hr=motion_observed_skin_attribution_->Render(command_list_.Get(),input,attribution);
      if(SUCCEEDED(skin_hr)) {
        const auto skin=motion_observed_skin_attribution_->Outputs();
        QueueMotionReadback(skin.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET,"observed_skin_motion_velocity");
        QueueMotionReadback(skin.validity,D3D12_RESOURCE_STATE_RENDER_TARGET,"observed_skin_motion_validity");
        REXLOG_INFO("native observed skin attribution GPU recorded: matches={} other_writers_invalidated=true DLSS_input=false",observed_skin_matches);
      } else REXLOG_WARN("native observed skin attribution failed {:08X}: {}",uint32_t(skin_hr),motion_observed_skin_attribution_->LastError());
    }
  }
  motion_color_pairs_.clear();motion_pre_hud_pending_=true;
  REXLOG_INFO("native motion GPU recorded: {}x{} position_matches={} observed_skin_matches={} complete_object_motion_verified=false raster_jitter_applied={} current_jitter_px={},{} previous_jitter_px={},{} candidate_scene_scope_only=true DLSS_applied=Off",
      input.width,input.height,matched,observed_skin_matches,motion_jitter_applied_,input.current_jitter[0],input.current_jitter[1],
      input.previous_jitter[0],input.previous_jitter[1]);
}

void Renderer::QueueMotionReadback(ID3D12Resource* texture,D3D12_RESOURCE_STATES state,const std::string& name) {
  if(!texture || REXCVAR_GET(sr_native_dump_dir).empty()) return;
  const auto desc=texture->GetDesc();
  if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count!=1 || desc.DepthOrArraySize!=1) return;
  MotionReadback readback;readback.slot=frame_index_;readback.swap=swap_number_;
  readback.name=REXCVAR_GET(sr_native_dump_dir)+"/"+fmt::format("{}_s{}.raw",name,swap_number_);
  device_->GetCopyableFootprints(&desc,0,1,0,&readback.layout,nullptr,nullptr,&readback.size);
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=readback.size;
  buffer.Height=1;buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,IID_PPV_ARGS(&readback.resource)))) return;
  const auto original=state;Transition(texture,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=texture;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.pResource=readback.resource.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=readback.layout;
  command_list_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Transition(texture,state,original);
  motion_readbacks_.push_back(std::move(readback));
}

void Renderer::QueueMotionClipReadback(ID3D12Resource* clips,uint32_t bytes) {
  if(!clips || !bytes || bytes%16 || bytes>graphics::d3d12::PostVertexClipCapture::kBytes ||
     REXCVAR_GET(sr_native_dump_dir).empty()) return;
  MotionReadback readback;readback.slot=frame_index_;readback.swap=swap_number_;
  readback.size=bytes;readback.clip_buffer=true;
  readback.name=fmt::format("{}/scene_clips_s{}.bin",REXCVAR_GET(sr_native_dump_dir),swap_number_);
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
  buffer.Height=1;buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;
  buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,IID_PPV_ARGS(&readback.resource)))) return;
  auto state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  Transition(clips,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
  command_list_->CopyBufferRegion(readback.resource.Get(),0,clips,0,bytes);
  Transition(clips,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  motion_readbacks_.push_back(std::move(readback));
}

void Renderer::ReportMotionReadbacks(uint32_t slot) {
  for(auto it=motion_readbacks_.begin();it!=motion_readbacks_.end();) {
    if(it->slot!=slot) {++it;continue;}
    // BeginFrame has waited for this exact slot's submission, including copies.
    void* mapped=nullptr;const D3D12_RANGE range{0,size_t(it->size)};
    if(SUCCEEDED(it->resource->Map(0,&range,&mapped))) {
      if(it->clip_buffer) {
        const auto path=std::filesystem::u8path(it->name);
        std::ofstream out(path,std::ios::binary);
        const uint32_t header[]{0x53434C50,1,uint32_t(it->size/16),16};
        out.write(reinterpret_cast<const char*>(header),sizeof(header));
        out.write(static_cast<const char*>(mapped),std::streamsize(it->size));out.close();
        const D3D12_RANGE empty{0,0};it->resource->Unmap(0,&empty);
        REXLOG_INFO("native motion clips readback: {} vertices={} saved={} fence_completed=true (diagnostic input)",
            path.string(),it->size/16,bool(out));
        it=motion_readbacks_.erase(it);continue;
      }
      const auto& footprint=it->layout.Footprint;
      uint32_t bytes_per_pixel=0;
      switch(footprint.Format) {
        case DXGI_FORMAT_R8_UINT:bytes_per_pixel=1;break;
        case DXGI_FORMAT_R16G16_FLOAT:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM:bytes_per_pixel=4;break;
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R32_UINT:bytes_per_pixel=4;break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:bytes_per_pixel=8;break;
        default:break;
      }
      const auto path=std::filesystem::u8path(it->name);
      const auto folder=path.parent_path();
      std::error_code error;std::filesystem::create_directories(folder,error);
      std::ofstream out(path,std::ios::binary);
      const uint32_t header[]{footprint.Width,footprint.Height,uint32_t(footprint.Format),footprint.RowPitch};
      out.write(reinterpret_cast<const char*>(header),sizeof(header));
      const bool rows=bytes_per_pixel && graphics::WritePaddedReadbackRows(out,
          {static_cast<const uint8_t*>(mapped),size_t(it->size)},size_t(it->layout.Offset),
          footprint.Width*bytes_per_pixel,footprint.RowPitch,footprint.Height);
      out.close();
      const D3D12_RANGE empty{0,0};it->resource->Unmap(0,&empty);
      REXLOG_INFO("native motion readback: {} {}x{} fmt={} saved={} fence_completed=true (experiment only)",
          path.string(),footprint.Width,footprint.Height,uint32_t(footprint.Format),rows && bool(out));
    }
    it=motion_readbacks_.erase(it);
  }
}

void Renderer::EndMotionCapture() {
  if(!motion_capture_active_) return;
  const bool valid_scene=motion_scene_finished_ && !motion_history_.samples().empty();
  if(!motion_scene_finished_) {motion_clips_->Finish(command_list_.Get());motion_history_.Seal();}
  motion_history_.End();motion_previous_camera_bad_=motion_camera_conflict_ || !motion_camera_valid_;
  motion_previous_jitter_=motion_jitter_applied_ ?
      std::array<float,2>{motion_frame_jitter_.pixel_x,motion_frame_jitter_.pixel_y}:std::array<float,2>{};
  motion_capture_active_=false;
  if(!valid_scene) motion_capture_phase_=0;
  else if(motion_capture_phase_>=2) {motion_capture_done_=true;ReportInstanceProbe();}
}

void Renderer::RecordMotionColorBinding(uint32_t slot,const uint32_t* fetch,uint32_t descriptor) {
  if(slot>=3 || !motion_capture_active_ || motion_capture_phase_!=2) return;
  auto& source=motion_color_sources_[slot];source={};
  source.descriptor=descriptor;source.guest_base=GuestPhysical(fetch[1]&0xFFFFF000u);
  if((fetch[0]&3)!=uint32_t(rex::graphics::xenos::FetchConstantType::kTexture)) return;
  const auto latest=resolve_latest_.find(source.guest_base);if(latest==resolve_latest_.end()) return;
  const auto found=resolve_textures_.find(latest->second);if(found==resolve_textures_.end()) return;
  const auto& resolved=found->second;if(!resolved.resource || resolved.stacked_volume) return;
  const uint32_t index=descriptor&0x7fff;
  const bool binding=std::any_of(resolved.srv_by_mapping.begin(),resolved.srv_by_mapping.end(),
      [&](const auto& entry){return entry.second==index;});
  if(index<3 || !binding) return;
  source.resource=reinterpret_cast<uint64_t>(resolved.resource.Get());
  source.source=resolved.motion_source_resource;source.frame=resolved.motion_source_frame;
  source.serial=resolved.motion_source_serial;source.full_copy=resolved.motion_full_color_copy;
}

void Renderer::BeforeMotionColorDraw() {
  if(!motion_capture_active_ || motion_capture_phase_!=2 || !motion_scene_finished_ ||
     motion_color_reports_>=64 || !bound_rts_[0] || bound_rts_[0]->guest_format!=0 ||
     bound_rts_[0]->width!=kOutputWidth || bound_rts_[0]->height!=kOutputHeight) return;
  ++motion_color_reports_;
  REXLOG_INFO("native motion color before actual draw: host_frame={} guest_swap={} next_serial={} vs={:016X} ps={:016X} destination={:X} rtv={:X} format={} samples={} depth={:08X} blend={:08X} control={:08X} mask={:X} full_viewport={} scissor={},{},{},{} (lineage candidate only)",
      frame_count_+1,swap_number_,motion_color_serial_+1,current_vs_hash_,current_ps_hash_,
      reinterpret_cast<uint64_t>(bound_rts_[0]->resource.Get()),bound_rts_[0]->view.ptr,
      uint32_t(bound_rts_[0]->view_format),bound_samples_,current_motion_raster_.depth_control,
      current_motion_raster_.blend_control,current_motion_raster_.color_control,current_motion_raster_.color_mask,
      current_motion_raster_.full_viewport,current_motion_raster_.scissor.left,current_motion_raster_.scissor.top,
      current_motion_raster_.scissor.right,current_motion_raster_.scissor.bottom);
  for(uint32_t slot=0;slot<3;++slot) {
    const auto& s=motion_color_sources_[slot];
    REXLOG_INFO("native motion color actual binding: slot={} descriptor={:X} guest={:08X} resource={:X} copied_from={:X} source_frame={} source_draw={} full_copy={}",
        slot,s.descriptor,s.guest_base,s.resource,s.source,s.frame,s.serial,s.full_copy);
  }
  if(current_vs_hash_==0x69523DC2B02555D3ull && current_ps_hash_==0x40746066C58DA8D5ull) {
    const auto& words=mirror_.FloatConstants(false);
    for(uint32_t row=0;row<4;++row)
      REXLOG_INFO("native motion HUD matrix: row={} values={},{},{},{}",row,
          std::bit_cast<float>(words[row*4]),std::bit_cast<float>(words[row*4+1]),
          std::bit_cast<float>(words[row*4+2]),std::bit_cast<float>(words[row*4+3]));
  }
}

void Renderer::CaptureMotionInlineColorGeometry(uint8_t* base,uint32_t declaration,
    std::span<const uint8_t> vertices,uint32_t stride,uint32_t count,
    D3D12_PRIMITIVE_TOPOLOGY topology,bool quads) {
  if(!motion_capture_active_ || motion_capture_phase_!=2 || !motion_scene_finished_ ||
     motion_color_reports_>=64 || REXCVAR_GET(sr_native_dump_dir).empty() ||
     !bound_rts_[0] || bound_rts_[0]->guest_format!=0 || !stride || !count ||
     count>64 || stride>256 || uint64_t(count)*stride>vertices.size()) return;
  const bool audited=(current_vs_hash_==0x0408D37D5D96E869ull && current_ps_hash_==0x84FDB8B3BB1596B3ull) ||
      (current_vs_hash_==0x815DF1B3CCBFAA4Eull && current_ps_hash_==0xB397B05F4CE5D5BBull) ||
      (current_vs_hash_==0x69523DC2B02555D3ull && current_ps_hash_==0x40746066C58DA8D5ull);
  if(!audited) return;
  const auto& attributes=ReadVertexDeclaration(base,declaration);
  const auto position=std::find_if(attributes.begin(),attributes.end(),[](const auto& a){
    return a.stream==0 && a.usage==0 && a.index==0;});
  if(position==attributes.end()) return;
  const bool color=std::any_of(attributes.begin(),attributes.end(),[](const auto& a){return a.stream==0 && a.usage==10 && a.index==0;});
  const bool uv=std::any_of(attributes.begin(),attributes.end(),[](const auto& a){return a.stream==0 && a.usage==5 && a.index==0;});
  // Decode the very HOST-order bytes about to feed the original guest draw.
  // This supplies a shape observation only, not texture lineage/HUD readiness.
  graphics::SceneColorVertexInput input;
  input.host_bytes={reinterpret_cast<const std::byte*>(vertices.data()),vertices.size()};
  input.stride=stride;input.vertex_count=count;input.position_offset=position->offset;
  const auto format=MapDeclType(position->type).format;
  input.position_components=format==DXGI_FORMAT_R32G32_FLOAT?2:
      format==DXGI_FORMAT_R32G32B32_FLOAT?3:format==DXGI_FORMAT_R32G32B32A32_FLOAT?4:0;
  input.quads=quads;
  input.role=current_vs_hash_==0x0408D37D5D96E869ull?graphics::SceneColorVertexRole::kPassthroughForceW1:
      current_vs_hash_==0x815DF1B3CCBFAA4Eull?graphics::SceneColorVertexRole::kPassthroughPreserveW:
      graphics::SceneColorVertexRole::kProjectedSprite;
  const auto& vertex_constants=mirror_.FloatConstants(false);
  for(uint32_t i=0;i<16;++i) input.matrix_rows[i]=std::bit_cast<float>(vertex_constants[i]);
  for(uint32_t i=0;i<4;++i) input.screen_xform[i]=std::bit_cast<float>(motion_clip_fixup_[i]);
  for(uint32_t i=0;i<2;++i) input.half_pixel[i]=std::bit_cast<float>(motion_clip_fixup_[i+4]);
  graphics::SceneColorVertexInputResult decoded;
  const bool constants_match=REXCVAR_GET(sr_native_pm4_mirror) && cb_vs_gpu_ &&
      cb_vs_version_==mirror_.vs_version && REXCVAR_GET(sr_native_shader_dir).empty();
  const bool decoded_ok=(quads || topology==D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST) &&
      graphics::DecodeSceneColorVertexInput(input,decoded);
  const uint32_t target_width=HostPx(bound_rts_[0]->width,bound_rts_[0]->scale);
  const uint32_t target_height=HostPx(bound_rts_[0]->height,bound_rts_[0]->scale);
  graphics::SceneColorGeometryInput shape;
  shape.clip_vertices={decoded.clip_vertices.data(),decoded.count};
  shape.target_width=target_width;shape.target_height=target_height;
  shape.valid_rect={0,0,target_width,target_height};
  const auto& sc=current_motion_raster_.scissor;
  shape.scissor={uint32_t(std::clamp<LONG>(sc.left,0,LONG(target_width))),
      uint32_t(std::clamp<LONG>(sc.top,0,LONG(target_height))),
      uint32_t(std::clamp<LONG>(sc.right,0,LONG(target_width))),
      uint32_t(std::clamp<LONG>(sc.bottom,0,LONG(target_height)))};
  shape.viewport={0,0,float(target_width),float(target_height),0,1};
  shape.raster_cull_keeps_all=current_motion_raster_.cull==D3D12_CULL_MODE_NONE;
  // Only F32 passthrough with identity final fixups has no projection error.
  // An integer viewport plus D3D's raster snap fits the strict 1/16 pixel margin.
  // Projected HUD geometry deliberately remains unqualified here.
  const bool full_shape=decoded_ok && decoded.exact_clip_positions && constants_match &&
      bound_samples_==1 && current_motion_raster_.full_viewport &&
      graphics::ProveSceneColorGeometryCoverage(shape);
  REXLOG_INFO("native motion inline shape evidence: serial={} vs={:016X} decoded={} exact_clip={} constants_match={} source_plane={} constant_w={} full_shape={} boundary_qualified=false",
      motion_color_serial_+1,current_vs_hash_,decoded_ok,decoded.exact_clip_positions,constants_match,
      decoded.constant_source_plane,decoded.constant_output_w,full_shape);
  const uint32_t bytes=count*stride;
  const uint32_t header[]{0x53434745,1,stride,count,position->offset,uint32_t(MapDeclType(position->type).format),
      position->type,uint32_t(topology),uint32_t(quads),uint32_t(HostPx(bound_rts_[0]->width,bound_rts_[0]->scale)),
      uint32_t(HostPx(bound_rts_[0]->height,bound_rts_[0]->scale)),bytes,uint32_t(current_motion_raster_.cull),
      uint32_t(current_motion_raster_.front_ccw),uint32_t(color),uint32_t(uv)};
  std::error_code error;const auto directory=std::filesystem::u8path(REXCVAR_GET(sr_native_dump_dir));
  std::filesystem::create_directories(directory,error);
  const auto path=directory/fmt::format("color_geometry_f{}_d{}_vs{:016X}_ps{:016X}.bin",
      frame_count_+1,motion_color_serial_+1,current_vs_hash_,current_ps_hash_);
  std::ofstream out(path,std::ios::binary);
  const auto& constants=mirror_.FloatConstants(false);
  out.write(reinterpret_cast<const char*>(header),sizeof(header));
  out.write(reinterpret_cast<const char*>(constants.data()),16*sizeof(uint32_t));
  out.write(reinterpret_cast<const char*>(motion_clip_fixup_.data()),sizeof(motion_clip_fixup_));
  out.write(reinterpret_cast<const char*>(vertices.data()),bytes);out.close();
  REXLOG_INFO("native motion actual inline geometry: file={} count={} stride={} position_format={} topology={} quads={} color={} uv={} saved={} (frozen host bytes, no GPU upload readback)",
      path.string(),count,stride,header[5],uint32_t(topology),quads,color,uv,bool(out));
}

void Renderer::AfterMotionColorDraw() {
  if(motion_capture_active_ && motion_jitter_draw_active_ && !motion_jitter_applied_) {
    motion_jitter_applied_=true;
    REXLOG_INFO("native motion scene jitter issued: phase={} frame={} pixels={},{} ndc={},{} shared_halfpixel_bits={:08X},{:08X} actual_original_draw=true (candidate scene experiment)",
        motion_capture_phase_,frame_count_+1,motion_frame_jitter_.pixel_x,motion_frame_jitter_.pixel_y,
        motion_frame_jitter_.ndc_x,motion_frame_jitter_.ndc_y,motion_clip_fixup_[4],motion_clip_fixup_[5]);
  }
  if(!motion_capture_active_ || motion_capture_phase_!=2) return;
  const uint64_t serial=++motion_color_serial_;
  if(motion_coverage_draw_active_ && motion_color_reports_<64)
    REXLOG_INFO("native motion original pixel owner issued: serial={} vs={:016X} ps={:016X} owner_id={} original_color_alpha_preserved=true",
        serial,current_vs_hash_,current_ps_hash_,motion_coverage_draw_id_);
  for(uint32_t slot=0;slot<4;++slot) {
    if(!bound_rts_[slot] || !((current_motion_raster_.color_mask>>(slot*4))&15)) continue;
    bound_rts_[slot]->motion_write_frame=frame_count_+1;bound_rts_[slot]->motion_write_serial=serial;
  }
}
