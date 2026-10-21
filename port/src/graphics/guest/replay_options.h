#pragma once
#include <string_view>

namespace superman_returns::graphics::guest {
enum class ReplayScheduling { kContiguous, kInterleaved };
struct ReplayOptions {
  bool prepared=false;
  unsigned shards=1;
  ReplayScheduling scheduling=ReplayScheduling::kContiguous;
  bool invalid_threads=false,invalid_scheduling=false;
};
inline ReplayOptions ParseReplayOptions(std::string_view threads,
    std::string_view scheduling,bool packet_sink) noexcept {
  ReplayOptions options;
  if(!threads.empty()) {
    if(threads.size()==1 && threads[0]>='1' && threads[0]<='8') {
      options.prepared=packet_sink;
      if(packet_sink)options.shards=unsigned(threads[0]-'0');
    } else options.invalid_threads=true;
  }
  if(scheduling=="interleaved")options.scheduling=ReplayScheduling::kInterleaved;
  else if(!scheduling.empty() && scheduling!="contiguous")options.invalid_scheduling=true;
  return options;
}
} // namespace superman_returns::graphics::guest
