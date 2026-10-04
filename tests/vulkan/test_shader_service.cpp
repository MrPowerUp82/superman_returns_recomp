#include "../native/test_main.h"
#include "../..//port/src/graphics/shaders/vulkan_shader_service.h"
#include <cstring>
#include <fstream>
#include <thread>
#include <atomic>
using namespace superman_returns::graphics::shaders;
namespace {
void Word(std::vector<uint8_t>& v,uint32_t n) {auto p=reinterpret_cast<uint8_t*>(&n);v.insert(v.end(),p,p+4);}
std::vector<uint8_t> Wire() {
  std::vector<uint8_t> v;for(uint32_t n:{0x33525653u,1u,1u,0u}) Word(v,n);
  for(uint32_t i=0;i<12;++i) Word(v,0); // requirements
  Word(v,0);Word(v,0);Word(v,20);
  for(uint32_t n:{0x07230203u,0x10300u,0u,1u,0u}) Word(v,n);
  return v;
}
}
SR_TEST(shader_service_rejects_empty_truncated_and_wrong_stage_results) {
  CompiledShader shader;std::string error;
  SR_CHECK(!DecodeShaderResult({},ShaderStage::kVertex,shader,error));
  auto w=Wire();SR_CHECK(DecodeShaderResult(w,ShaderStage::kVertex,shader,error));
  SR_CHECK(!DecodeShaderResult(w,ShaderStage::kPixel,shader,error));
  w.resize(w.size()-1);SR_CHECK(!DecodeShaderResult(w,ShaderStage::kVertex,shader,error));
  w=Wire();w[76]=0;SR_CHECK(!DecodeShaderResult(w,ShaderStage::kVertex,shader,error));
}
SR_TEST(shader_pair_rejects_mismatched_locations_and_types) {
  CompiledShader vs,ps;vs.stage=ShaderStage::kVertex;ps.stage=ShaderStage::kPixel;
  vs.outputs={{3,"float32x4"}};ps.inputs={{3,"float32x4"}};
  SR_CHECK(ValidateShaderPair(vs,ps).empty());
  ps.inputs[0].location=4;SR_CHECK(!ValidateShaderPair(vs,ps).empty());
  ps.inputs[0]={3,"uint32x4"};SR_CHECK(!ValidateShaderPair(vs,ps).empty());
}
SR_TEST(shader_service_owns_source_deduplicates_and_reports_process_failure) {
  VulkanShaderConfig config;config.cache=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::atomic<uint32_t> calls=0;std::vector<uint8_t> container{1,2,3};ShaderKey key=0;
  {
    VulkanShaderService service(config,[&](auto args,auto,std::stop_token,std::string&) {
      ++calls;std::ifstream raw(args[3],std::ios::binary);std::vector<uint8_t> source((std::istreambuf_iterator<char>(raw)),{});SR_CHECK((source==std::vector<uint8_t>{1,2,3}));
      auto bytes=Wire();std::ofstream result(args.back(),std::ios::binary);result.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());return true;
    });
    key=service.Request(container,ShaderStage::kVertex);SR_CHECK_EQ(key,service.Request(container,ShaderStage::kVertex));container.assign(3,99);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);ShaderResult result;
    do {result=service.Poll(key);if(result.status!=ShaderPoll::pending) break;std::this_thread::sleep_for(std::chrono::milliseconds(1));} while(std::chrono::steady_clock::now()<deadline);
    SR_CHECK(result.status==ShaderPoll::ready);SR_CHECK(bool(result.artifact));SR_CHECK_EQ(calls.load(),1u);
    SR_CHECK(service.Poll(0).status==ShaderPoll::failed);
  }
  auto job=config.cache/"runtime_requests"/std::to_string(key);std::filesystem::remove(job/"shader.vs.bin");std::filesystem::remove(job/"result.bin");std::filesystem::remove(job);std::filesystem::remove(config.cache/"runtime_requests");std::filesystem::remove(config.cache);
}
