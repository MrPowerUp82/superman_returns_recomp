#include "../../port/src/coop_camera_packet.h"
#include "test_main.h"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
using namespace superman_returns::coop::packet;
namespace {
std::array<float,16> Matrix() { return {1,0,0,0,0,1,0,0,0,0,1,0,12,20,-30,0}; }
auto View(uint32_t version=1) { return EncodeView(Matrix(), {0.5f,18000,1.2f,16.f/9.f}, version); }
}
SR_TEST(coop_packet_encodes_big_endian_without_touching_primary_or_other_fields) {
  std::array<uint8_t,304> data; data.fill(0x5A);
  std::fill(data.begin()+112,data.begin()+208,0);
  const auto before=data;
  PacketOwner owner;
  SR_CHECK(owner.Publish(data,0x1000,View())==Result::Published);
  SR_CHECK(std::equal(data.begin(),data.begin()+112,before.begin()));
  SR_CHECK(std::equal(data.begin()+208,data.end(),before.begin()+208));
  SR_CHECK_EQ(ReadWord(data,160),std::bit_cast<uint32_t>(12.f));
  SR_CHECK_EQ(ReadWord(data,176),std::bit_cast<uint32_t>(0.5f));
  SR_CHECK_EQ(ReadWord(data,196),0x050002D0u);
  SR_CHECK_EQ(ReadWord(data,200),1u);
}
SR_TEST(coop_packet_versions_rejoins_and_clears_only_its_owned_view) {
  std::array<uint8_t,208> data{}; PacketOwner owner;
  SR_CHECK(owner.Publish(data,0x1000,View(7))==Result::Published);
  SR_CHECK(owner.Publish(data,0x1000,View(8))==Result::Published);
  SR_CHECK_EQ(ReadWord(data,200),8u);
  SR_CHECK(owner.Publish(data,0x1000,{})==Result::Cleared);
  SR_CHECK(std::all_of(data.begin()+112,data.end(),[](auto x){return x==0;}));
  SR_CHECK(owner.Publish(data,0x1000,{})==Result::Inactive);
}
SR_TEST(coop_packet_refuses_native_occupied_slots_and_different_packet_ownership) {
  std::array<uint8_t,208> data{}; data[203]=9; const auto before=data;
  PacketOwner owner;
  SR_CHECK(owner.Publish(data,0x1000,View())==Result::Occupied);
  SR_CHECK(data==before);
  data[203]=0;
  SR_CHECK(owner.Publish(data,0x1000,View())==Result::Published);
  const auto published=data;
  SR_CHECK(owner.Publish(data,0x2000,{})==Result::Occupied);
  SR_CHECK(data==published);
  data[203]=9;
  SR_CHECK(owner.Publish(data,0x1000,{})==Result::Occupied);
  SR_CHECK_EQ(data[203],9);
}
SR_TEST(coop_packet_rejects_short_spans_invalid_projection_and_nonfinite_transform) {
  std::array<uint8_t,200> data{}; const auto before=data; PacketOwner owner;
  SR_CHECK(owner.Publish(data,0x1000,View())==Result::Invalid);
  SR_CHECK(data==before);
  auto matrix=Matrix(); matrix[0]=std::numeric_limits<float>::quiet_NaN();
  SR_CHECK(!EncodeView(matrix,{0.5f,18000,1.2f,1.7f},1));
  SR_CHECK(!EncodeView(Matrix(),{1,0.5f,1.2f,1.7f},1));
  SR_CHECK(!EncodeView(Matrix(),{0.5f,18000,4,1.7f},1));
  SR_CHECK(!View(0));
}
