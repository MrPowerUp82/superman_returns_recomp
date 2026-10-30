#include "test_main.h"
#include "../../port/src/graphics/guest/texture_storage_identity.h"
#include <array>

using namespace superman_returns::graphics::guest;

SR_TEST(texture_storage_default_keeps_vulkan_experimental) {
  const char* value=std::getenv("SR_NATIVE_TEXTURE_STORAGE_REUSE");
  if(value && value[0]) return; // Explicit user override has precedence.
  SR_CHECK(TextureStorageReuseEnabled(TextureStorageBackend::kD3D12));
  SR_CHECK(!TextureStorageReuseEnabled(TextureStorageBackend::kVulkan));
}

SR_TEST(texture_storage_sampler_variants_share_without_changing_fetch_identity) {
  const std::array<uint32_t,6> original{0x84000002,0x14989071,0x003fe1ff,0x01280d10,3,0x000c0a18};
  auto variant=original;
  variant[0]^=1u<<10; // clamp
  variant[3]^=1u<<19; // mag
  variant[4]^=1u<<12; // LOD bias
  SR_CHECK(TextureStorageFetch(original)==TextureStorageFetch(variant));
  SR_CHECK(original!=variant);
  SR_CHECK(TextureStorageFetch(original,false)==original);
  SR_CHECK(TextureStorageFetch(variant,false)==variant);
}

SR_TEST(texture_storage_preserves_every_bit_except_the_sampler_whitelist) {
  const std::array<uint32_t,6> original{2,0x100086,31u|(31u<<13),0x688,0,0x200};
  // Hand-derived Xenos bit ranges; mip min/max and unknown bits stay distinct.
  constexpr std::array<uint32_t,6> sampler{0x0007fc00u,0,0,0x0ff80000u,0x003ff003u,0};
  for(uint32_t word=0;word<6;++word) for(uint32_t bit=0;bit<32;++bit) {
    auto changed=original;changed[word]^=1u<<bit;
    const bool same=(sampler[word]&(1u<<bit))!=0;
    SR_CHECK((TextureStorageFetch(original)==TextureStorageFetch(changed))==same);
  }
}

SR_TEST(texture_storage_invalid_fetches_are_not_normalized) {
  for(uint32_t type:{0u,1u,3u}) {
    const std::array<uint32_t,6> invalid{type|0x7fc00u,0,0,0x0ff80000u,0x003ff003u,0};
    SR_CHECK(TextureStorageFetch(invalid)==invalid);
  }
}
