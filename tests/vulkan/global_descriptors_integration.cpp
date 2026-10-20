// Hardware-dependent readback coverage for the production global heap/ABI.
#include "global_descriptors.h"
#define main UnusedResourceFixtureMain
#include "resources_integration.cpp"
#undef main
bool expect_binding_batches=false;
#include "global_contract_fixture.inc"
int main(int argc,char** argv) try {
  Win32Window window;Context c;Error e;
  const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
  bool performance=false,binding_bench=false,require_validation=false;
  for(int i=1;i<argc;++i) {
    auto arg=std::string_view(argv[i]);
    if(arg=="--expect-batches")expect_binding_batches=true;
    else if(arg=="--binding-bench"){performance=true;binding_bench=true;}
    else if(arg=="--require-validation")require_validation=true;
    else throw std::runtime_error("Unknown global GPU test option");
  }
  // The full correctness suite always enforces the work-reduction regression.
  // Performance-only runs can opt in while comparing preserved executables.
  if(!binding_bench)expect_binding_batches=true;
  Require(c.CreateInstance(extensions,!performance,e),e);
  if(require_validation && !c.validation_active)
    throw std::runtime_error("Requested validation layers unavailable");
  Require(window.Open(64,64,e,false),e);auto surface=window.CreateSurface(c,e);Require(bool(surface),e);
  VkPhysicalDeviceFeatures core{};core.robustBufferAccess=VK_TRUE;
  Require(c.OpenDevice(surface,"",e,&core,false,true,true),e);
  if(!c.timeline_enabled)throw std::runtime_error("Pending frame proof requires timeline semaphores");
  if(binding_bench) for(uint32_t i=0;i<5;++i)RunLegacyContractProbe(c,3000,0,true,true,true);
  else {GlobalDescriptorHeap heap(c);Require(heap.Initialize(c.descriptor_indexing_features,GlobalHeapCapacities{},e),e);RunHeapProof(c,heap);}
  std::cout<<"validation_active="<<c.validation_active<<" validation_errors="<<c.validation_errors.load()<<'\n';
  return c.validation_errors.load()?1:0;
} catch(const std::exception& error) {std::cerr<<"ERROR: "<<error.what()<<'\n';return 1;}
