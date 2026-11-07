#include "vulkan_shader_service.h"
#include "../vulkan/vulkan_cache_audit.h"
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
std::vector<uint8_t> ReadFile(const std::filesystem::path& p,size_t maximum=17*1024*1024) {
  std::ifstream f(p,std::ios::binary|std::ios::ate);auto size=f.tellg();
  if(!f || size<0 || uint64_t(size)>maximum) throw std::runtime_error("Missing/oversized shader file");
  std::vector<uint8_t> bytes(static_cast<size_t>(size));f.seekg(0);if(!f.read(reinterpret_cast<char*>(bytes.data()),size)) throw std::runtime_error("Shader result read failed");return bytes;
}
uint64_t HashBytes(std::span<const uint8_t> bytes) {
  uint64_t hash=14695981039346656037ull;for(auto b:bytes) {hash^=b;hash*=1099511628211ull;}return hash;
}
ShaderKey RequestHash(std::span<const uint8_t> bytes,ShaderStage stage) {
  auto hash=(HashBytes(bytes)^uint32_t(stage))*1099511628211ull;return hash?hash:1;
}
}
bool DecodeShaderResult(std::span<const uint8_t> bytes,ShaderStage stage,CompiledShader& result,std::string& error,ShaderAbi expected) {
  try {
    Reader r(bytes);if((expected!=ShaderAbi::kPortable && expected!=ShaderAbi::kGlobal) || r.Word()!=0x33525653 || r.Word()!=uint32_t(expected)) throw std::runtime_error("Shader result schema/ABI mismatch");
    uint32_t status=r.Word();if(r.Word()!=uint32_t(stage)) throw std::runtime_error("Shader result stage mismatch");
    if(status==0) throw std::runtime_error(r.Text());if(status!=1) throw std::runtime_error("Invalid shader status");
    CompiledShader out;out.stage=stage;out.abi=expected;auto& q=out.requirements;
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
  struct Job {ShaderStage stage;std::vector<uint8_t> container;ShaderResult result;bool from_library=false;};
  VulkanShaderConfig config;ShaderProcess process;mutable std::mutex mutex;std::condition_variable cv;std::map<ShaderKey,Job> jobs;std::deque<ShaderKey> queue;bool stop=false;std::vector<std::jthread> threads;
  size_t precompiled_count=0;std::string library_diagnostic;
  std::map<std::pair<uint64_t,ShaderStage>,std::vector<std::shared_ptr<const CompiledShader>>> ready_by_digest;
  void IndexReady(const std::shared_ptr<const CompiledShader>& artifact) {
    if(artifact)ready_by_digest[{ShaderArtifactDigest(*artifact),artifact->stage}].push_back(artifact);
  }
  Impl(VulkanShaderConfig c,ShaderProcess p):config(std::move(c)),process(std::move(p)) {
    LoadLibrary();
    for(uint32_t i=0;i<std::clamp(config.compiler_workers,1u,4u);++i) threads.emplace_back([this](std::stop_token token){Run(token);});
  }
  void LoadLibrary() {
    if(config.library.empty()) return;
    try {
      auto bytes=ReadFile(config.library,256*1024*1024);Reader r(bytes);
      auto magic=r.Bytes(8);if(std::memcmp(magic.data(),"SRVKLIB\0",8) || r.Word()!=uint32_t(config.abi)) throw std::runtime_error("Vulkan shader library schema/ABI mismatch");
      auto count=r.Word();if(!count || count>16384) throw std::runtime_error("Invalid shader library count");
      uint64_t checksum=r.Word();checksum|=uint64_t(r.Word())<<32;
      if(HashBytes(std::span<const uint8_t>(bytes).subspan(24))!=checksum) throw std::runtime_error("Shader library checksum mismatch");
      // All-or-nothing: a damaged library must never publish a partial set.
      std::map<ShaderKey,Job> loaded;
      for(uint32_t i=0;i<count;++i) {
        auto stage_word=r.Word(),container_size=r.Word(),result_size=r.Word(),reserved=r.Word();
        if(stage_word>1 || reserved || container_size<24 || container_size>1024*1024 || result_size>17*1024*1024) throw std::runtime_error("Invalid shader library entry");
        auto stage=ShaderStage(stage_word);auto container=r.Bytes(container_size);auto wire=r.Bytes(result_size);
        auto artifact=std::make_shared<CompiledShader>();std::string error;
        if(!DecodeShaderResult(wire,stage,*artifact,error,config.abi)) throw std::runtime_error(error);
        auto key=RequestHash(container,stage);
        for(;;) {
          auto found=loaded.find(key);if(found==loaded.end()) break;
          if(found->second.stage==stage && std::equal(container.begin(),container.end(),found->second.container.begin(),found->second.container.end())) throw std::runtime_error("Duplicate shader library entry");
          if(!++key) key=1;
        }
        loaded.emplace(key,Job{stage,{container.begin(),container.end()},{ShaderPoll::ready,std::move(artifact),{}},true});
      }
      if(!r.Done()) throw std::runtime_error("Trailing shader library data");
      jobs=std::move(loaded);precompiled_count=count;
      for(const auto& [key,job]:jobs)IndexReady(job.result.artifact);
    } catch(const std::exception& e) {library_diagnostic=e.what();}
  }
  ~Impl() {{std::lock_guard lock(mutex);stop=true;}for(auto& thread:threads) thread.request_stop();cv.notify_all();for(auto& thread:threads) thread.join();}
  void Run(std::stop_token token) {
    std::stop_callback wake(token,[this] {cv.notify_all();});
    for(;;) {
      ShaderKey key;ShaderStage stage;std::vector<uint8_t> container;
      {std::unique_lock lock(mutex);cv.wait(lock,[&]{return stop || token.stop_requested() || !queue.empty();});if(stop || token.stop_requested()) return;key=queue.front();queue.pop_front();auto& j=jobs.at(key);stage=j.stage;container=j.container;}
      ++vulkan::VulkanCacheAudit::shader_active_jobs;
      ShaderResult result;result.status=ShaderPoll::failed;
      try {
        auto directory=config.cache/(config.abi==ShaderAbi::kGlobal?"runtime_requests_global":"runtime_requests")/std::to_string(key);std::filesystem::create_directories(directory);
        auto raw=directory/(stage==ShaderStage::kVertex?"shader.vs.bin":"shader.ps.bin"),output=directory/"result.bin";
        {std::ofstream file(raw,std::ios::binary|std::ios::trunc);if(!file.write(reinterpret_cast<const char*>(container.data()),container.size())) throw std::runtime_error("Cannot write captured shader container");}
        std::error_code ignored;std::filesystem::remove(output,ignored);
        auto audit_path=output;audit_path+=".audit";std::filesystem::remove(audit_path,ignored);
        // -B: concurrent workers must not race writing the scripts' __pycache__.
        std::vector<std::filesystem::path> args{config.python,"-B",config.script,"--container",raw,"--stage",stage==ShaderStage::kVertex?"vs":"ps","--cache",config.cache,"--emitter",config.emitter,"--common",config.common,"--dxc",config.dxc};
        if(config.abi==ShaderAbi::kGlobal) {args.emplace_back("--profile");args.emplace_back("global");}
        args.emplace_back("--result");args.push_back(output);
        // A shader that fails to translate still writes a failure result. A
        // process that dies without one is retried: a transient fault must not
        // stop the game (seen once in a package smoke test, not reproducible).
        auto audit_attempt=[&] {
          // Supplemental text metadata preserves the SVR3 result ABI. Legacy
          // scripts/runners without this proof are unknown, never zero.
          std::ifstream audit(audit_path);uint32_t schema=0,hit=0,runs=0;std::string trailing;
          if(audit>>schema>>hit>>runs && schema==1 && hit<=1 && runs<=2 && !(hit && runs) && !(audit>>trailing)) {
            if(hit)++vulkan::VulkanCacheAudit::shader_disk_hits;
            vulkan::VulkanCacheAudit::shader_compiler_invocations+=runs;
            if(config.audit_log)config.audit_log("native cache audit: vulkan_shader_job key="+std::to_string(key)+
                " disk_hit="+std::to_string(hit)+" compiler_invocations="+std::to_string(runs));
          } else {
            ++vulkan::VulkanCacheAudit::shader_unknown_audits;
            if(config.audit_log)config.audit_log("native cache audit: vulkan_shader_job_unknown key="+std::to_string(key));
          }
        };
        bool ran=false;
        for(int attempt=0;attempt<3 && !ran && process && !token.stop_requested();++attempt) {
          std::filesystem::remove(output,ignored);std::filesystem::remove(audit_path,ignored);
          ++vulkan::VulkanCacheAudit::shader_jobs;
          result.diagnostic.clear();ran=process(args,config.timeout,token,result.diagnostic);
          if(!ran)++vulkan::VulkanCacheAudit::shader_process_failures;
          audit_attempt();
          if(!ran && std::filesystem::is_regular_file(output)) break;
        }
        if(!ran) {if(result.diagnostic.empty()) result.diagnostic="Shader compiler process failed";}
        else {auto artifact=std::make_shared<CompiledShader>();if(DecodeShaderResult(ReadFile(output),stage,*artifact,result.diagnostic,config.abi)) {result.status=ShaderPoll::ready;result.artifact=std::move(artifact);}}
      } catch(const std::exception& e) {result.diagnostic=e.what();}
      {std::lock_guard lock(mutex);if(result.status==ShaderPoll::ready)IndexReady(result.artifact);jobs.at(key).result=std::move(result);}
      --vulkan::VulkanCacheAudit::shader_active_jobs;
    }
  }
};
VulkanShaderService::VulkanShaderService(VulkanShaderConfig c,ShaderProcess p):impl_(std::make_unique<Impl>(std::move(c),std::move(p))) {}
VulkanShaderService::~VulkanShaderService()=default;
ShaderKey VulkanShaderService::Request(std::span<const uint8_t> bytes,ShaderStage stage) {
  if(bytes.empty() || bytes.size()>16*1024*1024) return 0;
  auto hash=RequestHash(bytes,stage);
  std::lock_guard lock(impl_->mutex);
  // A hash collision never aliases a different container or stage.
  for(;;) {
    auto found=impl_->jobs.find(hash);if(found==impl_->jobs.end()) break;
    if(found->second.stage==stage && std::equal(bytes.begin(),bytes.end(),found->second.container.begin(),found->second.container.end())) {
      if(found->second.from_library)++vulkan::VulkanCacheAudit::shader_library_hits;
      else ++vulkan::VulkanCacheAudit::shader_memory_hits;
      return hash;
    }
    if(!++hash) hash=1;
  }
  ++vulkan::VulkanCacheAudit::shader_misses;
  if(impl_->config.audit_log)impl_->config.audit_log("native cache audit: vulkan_shader_miss key="+std::to_string(hash));
  impl_->jobs.emplace(hash,Impl::Job{stage,{bytes.begin(),bytes.end()},{}});impl_->queue.push_back(hash);impl_->cv.notify_one();return hash;
}
ShaderResult VulkanShaderService::Poll(ShaderKey key) const {
  std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(key);
  if(found==impl_->jobs.end()) {++vulkan::VulkanCacheAudit::shader_not_ready;return {ShaderPoll::failed,{},"Unknown/empty captured shader"};}
  if(found->second.result.status!=ShaderPoll::ready)++vulkan::VulkanCacheAudit::shader_not_ready;
  return found->second.result;
}
size_t VulkanShaderService::PrecompiledCount() const {return impl_->precompiled_count;}
const std::string& VulkanShaderService::LibraryDiagnostic() const {return impl_->library_diagnostic;}
std::shared_ptr<const CompiledShader> VulkanShaderService::FindReadyByDigest(uint64_t digest,ShaderStage stage) const {
  std::lock_guard lock(impl_->mutex);std::shared_ptr<const CompiledShader> result;
  const auto found=impl_->ready_by_digest.find({digest,stage});
  if(found==impl_->ready_by_digest.end())return {};
  for(const auto& artifact:found->second) {
    if(!artifact || artifact->abi!=impl_->config.abi)continue;
    if(result && result->words!=artifact->words)return {};
    result=artifact;
  }
  return result;
}
} // namespace superman_returns::graphics::shaders
