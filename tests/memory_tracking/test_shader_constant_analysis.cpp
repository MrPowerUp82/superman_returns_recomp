#include "../native/test_main.h"
#include "../../port/src/graphics/guest/shader_constant_usage.h"
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/cvar.h>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
namespace guest=superman_returns::graphics::guest;
// Reference Shader::AnalyzeUcode normally lives in the hidden Xenos plugin.
REXCVAR_DEFINE_STRING(dump_shaders,"","Tests","Keep reference shader disassembly in memory");
namespace {
void PutConstantWord(std::vector<uint8_t>& bytes,size_t offset,uint32_t word) {
  for(uint32_t i=0;i<4;++i) bytes[offset+i]=uint8_t(word>>(24-i*8));
}
bool CheckSdkUsage(const std::vector<uint8_t>& bytes) {
  const auto usage=guest::AnalyzeShaderConstantUsage(bytes);
  if(!usage.known) return false;
  const auto be=[&](size_t offset) {return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|(uint32_t(bytes[offset+2])<<8)|bytes[offset+3];};
  const size_t header=be(24),code=be(4)+size_t(be(header)),size=be(header+4);
  std::vector<uint32_t> words(size/4);std::memcpy(words.data(),bytes.data()+code,size);
  rex::graphics::Shader shader(bytes[3]?rex::graphics::xenos::ShaderType::kVertex:rex::graphics::xenos::ShaderType::kPixel,0,words.data(),words.size(),std::endian::big);
  rex::string::StringBuffer disassembly;shader.AnalyzeUcode(disassembly);
  const auto& expected=shader.constant_register_map();
  for(size_t i=0;i<4;++i) SR_CHECK((expected.float_bitmap[i]&~usage.mask[i])==0);
  if(expected.float_dynamic_addressing) {SR_CHECK(usage.dynamic);for(auto block:usage.mask) SR_CHECK_EQ(block,UINT64_MAX);}
  return true;
}
}
SR_TEST(shader_constant_masks_cover_sdk_analysis) {
  // Paired vector/scalar operations, source3's special constant forms, static
  // endpoints and relative reads. SDK is an independent production decoder.
  std::vector<uint8_t> bytes(88);PutConstantWord(bytes,0,0x102a1100);PutConstantWord(bytes,4,64);PutConstantWord(bytes,8,24);
  PutConstantWord(bytes,24,36);PutConstantWord(bytes,40,24);PutConstantWord(bytes,64,1|(1<<12));PutConstantWord(bytes,68,2<<12);
  for(uint32_t scalar:{0u,42u,43u,44u,45u,46u,47u}) for(uint32_t addressed:{0u,1u<<30,1u<<31}) {
    PutConstantWord(bytes,76,(scalar<<26)|(15<<16)|(15<<20));
    PutConstantWord(bytes,80,addressed);PutConstantWord(bytes,84,0x000040ff);
    SR_CHECK(CheckSdkUsage(bytes));
  }
  // Optional user-owned corpus, never required for the portable/CI fixture.
  if(const char* path=std::getenv("SR_SHADER_CONSTANT_CORPUS");path && *path) {
    size_t total=0,known=0;
    for(const auto& entry:std::filesystem::directory_iterator(path)) if(entry.path().extension()==".bin") {
      std::ifstream input(entry.path(),std::ios::binary);std::vector<uint8_t> container((std::istreambuf_iterator<char>(input)),{});
      ++total;known+=CheckSdkUsage(container);
    }
    std::printf("shader constant SDK corpus: known=%zu total=%zu\n",known,total);SR_CHECK(total>0 && known>0);
  }
}
