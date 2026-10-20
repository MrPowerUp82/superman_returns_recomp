#pragma once
#include <charconv>
#include <cstdint>
#include <string_view>
namespace superman_returns {
struct BenchmarkPadState {
  uint16_t buttons=0;
  int16_t forward=0;
  bool operator==(const BenchmarkPadState&) const = default;
};
class BenchmarkInput {
public:
  // Decimal: monotonically increasing id, hold milliseconds, button mask,
  // left-stick Y. Only an entirely valid new command changes the state.
  bool Update(std::string_view command,uint64_t now_ms) {
    auto whitespace=[](char c) {return c==' ' || c=='\t' || c=='\r' || c=='\n';};
    auto trim=[&] {while(!command.empty() && whitespace(command.front()))command.remove_prefix(1);};
    auto read=[&](auto& value) {
      trim();if(command.empty())return false;
      auto result=std::from_chars(command.data(),command.data()+command.size(),value);
      if(result.ec!=std::errc{})return false;
      command.remove_prefix(result.ptr-command.data());return command.empty() || whitespace(command.front());
    };
    uint64_t id;uint32_t hold,buttons;int32_t forward;
    if(!read(id) || !read(hold) || !read(buttons) || !read(forward))return false;
    trim();
    if(!command.empty() || id<=last_id_ || !hold || hold>30000 || buttons>65535 || forward< -32768 || forward>32767)return false;
    last_id_=id;start_=now_ms;hold_=hold;state_={uint16_t(buttons),int16_t(forward)};return true;
  }
  BenchmarkPadState Sample(uint64_t now_ms) const {
    return now_ms>=start_ && now_ms-start_<hold_ ? state_ : BenchmarkPadState{};
  }
private:
  uint64_t last_id_=0,start_=0;
  uint32_t hold_=0;
  BenchmarkPadState state_;
};
}
