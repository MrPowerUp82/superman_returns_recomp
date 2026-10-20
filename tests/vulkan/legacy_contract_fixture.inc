// Reference path: unmodified production shader_common and GameRenderer.
std::vector<uint8_t> RunLegacyContractProbe(Context& c,uint32_t count,uint32_t variant,bool indexed,bool global=false,bool ia=false,bool churn=false) {
  auto cpu_begin=std::chrono::steady_clock::now();Error e;UploadFixture fixture(c);
  auto vertex=std::make_shared<shaders::CompiledShader>(),pixel=std::make_shared<shaders::CompiledShader>();pixel->stage=shaders::ShaderStage::kPixel;
  const std::string prefix=std::string(SR_GLOBAL_SHADER_DIR)+(global?"/global":"/legacy");
  Require(ReadSpirv(prefix+(ia?"_ia_vs.spv":"_vs.spv"),vertex->words,e),e);Require(ReadSpirv(prefix+"_ps.spv",pixel->words,e),e);
  if(ia){vertex->inputs={{0,"float32x2"}};vertex->requirements.vertex_attributes=1;}
  if(global){vertex->abi=pixel->abi=shaders::ShaderAbi::kGlobal;vertex->requirements.storage_buffer_dynamic_indexing=pixel->requirements.storage_buffer_dynamic_indexing=true;vertex->requirements.descriptor_sets=pixel->requirements.descriptor_sets=1;}
  GameRenderer renderer(c,[&](const guest::ShaderCapture& capture){return shaders::ShaderResult{shaders::ShaderPoll::ready,capture.vertex?vertex:pixel,{}};});
  Require(renderer.Initialize({},e,global),e);Require(renderer.BeginSubmission(fixture.command,1,e,fixture.BeginUploads()),e);
  auto& resources=renderer.Resources();
  const float float_vertices[]{0,0,-1,-1,3,-1,-1,3};const uint16_t half_vertices[]{0,0,0x3c00,0xbc00,0x4400,0xbc00,0x3c00,0x4200};
  const uint32_t indices[]{0,1,2,3,4,5};Require(resources.UploadBuffer(7,std::as_bytes(std::span(indices)),1,e),e);
  std::array<guest::RenderPacket,2> packets;
  auto vs=std::make_shared<guest::ShaderCapture>(),ps=std::make_shared<guest::ShaderCapture>();vs->vertex=true;
  for(uint32_t i=0;i<2;++i){
    auto payload=i?std::span<const std::byte>(std::as_bytes(std::span(half_vertices))):std::span<const std::byte>(std::as_bytes(std::span(float_vertices)));
    Require(resources.UploadBuffer(2+i,payload,1,e),e);
    auto draw=std::make_shared<guest::DrawPacket>();auto pattern=ContractPattern(i,variant);
    draw->constants.vs.Share(std::make_shared<const guest::FloatConstantBank>(pattern.vs));
    draw->constants.ps.Share(std::make_shared<const guest::FloatConstantBank>(pattern.ps));
    std::copy_n(pattern.shared.begin(),draw->constants.shared.size(),draw->constants.shared.begin());
    draw->vertex_shader=vs;draw->pixel_shader=ps;draw->count=3;
    draw->indexed=indexed;draw->first=indexed?(i?3:0):1;draw->base_vertex=indexed?(i?-2:1):0;
    if(indexed){draw->indices.plan.key=7;draw->indices.plan.size=sizeof(indices);draw->indices.plan.format=2;}
    draw->colors[0]={601,false,{16,16,0,0,0,1}};draw->viewport={0,0,16,16,0,1};draw->scissor={0,0,16,16};
    draw->registers[0x104]=15;draw->registers[0x201]=1|(1<<16);
    guest::VertexStream stream{};stream.stream=7;stream.stride=i?4:8;stream.size=uint32_t(payload.size());stream.update.plan.key=2+i;stream.update.plan.size=stream.size;
    draw->streams.push_back(stream);draw->vertex_fetch[223]={7,0,stream.stride,i?0x2c235fu:0x2c23a5u};
    draw->attributes.push_back({7,0,draw->vertex_fetch[223].type,13,15});
    if(ia){draw->attributes.push_back({7,0,draw->vertex_fetch[223].type,0,0});draw->vertex_fetch[0]=draw->vertex_fetch[223];}
    auto& fetch=draw->texture_fetch[7];fetch[0]=2;fetch[1]=0x1006+(churn?0:i*4096);fetch[5]=1<<9;
    guest::LinearTexture texture;texture.width=texture.height=texture.depth=1;texture.dimension=1;texture.format=guest::LinearFormat::kRGBA8Unorm;texture.levels.push_back({1,1,4,1,0});texture.data={uint8_t(i==0?255:0),uint8_t(i==1?255:0),0,255};
    Require(resources.UploadTexture(TextureResourceId(fetch),texture,churn?i+1:1,e),e);
    auto capture=std::make_shared<guest::TextureCapture>();capture->version=1;capture->fetch=fetch;draw->textures[7]=capture;
    packets[i]=guest::DrawPacketOwner(std::move(draw));
  }
  guest::ClearPacket clear{};clear.colors=guest::GetDraw(packets[0])->colors;clear.flags=1;clear.color={0,0,0,1};Require(renderer.Record(guest::RenderPacket(clear),fixture.command,e),e);
  auto start=std::chrono::steady_clock::now();
  for(uint32_t i=0;i<count;++i){
    auto* draw=guest::GetDraw(packets[i%2]);draw->command_serial=i+1;
    if(churn) {
      auto payload=i%2?std::span<const std::byte>(std::as_bytes(std::span(half_vertices))):std::span<const std::byte>(std::as_bytes(std::span(float_vertices)));
      Require(resources.UploadBuffer(2,payload,i+3,e),e);draw->streams[0].update.plan.key=2;
      guest::LinearTexture texture;texture.width=texture.height=texture.depth=1;texture.dimension=1;texture.format=guest::LinearFormat::kRGBA8Unorm;texture.levels.push_back({1,1,4,1,0});texture.data={uint8_t(i%2==0?255:0),uint8_t(i%2==1?255:0),0,255};
      Require(resources.UploadTexture(TextureResourceId(draw->texture_fetch[7]),texture,i+3,e),e);
      auto capture=std::make_shared<guest::TextureCapture>();capture->version=i+3;capture->fetch=draw->texture_fetch[7];draw->textures[7]=capture;
    }
    Require(renderer.Record(packets[i%2],fixture.command,e),e);
  }
  renderer.FinishSubmission();double record_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  auto target=renderer.Targets().Get(renderer.Targets().Acquire(clear.colors[0],e),e);Require(bool(target),e);
  Require(renderer.Images().Transition(fixture.command,target->image,{target->aspects,0,1,0,1},ImageUsage::TransferSource(),e),e);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};c.f.vkCmdCopyImageToBuffer(fixture.command,target->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  double cpu_prepare_record_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpu_begin).count();auto pixels=fixture.Finish();renderer.Retire(1);CheckContractPixels(pixels,target->format,variant);pixels.resize(16*16*4);
  if(renderer.Stats().draws!=count || renderer.Stats().failed || renderer.Stats().pending)throw std::runtime_error("Legacy reference omitted work");
  auto bindings=renderer.BindingStats();
  if(global && bindings.texture_preparations+bindings.texture_reuses!=count)
    throw std::runtime_error("Texture work counters omitted a draw binding");
  if(global && churn && bindings.texture_preparations!=count)
    throw std::runtime_error("Replaced texture version reused a stale binding");
  if(expect_binding_batches && global && count==3000 && !churn && bindings.texture_preparations>4)
    throw std::runtime_error("Repeated textures exceeded preparation budget");
  std::cout<<(global?"contract_global_renderer_probe":"contract_legacy_probe")<<" variant="<<variant<<" draws="<<count<<" indexed="<<indexed<<" ia="<<ia<<" churn="<<churn<<" expected_pixels=PASS record_ms="<<record_ms<<" cpu_prepare_record_ms="<<cpu_prepare_record_ms<<" binding_pages="<<bindings.pages<<" heap_resource_acquires="<<bindings.resource_acquires<<" transient_allocations="<<bindings.transient_allocations<<'\n';
  if(global)std::cout<<"texture_binding_work prepares="<<bindings.texture_preparations<<" reuses="<<bindings.texture_reuses<<" churn="<<churn<<'\n';
  if(expect_binding_batches && global && count==3000 && !churn && bindings.resource_acquires>64)throw std::runtime_error("Repeated draw resources exceeded batch acquisition budget");
  if(expect_binding_batches && global && count==3000 && !churn && bindings.transient_allocations>64)throw std::runtime_error("Repeated draw constants exceeded batch allocation budget");
  return pixels;
}
