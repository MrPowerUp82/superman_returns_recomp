#include "../native/test_main.h"
#include "../../port/src/native_renderer/texture_decode.h"
#include "../../port/src/native_renderer/texture_content.h"
#include "../../port/src/graphics/guest/texture_capture.h"
#include "../../port/src/graphics/guest/texture_storage_identity.h"
#include <rex/graphics/xenos.h>
#include <cstring>
using namespace superman_returns;
SR_TEST(proof_matches_owned_snapshot_not_live_guest) {
  rex::graphics::xenos::xe_gpu_texture_fetch_t f{};
  f.type=rex::graphics::xenos::FetchConstantType::kTexture;
  f.dimension=rex::graphics::xenos::DataDimension::k2DOrStacked;
  f.format=rex::graphics::xenos::TextureFormat::k_8_8_8_8;
  f.size_2d.width=31; f.size_2d.height=31; f.pitch=1; f.tiled=1; f.base_address=1;
  f.swizzle=0|(1<<3)|(2<<6)|(3<<9);
  std::array<uint32_t,6> fetch{}; std::memcpy(fetch.data(),&f,sizeof(f));
  std::vector<uint8_t> live(4096,7);
  std::shared_ptr<const graphics::guest::TextureCapture> capture;
  std::string error;
  SR_CHECK(graphics::guest::CaptureTexture(fetch,1,[&](uint32_t,uint32_t) {return std::span<const uint8_t>(live);},capture,error));
  std::fill(live.begin(),live.end(),9);
  native::DecodedTexture decoded;
  SR_CHECK(native::DecodeTexture(*capture,decoded));
  SR_CHECK_EQ(decoded.width,32u); SR_CHECK_EQ(decoded.data.size(),4096u);
  SR_CHECK_EQ(decoded.data[0],7u); SR_CHECK_EQ(decoded.data.back(),7u);
  SR_CHECK_EQ(capture->memory.Read(0xa0001000,4096)[0],7u);
  auto incomplete=*capture; incomplete.memory={};
  SR_CHECK(!native::DecodeTexture(incomplete,decoded));
}

SR_TEST(sampler_variants_decode_the_same_owned_pixels_and_source_ranges) {
  using namespace graphics::guest;
  rex::graphics::xenos::xe_gpu_texture_fetch_t f{};
  f.type=rex::graphics::xenos::FetchConstantType::kTexture;
  f.dimension=rex::graphics::xenos::DataDimension::k2DOrStacked;
  f.format=rex::graphics::xenos::TextureFormat::k_8_8_8_8;
  f.size_2d.width=31;f.size_2d.height=31;f.pitch=1;f.tiled=1;f.base_address=1;
  f.swizzle=0|(1<<3)|(2<<6)|(3<<9);
  std::array<uint32_t,6> fetch{};std::memcpy(fetch.data(),&f,sizeof(f));
  std::vector<uint8_t> live(4096);for(size_t i=0;i<live.size();++i) live[i]=uint8_t(i*37+i/32);
  std::shared_ptr<const TextureCapture> capture;std::string error;
  SR_CHECK(CaptureTexture(fetch,1,[&](uint32_t,uint32_t){return std::span<const uint8_t>(live);},capture,error));
  if(!capture) return;
  LinearTexture baseline;SR_CHECK(DecodeTextureLayout(fetch,capture->memory,baseline,error));
  native::DecodedTexture native_baseline;SR_CHECK(native::DecodeTexture(*capture,native_baseline));
  constexpr std::array<uint32_t,6> sampler{0x0007fc00u,0,0,0x0ff80000u,0x003ff003u,0};
  for(uint32_t word=0;word<6;++word) for(uint32_t bit=0;bit<32;++bit) if(sampler[word]&(1u<<bit)) {
    auto variant=fetch;variant[word]^=1u<<bit;
    std::vector<TextureRange> ranges;SR_CHECK(DescribeTextureRanges(variant,ranges,error));
    SR_CHECK_EQ(ranges.size(),capture->ranges.size());
    for(size_t i=0;i<ranges.size() && i<capture->ranges.size();++i) {
      SR_CHECK_EQ(ranges[i].address,capture->ranges[i].address);SR_CHECK_EQ(ranges[i].length,capture->ranges[i].length);
    }
    auto changed=*capture;changed.fetch=variant;
    LinearTexture decoded;SR_CHECK(DecodeTextureLayout(variant,changed.memory,decoded,error));
    SR_CHECK(decoded.data==baseline.data);SR_CHECK(decoded.swizzle==baseline.swizzle);
    SR_CHECK_EQ(decoded.levels.size(),baseline.levels.size());SR_CHECK_EQ(decoded.width,baseline.width);SR_CHECK_EQ(decoded.height,baseline.height);
    SR_CHECK_EQ(decoded.depth,baseline.depth);SR_CHECK_EQ(decoded.mip_levels,baseline.mip_levels);SR_CHECK(decoded.format==baseline.format);
    for(size_t i=0;i<decoded.levels.size() && i<baseline.levels.size();++i) {
      const auto &a=decoded.levels[i],&b=baseline.levels[i];
      SR_CHECK(a.width==b.width && a.height==b.height && a.row_pitch==b.row_pitch && a.rows==b.rows && a.offset==b.offset);
    }
    native::DecodedTexture native_decoded;SR_CHECK(native::DecodeTexture(changed,native_decoded));
    SR_CHECK(native_decoded.data==native_baseline.data);SR_CHECK_EQ(native_decoded.component_mapping,native_baseline.component_mapping);
    SR_CHECK(TextureStorageFetch(variant)==TextureStorageFetch(fetch));
  }
}
