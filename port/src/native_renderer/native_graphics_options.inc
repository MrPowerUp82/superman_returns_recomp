// Included by native_renderer.cpp inside superman_returns::native.
// Runs only between closed frames, before any allocator/list reset or draw.
void Renderer::ApplyGraphicsFrameOptions() {
  const auto snapshot=graphics::SharedGraphicsOptions()->Snapshot();
  if(packet_sink_ || !initialized_ || frame_open_ ||
     graphics_host_attempt_revision_==snapshot.request_revision) return;
  graphics_host_attempt_revision_=snapshot.request_revision;
  if(graphics_dlss_mode_<0) {
    constexpr uint32_t old_widths[]{960,1280,1600,1920,2560,3840},old_heights[]{540,720,900,1080,1440,2160};
    for(int i=0;i<6;++i) if(output_width_==old_widths[i] && output_height_==old_heights[i]) graphics_host_applied_[6]=i;
    graphics_host_applied_[9]=msaa_samples_>=8 ? 2 : msaa_samples_>=4 ? 1 : 0;
    graphics_host_applied_[10]=2; // Startup native scaling default, refined on first successful apply.
    graphics_host_applied_[8]=fxaa_ ? 1 : 0;
  }
  const auto& requested=snapshot.requested;
  constexpr uint32_t widths[]{960,1280,1600,1920,2560,3840};
  constexpr uint32_t heights[]{540,720,900,1080,1440,2160};
  constexpr float scales[]{.5f,.75f,1.f,1.25f,1.5f,2.f,3.f,4.f};
  constexpr uint32_t samples[]{1,4,8};
  const uint32_t width=widths[requested[6]],height=heights[requested[6]];
  float scale=graphics::ComputeGraphicsRenderExtent(width,height,scales[requested[10]],1,1,16384,16384).scale;
  const uint32_t msaa=(requested[4] || requested[0] || requested[3]) ? 1u : samples[requested[9]];
  const bool configuration_changed=width!=output_width_ || height!=output_height_ ||
      graphics_host_applied_[10]!=requested[10] || msaa!=msaa_samples_ || graphics_dlss_mode_!=requested[4];
  if(configuration_changed) {
    graphics_host_failure_.clear();
    for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_D24_UNORM_S8_UINT}) {
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality{};
      quality.Format=format;quality.SampleCount=msaa;
      if(FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&quality,sizeof(quality))) ||
          !quality.NumQualityLevels) {
        graphics_host_failure_="Requested MSAA is unsupported for a scene render-target format";return;
      }
    }
    // Allocation is transactional: a failed output allocation leaves the previous
    // output and scene intact. No live descriptor is overwritten before fencing.
    ComPtr<ID3D12Resource> replacement[kFramesInFlight];
    if(width!=output_width_ || height!=output_height_) {
      D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=1;
      desc.Format=rex::ui::d3d12::D3D12Presenter::kGuestOutputFormat;
      desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      D3D12_CLEAR_VALUE clear{};clear.Format=desc.Format;
      for(auto& resource:replacement)
        if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(&resource)))) {
          graphics_host_failure_="Output allocation failed; previous graphics settings retained";
          return;
        }
    }
    auto wait_for=[&](ID3D12Fence* fence,uint64_t value) {
      const auto completed=fence->GetCompletedValue();
      if(completed==UINT64_MAX) return false;
      if(completed>=value) return true;
      return SUCCEEDED(fence->SetEventOnCompletion(value,fence_event_)) &&
          WaitForSingleObject(fence_event_,5000)==WAIT_OBJECT_0 &&
          fence->GetCompletedValue()!=UINT64_MAX && fence->GetCompletedValue()>=value;
    };
    // Cover every native submission (including unsuccessful NGX recording) and
    // every direct-queue presenter copy; the two queue lifetimes are distinct.
    const uint64_t covering=next_fence_value_++;
    if(FAILED(queue_->Signal(fence_.Get(),covering)) || !wait_for(fence_.Get(),covering)) {
      graphics_host_failure_="GPU did not retire graphics work; previous settings retained";return;
    }
    if(present_fence_) {
      const uint64_t presented=present_fence_next_++;
      if(FAILED(provider_->GetDirectQueue()->Signal(present_fence_.Get(),presented)) ||
          !wait_for(present_fence_.Get(),presented)) {
        graphics_host_failure_="Presenter did not retire graphics work; previous settings retained";return;
      }
    }
    // DLSS (dlss.cpp) renders the scene below the output scale itself: the
    // internal scale stays the user's. Its feature is recreated for the new
    // size or mode at the next scene.
    if(dlss_) dlss_.reset();
    const bool storage_changed=width!=output_width_ || height!=output_height_ ||
        scale!=render_scale_ || msaa!=msaa_samples_;
    // Fence completion now permits resource release and CPU descriptor reuse.
    if(replacement[0]) {
      for(uint32_t i=0;i<kFramesInFlight;++i) {
        output_rts_[i]=std::move(replacement[i]);
        output_rt_states_[i]=D3D12_RESOURCE_STATE_RENDER_TARGET;
        device_->CreateRenderTargetView(output_rts_[i].Get(),nullptr,output_rtvs_[i]);
      }
    }
    if(storage_changed) {
      for(auto& [key,surface]:surfaces_) {
        RetireSrvIndex(surface.srv_index);RetireSrvIndex(surface.stencil_srv_index);
      }
      for(auto& [key,resolve]:resolve_textures_) {
        RetireSrvIndex(resolve.plain_srv);
        for(auto [mapping,index]:resolve.srv_by_mapping) RetireSrvIndex(index);
        for(auto [mapping,index]:resolve.raw_srv_by_mapping) RetireSrvIndex(index);
      }
      InvalidateSlotBindings(~0u);
      surfaces_.clear();resolve_textures_.clear();resolve_latest_.clear();resolve_identities_={};
      scene_rt_=scene_ds_=upscale_rt_=dlss_scene_ds_=nullptr;
      dlss_scene_=nullptr;dlss_scene_depth_=nullptr;dlss_twin_=nullptr;dlss_resolve_source_=nullptr;
      coop_last_full_resolve_=nullptr;coop_cleared_source_=nullptr;coop_cleared_stamp_=0;
      coop_pair_.Reset();coop_deferred_front_=0;
      for(auto& image:coop_images_) image={};
      for(auto& bound:bound_rts_) bound=nullptr;
      for(auto& bound:bind_set_) bound=nullptr;
      bound_ds_=nullptr;bound_rt_count_=0;bound_samples_=1;bound_scale_=1;
      frame_state_bound_=fixed_state_cached_=false;bound_vertex_views_mask_=0;
      ssao_ao_.Reset();ssao_ao_rtv_={};fxaa_temp_.Reset();
      // Only output RTVs remain in these heaps; SRV and sampler heaps retain all
      // unrelated texture/buffer descriptors and their allocator generation.
      rtv_heap_next_=kFramesInFlight;dsv_heap_next_=0;
      post_vertex_depth_snapshot_.Reset();post_vertex_color_snapshot_.Reset();
      motion_history_={};object_motion_lineage_={};object_motion_ticket_={};object_motion_hdr_={};
      object_motion_previous_=object_motion_output_ready_=false;
      object_motion_previous_camera_frame_=0;
      motion_depth_snapshot_.Reset();motion_captured_depth_.Reset();motion_captured_color_.Reset();
      motion_readbacks_.clear();motion_color_pairs_.clear();
      for(auto& source:motion_color_sources_) source={};
      for(auto& resource:object_motion_depth_) resource.Reset();
      for(auto& resource:motion_coverage_) resource.Reset();
      for(auto& slot:motion_reactive_) slot.mask.Reset();
      // TemporalMotion retains dimension-independent prewarmed PSOs; BeginFrame/Render resize fenced slots.
      motion_static_candidate_.reset();
      motion_observed_skin_attribution_.reset();motion_observed_cloth_attribution_.reset();
    }
    output_width_=width;output_height_=height;render_scale_=scale;msaa_samples_=msaa;
    dlss_render_width_=render_scale::HostDimension(kOutputWidth,scale);
    dlss_render_height_=render_scale::HostDimension(kOutputHeight,scale);
    dlss_history_=dlss_wanted_=false;graphics_dlss_mode_=requested[4];
    dlss_scene_=nullptr;dlss_scene_done_=false;dlss_prev_camera_=false;
    REXLOG_INFO("native graphics hot settings: output={}x{} scale={} MSAA={} DLSS_request={} scene_ratio={}",
        width,height,scale,msaa,requested[4],DlssSceneRatio());
  }
  fxaa_=requested[8]!=0;
  graphics_host_applied_[5]=0; // This renderer/device is D3D12; API switch waits for restart.
  graphics_host_applied_[6]=requested[6];
  // Pacing consumers read this cvar each iteration; no simulation delta is changed.
  // The preference bridge owns cvars. Writing a renderer snapshot here races
  // the next guest UI request and can restore an older frame limit.
  graphics_host_applied_[7]=requested[7];
  graphics_host_applied_[8]=0; // FXAA needs an actually executed scene pass; Append reports it.
  graphics_host_applied_[9]=msaa>=8 ? 2 : msaa>=4 ? 1 : 0;graphics_host_applied_[10]=requested[10];
}

void Renderer::AppendGraphicsHostReport(graphics::GraphicsBackendReport& report) const {
  for(size_t row=5;row<graphics::kGraphicsOptionCount;++row) {
    report.features[row]={true,true,{}};
    report.applied[row]=graphics_host_applied_[row];
  }
  report.features[5].reason="Changing graphics API requires restart";
  report.applied[8]=fxaa_ && fxaa_scene_applied_ ? 1 : 0;
  if(graphics_dlss_mode_>0) {
    report.features[8].reason="Temporal AA owns antialiasing while DLSS is requested";
    report.features[9].reason="DLSS motion inputs require single-sample surfaces";
    report.applied[8]=0;
  }
  if(graphics_frame_request_.requested[0] || graphics_frame_request_.requested[3])
    report.features[9].reason="DXR temporal inputs require single-sample scene surfaces";
  if(!graphics_host_failure_.empty()) {
    if(!report.failure.empty()) report.failure+="; ";
    report.failure+=graphics_host_failure_;
  }
}
