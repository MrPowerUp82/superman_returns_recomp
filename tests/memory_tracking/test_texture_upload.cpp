#include "../native/test_main.h"
#include "../../port/src/native_renderer/texture_decode.h"
#include "../../port/src/native_renderer/texture_content.h"
#include "../../port/src/graphics/guest/texture_capture.h"
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
