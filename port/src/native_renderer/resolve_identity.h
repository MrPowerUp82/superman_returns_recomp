#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <map>

namespace superman_returns::native {
// No packed/truncated dimensions or float comparison: every allocation and
// sampling interpretation variant has its own stable host resource identity.
struct ResolveIdentity {
  uint32_t base=0, format=0, width=0, height=0, scale_bits=0, layers=1;
  bool volume=false, depth=false, shadow=false;
  auto Fields() const {
    return std::array<uint32_t,9>{base,format,width,height,scale_bits,layers,
                                 uint32_t(volume),uint32_t(depth),uint32_t(shadow)};
  }
  bool operator<(const ResolveIdentity& other) const { return Fields()<other.Fields(); }
};
class ResolveIdentities {
 public:
  uint64_t Get(const ResolveIdentity& identity) {
    const auto [it, inserted] = ids_.try_emplace(identity, next_);
    if (inserted) ++next_;
    return it->second;
  }
  std::size_t size() const { return ids_.size(); }
 private:
  std::map<ResolveIdentity,uint64_t> ids_;
  uint64_t next_=1; // zero stays the existing 'no scene depth' sentinel
};
} // namespace superman_returns::native
