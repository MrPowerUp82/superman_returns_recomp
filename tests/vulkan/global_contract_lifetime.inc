// Fixture ownership wrapper. The gate is released before member destructors
// can wait for device idle, including exception paths during preparation.
struct ContractHostGate {
  Context& c;VkSemaphore semaphore{};PFN_vkSignalSemaphoreKHR signal{};bool released=false;
  explicit ContractHostGate(Context& context):c(context){
    auto proc=[&](const char* name){return c.f.vkGetDeviceProcAddr(c.device,name);};
    signal=reinterpret_cast<PFN_vkSignalSemaphoreKHR>(proc("vkSignalSemaphoreKHR"));
    if(!signal)throw std::runtime_error("Timeline host signal entry point missing");
    VkSemaphoreTypeCreateInfo timeline{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};timeline.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;timeline.initialValue=0;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};info.pNext=&timeline;Require(c.f.vkCreateSemaphore(c.device,&info,nullptr,&semaphore),"Host timeline gate create");
  }
  VkResult Release() noexcept {if(released)return VK_SUCCESS;VkSemaphoreSignalInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};info.semaphore=semaphore;info.value=1;auto result=signal(c.device,&info);released=result==VK_SUCCESS;return result;}
  ~ContractHostGate(){if(semaphore){Release();c.f.vkDeviceWaitIdle(c.device);c.f.vkDestroySemaphore(c.device,semaphore,nullptr);}}
};
struct PreparedContractFrame {
  GlobalDescriptorHeap* heap=nullptr;uint64_t heap_serial=0;
  std::shared_ptr<ContractHostGate> gate;
  UploadFixture fixture;ImageState state;TargetStore targets;ResourceStore resources;BatchProbeHandles handles;
  VkFormat format{};uint32_t variant{},draws{};bool indexed{},direct{},batched{},wait_gate{};double cpu_ms{},issue_us{};
  explicit PreparedContractFrame(Context& c,std::shared_ptr<ContractHostGate> release):gate(std::move(release)),fixture(c),state(c.f),targets(c,state),resources(c),handles{c}{}
  ~PreparedContractFrame(){if(gate)gate->Release();if(heap){fixture.c.f.vkDeviceWaitIdle(fixture.c.device);heap->Retire(heap_serial);}}
  void Submit(){auto& c=fixture.c;
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=fixture.readback;barrier.size=VK_WHOLE_SIZE;
    c.f.vkCmdPipelineBarrier(fixture.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
    Require(c.f.vkEndCommandBuffer(fixture.command),"End prepared frame");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&fixture.command;
    uint64_t value=1;VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    if(wait_gate){timeline.waitSemaphoreValueCount=1;timeline.pWaitSemaphoreValues=&value;submit.pNext=&timeline;submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&gate->semaphore;submit.pWaitDstStageMask=&stage;}
    Require(c.f.vkQueueSubmit(c.graphics_queue,1,&submit,fixture.fence),"Submit prepared frame");
  }
  std::vector<uint8_t> Complete(){auto& c=fixture.c;Require(c.f.vkWaitForFences(c.device,1,&fixture.fence,VK_TRUE,10'000'000'000ull),"Wait prepared frame");
    void* mapped=nullptr;Require(c.f.vkMapMemory(c.device,fixture.memory,0,VK_WHOLE_SIZE,0,&mapped),"Prepared frame readback map");
    if(!fixture.coherent){VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=fixture.memory;range.size=VK_WHOLE_SIZE;Require(c.f.vkInvalidateMappedMemoryRanges(c.device,1,&range),"Invalidate prepared readback");}
    std::vector<uint8_t> pixels(16*16*4);std::memcpy(pixels.data(),mapped,pixels.size());c.f.vkUnmapMemory(c.device,fixture.memory);resources.Retire(1);targets.Retire(1);CheckContractPixels(pixels,format,variant);
    std::cout<<"contract_batch_probe variant="<<variant<<" mode="<<(direct?"direct_push":(batched?"indirect":"per_draw"))<<" draws="<<draws<<" indexed="<<indexed<<" resource_switch_and_order_pixels=PASS issue_cpu_us="<<issue_us<<" cpu_prepare_record_ms="<<cpu_ms<<" constants_bytes="<<ContractConstantBytes<<" stream_bytes=16\n";
    return pixels;
  }
};
