#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <optional>

namespace superman_returns::native {
inline uint32_t ReadXdkWord(std::span<const uint8_t> bytes,size_t offset) {
  return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|
      (uint32_t(bytes[offset+2])<<8)|bytes[offset+3];
}
enum class XdkDrawResult { Submitted, UnsupportedState, RingFull };
enum class XdkDrawRefusal : uint8_t {
  None = 0,
  GeneralUnsupported,
  MissingPointers,
  Mode10452Not4,
  DeviceFlags10810_10811,
  ProgramDirtyBit48,
  PsFlagsAlt20,
  VsPsMetaMismatch,
  PsFlagsHeavy40,
  Device10808_80,
  VsListNotEmpty,
  PsListNotEmpty,
  CacheTagMismatch,
  CacheMaskMismatch,
  UnreadableMemory,
  FastRingLimit,
  RingBufferFull,
  Count
};
struct XdkDrawArguments {
  bool indexed;
  uint32_t primitive,first_or_base,count,start_index,index_header,index_address;
};
struct XdkGuestMemoryView {
  uint8_t* base=nullptr;
  size_t size=0;
};
struct XdkDrawUnsupportedState {
  bool device_too_small=false;
  bool count_too_large=false;
  bool draw_flags=false;
  uint64_t state=0,context=0,misc=0;
  bool any() const {
    return device_too_small||count_too_large||draw_flags||state||context||misc;
  }
};
inline XdkDrawUnsupportedState InspectNativeXdkUnsupportedState(
    std::span<const uint8_t> device,const XdkDrawArguments& args) {
  XdkDrawUnsupportedState out{};
  if(device.size()<0x5700) {out.device_too_small=true;return out;}
  out.count_too_large=args.count>65535;
  out.draw_flags=(device[10808]&2)!=0;
  auto read64=[&](size_t offset) {
    return (uint64_t(ReadXdkWord(device,offset))<<32)|ReadXdkWord(device,offset+4);
  };
  constexpr uint64_t state_allowed=((uint64_t{1}<<49)-1)&~uint64_t{256};
  constexpr uint64_t context_allowed=(uint64_t{1}<<60)-2;
  constexpr uint64_t misc_allowed=(uint64_t{1}<<46)-1;
  out.state=read64(16)&~state_allowed;
  out.context=read64(24)&~context_allowed;
  out.misc=read64(32)&~misc_allowed;
  return out;
}
struct XdkRingPlan {uint64_t host_offset;size_t bytes;};
inline std::optional<XdkRingPlan> PlanNativeXdkRing(uint32_t write,uint32_t limit,
    uint32_t device,uint32_t device_size) {
  // Same address adjustment as generated REX_RAW_ADDR/REX_STORE_U32. The
  // command ring normally lives in a physical alias, not ordinary virtual RAM.
  const uint64_t extra=write>=0xe0000000u?0x1000u:0u;
  const uint64_t start=uint64_t(write)+4+extra,end=uint64_t(limit)+4+extra;
  if(!write || (write&3u) || limit<=write || end>0x100000000ull ||
      (write>=0xe0000000u)!=(limit>=0xe0000000u) ||
      (start<uint64_t(device)+device_size && uint64_t(device)<end) ||
      (start<0x80000000ull && end>0x7f000000ull)) return {};
  return XdkRingPlan{start,size_t(limit-write)};
}

// Native replacement of the admitted paths of 820FBBF8/820FC000 and their
// float/scalar/fetch writers 821078C0/82107520/82107760. Shader preparation,
// RT special writes, stream-out, predicated and split draws remain legacy.
// ring begins AFTER device.ring_write. Nothing changes on refusal, including
// when constants fit but the final draw packet does not.
inline XdkDrawResult SubmitNativeXdkDraw(std::span<uint8_t> device,
    std::span<uint8_t> ring,const XdkDrawArguments& args,
    XdkGuestMemoryView guest={},
    XdkDrawRefusal* refusal=nullptr) {
  auto set_refusal=[&](XdkDrawRefusal reason) {
    if(refusal) *refusal=reason;
  };
  const auto unsupported=InspectNativeXdkUnsupportedState(device,args);
  constexpr uint64_t shader_bit49=uint64_t{1}<<49,shader_bit50=uint64_t{1}<<50;
  constexpr uint64_t shader_bit51=uint64_t{1}<<51,shader_bit52=uint64_t{1}<<52;
  const uint64_t shader_mask=unsupported.state&(shader_bit49|shader_bit50|shader_bit51|shader_bit52);
  const bool cached_shader_mask=shader_mask==(shader_bit49|shader_bit51|shader_bit52) ||
      shader_mask==(shader_bit49|shader_bit50|shader_bit51|shader_bit52);
  const bool common_shader=(unsupported.state==shader_bit49 ||
      unsupported.state==(shader_bit49|shader_bit50) || cached_shader_mask) &&
      !unsupported.device_too_small && !unsupported.count_too_large && !unsupported.draw_flags &&
      unsupported.context==0 && unsupported.misc==0;
  if(unsupported.any() && !common_shader) {
    set_refusal(XdkDrawRefusal::GeneralUnsupported);
    return XdkDrawResult::UnsupportedState;
  }
  auto read64=[&](size_t offset) {
    return (uint64_t(ReadXdkWord(device,offset))<<32)|ReadXdkWord(device,offset+4);
  };
  uint64_t state=read64(16);
  const uint64_t context=read64(24),misc=read64(32);
  uint32_t shader_10432=0;
  uint32_t shader_10400=0,shader_10404=0;
  uint32_t shader_12424=0,shader_selected_mode=0;
  uint32_t shader_cache_stamp_address=0,shader_cache_stamp=0;
  uint64_t shader_cache_11552=0,shader_cache_11560=0;
  uint8_t shader_10809=0,shader_10810=0;
  bool shader_emit_4600=false,shader_program=false,shader_write_12424=false;
  bool shader_cached_extended=false,shader_dma_present=false;
  uint32_t shader_10408=0;
  std::array<uint32_t,6> shader_dma_packets{};
  if(common_shader) {
    auto guest_word=[&](uint64_t address,uint32_t& value) {
      if(!guest.base || address+4<address || address+4>guest.size) return false;
      value=ReadXdkWord(std::span<const uint8_t>(guest.base+size_t(address),4),0);
      return true;
    };
    auto guest_qword=[&](uint64_t address,uint64_t& value) {
      uint32_t high=0,low=0;
      if(!guest_word(address,high) || !guest_word(address+4,low)) return false;
      value=(uint64_t(high)<<32)|low;
      return true;
    };
    const uint32_t ps=ReadXdkWord(device,12420),vs=ReadXdkWord(device,12416),aux=ReadXdkWord(device,11536);
    shader_program=(device[10808]&0x40u)!=0;
    if(!ps || !vs || !aux) {
      set_refusal(XdkDrawRefusal::MissingPointers);
      return XdkDrawResult::UnsupportedState;
    }
    if((ReadXdkWord(device,10452)&7u)!=4u) {
      set_refusal(XdkDrawRefusal::Mode10452Not4);
      return XdkDrawResult::UnsupportedState;
    }
    if((device[10810]&0xc0u) || (device[10811]&0x20u)) {
      set_refusal(XdkDrawRefusal::DeviceFlags10810_10811);
      return XdkDrawResult::UnsupportedState;
    }
    if(shader_program && (state&(uint64_t{1}<<48))) {
      set_refusal(XdkDrawRefusal::ProgramDirtyBit48);
      return XdkDrawResult::UnsupportedState;
    }
    uint32_t vs_block=0,ps_block=0,ps_flags=0;
    if(!guest_word(uint64_t(vs)+64,vs_block) || !guest_word(uint64_t(ps)+896,ps_block) ||
        !guest_word(uint64_t(ps)+872,ps_flags)) {
      set_refusal(XdkDrawRefusal::UnreadableMemory);
      return XdkDrawResult::UnsupportedState;
    }
    if(ps_flags&0x20u) {
      set_refusal(XdkDrawRefusal::PsFlagsAlt20);
      return XdkDrawResult::UnsupportedState;
    }
    const uint64_t vs_meta=uint64_t(vs)+vs_block+40,ps_meta=uint64_t(ps)+ps_block+872;
    uint32_t vs_8=0,vs_12=0,vs_20=0,ps_8=0,ps_12=0,ps_20=0;
    if(!guest_word(vs_meta+8,vs_8) || !guest_word(vs_meta+12,vs_12) || !guest_word(vs_meta+20,vs_20) ||
        !guest_word(ps_meta+8,ps_8) || !guest_word(ps_meta+12,ps_12) || !guest_word(ps_meta+20,ps_20)) {
      set_refusal(XdkDrawRefusal::UnreadableMemory);
      return XdkDrawResult::UnsupportedState;
    }
    if((vs_20^ps_20)&0x7ffffu) {
      set_refusal(XdkDrawRefusal::VsPsMetaMismatch);
      return XdkDrawResult::UnsupportedState;
    }
    // 82107110: update the RT control word/flag and stage its optional packet.
    const bool vs_flag=((std::rotl(vs_8,5))&1u)!=0;
    const uint32_t packed=(std::rotl(ReadXdkWord(device,11580),12))&0xfffu;
    const uint32_t rt_state=ReadXdkWord(device,10420);
    const uint32_t rt_shifted=std::rotl(rt_state,31)&0x7fffffffu;
    shader_10432=ReadXdkWord(device,10432);
    shader_10809=device[10809];
    shader_10810=device[10810];
    uint32_t candidate=(std::rotl(uint32_t(shader_10810),27)&0x7ffffffu)&rt_shifted;
    shader_10432=(shader_10432&0xfffffffeu)|(candidate&1u);
    if((packed&2u)==0) candidate=packed&1u;
    else {
      const uint32_t mode=(std::rotl(rt_state,28))&7u;
      const bool selected=(shader_10432&0x10u)?(mode==4u||mode==6u||mode==2u):
          (mode==1u||mode==3u||mode==2u);
      candidate=(!vs_flag?1u:0u)&shader_10432&rt_shifted&(selected?1u:0u);
      if(rt_state&1u) {
        candidate&=(rt_state&0x3800u)==0;
        candidate&=(rt_state&0xe0000u)==0;
        if(rt_state&0x80u) {
          candidate&=(rt_state&0xe0000000u)==0;
          candidate&=(rt_state&0x03800000u)==0;
        }
      }
    }
    shader_10432=(shader_10432&~2u)|((candidate&1u)<<1);
    if(shader_10432&2u) {
      if(shader_10809&4u) {shader_10809&=uint8_t(~4u);shader_emit_4600=true;}
    } else if((rt_shifted&1u) && (rt_state&4u)) {
      const uint32_t mode=(rt_state>>4)&7u;
      const bool set_flag=mode==7u||mode==5u||
          ((shader_10432&0x10u)?(mode==1u||mode==3u):(mode==4u||mode==6u));
      if(set_flag) shader_10809|=4u;
    }

    // Observed 82108668 branch: VS/PS metadata agree, so only bit 3 of
    // device+10810 changes. All helper/stream programming branches are absent.
    const uint8_t shader_bit3=(vs_8&0x70000000u)>0x10000000u?8u:0u;
    shader_10810=uint8_t((shader_10810&~8u)|shader_bit3);
    if(cached_shader_mask) {
      // Bounded D/F path: both 82108470 metadata lists are empty and
      // 82108520 takes its cache-hit return. Any cache miss or list payload
      // stays on the generated XDK path.
      uint32_t vs_list=0,ps_list=0,ps_cache_tag=0,aux_cache_tag=0;
      uint64_t ps_cache0=0,ps_cache1=0,aux_mask0=0,aux_mask1=0;
      if(ps_flags&0x40u) {
        set_refusal(XdkDrawRefusal::PsFlagsHeavy40);
        return XdkDrawResult::UnsupportedState;
      }
      if(device[10808]&0x80u) {
        set_refusal(XdkDrawRefusal::Device10808_80);
        return XdkDrawResult::UnsupportedState;
      }
      if(!guest_word(uint64_t(vs)+60,vs_list) || !guest_word(uint64_t(ps)+892,ps_list)) {
        set_refusal(XdkDrawRefusal::UnreadableMemory);
        return XdkDrawResult::UnsupportedState;
      }
      if(vs_list) {
        set_refusal(XdkDrawRefusal::VsListNotEmpty);
        return XdkDrawResult::UnsupportedState;
      }
      if(ps_list) {
        set_refusal(XdkDrawRefusal::PsListNotEmpty);
        return XdkDrawResult::UnsupportedState;
      }
      if(!guest_word(uint64_t(ps)+40,ps_cache_tag) ||
          !guest_word(uint64_t(aux)+48,aux_cache_tag) ||
          !guest_qword(uint64_t(ps)+48,ps_cache0) ||
          !guest_qword(uint64_t(ps)+56,ps_cache1) ||
          !guest_qword(uint64_t(aux)+32,aux_mask0) ||
          !guest_qword(uint64_t(aux)+40,aux_mask1)) {
        set_refusal(XdkDrawRefusal::UnreadableMemory);
        return XdkDrawResult::UnsupportedState;
      }
      if(ps_cache_tag!=aux_cache_tag) {
        set_refusal(XdkDrawRefusal::CacheTagMismatch);
        return XdkDrawResult::UnsupportedState;
      }
      if(((ps_cache0^read64(12256))&aux_mask0) ||
          ((ps_cache1^read64(12264))&aux_mask1)) {
        set_refusal(XdkDrawRefusal::CacheMaskMismatch);
        return XdkDrawResult::UnsupportedState;
      }
      // 82108A4C tests device+10810 bit 6 (cleared above, and refused on entry)
      // before using the alternate PS words, so r29/r28 stay -1: no alt packet.
      // 82108668 bit-52 branch: DMA packet for the VS microcode block, then
      // 82108CB0: DMA packet for the PS block. Both follow 82108470 (empty).
      uint32_t vs_24=0,vs_block_addr=0,vs_block_size=0,vs_meta_24=0;
      uint32_t ps_32=0,ps_block_addr=0,ps_block_size=0;
      if(!guest_word(uint64_t(vs)+24,vs_24) || !guest_word(vs_meta+0,vs_block_addr) ||
          !guest_word(vs_meta+4,vs_block_size) || !guest_word(vs_meta+24,vs_meta_24) ||
          !guest_word(uint64_t(ps)+32,ps_32) || !guest_word(ps_meta+0,ps_block_addr) ||
          !guest_word(ps_meta+4,ps_block_size)) {
        set_refusal(XdkDrawRefusal::UnreadableMemory);
        return XdkDrawResult::UnsupportedState;
      }
      auto dma_address=[](uint32_t sum) {
        return (sum&0x1fffffffu)+((((sum>>20)&0xfffu)+512u)&0x1000u);
      };
      const uint32_t vs_sum=vs_block_addr+vs_24,ps_sum=ps_block_addr+ps_32;
      shader_dma_packets[0]=0xc0012700u;
      shader_dma_packets[1]=(dma_address(vs_sum)&~1u)|1u;
      shader_dma_packets[2]=vs_block_size>>2;
      shader_dma_packets[3]=0xc0012700u;
      shader_dma_packets[4]=dma_address(ps_sum);
      shader_dma_packets[5]=ps_block_size>>2;
      shader_dma_present=true;
      shader_10408=vs_meta_24;
      shader_cached_extended=true;
      shader_cache_stamp_address=ps+64;
      shader_cache_stamp=ReadXdkWord(device,10780);
      shader_cache_11552=read64(12256);
      shader_cache_11560=read64(12264);
      shader_10400=ps_8|vs_8;
      shader_10404=ps_12|vs_12;
      state|=(uint64_t{1}<<46)|(uint64_t{1}<<47)|(uint64_t{1}<<48);
    }
    if(shader_program) {
      shader_selected_mode=ReadXdkWord(device,10452);
      if((ReadXdkWord(device,10428)&8u)==0) {
        uint32_t vs_28=0;
        if(!guest_word(vs_meta+28,vs_28)) {
          set_refusal(XdkDrawRefusal::UnreadableMemory);
          return XdkDrawResult::UnsupportedState;
        }
        if((vs_28&0x30u)==0) shader_selected_mode=(shader_selected_mode&~7u)|5u;
      }
      shader_12424=ReadXdkWord(device,12424);
      shader_write_12424=shader_selected_mode!=shader_12424 || (state&8u)!=0;
      if(shader_write_12424) {
        shader_12424=shader_selected_mode;
        state|=8u;
      }
    }
  }
  std::array<uint8_t,12288> commands;
  size_t size=0;
  const uint32_t old_write=ReadXdkWord(device,40);
  const uint64_t hard_limit=uint64_t(old_write)+ring.size();
  bool fast=true;
  auto word=[&](uint32_t value) {
    for(unsigned i=0;i<4;++i) commands[size++]=uint8_t(value>>(24-i*8));
  };
  auto dirty_words=[&](uint64_t dirty,uint32_t first_reg,size_t source,unsigned words_per_bit) {
    if(!fast)return;
    while(dirty) {
      const unsigned first=std::countl_zero(dirty);
      unsigned groups=0;
      while(first+groups<64 && (dirty&(uint64_t{1}<<(63-first-groups)))) ++groups;
      const uint32_t count=groups*words_per_bit;
      const uint64_t cursor=uint64_t(old_write)+size;
      const uint32_t reserve=words_per_bit==16?12:words_per_bit==6?20:0;
      // Match the original helper's fast/slow capacity decision before
      // padding. A slow fetch ends with different cache-control words and
      // may switch rings; it must return to the original implementation.
      if(cursor+count*4+reserve>=hard_limit) {fast=false;return;}
      const uint32_t padding=words_per_bit==16?uint32_t((8-cursor)&15):
          words_per_bit==6?uint32_t(cursor&7):0;
      for(uint32_t p=0;p<padding;p+=4)word(0x80000000u);
      word(((count-1)<<16)|(first_reg+first*words_per_bit));
      if(shader_cached_extended && source==10400 && words_per_bit==1) {
        for(uint32_t i=0;i<count;++i) {
          const size_t offset=source+(first+i)*4;
          if(offset==10400) word(shader_10400);
          else if(offset==10404) word(shader_10404);
          else if(offset==10408) word(shader_10408);
          else word(ReadXdkWord(device,offset));
        }
      } else {
        std::memcpy(commands.data()+size,device.data()+source+first*words_per_bit*4,count*4);
        size+=count*4;
      }
      for(unsigned i=0;i<groups;++i)dirty&=~(uint64_t{1}<<(63-first-i));
    }
  };
  dirty_words(read64(0),0x4000,0x700,16);
  dirty_words(read64(8),0x4400,0x1700,16);
  if(common_shader) {
    if(shader_emit_4600) {word(0xc0004600u);word(15);}
    // 82108668: the VS block packet is emitted as soon as bit 52 is seen; the
    // PS block packet comes after the program/cached-register packets.
    if(shader_dma_present) for(unsigned i=0;i<3;++i) word(shader_dma_packets[i]);
    if(!shader_program && shader_dma_present)
      for(unsigned i=3;i<6;++i) word(shader_dma_packets[i]);
    if(shader_program) {
      word(0xc0006000u);word(0x15555555u);word(0xc0006100u);word(0);
      if(state&8u) {
        state&=~uint64_t{8};
        word(0x2208);word(ReadXdkWord(device,10452));
      }
      if(shader_write_12424) {
        word(0xc0012d01u);word(0x00040208u);word(shader_selected_mode);
      }
      if(shader_cached_extended) {
        // Bit 48 is consumed inline (10400/10404); bit 46 stays dirty.
        state&=~((uint64_t{1}<<47)|(uint64_t{1}<<48));
        word(0x00012180u);word(shader_10400);word(shader_10404);
        for(unsigned i=3;i<6;++i) word(shader_dma_packets[i]);
      }
      word(0xc0006000u);word(ReadXdkWord(device,12432));
      word(0xc0006100u);word(ReadXdkWord(device,12436));
    }
    // 821072F8 simple branch. The returned mask clears the temporary bit 8;
    // the original incoming state did not contain that special bit.
    word(0x2203);word(shader_10432);
  }
  dirty_words(std::rotl(state,52)&0xfff0000000000000ull,0x2200,10420,1);
  const auto fetch=std::rotl(state,20)&0xffffffff00000000ull;
  if(fetch) {
    dirty_words(fetch,0x4800,1024,6);
    // Exact fast-path footer from 82107760, including its middle 0x25000
    // value. This is not interchangeable with the slow-path three zeros.
    word(0x25000);word(0);word(0x25000);word(0);
  }
  dirty_words(std::rotl(state,15)&0xf800000000000000ull,0x2180,10400,1);
  const bool bools=context&2;
  if(bools) dirty_words(0xffffffffff000000ull,0x4900,9984,1);
  dirty_words(std::rotl(context,41)&0xfffff80000000000ull,0x2100,10316,1);
  dirty_words(std::rotl(context,25)&0xffff000000000000ull,0x2000,10240,1);
  dirty_words(std::rotl(context,4)&0xfffff80000000000ull,0x2280,10468,1);
  dirty_words(std::rotl(misc,26)&0xfffffffffc000000ull,0x2300,10552,1);
  dirty_words(std::rotl(misc,18)&0xff00000000000000ull,0x2380,10704,1);
  // The XDK needs the reserved tail for segment finalization. Like its draw
  // prologue, test the post-constant cursor before emitting the draw itself.
  if(!fast || uint64_t(old_write)+size>ReadXdkWord(device,48)) {
    set_refusal(XdkDrawRefusal::FastRingLimit);
    return XdkDrawResult::RingFull;
  }
  // VGT_INDX_OFFSET precedes both auto-index and DMA draws in the XDK.
  word(0x2102);word(args.first_or_base);
  const uint32_t instance=device[11915];
  const uint32_t visibility=(device[11850+instance]|(instance?256u:0u))&0x13fu;
  word(args.indexed?0xc0032201u:0xc0012201u);word(visibility);
  uint32_t initiator=(args.count<<16)|(args.primitive&63u);
  if(args.indexed) {
    const bool index32=(args.index_header&0x80000000u)!=0;
    const uint32_t address=args.index_address+args.start_index*(index32?4u:2u);
    const uint32_t physical=(address&0x1fffffffu)+(address>=0xe0000000u?0x1000u:0u);
    word(initiator|(index32?2048u:0u));word(physical);
    word((std::rotl(args.index_header,1)&0xc0000000u)|
         (args.count*(index32?2u:1u)));
  } else word(initiator|128u);
  if(size>ring.size() || size>UINT32_MAX-old_write) {
    set_refusal(XdkDrawRefusal::RingBufferFull);
    return XdkDrawResult::RingFull;
  }
  std::memcpy(ring.data(),commands.data(),size);
  std::memset(device.data(),0,40);
  if(bools) {
    const uint64_t invalidated=0xffffffffff000000ull;
    for(unsigned i=0;i<8;++i)device[11568+i]=uint8_t(invalidated>>(56-i*8));
  }
  if(common_shader) {
    for(unsigned i=0;i<4;++i)device[10432+i]=uint8_t(shader_10432>>(24-i*8));
    device[10809]=shader_10809;
    device[10810]=shader_10810;
    if(shader_write_12424)
      for(unsigned i=0;i<4;++i)device[12424+i]=uint8_t(shader_12424>>(24-i*8));
    if(shader_cached_extended) {
      auto put32=[](uint8_t* target,uint32_t value) {
        for(unsigned i=0;i<4;++i) target[i]=uint8_t(value>>(24-i*8));
      };
      auto put64=[&](uint8_t* target,uint64_t value) {
        put32(target,uint32_t(value>>32));put32(target+4,uint32_t(value));
      };
      put32(device.data()+10400,shader_10400);
      put32(device.data()+10404,shader_10404);
      put32(device.data()+10408,shader_10408);
      put64(device.data()+11552,shader_cache_11552);
      put64(device.data()+11560,shader_cache_11560);
      put32(guest.base+shader_cache_stamp_address,shader_cache_stamp);
    }
  }
  const uint32_t next=old_write+uint32_t(size);
  for(unsigned i=0;i<4;++i)device[40+i]=uint8_t(next>>(24-i*8));
  return XdkDrawResult::Submitted;
}
}
