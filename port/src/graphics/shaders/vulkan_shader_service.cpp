#include "vulkan_shader_service.h"
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace superman_returns::graphics::shaders {
namespace {
class Reader {
  std::span<const uint8_t> bytes_;size_t position_=0;
public:
  explicit Reader(std::span<const uint8_t> b):bytes_(b) {}
  std::span<const uint8_t> Bytes(size_t n) {if(n>bytes_.size()-position_) throw std::runtime_error("Truncated shader result");auto r=bytes_.subspan(position_,n);position_+=n;return r;}
  uint32_t Word() {uint32_t n;auto b=Bytes(4);std::memcpy(&n,b.data(),4);return n;}
  std::string Text() {auto n=Word();if(n>16384) throw std::runtime_error("Oversized shader diagnostic/type");auto b=Bytes(n);return {reinterpret_cast<const char*>(b.data()),b.size()};}
  bool Done() const {return position_==bytes_.size();}
};
std::vector<uint8_t> ReadFile(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary|std::ios::ate);auto size=f.tellg();
  if(!f || size<0 || size>17*1024*1024) throw std::runtime_error("Missing/oversized shader result");
  std::vector<uint8_t> bytes(static_cast<size_t>(size));f.seekg(0);if(!f.read(reinterpret_cast<char*>(bytes.data()),size)) throw std::runtime_error("Shader result read failed");return bytes;
}
}
bool DecodeShaderResult(std::span<const uint8_t> bytes,ShaderStage stage,CompiledShader& result,std::string& error) {
  try {
    Reader r(bytes);if(r.Word()!=0x33525653 || r.Word()!=1) throw std::runtime_error("Shader result schema/ABI mismatch");
    uint32_t status=r.Word();if(r.Word()!=uint32_t(stage)) throw std::runtime_error("Shader result stage mismatch");
    if(status==0) throw std::runtime_error(r.Text());if(status!=1) throw std::runtime_error("Invalid shader status");
    CompiledShader out;out.stage=stage;auto& q=out.requirements;
    q.storage_buffers=r.Word();q.uniform_buffers=r.Word();q.sampled_images=r.Word();q.samplers=r.Word();q.descriptor_sets=r.Word();q.vertex_attributes=r.Word();q.vertex_output_components=r.Word();q.fragment_input_components=r.Word();
    auto feature=[&]() {auto v=r.Word();if(v>1) throw std::runtime_error("Invalid shader feature flag");return bool(v);};
    q.sampled_image_dynamic_indexing=feature();q.storage_buffer_dynamic_indexing=feature();q.clip_distance=feature();q.cull_distance=feature();
    for(auto* locations:{&out.inputs,&out.outputs}) {
      auto count=r.Word();if(count>256) throw std::runtime_error("Too many shader locations");
      for(uint32_t i=0;i<count;++i) {ShaderLocation l{r.Word(),r.Text()};if(l.location>255 || l.type.empty() || std::any_of(locations->begin(),locations->end(),[&](auto& v){return l.location==v.location;})) throw std::runtime_error("Invalid/duplicate shader location");locations->push_back(std::move(l));}
    }
    uint32_t size=r.Word();if(size<20 || size%4 || size>16*1024*1024) throw std::runtime_error("Empty/invalid compiled shader");
    auto data=r.Bytes(size);out.words.resize(size/4);std::memcpy(out.words.data(),data.data(),size);
    if(!r.Done() || out.words[0]!=0x07230203 || out.words[1]<0x10000 || out.words[1]>0x10300 || !out.words[3] || out.words[4]) throw std::runtime_error("Compiled shader header/trailing bytes invalid");
    result=std::move(out);error.clear();return true;
  } catch(const std::exception& e) {error=e.what();return false;}
}
std::vector<std::string> ValidateShaderPair(const CompiledShader& vs,const CompiledShader& ps) {
  std::vector<std::string> errors;if(vs.stage!=ShaderStage::kVertex || ps.stage!=ShaderStage::kPixel) errors.push_back("Shader pair stages invalid");
  for(auto& input:ps.inputs) if(std::none_of(vs.outputs.begin(),vs.outputs.end(),[&](auto& output){return input.location==output.location && input.type==output.type;})) errors.push_back("VS/PS location/type mismatch "+std::to_string(input.location));
  return errors;
}
struct VulkanShaderService::Impl {
  struct Job {ShaderStage stage;std::vector<uint8_t> container;ShaderResult result;};
  VulkanShaderConfig config;ShaderProcess process;mutable std::mutex mutex;std::condition_variable cv;std::map<ShaderKey,Job> jobs;std::deque<ShaderKey> queue;bool stop=false;std::vector<std::jthread> threads;
  Impl(VulkanShaderConfig c,ShaderProcess p):config(std::move(c)),process(std::move(p)) {
    for(uint32_t i=0;i<std::clamp(config.compiler_workers,1u,4u);++i) threads.emplace_back([this](std::stop_token token){Run(token);});
  }
  ~Impl() {{std::lock_guard lock(mutex);stop=true;}for(auto& thread:threads) thread.request_stop();cv.notify_all();for(auto& thread:threads) thread.join();}
  void Run(std::stop_token token) {
    std::stop_callback wake(token,[this] {cv.notify_all();});
    for(;;) {
      ShaderKey key;ShaderStage stage;std::vector<uint8_t> container;
      {std::unique_lock lock(mutex);cv.wait(lock,[&]{return stop || token.stop_requested() || !queue.empty();});if(stop || token.stop_requested()) return;key=queue.front();queue.pop_front();auto& j=jobs.at(key);stage=j.stage;container=j.container;}
      ShaderResult result;result.status=ShaderPoll::failed;
      try {
        auto directory=config.cache/"runtime_requests"/std::to_string(key);std::filesystem::create_directories(directory);
        auto raw=directory/(stage==ShaderStage::kVertex?"shader.vs.bin":"shader.ps.bin"),output=directory/"result.bin";
        {std::ofstream file(raw,std::ios::binary|std::ios::trunc);if(!file.write(reinterpret_cast<const char*>(container.data()),container.size())) throw std::runtime_error("Cannot write captured shader container");}
        std::error_code ignored;std::filesystem::remove(output,ignored);
        std::vector<std::filesystem::path> args{config.python,config.script,"--container",raw,"--stage",stage==ShaderStage::kVertex?"vs":"ps","--cache",config.cache,"--emitter",config.emitter,"--common",config.common,"--dxc",config.dxc,"--result",output};
        if(!process || !process(args,config.timeout,token,result.diagnostic)) {if(result.diagnostic.empty()) result.diagnostic="Shader compiler process failed";}
        else {auto artifact=std::make_shared<CompiledShader>();if(DecodeShaderResult(ReadFile(output),stage,*artifact,result.diagnostic)) {result.status=ShaderPoll::ready;result.artifact=std::move(artifact);}}
      } catch(const std::exception& e) {result.diagnostic=e.what();}
      {std::lock_guard lock(mutex);jobs.at(key).result=std::move(result);}
    }
  }
};
VulkanShaderService::VulkanShaderService(VulkanShaderConfig c,ShaderProcess p):impl_(std::make_unique<Impl>(std::move(c),std::move(p))) {}
VulkanShaderService::~VulkanShaderService()=default;
ShaderKey VulkanShaderService::Request(std::span<const uint8_t> bytes,ShaderStage stage) {
  if(bytes.empty() || bytes.size()>16*1024*1024) return 0;
  uint64_t hash=14695981039346656037ull;for(auto b:bytes) {hash^=b;hash*=1099511628211ull;}hash^=uint32_t(stage);hash*=1099511628211ull;if(!hash) hash=1;
  std::lock_guard lock(impl_->mutex);
  // A hash collision never aliases a different container or stage.
  for(;;) {
    auto found=impl_->jobs.find(hash);if(found==impl_->jobs.end()) break;
    if(found->second.stage==stage && std::equal(bytes.begin(),bytes.end(),found->second.container.begin(),found->second.container.end())) return hash;
    if(!++hash) hash=1;
  }
  impl_->jobs.emplace(hash,Impl::Job{stage,{bytes.begin(),bytes.end()},{}});impl_->queue.push_back(hash);impl_->cv.notify_one();return hash;
}
ShaderResult VulkanShaderService::Poll(ShaderKey key) const {
  std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(key);if(found==impl_->jobs.end()) return {ShaderPoll::failed,{},"Unknown/empty captured shader"};return found->second.result;
}
} // namespace superman_returns::graphics::shaders
