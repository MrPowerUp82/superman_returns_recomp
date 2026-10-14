#include "native_frontend.h"
#include "test_main.h"
#include <type_traits>
using namespace superman_returns::native;
SR_TEST(texture_frontend_is_a_single_owner_of_cached_entry_addresses) {
  SR_CHECK(!std::is_copy_constructible_v<NativeFrontend>);
  SR_CHECK(!std::is_copy_assignable_v<NativeFrontend>);
  SR_CHECK(!std::is_move_constructible_v<NativeFrontend>);
  SR_CHECK(!std::is_move_assignable_v<NativeFrontend>);
}
SR_TEST(texture_slot_lookup_keeps_the_complete_fetch_identity) {
  NativeFrontend frontend;
  std::array<uint32_t,6> fetch{2,0x1000,3,4,5,6};
  auto* original=&frontend.CapturedTexture(0,fetch);
  original->watch_seq=17;original->checked_frame=9;
  SR_CHECK(&frontend.CapturedTexture(0,fetch)==original);
  SR_CHECK(&frontend.CapturedTexture(31,fetch)==original);
  for(uint32_t word=0;word<6;++word) {
    auto changed=fetch;changed[word]^=1;
    SR_CHECK(&frontend.CapturedTexture(0,changed)!=original);
    SR_CHECK(&frontend.CapturedTexture(0,fetch)==original);
  }
  SR_CHECK_EQ(frontend.CapturedTexture(0,fetch).watch_seq,17u);
  SR_CHECK_EQ(frontend.CapturedTexture(0,fetch).checked_frame,9u);
}
SR_TEST(texture_slot_lookup_is_invalidated_before_pruning_entries) {
  NativeFrontend frontend;
  std::array<uint32_t,6> fetch{2,0x1000,3,4,5,6};
  frontend.CapturedTexture(0,fetch).checked_frame=1;
  frontend.InvalidateTextureSlotLookups();
  frontend.captured_textures_.clear();
  auto& fresh=frontend.CapturedTexture(0,fetch);
  SR_CHECK_EQ(fresh.checked_frame,~0ull);
  SR_CHECK_EQ(fresh.watch_seq,0u);
  SR_CHECK_EQ(frontend.captured_textures_.size(),1u);
}
