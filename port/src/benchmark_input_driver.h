#pragma once
#include <memory>
namespace rex {class Runtime;}
namespace rex::input {class DeviceAssignment;}
// Local benchmark aid. No file polling without SR_BENCH_INPUT / SR_BENCH_INPUT_P2.
void InstallBenchmarkInput(rex::Runtime&);
// Normal SDK slots, with only an explicitly enabled P2 benchmark routed to port 1.
std::unique_ptr<rex::input::DeviceAssignment> MakeCoopDeviceAssignment();
