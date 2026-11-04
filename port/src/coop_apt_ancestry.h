#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
namespace superman_returns::coop::apt {
enum class Movie:uint32_t {Unknown,MainHud,StaminaMeter,GenericMeter};
enum class Role:uint32_t {None,Mask,FillRight,FillBody,FillLeft,Notch};
struct Node {
  uint32_t address=0,source=0,root=0,character=0,depth=0;
  Movie movie=Movie::Unknown;
  bool exported=false;
  std::array<float,6> affine{1,0,0,1,0,0};
  std::array<float,16> world{};
  bool affine_valid=false,world_valid=false;
};
struct Selection {Role role;size_t meter,child;};
class Ancestry {
 public:
  bool Push(Node node) {
    if(!valid_ || !node.address || size_==nodes_.size()){valid_=false;return false;}
    nodes_[size_++]=node;return true;
  }
  bool Pop(uint32_t node) {
    if(!valid_ || !size_ || nodes_[size_-1].address!=node){valid_=false;return false;}
    --size_;return true;
  }
  bool Valid()const{return valid_;}
  std::span<const Node> Path()const{return std::span(nodes_).first(size_);}
  Node* Top(){return valid_&&size_?&nodes_[size_-1]:nullptr;}
  std::optional<Selection> Select()const {
    if(!valid_)return {};
    for(size_t i=0;i+3<size_;++i) {
      const auto& main=nodes_[i];const auto& stamina=nodes_[i+1];const auto& meter=nodes_[i+2];
      if(main.movie!=Movie::MainHud || !main.exported ||
         stamina.movie!=Movie::StaminaMeter || !stamina.exported || stamina.depth!=172 ||
         meter.movie!=Movie::GenericMeter || !meter.exported || meter.depth!=1 ||
         !main.root || !stamina.root || !meter.root || main.root==stamina.root || main.root==meter.root || stamina.root==meter.root)continue;
      const auto& child=nodes_[i+3];
      for(size_t j=i+3;j<size_;++j)
        if(nodes_[j].movie!=Movie::GenericMeter || nodes_[j].root!=meter.root)return {};
      Role role=Role::None;
      if(child.character==13 && child.depth==24)role=Role::Mask;
      if(child.character==26 && child.depth==30)role=Role::FillRight;
      if(child.character==21 && child.depth==34)role=Role::FillBody;
      if(child.character==27 && child.depth==39)role=Role::FillLeft;
      if(child.character==40 && child.depth==43)role=Role::Notch;
      return role==Role::None?std::optional<Selection>{}:Selection{role,i+2,i+3};
    }
    return {};
  }
  Role SelectedRole()const {const auto selection=Select();return selection?selection->role:Role::None;}
 private:
  std::array<Node,64> nodes_{};
  size_t size_=0;
  bool valid_=true;
};
} // namespace
