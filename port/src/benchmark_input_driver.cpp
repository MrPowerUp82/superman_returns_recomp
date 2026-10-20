#include "benchmark_input_driver.h"
#include "benchmark_input.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
namespace {
using rex::X_STATUS;using rex::X_RESULT;
constexpr auto Device=static_cast<rex::input::DeviceId>(0x535242454E434800ull);
class BenchmarkDriver final : public rex::input::InputDriver {
public:
  explicit BenchmarkDriver(std::string path):InputDriver(nullptr,0),path_(std::move(path)){}
  rex::X_STATUS Setup() override {return X_STATUS_SUCCESS;}
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;info.id=Device;info.name="Local benchmark";info.synthetic=true;out.push_back(std::move(info));
  }
  rex::X_RESULT GetDeviceState(rex::input::DeviceId id,rex::input::X_INPUT_STATE* out) override {
    if(id!=Device)return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    using clock=std::chrono::steady_clock;
    const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
    if(now>=next_read_) {
      next_read_=now+100;
      std::ifstream file(path_,std::ios::binary);std::array<char,129> bytes{};
      if(file) {file.read(bytes.data(),bytes.size());const auto count=file.gcount();
        if(count>0 && count<128 && input_.Update(std::string_view(bytes.data(),size_t(count)),now))REXLOG_INFO("benchmark input: accepted {}",std::string(bytes.data(),size_t(count)));
      }
    }
    const auto state=input_.Sample(now);if(state!=last_){last_=state;++packet_;}
    if(out){*out={};out->packet_number=packet_;out->gamepad.buttons=state.buttons;out->gamepad.thumb_ly=state.forward;}
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id,uint32_t,rex::input::X_INPUT_CAPABILITIES* out) override {
    if(id!=Device)return X_ERROR_DEVICE_NOT_CONNECTED;
    if(out){*out={};out->type=1;out->sub_type=1;out->gamepad.buttons=0xffff;out->gamepad.thumb_ly=int16_t(32767);}
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId id,rex::input::X_INPUT_VIBRATION*) override {return id==Device?X_ERROR_SUCCESS:X_ERROR_DEVICE_NOT_CONNECTED;}
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId id,uint32_t,rex::input::X_INPUT_KEYSTROKE*) override {return id==Device?X_ERROR_EMPTY:X_ERROR_DEVICE_NOT_CONNECTED;}
private:
  std::string path_;std::mutex mutex_;superman_returns::BenchmarkInput input_;
  superman_returns::BenchmarkPadState last_;uint64_t next_read_=0;uint32_t packet_=0;
};
}
void InstallBenchmarkInput(rex::Runtime& runtime) {
  const char* path=std::getenv("SR_BENCH_INPUT");if(!path || !*path)return;
  if(auto* system=static_cast<rex::input::InputSystem*>(runtime.input_system())) {
    system->AddDriver(std::make_unique<BenchmarkDriver>(path));
    REXLOG_INFO("benchmark input: enabled local command file {}",path);
  }
}
