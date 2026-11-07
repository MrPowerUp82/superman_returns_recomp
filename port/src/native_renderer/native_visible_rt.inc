// Dedicated visible-scene DXR stage. Included inside the native namespace.
void Renderer::BeforeVisibleRaySubmit() {
  if(frame_open_ && visible_ray_requested_ && !visible_ray_recorded_) visible_ray_submission_split_=true;
}
void Renderer::BeginVisibleRayFrame() {
  visible_ray_recorded_=visible_ray_built_=visible_ray_characters_built_=false;visible_ray_suppressed_shadow_draws_=0;
  visible_ray_light_valid_=visible_ray_light_conflict_=false;
  visible_ray_standalone_flags_.clear();visible_ray_submission_split_=visible_ray_unpatched_receiver_=visible_ray_predicated_=false;
  const uint64_t completed=fence_->GetCompletedValue();
  for(auto& ticket:visible_ray_tickets_) {
    if(ticket.fence && completed!=UINT64_MAX && completed>=ticket.fence) {
      if(visible_ray_counter_readback_[&ticket-visible_ray_tickets_]) {
        const D3D12_RANGE range{0,8};void* mapped=nullptr;
        if(SUCCEEDED(visible_ray_counter_readback_[&ticket-visible_ray_tickets_]->Map(0,&range,&mapped))) {
          uint32_t counts[2]{};std::memcpy(counts,mapped,8);
          const D3D12_RANGE empty{0,0};visible_ray_counter_readback_[&ticket-visible_ray_tickets_]->Unmap(0,&empty);
          if(!counts[0]) ticket.requested[0]=ticket.requested[1]=ticket.requested[2]=0;
          if(!counts[1]) ticket.requested[3]=0;
          if(ticket.frame%120==0 || !visible_ray_applied_.frame)
            REXLOG_INFO("native visible DXR executed: frame={} sun_pixels={} reflection_pixels={} fence_completed=true",
                ticket.frame,counts[0],counts[1]);
        } else ticket.requested={};
      } else ticket.requested={};
      if(visible_ray_timing_heap_ && visible_ray_timestamp_frequency_ &&
         (ticket.frame%120==0 || !visible_ray_applied_.frame)) {
        const size_t offset=size_t(&ticket-visible_ray_tickets_)*4*sizeof(uint64_t);
        const D3D12_RANGE range{offset,offset+4*sizeof(uint64_t)};void* mapped=nullptr;
        if(SUCCEEDED(visible_ray_timing_readback_->Map(0,&range,&mapped))) {
          uint64_t times[4]{};std::memcpy(times,static_cast<const uint8_t*>(mapped)+offset,sizeof(times));
          const D3D12_RANGE empty{0,0};visible_ray_timing_readback_->Unmap(0,&empty);
          if(times[1]>=times[0] && times[3]>=times[2])
            REXLOG_INFO("native visible DXR GPU cost: frame={} geometry_build_ms={:.3f} trace_denoise_composite_ms={:.3f}",
                ticket.frame,double(times[1]-times[0])*1000/visible_ray_timestamp_frequency_,
                double(times[3]-times[2])*1000/visible_ray_timestamp_frequency_);
        }
      }
      if(ticket.frame>visible_ray_applied_.frame) visible_ray_applied_=ticket;
      ticket={};
    }
  }
  visible_ray_requested_=!packet_sink_ && ray_tracing_capable_ &&
      (graphics_frame_request_.requested[0] || graphics_frame_request_.requested[3]) &&
      REXCVAR_GET(sr_native_pm4_mirror);
  if(!visible_ray_requested_) return;
  if(!visible_ray_shadow_predicate_[frame_index_]) {
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=8;desc.Height=1;
    desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const D3D12_RANGE no_read{0,0};void* mapped=nullptr;
    if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,IID_PPV_ARGS(&visible_ray_shadow_predicate_[frame_index_]))) ||
       FAILED(visible_ray_shadow_predicate_[frame_index_]->Map(0,&no_read,&mapped))) {
      visible_ray_requested_=false;return;
    }
    visible_ray_shadow_enable_[frame_index_]=static_cast<uint64_t*>(mapped);
  }
  const uint64_t fallback=1;std::memcpy(visible_ray_shadow_enable_[frame_index_],&fallback,8);
  if(!visible_ray_timing_heap_) {
    D3D12_QUERY_HEAP_DESC queries{};queries.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;queries.Count=12;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=12*sizeof(uint64_t);desc.Height=1;
    desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device_->CreateQueryHeap(&queries,IID_PPV_ARGS(&visible_ray_timing_heap_))) ||
       FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr,IID_PPV_ARGS(&visible_ray_timing_readback_))) ||
       FAILED(queue_->GetTimestampFrequency(&visible_ray_timestamp_frequency_))) {
      visible_ray_timing_heap_.Reset();visible_ray_timestamp_frequency_=0;
    }
  }
  if(!visible_ray_counter_readback_[frame_index_]) {
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=8;desc.Height=1;
    desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,IID_PPV_ARGS(&visible_ray_counter_readback_[frame_index_])))) {visible_ray_requested_=false;return;}
  }
  if(!visible_ray_scene_) {
    auto scene=std::make_unique<graphics::d3d12::VisibleRayScene>();
    if(FAILED(scene->Initialize(device_))) {
      REXLOG_WARN("native visible DXR initialization failed: {}; raster fallback",scene->LastError());
      visible_ray_requested_=false;return;
    }
    visible_ray_scene_=std::move(scene);
  }
  if(FAILED(visible_ray_scene_->BeginFrame(frame_index_))) {
    REXLOG_WARN("native visible DXR frame failed: {}",visible_ray_scene_->LastError());
    visible_ray_requested_=false;
  }
}

void Renderer::BuildVisibleRayScene() {
  if(!visible_ray_requested_ || !motion_camera_valid_ || motion_camera_conflict_ ||
     !object_motion_output_ready_ || motion_history_.samples().empty()) return;
  graphics::Matrix4 inverse{};
  if(!graphics::InvertProjection(object_motion_final_camera_,inverse)) return;
  std::vector<graphics::d3d12::VisibleRayGeometry> geometry;
  uint32_t literal_alpha_draws=0;uint64_t literal_alpha_vertices=0;
  for(size_t index=0;index<motion_history_.samples().size();++index) {
    const auto& sample=motion_history_.samples()[index];const auto& raster=motion_rasters_[index];
    const bool opaque=raster.ray_opaque;
    if(opaque && (raster.blend_control&0x1fff)!=1) {
      ++literal_alpha_draws;literal_alpha_vertices+=sample.range.vertices;
    }
    geometry.push_back({motion_clips_->Buffer(sample.range.slot),sample.range.offset,
        sample.range.vertices,16,opaque,true,graphics::IsObservedSkinnedMotionShader(raster.shader) ||
        raster.shader==0xA5232A3184AA617Cull});
  }
  visible_ray_characters_built_=std::any_of(geometry.begin(),geometry.end(),[](const auto& draw){return draw.opaque && draw.character;});
  if(visible_ray_timing_heap_) command_list_->EndQuery(visible_ray_timing_heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frame_index_*4);
  const HRESULT hr=visible_ray_scene_->Build(command_list_.Get(),geometry,inverse.data());
  if(visible_ray_timing_heap_) {
    command_list_->EndQuery(visible_ray_timing_heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frame_index_*4+1);
    command_list_->ResolveQueryData(visible_ray_timing_heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frame_index_*4,2,
        visible_ray_timing_readback_.Get(),frame_index_*4*sizeof(uint64_t));
  }
  if(FAILED(hr)) {
    if(frame_count_%120==0) REXLOG_WARN("native visible DXR scene rejected: {}",visible_ray_scene_->LastError());
    return;
  }
  visible_ray_built_=true;
  if((frame_count_+1)%120==0 || CaptureObjectMotionFrame())
    REXLOG_INFO("native visible DXR geometry: frame={} literal_alpha_body_draws={} literal_alpha_body_vertices={} characters_built={}",
        frame_count_+1,literal_alpha_draws,literal_alpha_vertices,visible_ray_characters_built_);
  frame_state_bound_=fixed_state_cached_=false;bound_vertex_views_mask_=bound_rt_count_=0;
  bound_ds_=nullptr;std::memset(bound_rts_,0,sizeof(bound_rts_));
}

void Renderer::RecordVisibleRayFrame(HostSurface& target) {
  if(!visible_ray_requested_ || !visible_ray_built_ || !motion_depth_snapshot_ ||
     target.samples!=1 || !temporal_motion_ || !post_vertex_projection_valid_ || post_vertex_camera_conflict_) {
    if(visible_ray_requested_ && frame_count_%120==0)
      REXLOG_INFO("native visible DXR scene pending: built={} motion={} projection={} camera_conflict={} samples={}",
          visible_ray_built_,bool(temporal_motion_),post_vertex_projection_valid_,post_vertex_camera_conflict_,target.samples);
    return;
  }
  const auto desc=target.resource->GetDesc();
  if(desc.Width!=object_motion_ticket_.width || desc.Height!=object_motion_ticket_.height) return;
  graphics::d3d12::VisibleRayInputs inputs;
  auto& scene=inputs.scene;
  if(!graphics::InvertProjection(object_motion_final_camera_,visible_ray_inverse_)) return;
  auto& source=visible_ray_color_[frame_index_];
  const auto previous_desc=source ? source->GetDesc() : D3D12_RESOURCE_DESC{};
  if(!source || desc.Width!=previous_desc.Width || desc.Height!=previous_desc.Height || desc.Format!=previous_desc.Format) {
    source.Reset();auto copy=desc;copy.Flags=D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&copy,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source)))) return;
    visible_ray_color_state_[frame_index_]=D3D12_RESOURCE_STATE_COPY_DEST;
  }
  Transition(target.resource.Get(),target.state,D3D12_RESOURCE_STATE_COPY_SOURCE);
  Transition(source.Get(),visible_ray_color_state_[frame_index_],D3D12_RESOURCE_STATE_COPY_DEST);
  command_list_->CopyResource(source.Get(),target.resource.Get());
  Transition(source.Get(),visible_ray_color_state_[frame_index_],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(motion_depth_snapshot_.Get(),object_motion_depth_state_[frame_index_],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  const auto motion=temporal_motion_->Outputs();
  auto velocity_state=D3D12_RESOURCE_STATE_RENDER_TARGET,validity_state=D3D12_RESOURCE_STATE_RENDER_TARGET;
  Transition(motion.velocity,velocity_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(motion.validity,validity_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  scene.depth=motion_depth_snapshot_.Get();scene.color=source.Get();
  scene.width=uint32_t(desc.Width);scene.height=desc.Height;
  std::memcpy(scene.inv_view_projection,visible_ray_inverse_.data(),64);
  std::memcpy(inputs.view_projection,object_motion_final_camera_.data(),64);
  std::memcpy(inputs.previous_view_projection,visible_ray_previous_camera_.data(),64);
  std::memcpy(scene.camera,post_vertex_camera_.data(),12);
  scene.depth_valid=scene.color_valid=scene.inv_view_projection_valid=scene.camera_valid=true;
  scene.light_valid=visible_ray_light_valid_ && !visible_ray_light_conflict_;
  if(scene.light_valid) std::memcpy(scene.direction_to_light,visible_ray_light_.data(),12);
  inputs.enable_shadows=graphics_frame_request_.requested[0] && scene.light_valid && rt_shadow_requested_ &&
      !visible_ray_unpatched_receiver_ && !visible_ray_submission_split_;
  scene.enable_reflections=graphics_frame_request_.requested[3]!=0;
  scene.exclude_character_shadows=graphics_frame_request_.requested[2]==0;
  scene.ray_bias=.05f;scene.max_distance=10000;scene.max_depth_delta=.02f;
  scene.clear_depth=object_motion_depth_reversed_ ? 0.f : 1.f;
  const bool high=graphics_frame_request_.requested[1]==2;
  scene.shadow_quality=high ? graphics::d3d12::RayTracingShadowQuality::High : graphics::d3d12::RayTracingShadowQuality::Low;
  scene.cone_half_angle=high ? .01f : .003f;
  inputs.reflection_distance=float(REXCVAR_GET(sr_native_rt_reflection_distance));
  inputs.reflection_rays=high ? 2 : 1;
  inputs.trace_scale=float(REXCVAR_GET(sr_native_rt_trace_scale));
  Transition(motion_reactive_[frame_index_].mask.Get(),motion_reactive_[frame_index_].state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  inputs.reactivity=motion_reactive_[frame_index_].mask.Get();
  inputs.current_jitter[0]=motion_frame_jitter_.pixel_x;inputs.current_jitter[1]=motion_frame_jitter_.pixel_y;
  inputs.previous_jitter[0]=motion_previous_jitter_[0];inputs.previous_jitter[1]=motion_previous_jitter_[1];
  inputs.motion=motion.velocity;inputs.validity=motion.validity;inputs.frame_serial=frame_count_+1;
  inputs.previous_projection_valid=visible_ray_previous_frame_==frame_count_;
  inputs.reset_history=!inputs.previous_projection_valid || motion_camera_conflict_ || motion_previous_camera_bad_;
  if(visible_ray_timing_heap_) command_list_->EndQuery(visible_ray_timing_heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frame_index_*4+2);
  const int failure_interval=REXCVAR_GET(sr_native_debug_visible_rt_failure_interval);
  const bool injected_failure=failure_interval>0 && (frame_count_+1)%uint32_t(failure_interval)==0;
  HRESULT hr=injected_failure ? E_FAIL : visible_ray_scene_->Trace(command_list_.Get(),inputs);
  if(injected_failure && (frame_count_+1)%120==0)
    REXLOG_INFO("native visible DXR injected failure: frame={} raster_shadow_fallback=true predicate_execute=1 standalone=0",frame_count_+1);
  if(SUCCEEDED(hr)) hr=visible_ray_scene_->Composite(command_list_.Get());
  if(visible_ray_timing_heap_) {
    command_list_->EndQuery(visible_ray_timing_heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frame_index_*4+3);
    command_list_->ResolveQueryData(visible_ray_timing_heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frame_index_*4+2,2,
        visible_ray_timing_readback_.Get(),(frame_index_*4+2)*sizeof(uint64_t));
  }
  if(SUCCEEDED(hr)) {
    const auto output=visible_ray_scene_->Outputs();
    const auto source_ticket=object_motion_lineage_.Find(MotionVersion(target));
    Transition(target.resource.Get(),target.state,D3D12_RESOURCE_STATE_COPY_DEST);
    command_list_->CopyResource(target.resource.Get(),output.color);
    // This audited stage changes radiance while preserving scene coordinates.
    // Give the new bytes their own version and carry only the exact input ticket.
    object_motion_lineage_.Invalidate(reinterpret_cast<uintptr_t>(target.resource.Get()));
    target.motion_write_frame=frame_count_+1;target.motion_write_serial=++motion_color_serial_;
    if(source_ticket) object_motion_lineage_.Seed(MotionVersion(target),*source_ticket);
    Transition(target.resource.Get(),target.state,D3D12_RESOURCE_STATE_RENDER_TARGET);
    command_list_->CopyBufferRegion(visible_ray_counter_readback_[frame_index_].Get(),0,output.counters,0,8);
    if(inputs.enable_shadows) {
      const uint64_t suppress=0;std::memcpy(visible_ray_shadow_enable_[frame_index_],&suppress,8);
      const uint32_t standalone=1;
      for(auto* field:visible_ray_standalone_flags_) std::memcpy(field,&standalone,4);
    }
    visible_ray_recorded_=true;
    visible_ray_previous_camera_=object_motion_final_camera_;visible_ray_previous_frame_=frame_count_+1;
    if(++visible_ray_recorded_frames_==1 || visible_ray_recorded_frames_%120==0)
      REXLOG_INFO("native visible DXR recorded: frame={} spans={} shadows={} reflections={} trace_scale={} temporal={} shadow_draws_suppressed={} visible_geometry_only=true",
          frame_count_+1,motion_rasters_.size(),inputs.enable_shadows,scene.enable_reflections,inputs.trace_scale,output.history_used,visible_ray_suppressed_shadow_draws_);
    if(CaptureObjectMotionFrame()) {
      QueueMotionReadback(output.shadow,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"rt_sun_visibility");
      QueueMotionReadback(output.reflection,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"rt_reflection");
    }
  } else if(frame_count_%120==0) REXLOG_WARN("native visible DXR trace rejected: {}",visible_ray_scene_->LastError());
  Transition(motion.velocity,velocity_state,D3D12_RESOURCE_STATE_RENDER_TARGET);
  Transition(motion.validity,validity_state,D3D12_RESOURCE_STATE_RENDER_TARGET);
  Transition(motion_depth_snapshot_.Get(),object_motion_depth_state_[frame_index_],D3D12_RESOURCE_STATE_DEPTH_READ);
  Transition(target.resource.Get(),target.state,D3D12_RESOURCE_STATE_RENDER_TARGET);
  frame_state_bound_=fixed_state_cached_=false;bound_vertex_views_mask_=bound_rt_count_=0;
  bound_ds_=nullptr;std::memset(bound_rts_,0,sizeof(bound_rts_));
}
