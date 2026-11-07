// Windows microbenchmark, not a split-screen FPS benchmark.
// Build with clang++ -std=c++20 -O2 tools/coop/bench_guest_symbol.cpp -o <exe>.
#include "../../port/src/coop_guest_string.h"
#include <windows.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

int main() {
  char memory[256]{};
  const char* symbols[]={"Main_Hud","StaminaMeter","GenericMeter","mFillLeft","mNotch"};
  for(size_t i=0;i<5;++i)std::strcpy(memory+i*32,symbols[i]);
  uint64_t calls=0;
  auto read=[&](uint64_t address,void* out,size_t size) {
    ++calls;SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),memory+address-1,out,size,&copied) && copied==size;
  };
  auto legacy=[&](uint32_t address) {
    std::string result;
    for(size_t i=0;i<128;++i) {
      uint8_t byte=0;if(!read(uint64_t(address)+i,&byte,1))return std::string{};
      if(!byte)return result;
      if(byte<32 || byte>126)return std::string{};
      result+=char(byte);
    }
    return std::string{};
  };
  std::cout<<"round,mode,strings,read_calls,total_ms,checksum\n";
  for(int round=1;round<=3;++round)for(int step=0;step<2;++step) {
    const bool batched=(step==(round%2));
    calls=0;uint64_t checksum=0;
    const auto start=std::chrono::steady_clock::now();
    for(int n=0;n<20000;++n)for(uint32_t i=0;i<5;++i) {
      auto value=batched?superman_returns::coop::ReadGuestSymbol(1+i*32,read):legacy(1+i*32);
      if(value!=symbols[i])return 1;
      checksum+=value.size();
    }
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<round<<','<<(batched?"batched":"byte")<<",100000,"<<calls<<','<<ms<<','<<checksum<<'\n';
  }
}
