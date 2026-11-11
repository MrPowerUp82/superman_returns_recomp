#pragma once
#include <charconv>
#include <cstdint>
#include <string_view>
namespace superman_returns {
struct BenchmarkPadState {
  uint16_t buttons=0;
  int16_t forward=0;
  int16_t look_x=0,look_y=0;
  uint8_t left_trigger=0,right_trigger=0;
  int16_t strafe=0;
  bool operator==(const BenchmarkPadState&) const = default;
};
class BenchmarkInput {
public:
  // Decimal: monotonically increasing id, hold milliseconds, button mask,
  // left-stick Y, optionally right-stick X/Y and then left/right triggers
  // (0..255), then left-stick X. Only an entirely valid new command changes
  // the state; the original four- and six-field formats stay valid.
  bool Update(std::string_view command,uint64_t now_ms) {
    auto whitespace=[](char c) {return c==' ' || c=='\t' || c=='\r' || c=='\n';};
    auto trim=[&] {while(!command.empty() && whitespace(command.front()))command.remove_prefix(1);};
    auto read=[&](auto& value) {
      trim();if(command.empty())return false;
      auto result=std::from_chars(command.data(),command.data()+command.size(),value);
      if(result.ec!=std::errc{})return false;
      command.remove_prefix(result.ptr-command.data());return command.empty() || whitespace(command.front());
    };
    uint64_t id;uint32_t hold,buttons,lt=0,rt=0;int32_t forward,look_x=0,look_y=0,strafe=0;
    if(!read(id) || !read(hold) || !read(buttons) || !read(forward))return false;
    trim();
    if(!command.empty() && (!read(look_x) || !read(look_y)))return false;
    trim();
    if(!command.empty() && (!read(lt) || !read(rt)))return false;
    trim();
    if(!command.empty() && !read(strafe))return false;
    trim();
    if(!command.empty() || id<=last_id_ || !hold || hold>30000 || buttons>65535 || forward< -32768 || forward>32767 ||
       look_x< -32768 || look_x>32767 || look_y< -32768 || look_y>32767 || lt>255 || rt>255 || strafe< -32768 || strafe>32767)return false;
    last_id_=id;start_=now_ms;hold_=hold;state_={uint16_t(buttons),int16_t(forward),int16_t(look_x),int16_t(look_y),uint8_t(lt),uint8_t(rt),int16_t(strafe)};return true;
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
