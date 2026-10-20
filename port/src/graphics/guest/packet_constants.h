#pragma once
#include "constant_snapshot.h"
#include <memory>
namespace superman_returns::graphics::guest {
using FloatConstantBank=std::array<uint32_t,1024>;
// Published replay banks are immutable. Manually constructed/expanded draws
// retain array-like editing, with a private copy on their first modification.
class PacketFloatBank {
public:
  PacketFloatBank()=default;
  PacketFloatBank(const PacketFloatBank& other):snapshot_(other.snapshot_),
      private_(other.private_?std::make_unique<FloatConstantBank>(*other.private_):nullptr) {}
  PacketFloatBank(PacketFloatBank&&)=default;
  PacketFloatBank& operator=(PacketFloatBank&&)=default;
  PacketFloatBank& operator=(const PacketFloatBank& other) {
    PacketFloatBank copy(other);*this=std::move(copy);return *this;
  }
  PacketFloatBank& operator=(const FloatConstantBank& value) {
    auto copy=std::make_unique<FloatConstantBank>(value);snapshot_.reset();private_=std::move(copy);return *this;
  }
  void Share(std::shared_ptr<const FloatConstantBank> value) {snapshot_=std::move(value);private_.reset();}
  const uint32_t* data() const {return Read().data();}
  uint32_t* data() {return Edit().data();}
  const uint32_t& operator[](size_t i) const {return Read()[i];}
  uint32_t& operator[](size_t i) {return Edit()[i];}
  const uint32_t* begin() const {return data();}
  const uint32_t* end() const {return data()+size();}
  uint32_t* begin() {return data();}
  uint32_t* end() {return data()+size();}
  static constexpr size_t size() {return 1024;}
  void fill(uint32_t value) {Edit().fill(value);}
  friend bool operator==(const PacketFloatBank& a,const PacketFloatBank& b) {return a.Read()==b.Read();}
  friend bool operator==(const PacketFloatBank& a,const FloatConstantBank& b) {return a.Read()==b;}
private:
  const FloatConstantBank& Read() const {
    static const FloatConstantBank zero{};
    return private_?*private_:(snapshot_?*snapshot_:zero);
  }
  FloatConstantBank& Edit() {
    if(!private_) {private_=std::make_unique<FloatConstantBank>(Read());snapshot_.reset();}
    return *private_;
  }
  std::shared_ptr<const FloatConstantBank> snapshot_;
  // Copies of a mutable bank must be deep even if the caller retains data().
  std::unique_ptr<FloatConstantBank> private_;
};
struct PacketConstants {
  PacketFloatBank vs,ps;
  std::array<uint8_t,512> shared{};
  PacketConstants()=default;
  explicit PacketConstants(ConstantSnapshot::ReplayStorageTag) {}
  PacketConstants& operator=(const ConstantSnapshot& value) {
    vs=value.vs;ps=value.ps;std::copy_n(value.shared.begin(),shared.size(),shared.begin());return *this;
  }
};
}
