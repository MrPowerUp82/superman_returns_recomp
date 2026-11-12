// Included inside native_renderer.cpp's namespace to share its audited IA helpers.
// Diagnostic two-frame capture and continuous producer. Production readiness
// additionally requires complete correspondence and versioned color lineage.
void Renderer::ReportObservedClothDraw(const WorkBatch& batch,const WorkCmd& cmd) {
  if((motion_continuous_ && !CaptureObjectMotionFrame()) || !motion_capture_active_ || motion_scene_finished_ || motion_cloth_reports_>=2 ||
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
  // Explicit live-capture diagnostics only: ownership can precede the retained
  // scene epoch. Never treat an owner ID by itself as object correspondence.
  if(motion_continuous_ && CaptureObjectMotionFrame() && REXCVAR_GET(sr_native_debug_observed_skin_motion) &&
     current_motion_raster_.coverage_id && current_motion_raster_.coverage_id<=32) {
    REXLOG_INFO("native motion early owner: owner={} vs={:016X} ps={:016X} serial={} active={} finished={} clips={} pipeline={} skip={} mask={} color={}x{}/{} depth={} samples={} full_viewport={} camera={} scene_depth={} retained_pair={} topology={} count={}",
        current_motion_raster_.coverage_id,current_vs_hash_,current_ps_hash_,cmd?cmd->command_serial:0,
        motion_capture_active_,motion_scene_finished_,bool(motion_clips_),bool(current_raster_pipeline_),SkipGpuDraw(),
        current_motion_raster_.color_mask,bound_rts_[0]?bound_rts_[0]->width:0,bound_rts_[0]?bound_rts_[0]->height:0,
        bound_rts_[0]?bound_rts_[0]->guest_format:0,bool(bound_ds_),bound_samples_,current_motion_raster_.full_viewport,
        dlss_view_projection_valid_,bound_ds_==dlss_scene_ds_,graphics::MatchesRetainedMotionCapturePair(
          {reinterpret_cast<uintptr_t>(motion_captured_color_.Get()),reinterpret_cast<uintptr_t>(motion_captured_depth_.Get())},
          {bound_rts_[0]?reinterpret_cast<uintptr_t>(bound_rts_[0]->resource.Get()):0,bound_ds_?reinterpret_cast<uintptr_t>(bound_ds_->resource.Get()):0}),
        uint32_t(topology),count);
  }
  if(!motion_capture_active_ || motion_scene_finished_ || !motion_clips_ || !current_raster_pipeline_ ||
     SkipGpuDraw() || !(current_motion_raster_.color_mask&15) || !bound_rts_[0] || !bound_ds_ || bound_rts_[0]->guest_format!=12 ||
     bound_rts_[0]->width!=kOutputWidth || bound_rts_[0]->height!=kOutputHeight ||
     bound_samples_!=1 || !current_motion_raster_.full_viewport ||
     (topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST && topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)) return;
  if(!graphics::CanCaptureMotionSceneDepth(motion_continuous_,dlss_view_projection_valid_,
      reinterpret_cast<uintptr_t>(bound_ds_),reinterpret_cast<uintptr_t>(dlss_scene_ds_))) return;
  if(!graphics::MatchesRetainedMotionCapturePair(
      {reinterpret_cast<uintptr_t>(motion_captured_color_.Get()),reinterpret_cast<uintptr_t>(motion_captured_depth_.Get())},
      {reinterpret_cast<uintptr_t>(bound_rts_[0]->resource.Get()),reinterpret_cast<uintptr_t>(bound_ds_->resource.Get())})) return;
  const uint32_t vertices=graphics::PostVertexCapacity(count,topology==D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  if(!vertices) return;
  if((!motion_continuous_ || CaptureObjectMotionFrame()) && cmd && motion_owner_reports_[current_vs_hash_==0xC687EB65D635C5D7ull?1:0]<16 &&
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
    PsoRecord observed{};
    observed.key=pipeline_key;observed.vs_hash=current_vs_hash_;observed.pipeline_kind=1;
    observed.blend=desc.BlendState;observed.raster=desc.RasterizerState;
    observed.depth_stencil=desc.DepthStencilState;
    for(uint32_t i=0;i<8;++i)observed.rtv_formats[i]=desc.RTVFormats[i];
    observed.dsv_format=desc.DSVFormat;observed.num_render_targets=desc.NumRenderTargets;
    observed.sample_count=desc.SampleDesc.Count;
    observed.topology_type=uint32_t(desc.PrimitiveTopologyType);
    observed.strip_cut=uint32_t(desc.IBStripCutValue);
    bool serializable=elements.size()<=PsoRecord::kMaxElements;
    if(serializable) {
      observed.element_count=uint32_t(elements.size());
      for(size_t i=0;i<elements.size();++i) {
        const auto& e=elements[i];uint32_t usage=0;
        while(usage<14 && std::strcmp(e.SemanticName,UsageSemantic(usage)))++usage;
        if(usage==14) {serializable=false;break;}
        observed.elements[i]={uint8_t(usage),uint8_t(e.SemanticIndex),uint8_t(e.InputSlot),0,
                              uint32_t(e.Format),e.AlignedByteOffset};
      }
    }
    ComPtr<ID3D12PipelineState> pipeline;
    HRESULT hr=S_OK;
    if(!serializable || !pipeline_cache_.TakeByDesc(observed,pipeline)) {
      PipelineCache::RecordDiagnosticCreate();
      hr=device_->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pipeline));
      if(SUCCEEDED(hr) && serializable)pipeline_cache_.Record(observed);
    }
    if(FAILED(hr)) REXLOG_WARN("native motion clip pipeline failed: vs={:016X} hr={:08X}",current_vs_hash_,uint32_t(hr));
    found=motion_clip_pipelines_.emplace(pipeline_key,std::move(pipeline)).first;
  }
  if(!found->second) return;
  const auto range=motion_clips_->Reserve(vertices);if(!range) return;
  graphics::TemporalGeometrySample sample;
  sample.range={frame_index_,range->offset,range->vertices};
  const auto& words=mirror_.FloatConstants(false);
  const bool candidate=graphics::IsObservedSkinnedMotionShader(current_vs_hash_) ||
      current_vs_hash_==0xA5232A3184AA617Cull || current_vs_hash_==0x4F17209D364D5B51ull ||
      current_vs_hash_==0x18B4F69400B7F0AFull || current_vs_hash_==0x7D1AA67BBDC418D1ull ||
      current_vs_hash_==0xC5E8551EFDE01162ull || current_vs_hash_==0xC63F6F22679609F0ull ||
      current_vs_hash_==0x8E5232F5E5E6760Eull || current_vs_hash_==0x1023A9B52C1407C1ull ||
      current_vs_hash_==0xDF1EB50388CC2563ull || current_vs_hash_==0x9A62E7C65051447Aull || current_vs_hash_==0x315E6843ECE68A00ull;
  const size_t candidate_family=current_vs_hash_==0xC20921D0DBBD2F1Full ? 0 :
      current_vs_hash_==0xC687EB65D635C5D7ull ? 1 : current_vs_hash_==0xA5232A3184AA617Cull ? 2 :
      current_vs_hash_==0x8E5232F5E5E6760Eull ? 4 : current_vs_hash_==0x1023A9B52C1407C1ull ? 5 :
      current_vs_hash_==0xDF1EB50388CC2563ull ? 6 : current_vs_hash_==0x9A62E7C65051447Aull ? 7 :
      current_vs_hash_==0x315E6843ECE68A00ull ? 8 : 3;
  const size_t candidate_bucket=candidate_family*2+size_t(cmd && (cmd->observed_render.token.generation || cmd->observed_render.vehicle_batch.source_route_observed));
  if(motion_continuous_ && CaptureObjectMotionFrame() && cmd && candidate && motion_candidate_reports_[candidate_bucket]++<8) {
    const auto& o=cmd->observed_render;
    uint32_t nonfinite=0;for(uint32_t word:words)nonfinite+=!std::isfinite(std::bit_cast<float>(word));
    REXLOG_INFO("native motion candidate: serial={} vs={:016X} indexed={} range={}:{} owner={:08X} generation={} render={:08X} vtable={:08X} part={} route={} world={} cb_uploaded={} nonfinite={} coverage={} blend={:08X} camera={} conflict={} correspondence_unverified=true",
        cmd->command_serial,current_vs_hash_,indexed,cmd->u[6],cmd->u[7],o.token.address,o.token.generation,
        o.render,o.vtable,o.part.origin,o.part.source_route_observed,o.rigid_owner_world_observed,
        cb_vs_version_==mirror_.vs_version && cb_vs_gpu_,nonfinite,current_motion_raster_.coverage_id,
        current_motion_raster_.blend_control,motion_common_camera_valid_,motion_camera_conflict_);
    if(current_vs_hash_==0x9A62E7C65051447Aull || current_vs_hash_==0x315E6843ECE68A00ull) {
      const auto& b=o.vehicle_batch;
      REXLOG_INFO("native motion batch candidate: serial={} observed={} members={} rows={} identity={:016X} generation={:016X} palette={:016X} shader_fetch={} correspondence_unverified=true",
          cmd->command_serial,b.source_route_observed,b.members,b.rows,b.instance_key,b.generation_key,b.palette_signature,cmd->dynamic_vertex_fetch);
    }
    if(!REXCVAR_GET(sr_native_dump_dir).empty()) {
      std::error_code error;const auto folder=std::filesystem::u8path(REXCVAR_GET(sr_native_dump_dir));
      std::filesystem::create_directories(folder,error);
      std::ofstream dump(folder/fmt::format("motion_candidate_s{}_serial{}.bin",swap_number_,cmd->command_serial),std::ios::binary);
      // Little-endian uint32 metadata, 1024 float-bank words, 4 uploaded bools,
      // 16 frozen owner words and 16 audited camera words. No struct padding.
      const uint32_t header[]{0x4D56434E,1,uint32_t(current_vs_hash_),uint32_t(current_vs_hash_>>32),
          o.token.address,uint32_t(o.token.generation),uint32_t(o.token.generation>>32),o.render,o.vtable,
          o.part.model,o.part.sections,o.part.origin,o.part.lod_slice,o.part.actor_palette,o.part.group,
          o.part.attachment,o.part.stream_offset,o.part.submesh,uint32_t(o.part.source_route_observed),
          uint32_t(o.rigid_owner_world_observed),cmd->u[6],cmd->u[7],current_motion_raster_.coverage_id,
          current_motion_raster_.blend_control};
      dump.write(reinterpret_cast<const char*>(header),sizeof(header));
      dump.write(reinterpret_cast<const char*>(words.data()),words.size()*sizeof(uint32_t));
      dump.write(reinterpret_cast<const char*>(motion_uploaded_vs_bools_.data()),sizeof(motion_uploaded_vs_bools_));
      dump.write(reinterpret_cast<const char*>(o.rigid_owner_world.data()),sizeof(o.rigid_owner_world));
      dump.write(reinterpret_cast<const char*>(motion_camera_words_.data()),sizeof(motion_camera_words_));
    }
  }
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
  const uint64_t camera_domain_words[]{bound_rts_[0]->edram_base,bound_rts_[0]->width,bound_rts_[0]->height,
      uint64_t(HostPx(bound_rts_[0]->width,bound_rts_[0]->scale)),uint64_t(HostPx(bound_rts_[0]->height,bound_rts_[0]->scale))};
  const uint64_t current_camera_domain=XXH3_64bits(camera_domain_words,sizeof(camera_domain_words));
  uint8_t rigid_source=0;
  bool shared_world_camera=false,dependencies_valid=false;
  uint64_t position_signature=0;
  graphics::ObservedSkinnedMotionInputs observed_skin;
  graphics::ObservedVehicleBatchShaderInputs observed_batch;
  const bool observed_batch_inputs=(motion_continuous_ || REXCVAR_GET(sr_native_debug_observed_skin_motion)) && cmd &&
      vertex_range_valid && motion_coverage_draw_active_ && current_motion_raster_.coverage_id &&
      graphics::IsPixelCoverageShaderPair(current_vs_hash_,current_ps_hash_) &&
      graphics::CaptureObservedVehicleBatchShaderInputs(current_vs_hash_,cmd->observed_render.vehicle_batch,
          words,motion_uploaded_vs_bools_,cmd->dynamic_vertex_fetch,indexed,vertex_first,vertex_last+1,observed_batch);
  graphics::ObservedRigidMotionInputs observed_single;
  const bool observed_single_inputs=(motion_continuous_ || REXCVAR_GET(sr_native_debug_observed_skin_motion)) && cmd &&
      !indexed && cmd->dynamic_vertex_fetch && vertex_range_valid && motion_coverage_draw_active_ && current_motion_raster_.coverage_id &&
      graphics::IsPixelCoverageShaderPair(current_vs_hash_,current_ps_hash_) &&
      graphics::CaptureObservedSingleInstanceShaderInputs(current_vs_hash_,cmd->observed_render,
          words,motion_uploaded_vs_bools_,vertex_first,vertex_last+1,observed_single);
  graphics::ObservedSkinnedPartKey observed_part;
  graphics::ObservedClothShaderInputs observed_cloth;
  const bool observed_cloth_inputs=(motion_continuous_ || REXCVAR_GET(sr_native_debug_observed_skin_motion)) && cmd &&
      motion_coverage_draw_active_ && current_motion_raster_.coverage_id &&
      graphics::IsPixelCoverageShaderPair(current_vs_hash_,current_ps_hash_) &&
      graphics::CaptureObservedClothShaderInputs(current_vs_hash_,words,motion_uploaded_vs_bools_,observed_cloth);
  const bool observed_skin_inputs=(motion_continuous_ || REXCVAR_GET(sr_native_debug_observed_skin_motion)) && cmd &&
      motion_coverage_draw_active_ && current_motion_raster_.coverage_id &&
      graphics::IsPixelCoverageShaderPair(current_vs_hash_,current_ps_hash_) &&
      graphics::CaptureObservedSkinnedPartKey(cmd->observed_render,observed_part) &&
      graphics::CaptureObservedSkinnedMotionInputs(current_vs_hash_,cmd->observed_render,
          words,motion_uploaded_vs_bools_,observed_skin);
  if(observed_cloth_inputs && vertex_range_valid) {
    projection_words=observed_cloth.clip_rows;
    dependencies_valid=true;
    position_signature=XXH3_64bits(observed_cloth.position_words.data(),sizeof(observed_cloth.position_words));
  } else if(observed_skin_inputs && vertex_range_valid) {
    projection_words=observed_skin.clip_rows;
    dependencies_valid=true; // Object-to-clip, not a shared camera.
    position_signature=XXH3_64bits(observed_skin.position_words.data(),sizeof(observed_skin.position_words));
  } else if(observed_batch_inputs) {
    projection_words=observed_batch.clip_rows;dependencies_valid=true;
    position_signature=XXH3_64bits(observed_batch.position_words.data(),sizeof(observed_batch.position_words));
  } else if(observed_single_inputs) {
    projection_words=observed_single.clip_rows;
    dependencies_valid=true;
    position_signature=XXH3_64bits(observed_single.position_words.data(),sizeof(observed_single.position_words));
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
      if(!motion_camera_valid_) {
        motion_camera_words_=projection_words;motion_camera_valid_=true;
        std::memcpy(object_motion_final_camera_.data(),projection_words.data(),64);
        const float sx=std::bit_cast<float>(motion_clip_fixup_[0]),sy=std::bit_cast<float>(motion_clip_fixup_[1]);
        const float tx=std::bit_cast<float>(motion_clip_fixup_[2])+std::bit_cast<float>(motion_clip_fixup_[4]);
        const float ty=std::bit_cast<float>(motion_clip_fixup_[3])+std::bit_cast<float>(motion_clip_fixup_[5]);
        for(size_t column=0;column<4;++column) {
          object_motion_final_camera_[column]=sx*object_motion_final_camera_[column]+tx*object_motion_final_camera_[12+column];
          object_motion_final_camera_[4+column]=sy*object_motion_final_camera_[4+column]+ty*object_motion_final_camera_[12+column];
        }
        const uint32_t z_func=(current_motion_raster_.depth_control>>4)&7;
        object_motion_depth_reversed_=z_func==4 || z_func==6;
      }
      // The common-object certificate specifically uses the audited 2C45
      // scene camera, before shared clip fixups; never infer a global matrix.
      if(current_vs_hash_==0x2C45BFFE76D77398ull && !motion_common_camera_valid_ &&
         projection_words==motion_camera_words_ && !motion_camera_conflict_) {
        motion_common_camera_valid_=true;motion_common_camera_domain_=current_camera_domain;
        motion_common_camera_fixup_=motion_clip_fixup_;
      }
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
    bool fetch_valid=versioned && graphics::CaptureTemporalVertexFetch(vertex_fetch_,fetch_resources,fetch_signature,cmd->dynamic_vertex_fetch);
    if(motion_continuous_ && CaptureObjectMotionFrame() && candidate && motion_candidate_reports_[candidate_bucket]<=8)
      REXLOG_INFO("native motion fetch qualification: serial={} vs={:016X} shader_fetch={} versioned={} fetch_valid={} correspondence_unverified=true",
          cmd->command_serial,current_vs_hash_,cmd->dynamic_vertex_fetch,versioned,fetch_valid);
    graphics::ObservedClothMotionIdentity cloth_identity;
    if(observed_cloth_inputs) {
      const auto ib=buffers_.find(cmd->index.key);
      const graphics::TemporalFetchResource index_resource=ib==buffers_.end()?graphics::TemporalFetchResource{}:
          graphics::TemporalFetchResource{0,cmd->index.key,ib->second.resource_generation,ib->second.content_version};
      fetch_valid=fetch_valid && count==cmd->u[5] && !first && !base_vertex &&
          topology==D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST &&
          graphics::CaptureObservedClothMotionIdentity(current_vs_hash_,*batch,*cmd,
              fetch_resources,index_resource,vertex_fetch_,cloth_identity);
      if(!motion_continuous_ && motion_cloth_reports_<=2)
        REXLOG_INFO("native observed cloth correspondence: phase={} serial={} qualified={} vertices={} topology_generation={}",
            motion_capture_phase_,cmd->command_serial,fetch_valid,count,cmd->observed_render.cloth.topology_generation);
      if(fetch_valid) {
        const uint64_t actual_inputs[]{position_signature,
            XXH3_64bits(cloth_identity.actual_streams.data(),sizeof(cloth_identity.actual_streams))};
        position_signature=XXH3_64bits(actual_inputs,sizeof(actual_inputs));
        // Dynamic addresses and versions are deliberately absent ONLY after
        // the actual bindings have passed the source-route validator above.
        identity.resize(8);
        identity.insert(identity.end(),{cmd->observed_render.cloth.topology_generation,
            cmd->observed_render.cloth.vertices,cmd->observed_render.cloth.quads,
            XXH3_64bits(cloth_identity.mesh_streams.data(),sizeof(cloth_identity.mesh_streams)),
            XXH3_64bits(cloth_identity.index.data(),sizeof(cloth_identity.index)),
            XXH3_64bits(batch->bytes.data()+cmd->u[4],size_t(cmd->u[5])*4)});
        fetch_signature=cloth_identity.fetch;
      }
    }
    if(fetch_valid) {
      const uint64_t buffers_signature=XXH3_64bits(identity.data(),identity.size()*sizeof(identity[0]));
      const uint64_t fetch_hash=XXH3_64bits(fetch_signature.data(),sizeof(fetch_signature));
      identity.push_back(fetch_hash);
      sample.mesh=XXH3_64bits(identity.data(),identity.size()*sizeof(identity[0]));
      sample.camera_domain=current_camera_domain;
      sample.geometry_inputs=position_signature;
      graphics::ObservedRigidMotionInputs rigid;
      const uint64_t bindings[]{sample.mesh,buffers_signature,fetch_hash};
      bool observed_rigid=observed_single_inputs;
      if(observed_rigid) {
        rigid=observed_single;rigid.actual_bindings_signature=XXH3_64bits(bindings,sizeof(bindings));rigid_source=3;
      } else observed_rigid=(motion_continuous_ || REXCVAR_GET(sr_native_debug_observed_skin_motion)) &&
          graphics::CaptureObservedRigidMotionInputs(current_vs_hash_,cmd->observed_render,words,motion_uploaded_vs_bools_,
              XXH3_64bits(bindings,sizeof(bindings)),rigid);
      if(observed_rigid && !observed_single_inputs) rigid_source=1;
      else if(!observed_rigid && (motion_continuous_ || REXCVAR_GET(sr_native_debug_observed_skin_motion)) &&
          graphics::CaptureObservedRigidCommonMotionInputs(current_vs_hash_,cmd->observed_render,words,motion_uploaded_vs_bools_,
              XXH3_64bits(bindings,sizeof(bindings)),motion_camera_words_,
              motion_camera_valid_ && motion_common_camera_valid_ && motion_common_camera_domain_==current_camera_domain &&
                  motion_common_camera_fixup_==motion_clip_fixup_,motion_camera_conflict_,rigid)) {
        observed_rigid=true;rigid_source=2;
      }
      if(observed_rigid) {
        const uint64_t actual[]{XXH3_64bits(rigid.position_words.data(),sizeof(rigid.position_words)),
            rigid.shader,rigid.actual_bindings_signature};
        sample.geometry_inputs=XXH3_64bits(actual,sizeof(actual));
      }
      if(observed_batch_inputs) {
        const uint64_t actual[]{position_signature,current_vs_hash_,XXH3_64bits(bindings,sizeof(bindings))};
        sample.geometry_inputs=XXH3_64bits(actual,sizeof(actual));rigid_source=4;
      }
      const auto clip_inputs=graphics::CaptureTemporalProjectionInputs(projection_words,motion_clip_fixup_);
      sample.projection_inputs=XXH3_64bits(clip_inputs.data(),sizeof(clip_inputs));
      sample.proof=graphics::GeometryHistoryProof::kStaticWorldInputs;
      if(observed_batch_inputs) {
        sample.proof=graphics::GeometryHistoryProof::kVerifiedInstance;
        sample.instance=observed_batch.instance_key;sample.instance_generation=observed_batch.generation_key;
      } else if(observed_skin_inputs) {
        sample.proof=graphics::GeometryHistoryProof::kVerifiedInstance;
        sample.instance=XXH3_64bits(observed_part.instance_words.data(),sizeof(observed_part.instance_words));
        sample.instance_generation=observed_part.generation;
      } else if(observed_cloth_inputs) {
        sample.proof=graphics::GeometryHistoryProof::kVerifiedInstance;
        sample.instance=XXH3_64bits(cloth_identity.instance_words.data(),sizeof(cloth_identity.instance_words));
        sample.instance_generation=cloth_identity.generation;
      } else if(observed_rigid) {
        sample.proof=graphics::GeometryHistoryProof::kVerifiedInstance;
        sample.instance=XXH3_64bits(rigid.part.instance_words.data(),sizeof(rigid.part.instance_words));
        sample.instance_generation=rigid.part.generation;
      }
      if(!motion_continuous_ && motion_signature_reports_++<12) {
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
  motion_rigid_sources_.push_back(rigid_source);
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
  const bool vfx=vs==0x0408D37D5D96E869ull &&
      (ps==0x201C30CD5C58A1BCull || ps==0x5C1E60F40BD1DC6Eull);
  const bool paired=graphics::IsPixelCoverageShaderPair(vs,ps);
  if(vfx && !motion_reactive_[frame_index_].mask) return;
  if(vfx) Transition(motion_reactive_[frame_index_].mask.Get(),motion_reactive_[frame_index_].state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(!paired || motion_coverage_failed_shaders_.contains(ps) || !motion_capture_active_ || (motion_scene_finished_ && !vfx) || !single_color || !color || !depth ||
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
  motion_coverage_draw_id_=vfx ? UINT32_MAX : ++motion_coverage_next_id_;motion_coverage_draw_active_=true;
}

bool Renderer::EnsureMotionReactivity() {
  auto& slot=motion_reactive_[frame_index_];
  const uint32_t width=HostPx(kOutputWidth,render_scale_),height=HostPx(kOutputHeight,render_scale_);
  if(!width || !height || uint64_t(width)*height>graphics::d3d12::TemporalMotion::kMaxPixels) return false;
  const auto old=slot.mask ? slot.mask->GetDesc() : D3D12_RESOURCE_DESC{};
  if(!slot.mask || old.Width!=width || old.Height!=height) {
    slot.mask.Reset();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
    desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R32_UINT;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    slot.state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,slot.state,nullptr,IID_PPV_ARGS(&slot.mask)))) return false;
    if(!slot.uav) slot.uav=AllocSrvIndex();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=desc.Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(slot.mask.Get(),nullptr,&uav,
        provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(),slot.uav));
  }
  Transition(slot.mask.Get(),slot.state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12DescriptorHeap* heaps[]{srv_heap_.Get(),sampler_heap_.Get()};command_list_->SetDescriptorHeaps(2,heaps);
  const UINT zero[4]{};
  command_list_->ClearUnorderedAccessViewUint(
      provider_->OffsetViewDescriptor(srv_heap_->GetGPUDescriptorHandleForHeapStart(),slot.uav),
      provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(),slot.uav),slot.mask.Get(),zero,0,nullptr);
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=slot.mask.Get();
  command_list_->ResourceBarrier(1,&barrier);frame_state_bound_=false;
  return true;
}

bool Renderer::CaptureObjectMotionFrame() const {
  const int explicit_swap=REXCVAR_GET(sr_native_object_motion_capture_swap);
  return (explicit_swap>0 && swap_number_==uint64_t(explicit_swap)) ||
      (motion_live_capture_swap_>=0 && swap_number_==uint64_t(motion_live_capture_swap_));
}
void Renderer::BeginMotionCapture() {
  ReportMotionReadbacks(frame_index_);
  motion_capture_active_=false;motion_scene_finished_=false;
  motion_candidate_reports_={};
  motion_jitter_draw_active_=motion_jitter_applied_=motion_jitter_ready_=false;motion_frame_jitter_={};
  motion_captured_depth_.Reset();motion_captured_color_.Reset();
  motion_coverage_draw_active_=false;motion_coverage_bound_=false;motion_coverage_cleared_=false;
  motion_coverage_next_id_=motion_coverage_draw_id_=0;motion_coverage_shader_=nullptr;
  object_motion_requested_=!packet_sink_ && REXCVAR_GET(sr_native_object_motion);
  const bool previous=object_motion_previous_;
  motion_continuous_=object_motion_requested_ && dlss_wanted_;
  object_motion_previous_=motion_continuous_;
  object_motion_output_ready_=object_motion_full_cover_=false;
  object_motion_ticket_={};object_motion_hdr_={};
  object_motion_frame_.Begin(frame_count_+1,dlss_render_width_,dlss_render_height_);
  object_motion_lineage_.Begin(frame_count_+1);
  if(packet_sink_ || (motion_capture_done_ && !motion_continuous_)) return;
  const auto& trigger=REXCVAR_GET(sr_native_motion_capture_trigger_path);
  std::error_code error;
  if(motion_continuous_ && !trigger.empty() && std::filesystem::remove(std::filesystem::u8path(trigger),error) && !error) {
    motion_live_capture_swap_=int64_t(swap_number_);
    motion_live_capture_frame_=frame_count_+1;
    REXLOG_INFO("native motion live capture: frame={} guest_swap={} continuous=true",frame_count_+1,swap_number_);
  }
  if(!motion_continuous_ && !motion_capture_phase_ &&
     (trigger.empty() || !std::filesystem::exists(std::filesystem::u8path(trigger),error) || error)) return;
  if(!EnsureMotionReactivity()) {motion_continuous_=false;return;}
  if(!motion_clips_) {
    auto clips=std::make_unique<graphics::d3d12::PostVertexClipCapture>();
    if(!clips->Initialize(device_)) {REXLOG_ERROR("native motion: clip allocation failed");motion_capture_done_=true;return;}
    motion_clips_=std::move(clips);
  }
  if(!motion_clips_->Begin(command_list_.Get(),frame_index_)) {motion_capture_done_=true;return;}
  motion_capture_phase_=motion_continuous_ ? 2 : motion_capture_phase_+1;motion_capture_active_=true;motion_signature_reports_=0;motion_owner_reports_={};motion_cloth_reports_=0;
  if(motion_capture_phase_==1) {motion_previous_jitter_={};ArmInstanceProbe();}
  motion_jitter_ready_=graphics::MakeSceneRasterJitter(motion_capture_phase_-1,
      HostPx(kOutputWidth,render_scale_),HostPx(kOutputHeight,render_scale_),
      REXCVAR_GET(sr_native_debug_motion_jitter),motion_frame_jitter_);
  if(motion_continuous_) {
    motion_frame_jitter_=graphics::SceneRasterJitter{};motion_jitter_ready_=dlss_wanted_;
    if(!previous) {motion_previous_jitter_={};motion_previous_camera_bad_=true;}
  }
  motion_camera_valid_=motion_camera_conflict_=false;motion_rasters_.clear();motion_rigid_sources_.clear();
  motion_common_camera_valid_=false;motion_common_camera_domain_=0;motion_common_camera_fixup_={};
  motion_history_.Begin(frame_count_+1,HostPx(kOutputWidth,render_scale_),HostPx(kOutputHeight,render_scale_),motion_previous_camera_bad_);
}

void Renderer::MaybeFinishMotionBeforeDepthWrite(HostSurface* depth,bool writes_depth,const char* reason) {
  const graphics::MotionDepthBoundaryState state{motion_capture_active_,motion_scene_finished_,
      !motion_history_.samples().empty(),reinterpret_cast<uintptr_t>(motion_captured_depth_.Get())};
  if(!graphics::ShouldFreezeMotionDepthBeforeWrite(state,
      depth?reinterpret_cast<uintptr_t>(depth->resource.Get()):0,writes_depth)) return;
  if(!motion_continuous_) REXLOG_INFO("native motion depth boundary: phase={} host_frame={} reason={} depth={:X} before_mutation=true",
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
  const bool scene_camera_matches=graphics::CanSealMotionSceneDepth(motion_continuous_,dlss_view_projection_valid_,
      reinterpret_cast<uintptr_t>(depth),reinterpret_cast<uintptr_t>(dlss_scene_ds_));
  bool camera_cut=false;
  if(motion_continuous_ && motion_camera_valid_ && object_motion_previous_camera_frame_==frame_count_) {
    graphics::Matrix4 camera{};std::memcpy(camera.data(),motion_camera_words_.data(),sizeof(camera));
    camera_cut=graphics::ObjectMotionCameraJump(object_motion_previous_camera_,camera);
    if(camera_cut) {object_motion_frame_.Fail(graphics::ObjectMotionRejection::kCamera);dlss_history_=false;}
  }
  uint32_t matched=0,observed_skin_matches=0,observed_cloth_matches=0,unverified=0,missing=0,ambiguous=0;uint64_t vertex_total=0;
  struct Family {uint32_t draws=0,opaque=0,matches=0,stencil=0,no_depth_write=0,blend=0,alpha_test=0;};
  std::unordered_map<uint64_t,Family> families;
  uint32_t rigid_qualified=0,rigid_matched=0;
  uint32_t rigid_asset_qualified=0,rigid_asset_matched=0,rigid_common_qualified=0,rigid_common_matched=0;
  graphics::TemporalClassCoverageStats class_coverage;
  std::vector<MotionGeometry> geometry;
  const auto dimensions=depth->resource->GetDesc();
  if(!motion_continuous_) REXLOG_INFO("native motion scene snapshot source: phase={} host_frame={} prepare_observations={} color_edram={} color={}x{} scale={} color_resource={:X} depth_edram={} depth={}x{} depth_resource={:X} host_depth={}x{}",
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
    const bool is_skin_shader=graphics::IsObservedSkinnedMotionShader(motion_rasters_[i].shader);
    const bool is_cloth_shader=motion_rasters_[i].shader==0xA5232A3184AA617Cull;
    const auto motion_class=graphics::ClassifyTemporalMotionSource(is_skin_shader,is_cloth_shader,motion_rigid_sources_[i],sample.proof);
    class_coverage.Record(motion_class,sample.range.vertices,match.status);
    const bool rigid_instance=sample.proof==graphics::GeometryHistoryProof::kVerifiedInstance &&
        !is_skin_shader && !is_cloth_shader;
    rigid_qualified+=rigid_instance;rigid_matched+=rigid_instance && match.status==graphics::GeometryHistoryStatus::kMatch;
    rigid_asset_qualified+=rigid_instance && (motion_rigid_sources_[i]==1 || motion_rigid_sources_[i]==3 || motion_rigid_sources_[i]==4);
    rigid_asset_matched+=rigid_instance && (motion_rigid_sources_[i]==1 || motion_rigid_sources_[i]==3 || motion_rigid_sources_[i]==4) && match.status==graphics::GeometryHistoryStatus::kMatch;
    rigid_common_qualified+=rigid_instance && motion_rigid_sources_[i]==2;
    rigid_common_matched+=rigid_instance && motion_rigid_sources_[i]==2 && match.status==graphics::GeometryHistoryStatus::kMatch;
    MotionGeometry draw;
    draw.currentClip=motion_clips_->Buffer(sample.range.slot);draw.current_offset=sample.range.offset;
    draw.vertex_count=sample.range.vertices;
    draw.correspondence_valid=match.status==graphics::GeometryHistoryStatus::kMatch &&
        scene_camera_matches && !camera_cut && !motion_camera_conflict_ && !motion_previous_camera_bad_ && !motion_clips_->dropped() && !motion_history_.dropped();
    if(draw.correspondence_valid) {
      observed_skin_matches+=sample.proof==graphics::GeometryHistoryProof::kVerifiedInstance &&
          graphics::IsObservedSkinnedMotionShader(motion_rasters_[i].shader);
      observed_cloth_matches+=sample.proof==graphics::GeometryHistoryProof::kVerifiedInstance &&
          motion_rasters_[i].shader==0xA5232A3184AA617Cull;
      draw.previousClip=motion_clips_->Buffer(match.previous.slot);draw.previous_offset=match.previous.offset;
    }
    const auto& raster=motion_rasters_[i];
    if(!graphics::MotionBlendHasSingleOwner(raster.blend_control,raster.coverage_id,raster.color_mask)) draw.correspondence_valid=false;
    draw.invalidator_depth_func=D3D12_COMPARISON_FUNC(graphics::MotionInvalidatorDepthComparison(
        (raster.depth_control&2)!=0,raster.z_write,REXCVAR_GET(sr_native_debug_no_ztest),raster.depth_control>>4));
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
  if(motion_continuous_) {
    for(size_t i=0;i<geometry.size();++i) {
      const auto& raster=motion_rasters_[i];
      object_motion_frame_.CaptureDraw(raster.color_serial,geometry[i].correspondence_valid,
          graphics::MotionBlendHasSingleOwner(raster.blend_control,raster.coverage_id,raster.color_mask));
    }
    if(!scene_camera_matches || motion_camera_conflict_ || !motion_camera_valid_) object_motion_frame_.Fail(graphics::ObjectMotionRejection::kCamera);
    if(motion_clips_->dropped() || motion_history_.dropped()) object_motion_frame_.Fail(graphics::ObjectMotionRejection::kCapacity);
  }
  if(!motion_continuous_ || CaptureObjectMotionFrame()) REXLOG_INFO("native motion clip history: phase={} frame={} guest_swap={} draws={} vertices={} matched={} unverified={} missing={} ambiguous={} camera_conflict={} dropped={} (partial position proof; unsupported objects/deformation unverified)",
      motion_capture_phase_,frame_count_+1,swap_number_,geometry.size(),vertex_total,matched,unverified,missing,ambiguous,
      motion_camera_conflict_,motion_clips_->dropped()+motion_history_.dropped());
  if(!motion_continuous_ || CaptureObjectMotionFrame()) for(const auto& [shader,family]:families)
    REXLOG_INFO("native motion family: phase={} vs={:016X} draws={} position_eligible={} matches={} stencil={} no_z_write={} blend={} alpha_test={} (coverage unqualified)",
        motion_capture_phase_,shader,family.draws,family.opaque,family.matches,family.stencil,family.no_depth_write,family.blend,family.alpha_test);
  if(motion_continuous_ && ((frame_count_+1)%120==0 || CaptureObjectMotionFrame())) {
    const auto& sk=class_coverage[graphics::TemporalMotionClass::SkinnedCharacter];
    const auto& cl=class_coverage[graphics::TemporalMotionClass::DynamicCloth];
    const auto& vb=class_coverage[graphics::TemporalMotionClass::VehicleBatch];
    const auto& ra=class_coverage[graphics::TemporalMotionClass::RigidAsset];
    const auto& rc=class_coverage[graphics::TemporalMotionClass::RigidCommon];
    REXLOG_INFO("native rigid object motion: frame={} qualified={} matched={} asset_qualified={} asset_matched={} common_qualified={} common_matched={} asset_routes=4 common_domains=2 class_identification=true "
                "skin_draws={}/{} cloth_draws={}/{} vehicle_draws={}/{} asset_draws={}/{} common_draws={}/{}",
        frame_count_+1,rigid_qualified,rigid_matched,rigid_asset_qualified,rigid_asset_matched,rigid_common_qualified,rigid_common_matched,
        sk.matched_draws,sk.draws,cl.matched_draws,cl.draws,vb.matched_draws,vb.draws,ra.matched_draws,ra.draws,rc.matched_draws,rc.draws);
  }
  if(motion_capture_phase_!=2) return;
  if(!temporal_motion_) {
    auto motion=std::make_unique<TemporalMotion>();
    if(FAILED(motion->Initialize(device_,rex::filesystem::GetExecutableFolder()/"superman_returns_motion_raster.bin"))) {
      REXLOG_ERROR("native motion initialization failed: {}",motion->LastError());return;
    }
    temporal_motion_=std::move(motion);
  }
  // The guest may change its depth again for UI/post effects later in this
  // list. Freeze a private, immutable snapshot for the motion module's fence.
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  HRESULT hr=S_OK;
  auto snapshot_state=D3D12_RESOURCE_STATE_COPY_DEST;
  if(motion_continuous_) {
    auto& snapshot=object_motion_depth_[frame_index_];
    const auto old=snapshot ? snapshot->GetDesc() : D3D12_RESOURCE_DESC{};
    if(!snapshot || old.Width!=dimensions.Width || old.Height!=dimensions.Height || old.Format!=dimensions.Format) {
      snapshot.Reset();
      hr=device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&dimensions,
          D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&snapshot));
      object_motion_depth_state_[frame_index_]=D3D12_RESOURCE_STATE_COPY_DEST;
    }
    if(FAILED(hr)) {object_motion_frame_.Fail(graphics::ObjectMotionRejection::kResources);return;}
    motion_depth_snapshot_=snapshot;
    Transition(snapshot.Get(),object_motion_depth_state_[frame_index_],D3D12_RESOURCE_STATE_COPY_DEST);
  } else {
    hr=device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&dimensions,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&motion_depth_snapshot_));
    if(FAILED(hr)) {REXLOG_ERROR("native motion depth allocation failed {:08X}",uint32_t(hr));return;}
  }
  const auto source_state=depth->state;
  Transition(depth->resource.Get(),depth->state,D3D12_RESOURCE_STATE_COPY_SOURCE);
  command_list_->CopyResource(motion_depth_snapshot_.Get(),depth->resource.Get());
  Transition(depth->resource.Get(),depth->state,source_state);
  Transition(motion_depth_snapshot_.Get(),snapshot_state,D3D12_RESOURCE_STATE_DEPTH_READ);
  if(motion_continuous_) object_motion_depth_state_[frame_index_]=snapshot_state;
  TemporalMotionInputs input;
  input.depth=motion_depth_snapshot_.Get();input.width=uint32_t(dimensions.Width);input.height=dimensions.Height;
  input.current_jitter[0]=motion_jitter_applied_?motion_frame_jitter_.pixel_x:0;
  input.current_jitter[1]=motion_jitter_applied_?motion_frame_jitter_.pixel_y:0;
  input.previous_jitter[0]=motion_previous_jitter_[0];input.previous_jitter[1]=motion_previous_jitter_[1];
  if(motion_coverage_cleared_) {
    Transition(motion_coverage_[frame_index_].Get(),motion_coverage_state_[frame_index_],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    input.coverage=motion_coverage_[frame_index_].Get();
    if(!motion_continuous_) QueueMotionReadback(input.coverage,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,"audited_pixel_owners");
  }
  hr=temporal_motion_->BeginFrame(frame_index_);
  if(SUCCEEDED(hr)) hr=temporal_motion_->Render(command_list_.Get(),input,geometry);
  frame_state_bound_=false;fixed_state_cached_=false;bound_vertex_views_mask_=0;bound_rt_count_=0;bound_ds_=nullptr;
  std::memset(bound_rts_,0,sizeof(bound_rts_));
  if(FAILED(hr)) {REXLOG_ERROR("native motion render failed {:08X}: {}",uint32_t(hr),temporal_motion_->LastError());return;}
  const auto outputs=temporal_motion_->Outputs();
  if(motion_continuous_) {
    object_motion_output_ready_=true;
    std::memcpy(object_motion_camera_.data(),motion_camera_words_.data(),sizeof(object_motion_camera_));
    object_motion_ticket_={frame_count_+1,object_motion_hdr_.serial,
        XXH3_64bits(motion_camera_words_.data(),sizeof(motion_camera_words_)),input.width,input.height};
    if(!object_motion_lineage_.Seed(object_motion_hdr_,object_motion_ticket_))
      object_motion_frame_.Fail(graphics::ObjectMotionRejection::kLineage);
    if(CaptureObjectMotionFrame()) {
      REXLOG_INFO("native motion lineage seed: resource={:X} lifetime={} serial={} ticket_scene={}",
          object_motion_hdr_.resource,object_motion_hdr_.lifetime,object_motion_hdr_.serial,object_motion_ticket_.scene);
      QueueMotionReadback(outputs.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET,"object_motion_velocity");
      QueueMotionReadback(outputs.validity,D3D12_RESOURCE_STATE_RENDER_TARGET,"object_motion_validity");
      QueueMotionReadback(input.depth,D3D12_RESOURCE_STATE_DEPTH_READ,"object_motion_depth");
      if(input.coverage) QueueMotionReadback(input.coverage,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,"object_pixel_owners");
      const auto directory=std::filesystem::u8path(REXCVAR_GET(sr_native_dump_dir));
      if(!directory.empty()) {
        std::ofstream manifest(directory/fmt::format("object_motion_draws_s{}.csv",swap_number_));
        manifest<<"draw,vs,owner,valid,depth_func,blend,color_mask,vertices,ps,ray_opaque,instance,generation,proof\n";
        for(size_t i=0;i<geometry.size();++i) {
          const auto& g=geometry[i];const auto& r=motion_rasters_[i];
          manifest<<i<<','<<std::hex<<r.shader<<std::dec<<','<<g.coverage_id<<','
              <<g.correspondence_valid<<','<<uint32_t(g.invalidator_depth_func)<<','
              <<r.blend_control<<','<<r.color_mask<<','<<g.vertex_count<<','
              <<std::hex<<r.pixel_shader<<std::dec<<','<<r.ray_opaque<<','
              <<motion_history_.samples()[i].instance<<','<<motion_history_.samples()[i].instance_generation<<','
              <<uint32_t(motion_history_.samples()[i].proof)<<'\n';
        }
      }
      if(REXCVAR_GET(sr_native_debug_observed_skin_motion) && !directory.empty()) {
        if(!motion_visible_draw_attribution_) {
          auto diagnostic=std::make_unique<TemporalMotion>();
          if(SUCCEEDED(diagnostic->Initialize(device_))) motion_visible_draw_attribution_=std::move(diagnostic);
        }
        if(motion_visible_draw_attribution_) {
          auto tagged=geometry;
          for(auto& draw:tagged) {
            draw.previousClip=draw.currentClip;draw.previous_offset=draw.current_offset;
            draw.correspondence_valid=true;draw.coverage_id=0;
          }
          auto diagnostic_input=input;
          diagnostic_input.coverage=nullptr;diagnostic_input.diagnostic_draw_ids=true;
          HRESULT diagnostic_hr=motion_visible_draw_attribution_->BeginFrame(frame_index_);
          if(SUCCEEDED(diagnostic_hr)) diagnostic_hr=motion_visible_draw_attribution_->Render(command_list_.Get(),diagnostic_input,tagged);
          if(SUCCEEDED(diagnostic_hr)) {
            const auto visible=motion_visible_draw_attribution_->Outputs();
            QueueMotionReadback(visible.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET,"visible_motion_draw_ids");
            QueueMotionReadback(visible.validity,D3D12_RESOURCE_STATE_RENDER_TARGET,"visible_motion_draw_validity");
            REXLOG_INFO("native visible draw attribution GPU recorded: draws={} depth_equal=true original_PS_coverage_unverified=true motion_identity_unverified=true DLSS_input=false",tagged.size());
          } else REXLOG_WARN("native visible draw attribution rejected: {:08X}",uint32_t(diagnostic_hr));
        }
        if(!motion_observed_rigid_attribution_) {
          auto diagnostic=std::make_unique<TemporalMotion>();
          if(SUCCEEDED(diagnostic->Initialize(device_))) motion_observed_rigid_attribution_=std::move(diagnostic);
        }
        if(motion_observed_rigid_attribution_) {
          auto attribution=geometry;
          for(size_t i=0;i<attribution.size();++i)
            if(motion_history_.samples()[i].proof!=graphics::GeometryHistoryProof::kVerifiedInstance ||
               graphics::IsObservedSkinnedMotionShader(motion_rasters_[i].shader) ||
               motion_rasters_[i].shader==0xA5232A3184AA617Cull)
              attribution[i].correspondence_valid=false;
          HRESULT diagnostic_hr=motion_observed_rigid_attribution_->BeginFrame(frame_index_);
          if(SUCCEEDED(diagnostic_hr)) diagnostic_hr=motion_observed_rigid_attribution_->Render(command_list_.Get(),input,attribution);
          if(SUCCEEDED(diagnostic_hr)) {
            const auto rigid=motion_observed_rigid_attribution_->Outputs();
            QueueMotionReadback(rigid.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET,"observed_rigid_motion_velocity");
            QueueMotionReadback(rigid.validity,D3D12_RESOURCE_STATE_RENDER_TARGET,"observed_rigid_motion_validity");
            REXLOG_INFO("native rigid attribution GPU recorded: matched_draws={} other_writers_invalidated=true DLSS_input=false actor_class_unverified=true",rigid_matched);
          } else REXLOG_WARN("native rigid attribution rejected: {:08X}",uint32_t(diagnostic_hr));
        }
      }
    }
    return;
  }
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
  if(observed_cloth_matches && REXCVAR_GET(sr_native_debug_observed_skin_motion)) {
    if(!motion_observed_cloth_attribution_) {
      auto candidate=std::make_unique<TemporalMotion>();
      if(SUCCEEDED(candidate->Initialize(device_))) motion_observed_cloth_attribution_=std::move(candidate);
    }
    if(motion_observed_cloth_attribution_) {
      auto attribution=geometry;
      for(size_t i=0;i<attribution.size();++i)
        if(motion_rasters_[i].shader!=0xA5232A3184AA617Cull ||
           motion_history_.samples()[i].proof!=graphics::GeometryHistoryProof::kVerifiedInstance)
          attribution[i].correspondence_valid=false;
      HRESULT cloth_hr=motion_observed_cloth_attribution_->BeginFrame(frame_index_);
      if(SUCCEEDED(cloth_hr)) cloth_hr=motion_observed_cloth_attribution_->Render(command_list_.Get(),input,attribution);
      if(SUCCEEDED(cloth_hr)) {
        const auto cloth=motion_observed_cloth_attribution_->Outputs();
        QueueMotionReadback(cloth.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET,"observed_cloth_motion_velocity");
        QueueMotionReadback(cloth.validity,D3D12_RESOURCE_STATE_RENDER_TARGET,"observed_cloth_motion_validity");
        REXLOG_INFO("native observed cloth attribution GPU recorded: matches={} other_writers_invalidated=true DLSS_input=false",observed_cloth_matches);
      } else REXLOG_WARN("native observed cloth attribution failed {:08X}: {}",uint32_t(cloth_hr),motion_observed_cloth_attribution_->LastError());
    }
  }
  motion_color_pairs_.clear();motion_pre_hud_pending_=true;
  REXLOG_INFO("native motion GPU recorded: {}x{} position_matches={} observed_skin_matches={} observed_cloth_matches={} complete_object_motion_verified=false raster_jitter_applied={} current_jitter_px={},{} previous_jitter_px={},{} candidate_scene_scope_only=true DLSS_applied=Off",
      input.width,input.height,matched,observed_skin_matches,observed_cloth_matches,motion_jitter_applied_,input.current_jitter[0],input.current_jitter[1],
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
  if(motion_continuous_ && motion_camera_valid_ && !motion_camera_conflict_) {
    std::memcpy(object_motion_previous_camera_.data(),motion_camera_words_.data(),sizeof(object_motion_previous_camera_));
    object_motion_previous_camera_frame_=frame_count_+1;
  }
  motion_previous_jitter_=motion_jitter_applied_ ?
      std::array<float,2>{motion_frame_jitter_.pixel_x,motion_frame_jitter_.pixel_y}:std::array<float,2>{};
  motion_capture_active_=false;
  if(!valid_scene) motion_capture_phase_=0;
  else if(!motion_continuous_ && motion_capture_phase_>=2) {motion_capture_done_=true;ReportInstanceProbe();}
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
  const auto description=resolved.resource->GetDesc();
  source.version={reinterpret_cast<uintptr_t>(resolved.resource.Get()),resolved.motion_lifetime,
      resolved.motion_write_frame,resolved.motion_write_serial,uint32_t(description.Width),description.Height};
}

void Renderer::BeforeMotionColorDraw() {
  if(motion_continuous_ || !motion_capture_active_ || motion_capture_phase_!=2 || !motion_scene_finished_ ||
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
     (!motion_continuous_ && (motion_color_reports_>=64 || REXCVAR_GET(sr_native_dump_dir).empty())) ||
     !bound_rts_[0] || (bound_rts_[0]->guest_format!=0 && bound_rts_[0]->guest_format!=12) || !stride || !count ||
     count>64 || stride>256 || uint64_t(count)*stride>vertices.size()) return;
  const bool audited=(current_vs_hash_==0x0408D37D5D96E869ull && current_ps_hash_==0x84FDB8B3BB1596B3ull) ||
      (current_vs_hash_==0x815DF1B3CCBFAA4Eull && current_ps_hash_==0xB397B05F4CE5D5BBull) ||
      (current_vs_hash_==0x7C8C263E50FCA091ull && current_ps_hash_==0x40541E232FDE42AFull) ||
      (current_vs_hash_==0x0408D37D5D96E869ull && (current_ps_hash_==0x201C30CD5C58A1BCull || current_ps_hash_==0x5C1E60F40BD1DC6Eull)) ||
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
      (current_vs_hash_==0x815DF1B3CCBFAA4Eull || current_vs_hash_==0x7C8C263E50FCA091ull)?graphics::SceneColorVertexRole::kPassthroughPreserveW:
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
  if(motion_continuous_) {
    std::array<std::array<float,4>,6> clips{};
    std::array<std::array<float,2>,6> coordinates{};
    const auto texcoord=std::find_if(attributes.begin(),attributes.end(),[](const auto& a){return a.stream==0 && a.usage==5 && a.index==0;});
    bool identity_uv=texcoord!=attributes.end() && texcoord->offset+8<=stride;
    if(identity_uv) {
      const auto uv_format=MapDeclType(texcoord->type).format;
      identity_uv=uv_format==DXGI_FORMAT_R32G32_FLOAT || uv_format==DXGI_FORMAT_R32G32B32_FLOAT || uv_format==DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    if(identity_uv && decoded_ok) {
      constexpr uint32_t expanded[]{0,1,2,0,2,3};
      for(uint32_t i=0;i<decoded.count;++i) {
        clips[i]=decoded.clip_vertices[i];
        std::memcpy(coordinates[i].data(),vertices.data()+size_t(quads?expanded[i]:i)*stride+texcoord->offset,8);
      }
      identity_uv=graphics::ProveObjectMotionIdentityUv({clips.data(),decoded.count},{coordinates.data(),decoded.count});
    } else identity_uv=false;
    object_motion_full_cover_=full_shape && identity_uv;
    return;
  }
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
    if(!motion_continuous_) REXLOG_INFO("native motion scene jitter issued: phase={} frame={} pixels={},{} ndc={},{} shared_halfpixel_bits={:08X},{:08X} actual_original_draw=true (candidate scene experiment)",
        motion_capture_phase_,frame_count_+1,motion_frame_jitter_.pixel_x,motion_frame_jitter_.pixel_y,
        motion_frame_jitter_.ndc_x,motion_frame_jitter_.ndc_y,motion_clip_fixup_[4],motion_clip_fixup_[5]);
  }
  if(!motion_capture_active_ || motion_capture_phase_!=2) return;
  const uint64_t serial=++motion_color_serial_;
  if(!motion_continuous_ && motion_coverage_draw_active_ && motion_color_reports_<64)
    REXLOG_INFO("native motion original pixel owner issued: serial={} vs={:016X} ps={:016X} owner_id={} original_color_alpha_preserved=true",
        serial,current_vs_hash_,current_ps_hash_,motion_coverage_draw_id_);
  for(uint32_t slot=0;slot<4;++slot) {
    if(!bound_rts_[slot] || !((current_motion_raster_.color_mask>>(slot*4))&15)) continue;
    auto& target=*bound_rts_[slot];
    if(motion_continuous_ && motion_scene_finished_ && target.resource==motion_captured_color_ &&
       CaptureObjectMotionFrame())
      REXLOG_INFO("native motion HDR after freeze: vs={:016X} ps={:016X} previous_serial={} next_serial={} depth={:X} blend={:X} mask={:X} full={}",
          current_vs_hash_,current_ps_hash_,target.motion_write_serial,serial,current_motion_raster_.depth_control,
          current_motion_raster_.blend_control,current_motion_raster_.color_mask,object_motion_full_cover_);
    const auto source=motion_color_sources_[0].version;
    const auto source_ticket=motion_continuous_ ? object_motion_lineage_.Find(source) : std::optional<graphics::MotionSceneTicket>{};
    const auto previous_content=MotionVersion(target);
    const auto previous_ticket=motion_continuous_ ? object_motion_lineage_.Find(previous_content) : std::optional<graphics::MotionSceneTicket>{};
    target.motion_write_frame=frame_count_+1;target.motion_write_serial=serial;
    if(motion_continuous_) {
      const auto version=MotionVersion(target);
      object_motion_lineage_.Invalidate(version.resource);
      if(!motion_scene_finished_ && slot==0 && target.guest_format==12 &&
         target.width==kOutputWidth && target.height==kOutputHeight &&
         graphics::CanCaptureMotionSceneDepth(true,dlss_view_projection_valid_,
             reinterpret_cast<uintptr_t>(bound_ds_),reinterpret_cast<uintptr_t>(dlss_scene_ds_))) {
        object_motion_hdr_=version;
        object_motion_frame_.ExpectDraw(serial);current_motion_raster_.color_serial=serial;
      } else if(motion_scene_finished_) {
        const bool fog=current_vs_hash_==0x7C8C263E50FCA091ull && current_ps_hash_==0x40541E232FDE42AFull;
        const bool reactive_vfx=motion_coverage_draw_active_ && motion_coverage_draw_id_==UINT32_MAX &&
            (current_ps_hash_==0x201C30CD5C58A1BCull || current_ps_hash_==0x5C1E60F40BD1DC6Eull);
        if(reactive_vfx && previous_ticket) object_motion_lineage_.Seed(version,*previous_ticket);
        const bool tone=current_vs_hash_==0x0408D37D5D96E869ull && current_ps_hash_==0x84FDB8B3BB1596B3ull;
        const bool filter=current_vs_hash_==0x815DF1B3CCBFAA4Eull && current_ps_hash_==0xB397B05F4CE5D5BBull;
        const bool overwrite=object_motion_full_cover_ && slot==0 && bound_rt_count_==1 &&
            !(current_motion_raster_.depth_control&3) &&
            (current_motion_raster_.blend_control&0x1fff)==1 &&
            (current_motion_raster_.color_mask&7)==7 && !(current_motion_raster_.color_control&8);
        if((tone || filter) && CaptureObjectMotionFrame())
          REXLOG_INFO("native motion lineage capture: tone={} filter={} full={} overwrite={} source_ticket={} source={:X}/{} destination={:X}/{} depth={:X} blend={:X} control={:X}",
            tone,filter,object_motion_full_cover_,overwrite,bool(source_ticket),source.resource,source.serial,
            version.resource,version.serial,current_motion_raster_.depth_control,current_motion_raster_.blend_control,current_motion_raster_.color_control);
        // These audited shaders sample the identity-UV primary scene. Exposure
        // and bloom alter radiance, not scene position or its motion coordinates.
        if((tone || filter || fog) && overwrite)
          object_motion_lineage_.Propagate(source,version,true);
      }
    }
  }
  object_motion_full_cover_=false;
}

// Generation is assigned on allocation, never inferred from a recycled COM address.
graphics::MotionContentVersion Renderer::MotionVersion(const HostSurface& surface) const {
  if(!surface.resource) return {};
  const auto description=surface.resource->GetDesc();
  return {reinterpret_cast<uintptr_t>(surface.resource.Get()),surface.motion_lifetime,
      surface.motion_write_frame,surface.motion_write_serial,uint32_t(description.Width),description.Height};
}
void Renderer::InvalidateObjectMotionColor(HostSurface& surface) {
  if(!motion_continuous_) return;
  if(motion_scene_finished_ && surface.resource==motion_captured_color_ &&
     CaptureObjectMotionFrame())
    REXLOG_INFO("native motion HDR invalidation: serial={} next_serial={} vs={:016X} ps={:016X}",
        surface.motion_write_serial,motion_color_serial_+1,current_vs_hash_,current_ps_hash_);
  object_motion_lineage_.Invalidate(reinterpret_cast<uintptr_t>(surface.resource.Get()));
  surface.motion_write_frame=frame_count_+1;surface.motion_write_serial=++motion_color_serial_;
}
bool Renderer::ObjectMotionInputsReady(const HostSurface& scene) {
  auto reason=object_motion_frame_.Rejection();
  if(!motion_continuous_ || !object_motion_output_ready_ || !temporal_motion_ || !motion_depth_snapshot_ || !motion_reactive_[frame_index_].mask)
    reason=graphics::ObjectMotionRejection::kResources;
  const auto ticket=object_motion_lineage_.Find(MotionVersion(scene));
  // Unknown/new/blended draws are represented explicitly by the GPU mask.
  // They do not constitute full motion coverage and do not reuse static zeros.
  if(reason==graphics::ObjectMotionRejection::kCorrespondence ||
     reason==graphics::ObjectMotionRejection::kCoverage)
    reason=graphics::ObjectMotionRejection::kNone;
  if(reason==graphics::ObjectMotionRejection::kNone && !motion_jitter_applied_)
    reason=graphics::ObjectMotionRejection::kJitter;
  reason=graphics::ObjectMotionConsumerRejection(reason,
      ticket && *ticket==object_motion_ticket_,true);
  ++object_motion_reports_;
  if(object_motion_reports_==1 || object_motion_reports_%120==0)
    REXLOG_INFO("native object motion: frame={} reason={} draws={} captured={} matched={} evaluated={} output={} lineage={} continuous=true",
        frame_count_+1,graphics::ObjectMotionRejectionName(reason),object_motion_frame_.draws(),
        object_motion_frame_.captured(),object_motion_frame_.matched(),object_motion_applied_,object_motion_output_ready_,bool(ticket));
  return reason==graphics::ObjectMotionRejection::kNone;
}
