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
      ++calls;SR_CHECK(args[1]=="-B");std::ifstream raw(args[4],std::ios::binary);std::vector<uint8_t> source((std::istreambuf_iterator<char>(raw)),{});SR_CHECK((source==std::vector<uint8_t>{1,2,3}));
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
SR_TEST(shader_service_retries_a_process_that_dies_without_a_result) {
  VulkanShaderConfig config;config.cache=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::atomic<uint32_t> calls=0;
  {
    VulkanShaderService service(config,[&](auto args,auto,std::stop_token,std::string& error) {
      if(++calls==1) {error="Shader compiler exited with code 1";return false;}  // transient death, no result
      auto bytes=Wire();std::ofstream result(args.back(),std::ios::binary);result.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());return true;
    });
    uint8_t a=7;auto key=service.Request({&a,1},ShaderStage::kVertex);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);ShaderResult result;
    do {result=service.Poll(key);if(result.status!=ShaderPoll::pending) break;std::this_thread::sleep_for(std::chrono::milliseconds(1));} while(std::chrono::steady_clock::now()<deadline);
    SR_CHECK(result.status==ShaderPoll::ready);SR_CHECK_EQ(calls.load(),2u);
  }
  std::filesystem::remove_all(config.cache); // Unique temporary test directory.
}
SR_TEST(shader_service_compiles_distinct_requests_concurrently_when_configured) {
  VulkanShaderConfig config;config.cache=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  config.compiler_workers=2;
  std::atomic<uint32_t> entered=0;std::atomic<bool> release=false;
  {
    VulkanShaderService service(config,[&](auto args,auto,std::stop_token token,std::string&) {
      ++entered;
      while(!release && !token.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      auto bytes=Wire();std::ofstream file(args.back(),std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());return true;
    });
    uint8_t a=1,b=2;auto first=service.Request({&a,1},ShaderStage::kVertex),second=service.Request({&b,1},ShaderStage::kVertex);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(entered<2 && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool concurrent=entered==2;release=true;SR_CHECK(concurrent);
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while((service.Poll(first).status==ShaderPoll::pending || service.Poll(second).status==ShaderPoll::pending) && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    SR_CHECK(service.Poll(first).status==ShaderPoll::ready);SR_CHECK(service.Poll(second).status==ShaderPoll::ready);
  }
  std::filesystem::remove_all(config.cache); // Unique temporary test directory.
}
SR_TEST(shader_service_cancels_all_inflight_workers_on_shutdown) {
  VulkanShaderConfig config;config.cache=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());config.compiler_workers=2;
  std::atomic<uint32_t> entered=0,cancelled=0;
  {
    VulkanShaderService service(config,[&](auto,auto,std::stop_token token,std::string&) {
      ++entered;
      while(!token.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      ++cancelled;return false;
    });
    uint8_t a=1,b=2;service.Request({&a,1},ShaderStage::kVertex);service.Request({&b,1},ShaderStage::kVertex);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(entered<2 && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    SR_CHECK_EQ(entered.load(),2u);
  }
  SR_CHECK_EQ(cancelled.load(),2u);
  std::filesystem::remove_all(config.cache);
}
namespace {
std::vector<uint8_t> Library(std::vector<uint8_t> body,uint32_t count=1) {
  std::vector<uint8_t> bytes{'S','R','V','K','L','I','B',0};Word(bytes,1);Word(bytes,count);
  uint64_t checksum=14695981039346656037ull;for(auto b:body) {checksum^=b;checksum*=1099511628211ull;}
  Word(bytes,uint32_t(checksum));Word(bytes,uint32_t(checksum>>32));bytes.insert(bytes.end(),body.begin(),body.end());return bytes;
}
std::vector<uint8_t> LibraryBody(ShaderStage stage,const std::vector<uint8_t>& container,const std::vector<uint8_t>& result) {
  std::vector<uint8_t> body;Word(body,uint32_t(stage));Word(body,uint32_t(container.size()));Word(body,uint32_t(result.size()));Word(body,0);
  body.insert(body.end(),container.begin(),container.end());body.insert(body.end(),result.begin(),result.end());return body;
}
void WriteLibrary(const std::filesystem::path& path,const std::vector<uint8_t>& data) {
  std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),data.size());
}
}
SR_TEST(shader_library_returns_ready_without_a_compiler_and_misses_use_runtime) {
  auto directory=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directory(directory);
  VulkanShaderConfig config;config.cache=directory/"cache";config.library=directory/"shaders.srvk";
  std::vector<uint8_t> original(24,7);auto bytes=Library(LibraryBody(ShaderStage::kVertex,original,Wire()));WriteLibrary(config.library,bytes);
  std::atomic<uint32_t> calls=0;
  {
    VulkanShaderService service(config,[&](auto args,auto,std::stop_token,std::string&) {
      ++calls;auto wire=Wire();std::ofstream result(args.back(),std::ios::binary);result.write(reinterpret_cast<const char*>(wire.data()),wire.size());return true;
    });
    SR_CHECK_EQ(service.PrecompiledCount(),1u);SR_CHECK(service.LibraryDiagnostic().empty());
    auto key=service.Request(original,ShaderStage::kVertex);auto result=service.Poll(key);
    SR_CHECK(result.status==ShaderPoll::ready);SR_CHECK(bool(result.artifact));SR_CHECK_EQ(calls.load(),0u);
    SR_CHECK_EQ(key,service.Request(original,ShaderStage::kVertex));original.back()=8;
    auto miss=service.Request(original,ShaderStage::kVertex);SR_CHECK(miss!=key);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(service.Poll(miss).status==ShaderPoll::pending && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    SR_CHECK(service.Poll(miss).status==ShaderPoll::ready);SR_CHECK_EQ(calls.load(),1u);
  }
  std::filesystem::remove_all(directory);
}
SR_TEST(shader_library_rejects_corruption_wrong_stage_duplicates_and_partial_loads) {
  auto directory=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directory(directory);
  VulkanShaderConfig config;config.cache=directory;config.library=directory/"shaders.srvk";
  std::vector<uint8_t> original(24,7);auto body=LibraryBody(ShaderStage::kVertex,original,Wire());auto valid=Library(body);
  std::vector<std::vector<uint8_t>> invalid;
  auto truncated=valid;truncated.pop_back();invalid.push_back(truncated);
  auto checksum=valid;checksum[16]^=1;invalid.push_back(checksum);
  auto version=valid;version[8]=2;invalid.push_back(version);
  auto trailing=body;trailing.push_back(0);invalid.push_back(Library(trailing));
  auto duplicated=body;duplicated.insert(duplicated.end(),body.begin(),body.end());invalid.push_back(Library(duplicated,2));
  auto wrong=LibraryBody(ShaderStage::kPixel,original,Wire());auto partial=body;partial.insert(partial.end(),wrong.begin(),wrong.end());invalid.push_back(Library(partial,2));
  for(const auto& data:invalid) {
    WriteLibrary(config.library,data);VulkanShaderService service(config,{});
    SR_CHECK_EQ(service.PrecompiledCount(),0u);SR_CHECK(!service.LibraryDiagnostic().empty());
  }
  std::filesystem::remove_all(directory);
}
