#include "texture_source_probe.h"
#include "test_main.h"
#include <vector>
using namespace superman_returns::native;

SR_TEST(texture_source_probe_alternates_order_and_brackets_with_guest_read) {
  for (bool guest_first : {false, true}) {
    std::vector<char> order;
    auto guest = [&](uint64_t& value) { order.push_back('g'); value = 42; return true; };
    auto owned = [&](uint64_t& value) { order.push_back('o'); value = 42; return true; };
    const auto result = ProbeTextureSource(42, guest_first, guest, owned);
    SR_CHECK(result.readable && result.stable);
    SR_CHECK(order == (guest_first ? std::vector<char>{'g','o','g'} : std::vector<char>{'o','g','g'}));
  }
}
SR_TEST(texture_source_probe_rejects_changes_at_each_observation) {
  for (int mismatch = 0; mismatch < 4; ++mismatch) {
    int guest_calls = 0;
    auto guest = [&](uint64_t& value) {
      value = (mismatch == 1 && guest_calls == 0) || (mismatch == 3 && guest_calls == 1) ? 43 : 42;
      ++guest_calls;
      return true;
    };
    auto owned = [&](uint64_t& value) { value = mismatch == 2 ? 43 : 42; return true; };
    const auto result = ProbeTextureSource(mismatch == 0 ? 43 : 42, true, guest, owned);
    SR_CHECK(result.readable && !result.stable);
  }
}
SR_TEST(texture_source_probe_rejects_unreadable_sources) {
  for (int failure = 0; failure < 3; ++failure) {
    int guest_calls = 0;
    auto guest = [&](uint64_t& value) { value = 42; return guest_calls++ != failure; };
    auto owned = [&](uint64_t& value) { value = 42; return failure != 2; };
    const auto result = ProbeTextureSource(42, false, guest, owned);
    SR_CHECK(!result.readable && !result.stable);
  }
}
