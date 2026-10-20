#include <bit>
// Only the float banks and shared prefix remain per draw. Declaration metadata
// is immutable per layout, and the draw carries one 16-byte stream record.
constexpr uint32_t ContractConstantBytes=8192+512;
guest::ConstantSnapshot ContractPattern(uint32_t parity,uint32_t variant) {
  guest::ConstantSnapshot result;
  result.vs[0]=std::bit_cast<uint32_t>(parity?-1.0f:0.0f);
  result.ps[0]=std::bit_cast<uint32_t>(parity?1.0f:(64.0f/255.0f));
  result.ps[1]=std::bit_cast<uint32_t>(parity?(128.0f/255.0f):1.0f);
  result.ps[2]=result.ps[3]=std::bit_cast<uint32_t>(1.0f);
  uint32_t texture=parity?4095:511;std::memcpy(result.shared.data()+7*4,&texture,4);
  uint32_t flag=(variant==1 && !parity)||(variant==2 && parity),loop=flag?1:2;
  std::memcpy(result.shared.data()+256+4*4,&flag,4);
  std::memcpy(result.shared.data()+304+16*4,&loop,4);
  return result;
}
void CheckContractPixels(const std::vector<uint8_t>& pixels,VkFormat format,uint32_t variant) {
  bool bgra=format==VK_FORMAT_B8G8R8A8_UNORM;
  if(format!=VK_FORMAT_R8G8B8A8_UNORM && !bgra)throw std::runtime_error("Unexpected contract format");
  for(uint32_t y=0;y<16;++y)for(uint32_t x=0;x<16;++x){uint8_t red=variant==1?32:64,green=variant==2?64:128;
    const uint8_t expected[]{uint8_t(x<8?red:0),uint8_t(x>=8?green:0),0,255};
    for(uint32_t ch=0;ch<4;++ch){auto source=(bgra && (ch==0 || ch==2))?2-ch:ch;
      if(pixels[(y*16+x)*4+source]!=expected[ch])throw std::runtime_error("Contract pixel mismatch variant="+std::to_string(variant)+" x="+std::to_string(x)+" y="+std::to_string(y)+" channel="+std::to_string(ch)+" actual="+std::to_string(pixels[(y*16+x)*4+source])+" expected="+std::to_string(expected[ch]));
    }
  }
}
// Project-owned global ABI fixture using production upload/target/descriptor stores.
struct BatchProbeHandles {
  Context& c;VkBuffer indirect{};VkDeviceMemory memory{};VkShaderModule vs{},ps{};VkPipelineLayout layout{};VkPipeline pipeline{};
  ~BatchProbeHandles() {c.f.vkDeviceWaitIdle(c.device);if(pipeline)c.f.vkDestroyPipeline(c.device,pipeline,nullptr);if(layout)c.f.vkDestroyPipelineLayout(c.device,layout,nullptr);if(vs)c.f.vkDestroyShaderModule(c.device,vs,nullptr);if(ps)c.f.vkDestroyShaderModule(c.device,ps,nullptr);if(indirect)c.f.vkDestroyBuffer(c.device,indirect,nullptr);if(memory)c.f.vkFreeMemory(c.device,memory,nullptr);}
};
#include "global_contract_lifetime.inc"
std::unique_ptr<PreparedContractFrame> PrepareHeapProbe(Context& c,GlobalDescriptorHeap& heap,uint64_t serial,uint32_t count,uint32_t variant,bool indexed,uint32_t generation=0,std::shared_ptr<ContractHostGate> gate={},bool wait_gate=false,uint32_t texture_mode=0) {
  constexpr bool direct=true,batched=false;auto global_layout=heap.Layout();auto global_set=heap.Set();
  auto cpu_begin=std::chrono::steady_clock::now();auto frame=std::make_unique<PreparedContractFrame>(c,gate);Error e;
  auto& fixture=frame->fixture;auto& state=frame->state;auto& targets=frame->targets;auto& resources=frame->resources;auto& handles=frame->handles;
  frame->wait_gate=wait_gate;
  frame->heap=&heap;frame->heap_serial=serial;
  Require(resources.BeginSubmission(fixture.command,1,e),e);Require(targets.BeginSubmission(fixture.command,1,e),e);
  uint32_t table_slot=0,constant_slot=0,layout_slot=0,stream_slot=0,vertex_slots[2]{},image_slots[2]{},sampler_slot=0;
  if(texture_mode) {
    auto sampler=std::make_shared<SamplerResource>();sampler->context=&c;
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};info.magFilter=info.minFilter=VK_FILTER_NEAREST;
    info.addressModeU=info.addressModeV=info.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Require(c.f.vkCreateSampler(c.device,&info,nullptr,&sampler->handle),"Fixture sampler");Require(heap.Sampler(sampler,serial,sampler_slot,e),e);
  }
  const float float_vertices[]{0,0,-1,-1,3,-1,-1,3};const uint16_t half_vertices[]{0,0,0x3c00,0xbc00,0x4400,0xbc00,0x3c00,0x4200};
  for(uint32_t i=0;i<2;++i) {
    auto payload=i?std::span<const std::byte>(std::as_bytes(std::span(half_vertices))):std::span<const std::byte>(std::as_bytes(std::span(float_vertices)));
    Require(resources.UploadBuffer(2+i,payload,1,e),e);
    Require(heap.Buffer(resources.Buffer(2+i,e),serial,vertex_slots[i],e),e);
    guest::LinearTexture texture;texture.width=texture.height=1;texture.dimension=texture_mode>1?texture_mode:1;
    texture.depth=texture_mode==2?2:(texture_mode==3?6:1);texture.format=guest::LinearFormat::kRGBA8Unorm;
    for(uint32_t z=0;z<texture.depth;++z){texture.levels.push_back({1,1,4,1,texture.data.size()});texture.data.insert(texture.data.end(),{uint8_t(i==0?(generation==2?64:255):0),uint8_t(i==1?(generation==2?128:255):0),0,255});}
    Require(resources.UploadTexture(10+i,texture,1,e),e);Require(heap.Image(resources.Texture(10+i,e),serial,image_slots[i],e),e);
  }
  std::array<guest::ConstantSnapshot,2> patterns{ContractPattern(0,variant),ContractPattern(1,variant)};
  for(uint32_t i=0;i<2;++i){std::memcpy(patterns[i].shared.data()+7*4,&image_slots[i],4);
    std::memcpy(patterns[i].shared.data()+128+7*4,&sampler_slot,4);
    if(generation==2)patterns[i].ps[0]=patterns[i].ps[1]=patterns[i].ps[2]=std::bit_cast<uint32_t>(1.0f);
  }
  TransientSlice constants;Require(resources.MapTransient(uint64_t(count)*ContractConstantBytes,constants,e),e);
  for(uint32_t i=0;i<count;++i)std::memcpy(constants.data+size_t(i)*ContractConstantBytes,&patterns[i%2],ContractConstantBytes);
  Require(resources.FlushTransient(constants,e),e);Require(heap.BufferRange(constants.chunk,constants.offset,constants.size,serial,constant_slot,e),e);
  std::array<std::array<uint32_t,4>,224*2> attributes{};attributes[223]={0,0,0,0x2c23a5};attributes[224+223]={0,0,0,0x2c235f};
  Require(resources.UploadBuffer(5,std::as_bytes(std::span(attributes)),1,e),e);Require(heap.Buffer(resources.Buffer(5,e),serial,layout_slot,e),e);
  std::vector<std::array<uint32_t,4>> streams(count),records(count+1);
  records[0]={constant_slot,layout_slot,stream_slot,0};
  for(uint32_t i=0;i<count;++i){records[i+1]={i%2*224*16,i*16,i*ContractConstantBytes,0};streams[i]={vertex_slots[i%2],0,i%2?4u:8u,0};}
  Require(resources.UploadBuffer(6,std::as_bytes(std::span(streams)),1,e),e);Require(heap.Buffer(resources.Buffer(6,e),serial,stream_slot,e),e);
  records[0][2]=stream_slot;
  Require(resources.UploadBuffer(1,std::as_bytes(std::span(records)),1,e),e);Require(heap.Buffer(resources.Buffer(1,e),serial,table_slot,e),e);
  auto before=heap.Writes();uint32_t duplicate=UINT32_MAX;Require(heap.Buffer(resources.Buffer(1,e),serial,duplicate,e),e);
  if(duplicate!=table_slot || heap.Writes()!=before)throw std::runtime_error("Heap repeated binding wrote a descriptor");
  resources.FinishUploads();
  guest::DrawPacket draw;draw.colors[0]={601,false,{16,16,0,0,0,1}};draw.registers[0x104]=15;
  guest::ClearPacket clear{};clear.colors=draw.colors;clear.flags=1;clear.color={0,0,0,1};Require(targets.Clear(clear,e),e);
  auto plan=PlanAttachments(draw,e);Require(e.message.empty(),e);auto pass=targets.PreparePass(plan,e);Require(bool(pass),e);
  auto shader=[&](const char* path,VkShaderModule& module) {std::vector<uint32_t> words;Require(ReadSpirv(path,words,e),e);VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=words.size()*4;info.pCode=words.data();Require(c.f.vkCreateShaderModule(c.device,&info,nullptr,&module),"Batch probe shader");};
  std::string prefix=std::string(SR_GLOBAL_SHADER_DIR)+"/global";
  const char* pixel_suffix[]{"_ps.spv","_sample2d_ps.spv","_sample3d_ps.spv","_samplecube_ps.spv"};
  shader((prefix+"_vs.spv").c_str(),handles.vs);shader((prefix+pixel_suffix[texture_mode]).c_str(),handles.ps);
  VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,8};VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&global_layout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&range;Require(c.f.vkCreatePipelineLayout(c.device,&layout,nullptr,&handles.layout),"Batch probe pipeline layout");
  VkPipelineShaderStageCreateInfo stages[2]{};stages[0]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=handles.vs;stages[0].pName="VSMain";stages[1]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=handles.ps;stages[1].pName="PSMain";
  VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0,0,16,16,0,1};VkRect2D scissor{{0,0},{16,16}};VkPipelineViewportStateCreateInfo viewport_info{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport_info.viewportCount=1;viewport_info.pViewports=&viewport;viewport_info.scissorCount=1;viewport_info.pScissors=&scissor;
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
  VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blending.attachmentCount=1;blending.pAttachments=&blend;
  VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pipeline.stageCount=2;pipeline.pStages=stages;pipeline.pVertexInputState=&vertex;pipeline.pInputAssemblyState=&assembly;pipeline.pViewportState=&viewport_info;pipeline.pRasterizationState=&raster;pipeline.pMultisampleState=&samples;pipeline.pColorBlendState=&blending;pipeline.layout=handles.layout;pipeline.renderPass=pass->render_pass;Require(c.f.vkCreateGraphicsPipelines(c.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&handles.pipeline),"Batch probe graphics pipeline");
  const uint32_t indices[]{0,1,2,3,4,5};
  std::vector<VkDrawIndirectCommand> commands(count,VkDrawIndirectCommand{3,1,1,0});
  std::vector<VkDrawIndexedIndirectCommand> index_commands(count);
  for(uint32_t i=0;i<count;++i)index_commands[i]={3,1,i%2?3u:0u,i%2?-2:1,0};
  VkDeviceSize command_bytes=uint64_t(count)*(indexed?sizeof(VkDrawIndexedIndirectCommand):sizeof(VkDrawIndirectCommand));
  VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=command_bytes+(indexed?sizeof(indices):0);buffer.usage=VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT;buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;Require(c.f.vkCreateBuffer(c.device,&buffer,nullptr,&handles.indirect),"Batch indirect buffer");
  VkMemoryRequirements memory_requirements{};c.f.vkGetBufferMemoryRequirements(c.device,handles.indirect,&memory_requirements);std::vector<VkMemoryPropertyFlags> memory_flags;for(uint32_t i=0;i<c.memory.memoryTypeCount;++i)memory_flags.push_back(c.memory.memoryTypes[i].propertyFlags);
  auto type=ChooseMemoryType(memory_requirements.memoryTypeBits,memory_flags,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0);if(!type)throw std::runtime_error("Coherent indirect memory unavailable");
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=memory_requirements.size;allocation.memoryTypeIndex=*type;Require(c.f.vkAllocateMemory(c.device,&allocation,nullptr,&handles.memory),"Batch indirect allocation");Require(c.f.vkBindBufferMemory(c.device,handles.indirect,handles.memory,0),"Batch indirect bind");void* mapped=nullptr;Require(c.f.vkMapMemory(c.device,handles.memory,0,VK_WHOLE_SIZE,0,&mapped),"Batch indirect map");std::memcpy(mapped,indexed?static_cast<const void*>(index_commands.data()):static_cast<const void*>(commands.data()),command_bytes);if(indexed)std::memcpy(static_cast<std::byte*>(mapped)+command_bytes,indices,sizeof(indices));c.f.vkUnmapMemory(c.device,handles.memory);
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass->render_pass;begin.framebuffer=pass->framebuffer;begin.renderArea={{0,0},pass->extent};c.f.vkCmdBeginRenderPass(fixture.command,&begin,VK_SUBPASS_CONTENTS_INLINE);c.f.vkCmdBindPipeline(fixture.command,VK_PIPELINE_BIND_POINT_GRAPHICS,handles.pipeline);c.f.vkCmdBindDescriptorSets(fixture.command,VK_PIPELINE_BIND_POINT_GRAPHICS,handles.layout,0,1,&global_set,0,nullptr);
  auto indirect=reinterpret_cast<PFN_vkCmdDrawIndirect>(c.f.vkGetDeviceProcAddr(c.device,"vkCmdDrawIndirect"));if(!indirect)throw std::runtime_error("Indirect draw entry point unavailable");
  auto indirect_indexed=reinterpret_cast<PFN_vkCmdDrawIndexedIndirect>(c.f.vkGetDeviceProcAddr(c.device,"vkCmdDrawIndexedIndirect"));if(!indirect_indexed)throw std::runtime_error("Indexed indirect unavailable");
  if(indexed)c.f.vkCmdBindIndexBuffer(fixture.command,handles.indirect,command_bytes,VK_INDEX_TYPE_UINT32);
  auto start=std::chrono::steady_clock::now();
  if(batched) {uint32_t base[]{0,table_slot};c.f.vkCmdPushConstants(fixture.command,handles.layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,8,base);if(indexed)indirect_indexed(fixture.command,handles.indirect,0,count,sizeof(VkDrawIndexedIndirectCommand));else indirect(fixture.command,handles.indirect,0,count,sizeof(VkDrawIndirectCommand));}
  else for(uint32_t i=0;i<count;++i) {uint32_t push[]{i,table_slot};c.f.vkCmdPushConstants(fixture.command,handles.layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,8,push);if(indexed)c.f.vkCmdDrawIndexed(fixture.command,3,1,i%2?3:0,i%2?-2:1,0);else c.f.vkCmdDraw(fixture.command,3,1,1,0);}
  const double issue_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
  c.f.vkCmdEndRenderPass(fixture.command);targets.MarkWritten(*pass);auto target=targets.Get(targets.Acquire(draw.colors[0],e),e);Require(bool(target),e);Require(state.Transition(fixture.command,target->image,{target->aspects,0,1,0,1},ImageUsage::TransferSource(),e),e);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};c.f.vkCmdCopyImageToBuffer(fixture.command,target->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  frame->cpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpu_begin).count();frame->issue_us=issue_us;frame->format=target->format;frame->variant=variant;frame->draws=count;frame->indexed=indexed;frame->direct=direct;frame->batched=batched;
  return frame;
}
#include "legacy_contract_fixture.inc"
void RunHeapProof(Context& c,GlobalDescriptorHeap& heap){
  uint64_t serial=0;
  for(uint32_t variant=0;variant<3;++variant)for(uint32_t draws:{2u,3000u})for(bool indexed:{false,true}){
    auto frame=PrepareHeapProbe(c,heap,++serial,draws,variant,indexed);frame->Submit();auto pixels=frame->Complete();
    auto legacy=RunLegacyContractProbe(c,draws,variant,indexed);
    if(pixels!=legacy)throw std::runtime_error("Production heap versus legacy pixels mismatch");
    auto recorded=RunLegacyContractProbe(c,draws,variant,indexed,true);
    if(recorded!=legacy)throw std::runtime_error("Global GameRenderer versus legacy pixels mismatch");
    heap.Retire(serial);
  }
  for(uint32_t mode=1;mode<=3;++mode)for(bool indexed:{false,true}) {
    auto frame=PrepareHeapProbe(c,heap,++serial,3000,0,indexed,0,{},false,mode);frame->Submit();frame->Complete();heap.Retire(serial);
    std::cout<<"production_heap_sample_pixels=PASS mode="<<mode<<" indexed="<<indexed<<char(10);
  }
  auto gate=std::make_shared<ContractHostGate>(c);
  for(bool indexed:{false,true}) {
    auto reference=RunLegacyContractProbe(c,258,0,indexed,false,true,true);
    auto actual=RunLegacyContractProbe(c,258,0,indexed,true,true,true);
    if(actual!=reference)throw std::runtime_error("Global same-ID version replacement versus legacy pixels mismatch");
  }
  for(bool indexed:{false,true}) {
    auto reference=RunLegacyContractProbe(c,3000,0,indexed,false,true);
    auto actual=RunLegacyContractProbe(c,3000,0,indexed,true,true);
    if(actual!=reference)throw std::runtime_error("Global IA GameRenderer versus legacy pixels mismatch");
  }
  auto first=PrepareHeapProbe(c,heap,++serial,3000,0,true,1,gate,true);first->Submit();auto previous_writes=heap.Writes();
  auto second=PrepareHeapProbe(c,heap,++serial,3000,1,true,2,gate);second->Submit();
  auto status=reinterpret_cast<PFN_vkGetFenceStatus>(c.f.vkGetDeviceProcAddr(c.device,"vkGetFenceStatus"));
  if(status(c.device,first->fixture.fence)!=VK_NOT_READY || status(c.device,second->fixture.fence)!=VK_NOT_READY || heap.Writes()<=previous_writes)
    throw std::runtime_error("Production heap pending submission proof incomplete");
  Require(gate->Release(),"Release production heap GPU gate");first->Complete();second->Complete();heap.Retire(serial);
  std::cout<<"production_heap_pixels=PASS repeated_binding_no_write=PASS two_pending_versions=PASS descriptor_writes="<<heap.Writes()<<char(10);
}
