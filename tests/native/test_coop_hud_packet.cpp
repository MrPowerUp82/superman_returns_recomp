#include "../../port/src/coop_hud_packet.h"
#include "test_main.h"
#include <limits>
using namespace superman_returns::coop;
SR_TEST(coop_radar_retargets_only_actor_fields_and_preserves_shared_threats) {
  std::array<uint8_t,hud::kStateSize> bytes;bytes.fill(0xA5);
  packet::WriteWord(bytes,0,0x820215D0);packet::WriteWord(bytes,4,0x56414C44);
  const auto original=bytes;
  hud::Snapshot value;value.target=8780;value.generation=0xC0000001;
  for(size_t i=0;i<6;++i)packet::WriteWord(value.anchor,i*4,std::bit_cast<uint32_t>(float(i+1)));
  SR_CHECK(hud::Retarget(bytes,value,0xC0000001));
  SR_CHECK(std::equal(bytes.begin(),bytes.begin()+16,original.begin()));
  SR_CHECK(std::equal(bytes.begin()+16,bytes.begin()+40,value.anchor.begin()));
  SR_CHECK(std::equal(bytes.begin()+40,bytes.end(),original.begin()+40));
}
SR_TEST(coop_radar_rejects_stale_join_nonfinite_or_foreign_packets_without_writing) {
  std::array<uint8_t,hud::kStateSize> bytes{};
  packet::WriteWord(bytes,0,0x820215D0);packet::WriteWord(bytes,4,0x56414C44);
  const auto original=bytes;
  hud::Snapshot value;value.target=8780;value.generation=0xC0000001;
  SR_CHECK(!hud::Retarget(bytes,value,0xC0000002));
  SR_CHECK(!hud::Retarget(bytes,value,0xC0000000));
  value.target=255;SR_CHECK(!hud::Retarget(bytes,value,0xC0000001));value.target=8780;
  packet::WriteWord(value.anchor,20,std::bit_cast<uint32_t>(std::numeric_limits<float>::infinity()));
  SR_CHECK(!hud::Retarget(bytes,value,0xC0000001));SR_CHECK(bytes==original);
  value.anchor={};packet::WriteWord(bytes,0,0x820215A0);
  SR_CHECK(!hud::Retarget(bytes,value,0xC0000001));
  packet::WriteWord(bytes,0,0x820215D0);packet::WriteWord(bytes,4,0);
  SR_CHECK(!hud::Retarget(bytes,value,0xC0000001));
  SR_CHECK(!hud::Retarget(std::span(bytes).first(40),value,0xC0000001));
}
