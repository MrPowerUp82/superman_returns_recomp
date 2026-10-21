#pragma once
#include <cstdint>
#include <deque>
#include <utility>
namespace superman_returns::native {
using RetiredDescriptors=std::deque<std::pair<uint64_t,uint32_t>>;
template<class Query>
uint32_t AllocateDescriptor(RetiredDescriptors& retired,uint64_t& completed,
                            uint32_t& next,uint32_t limit,Query&& query) {
  // Completion is sampled at frame start. A stale value can delay recycling,
  // never permit early reuse; poll again only when the heap has no fresh slot.
  if (next>=limit && !retired.empty() && retired.front().first>completed) {
    const uint64_t observed=query();
    // D3D12 reports UINT64_MAX on device removal, not completed GPU work.
    if (observed!=UINT64_MAX && observed>completed) completed=observed;
  }
  if (!retired.empty() && retired.front().first!=UINT64_MAX && retired.front().first<=completed) {
    auto index=retired.front().second;
    retired.pop_front();
    return index;
  }
  return next<limit?next++:0;
}
}
