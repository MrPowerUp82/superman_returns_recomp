#pragma once
namespace rex {class Runtime;}
// Local benchmark aid. No driver or file polling without SR_BENCH_INPUT.
void InstallBenchmarkInput(rex::Runtime&);
