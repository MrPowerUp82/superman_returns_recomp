#include "benchmark_input_driver.h"
#include "benchmark_input.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include "title_benchmark.h"
namespace {
using rex::X_STATUS;using rex::X_RESULT;
constexpr auto Device=static_cast<rex::input::DeviceId>(0x535242454E434800ull);
constexpr auto DeviceP2=static_cast<rex::input::DeviceId>(0x535242454E434801ull);
bool p2_enabled = false;
class CoopBenchmarkAssignment final : public rex::input::DeviceAssignment {
public:
  void OnDevicesChanged(const std::vector<rex::input::DeviceInfo>& devices) override {
    std::vector<rex::input::DeviceInfo> normal;
    connected_ = false;
    for (const auto& device : devices) {
      if (device.id == DeviceP2) connected_ = true;
      else normal.push_back(device);
    }
    slots_.OnDevicesChanged(normal);
  }
  void DevicesForUser(uint32_t user, std::vector<rex::input::DeviceId>& out) const override {
    slots_.DevicesForUser(user, out);
    if (user == 1 && connected_) out.push_back(DeviceP2);
  }
private:
  rex::input::SlotAssignment slots_;
  bool connected_ = false;
};
class BenchmarkDriver final : public rex::input::InputDriver {
public:
  explicit BenchmarkDriver(std::string path, bool p2=false):InputDriver(nullptr,0),path_(std::move(path)),device_(p2?DeviceP2:Device){}
  rex::X_STATUS Setup() override {return X_STATUS_SUCCESS;}
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;info.id=device_;info.name=device_==Device?"Local benchmark":"Local benchmark P2";info.synthetic=true;out.push_back(std::move(info));
  }
  rex::X_RESULT GetDeviceState(rex::input::DeviceId id,rex::input::X_INPUT_STATE* out) override {
    if(id!=device_)return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    using clock=std::chrono::steady_clock;
    const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
    if(now>=next_read_) {
      next_read_=now+100;
      std::ifstream file(path_,std::ios::binary);std::array<char,129> bytes{};
      if(file) {file.read(bytes.data(),bytes.size());const auto count=file.gcount();
        if(count>0 && count<128 && input_.Update(std::string_view(bytes.data(),size_t(count)),now))REXLOG_INFO("benchmark input: accepted {}port={}",std::string(bytes.data(),size_t(count)),device_==Device?0:1);
      }
    }
    const auto state=input_.Sample(now);if(state!=last_){last_=state;++packet_;}
    if(out){*out={};out->packet_number=packet_;out->gamepad.buttons=state.buttons;out->gamepad.thumb_ly=state.forward;}
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id,uint32_t,rex::input::X_INPUT_CAPABILITIES* out) override {
    if(id!=device_)return X_ERROR_DEVICE_NOT_CONNECTED;
    if(out){*out={};out->type=1;out->sub_type=1;out->gamepad.buttons=0xffff;out->gamepad.thumb_ly=int16_t(32767);}
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId id,rex::input::X_INPUT_VIBRATION*) override {return id==device_?X_ERROR_SUCCESS:X_ERROR_DEVICE_NOT_CONNECTED;}
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId id,uint32_t,rex::input::X_INPUT_KEYSTROKE*) override {return id==device_?X_ERROR_EMPTY:X_ERROR_DEVICE_NOT_CONNECTED;}
private:
  std::string path_;rex::input::DeviceId device_;std::mutex mutex_;superman_returns::BenchmarkInput input_;
  superman_returns::BenchmarkPadState last_;uint64_t next_read_=0;uint32_t packet_=0;
};
}
void InstallBenchmarkInput(rex::Runtime& runtime) {
  const char* path=std::getenv("SR_BENCH_INPUT");if(!path || !*path)return;
  if(auto* system=static_cast<rex::input::InputSystem*>(runtime.input_system())) {
    system->AddDriver(std::make_unique<BenchmarkDriver>(path));
    REXLOG_INFO("benchmark input: enabled local command file {}",path);
    const char* p2 = std::getenv("SR_BENCH_INPUT_P2");
    if (p2 && *p2 && !superman_returns::benchmark::Active() &&
        rex::cvar::GetFlagByName("sr_coop_input") == "true") {
      system->AddDriver(std::make_unique<BenchmarkDriver>(p2, true));
      p2_enabled = true;
      REXLOG_INFO("benchmark input: enabled isolated guest port 1 command file {}", p2);
    }
  }
}

std::unique_ptr<rex::input::DeviceAssignment> MakeCoopDeviceAssignment() {
  if (p2_enabled) return std::make_unique<CoopBenchmarkAssignment>();
  return std::make_unique<rex::input::SlotAssignment>();
}
