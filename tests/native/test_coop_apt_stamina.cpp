#include "../../port/src/coop_apt_stamina.h"
#include "test_main.h"
using namespace superman_returns::coop;
SR_TEST(coop_apt_stamina_uses_native_minimum_and_absolute_role_positions) {
  using apt::Role;
  SR_CHECK(apt::Position(Role::FillLeft,10000,164,8,-2.1f)==8.f);
  const auto half=apt::Position(Role::FillLeft,5000,164,8,-2.1f);
  SR_CHECK(half && std::abs(*half+78.315789f)<0.0001f);
  const auto quarter=apt::Position(Role::FillRight,2500,164,8,-2.1f);
  SR_CHECK(quarter && std::abs(*quarter-34.526316f)<0.0001f);
  const auto empty=apt::Position(Role::Notch,0,164,8,-2.1f);
  SR_CHECK(empty && *empty==-2.1f);
  SR_CHECK(!apt::Position(Role::Mask,5000,164,8,-2.1f));
  SR_CHECK(!apt::Position(Role::FillBody,10001,164,8,-2.1f));
  SR_CHECK(!apt::Position(Role::FillBody,5000,16,8,-2.1f));
}
SR_TEST(coop_apt_stamina_private_packet_changes_only_pointer_and_projected_translation) {
  std::array<uint8_t,16> original{};std::array<uint8_t,80> matrix{};
  packet::WriteWord(original,0,0x82088618);packet::WriteWord(original,4,0x05048000);
  packet::WriteWord(original,8,12);packet::WriteWord(original,12,24);
  for(size_t i=0;i<16;++i)packet::WriteWord(matrix,i*4,std::bit_cast<uint32_t>(float(i)));
  packet::WriteWord(matrix,64,0x00FFFFFF);packet::WriteWord(matrix,68,0x11223344);
  const auto saved_packet=original;const auto saved_matrix=matrix;
  std::array<float,16> basis{};basis[0]=2;basis[1]=3;
  const auto result=apt::PrivatePacket(original,matrix,0x30010000,20,10,basis);
  SR_CHECK(result.has_value());if(!result)return;
  SR_CHECK(packet::ReadWord(*result,4)==0x30010010);
  SR_CHECK(packet::ReadWord(*result,0)==packet::ReadWord(original,0));
  SR_CHECK(packet::ReadWord(*result,8)==12 && packet::ReadWord(*result,12)==24);
  SR_CHECK(std::bit_cast<float>(packet::ReadWord(*result,64))==32);
  SR_CHECK(std::bit_cast<float>(packet::ReadWord(*result,68))==43);
  for(size_t i=0;i<48;++i)SR_CHECK((*result)[16+i]==matrix[i]);
  for(size_t i=64;i<80;++i)SR_CHECK((*result)[16+i]==matrix[i]);
  SR_CHECK(original==saved_packet && matrix==saved_matrix);
  SR_CHECK(!apt::PrivatePacket(original,matrix,0xFFFFFFF0,20,10,basis));
}
