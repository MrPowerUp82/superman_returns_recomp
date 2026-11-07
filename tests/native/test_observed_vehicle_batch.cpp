#include "test_main.h"
#include "../../port/src/graphics/observed_vehicle_batch.h"
using namespace superman_returns::graphics;
namespace {
constexpr uint32_t entry=0x20000;
VehicleBatchShape shape{0x40000,0x50000,6,1};
VehicleBatchMember Member(ObservedInstanceToken token,uint32_t part=0) {
  VehicleBatchMember m;m.token=token;
  m.part_words={token.address,0x823103C8,0x82021930,0x60000,0x70000,9,part,part*4,part};
  for(size_t i:{0u,5u,10u,15u})m.palette[i]=std::bit_cast<uint32_t>(1.f);
  m.palette[12]=std::bit_cast<uint32_t>(float(token.address));return m;
}
std::array<VehicleBatchShape,2> Shapes() {return {shape,{}};}
bool Snapshot(ObservedVehicleBatchRegistry& registry,const ObservedInstanceLifetimes& lifetimes,
    std::span<const uint32_t> words,uint32_t count,ObservedVehicleBatchSelection& output,uint32_t first=0) {
  const auto shapes=Shapes();const std::array<uint32_t,2> counts{count,0};
  return registry.CaptureUpload(entry,shapes,counts,shape.buffer+first*96,uint32_t(words.size()/4),words,lifetimes,output);
}
}
SR_TEST(vehicle_batch_exact_copy_order_and_live_generation_produce_stable_history_key) {
  ObservedInstanceLifetimes lifetimes;auto a=Member(*lifetimes.Create(0x80000));auto b=Member(*lifetimes.Create(0x90000),2);
  ObservedVehicleBatchRegistry registry;
  SR_CHECK(registry.ObserveAppend(entry,0,shape,0,1,&a,a.palette));
  SR_CHECK(registry.ObserveAppend(entry,0,shape,1,2,&b,b.palette));
  std::array<uint32_t,48> words;std::copy(a.palette.begin(),a.palette.end(),words.begin());std::copy(b.palette.begin(),b.palette.end(),words.begin()+24);
  ObservedVehicleBatchSelection before;SR_CHECK(Snapshot(registry,lifetimes,words,2,before));
  SR_CHECK(before.members==2 && before.rows==6 && before.source_route_observed);
  registry.Clear(entry);a.palette[12]=std::bit_cast<uint32_t>(234.f);b.palette[21]=std::bit_cast<uint32_t>(.8f);
  SR_CHECK(registry.ObserveAppend(entry,0,shape,0,1,&a,a.palette));SR_CHECK(registry.ObserveAppend(entry,0,shape,1,2,&b,b.palette));
  std::copy(a.palette.begin(),a.palette.end(),words.begin());std::copy(b.palette.begin(),b.palette.end(),words.begin()+24);
  ObservedVehicleBatchSelection moved;SR_CHECK(Snapshot(registry,lifetimes,words,2,moved));
  SR_CHECK(moved.instance_key==before.instance_key && moved.generation_key==before.generation_key);
  SR_CHECK(moved.palette_signature!=before.palette_signature);
  registry.Clear(entry);registry.ObserveAppend(entry,0,shape,0,1,&b,b.palette);registry.ObserveAppend(entry,0,shape,1,2,&a,a.palette);
  std::copy(b.palette.begin(),b.palette.end(),words.begin());std::copy(a.palette.begin(),a.palette.end(),words.begin()+24);
  ObservedVehicleBatchSelection reordered;SR_CHECK(Snapshot(registry,lifetimes,words,2,reordered));
  SR_CHECK(reordered.instance_key!=before.instance_key);
  lifetimes.Destroy(a.token.address);SR_CHECK(!Snapshot(registry,lifetimes,words,2,reordered));
  lifetimes.Create(a.token.address);SR_CHECK(!Snapshot(registry,lifetimes,words,2,reordered));
}
SR_TEST(vehicle_batch_unknown_holes_wrong_bytes_shape_and_reuse_never_inherit_members) {
  ObservedInstanceLifetimes lifetimes;auto a=Member(*lifetimes.Create(0x80000));ObservedVehicleBatchRegistry registry;
  registry.ObserveAppend(entry,0,shape,0,1,&a,a.palette);
  ObservedVehicleBatchSelection out;SR_CHECK(Snapshot(registry,lifetimes,a.palette,1,out));
  auto wrong=a.palette;wrong[12]^=1;SR_CHECK(!Snapshot(registry,lifetimes,wrong,1,out));
  // An unknown first append in a new epoch replaces the old known slot.
  SR_CHECK(!registry.ObserveAppend(entry,0,shape,0,1,nullptr,a.palette));
  SR_CHECK(!Snapshot(registry,lifetimes,a.palette,1,out));
  registry.ObserveAppend(entry,0,shape,1,2,&a,a.palette);
  SR_CHECK(Snapshot(registry,lifetimes,a.palette,2,out,1));
  std::array<uint32_t,48> all;std::copy(a.palette.begin(),a.palette.end(),all.begin());std::copy(a.palette.begin(),a.palette.end(),all.begin()+24);
  SR_CHECK(!Snapshot(registry,lifetimes,all,2,out));
  auto changed=shape;changed.packet+=4;registry.ObserveAppend(entry,0,changed,2,3,&a,a.palette);
  SR_CHECK(!Snapshot(registry,lifetimes,a.palette,3,out,2));
  registry.Clear(entry);SR_CHECK(!Snapshot(registry,lifetimes,a.palette,1,out));
  registry.ObserveAppend(entry,0,shape,3,4,&a,a.palette); // Unobserved prefix stays unknown.
  SR_CHECK(Snapshot(registry,lifetimes,a.palette,4,out,3));
  SR_CHECK(!Snapshot(registry,lifetimes,a.palette,4,out,0));
  registry.ObserveAppend(entry,0,shape,4,2,&a,a.palette);SR_CHECK(!Snapshot(registry,lifetimes,a.palette,2,out));
}
SR_TEST(vehicle_batch_full_queue_no_append_and_exact_chunk_bounds) {
  ObservedInstanceLifetimes lifetimes;auto a=Member(*lifetimes.Create(0x80000));ObservedVehicleBatchRegistry registry;
  for(uint32_t i=0;i<100;++i) SR_CHECK(registry.ObserveAppend(entry,0,shape,i,i+1,&a,a.palette));
  SR_CHECK(!registry.ObserveAppend(entry,0,shape,100,100,nullptr,a.palette));
  std::array<uint32_t,21*24> words;for(size_t i=0;i<21;++i)std::copy(a.palette.begin(),a.palette.end(),words.begin()+i*24);
  ObservedVehicleBatchSelection out;SR_CHECK(Snapshot(registry,lifetimes,words,100,out,79));
  SR_CHECK(!Snapshot(registry,lifetimes,words,100,out,80));
  const auto shapes=Shapes();const std::array<uint32_t,2> counts{100,0};
  SR_CHECK(!registry.CaptureUpload(entry,shapes,counts,shape.buffer+1,126,words,lifetimes,out));
  SR_CHECK(!registry.CaptureUpload(entry,shapes,counts,shape.buffer,125,words,lifetimes,out));
  a.palette[15]=0;SR_CHECK(!ValidVehicleBatchMember(a));
}
SR_TEST(vehicle_batch_shader_requires_actual_source_palette_range_and_fetch_for_body_and_wheels) {
  std::array<uint32_t,1024> floats{};std::array<uint32_t,4> bools{};
  floats[16]=std::bit_cast<uint32_t>(600.f);
  ObservedVehicleBatchSelection selection{3,4,0,2,6,true};
  for(size_t i=0;i<48;++i)floats[400+i]=std::bit_cast<uint32_t>(float(i));
  selection.palette_signature=VehicleBatchHashWords(std::span(floats).subspan(400,48));
  for(uint64_t shader:{0x9A62E7C65051447Aull,0x315E6843ECE68A00ull}) {
    ObservedVehicleBatchShaderInputs inputs;
    SR_CHECK(CaptureObservedVehicleBatchShaderInputs(shader,selection,floats,bools,true,false,0,1200,inputs));
    SR_CHECK(inputs.instance_key==3 && inputs.generation_key==4);
    for(unsigned fault=0;fault<10;++fault) {
      auto batch=selection;auto values=floats;bool fetch=true,indexed=false;uint32_t first=0,end=1200;
      switch(fault) {
        case 0:batch.source_route_observed=false;break;case 1:batch.rows=4;break;
        case 2:batch.members=22;break;case 3:values[400]^=1;break;
        case 4:fetch=false;break;case 5:indexed=true;break;case 6:first=1;break;
        case 7:end++;break;case 8:values[17]=std::bit_cast<uint32_t>(1.f);break;
        case 9:values[24]=0x7fc00000;break;
      }
      SR_CHECK(!CaptureObservedVehicleBatchShaderInputs(shader,batch,values,bools,fetch,indexed,first,end,inputs));
    }
  }
  ObservedVehicleBatchShaderInputs inputs;
  SR_CHECK(!CaptureObservedVehicleBatchShaderInputs(0xDF1EB50388CC2563ull,selection,floats,bools,true,false,0,1200,inputs));
}
