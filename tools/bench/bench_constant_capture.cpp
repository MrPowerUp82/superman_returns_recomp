// Manual optimized-build benchmark. Link the real constant_snapshot.cpp object.
// Optional local gate: bench_constant_capture.exe 700 (host ns/bank).
#include "../../port/src/graphics/guest/constant_snapshot.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace superman_returns::graphics::guest;
int main(int argc,char** argv) {
  using Bank=std::array<uint32_t,1024>;
  const double ceiling=argc==2?std::strtod(argv[1],nullptr):0;
  if(argc>2 || (argc==2 && !(ceiling>0))) return 2;
  std::vector<Bank> inputs(512);
  uint32_t random=0x41378421;
  for(auto& b:inputs) for(auto& v:b) {
    random^=random<<13;random^=random>>17;random^=random<<5;v=random;
  }
  Bank out{};std::string error;uint64_t checksum=0;
  constexpr size_t iterations=131072;
  double host_median=0;
  for(bool be:{false,true}) {
    std::vector<double> samples;
    for(unsigned round=0;round<9;++round) {
      auto start=std::chrono::steady_clock::now();
      for(size_t i=0;i<iterations;++i) {
        if(!CaptureFloatConstants(inputs[i%inputs.size()],be,out,error)) return 2;
        checksum+=out[(i>>9)%1024];
      }
      const double ns=std::chrono::duration<double,std::nano>(
          std::chrono::steady_clock::now()-start).count()/iterations;
      std::printf("big_endian=%u round=%u ns_per_bank=%.3f\n",unsigned(be),round,ns);
      if(round) samples.push_back(ns); // first round warms code/data
    }
    std::sort(samples.begin(),samples.end());
    const double median=(samples[3]+samples[4])/2;
    std::printf("big_endian=%u median_ns_per_bank=%.3f\n",unsigned(be),median);
    if(!be) host_median=median;
  }
  std::printf("checksum=%llu\n",(unsigned long long)checksum);
  if(ceiling>0 && host_median>ceiling) {
    std::printf("FAIL local host ceiling %.3f ns, measured %.3f ns\n",ceiling,host_median);
    return 1;
  }
  return 0;
}
