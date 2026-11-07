#include "../../port/src/graphics/presentation_pacing.h"
#include "../../port/src/graphics/presentation_cadence.h"
#include "test_main.h"
#include <sstream>
#include <cmath>

using namespace superman_returns::graphics;

SR_TEST(presentation_target_absent_or_invalid_is_unlimited) {
  SR_CHECK_EQ(ParsePresentationTarget(""),0u);
  SR_CHECK_EQ(ParsePresentationTarget("-1"),0u);
  SR_CHECK_EQ(ParsePresentationTarget("60x"),0u);
  SR_CHECK_EQ(ParsePresentationTarget("120"),120u);
  SR_CHECK_EQ(ParsePresentationTarget("999999"),1000u);
}

SR_TEST(presentation_deadlines_have_no_fractional_period_drift) {
  for (uint32_t hz : {30u,60u,120u}) {
    PresentationDeadline p;
    p.Configure(hz,0);
    SR_CHECK_EQ(p.Remaining(0),0u);
    for (uint32_t i=0;i<hz;++i) p.Complete(p.deadline());
    SR_CHECK_EQ(p.deadline(),1000000000u);
  }
}
SR_TEST(presentation_jitter_retains_phase_without_catchup_bursts) {
  PresentationDeadline p;
  p.Configure(100,0);p.Complete(0);
  p.Complete(10200000);
  SR_CHECK_EQ(p.deadline(),20000000u);
  p.Complete(55900000);
  SR_CHECK_EQ(p.deadline(),60000000u);
  SR_CHECK_EQ(p.skipped(),3u);
  p.Configure(30,55900000);
  SR_CHECK_EQ(p.Remaining(55900000),0u);
  p.Configure(0,55900000);
  SR_CHECK_EQ(p.Remaining(0),0u);
}
SR_TEST(presentation_sleep_history_is_bounded_and_forgets_old_outliers) {
  PresentationDeadline p;
  SR_CHECK_EQ(p.WakeMargin(),100000u);
  for (unsigned i=0;i<32;++i) p.ObserveSleep(1000000,1300000);
  SR_CHECK_EQ(p.WakeMargin(),350000u);
  for (unsigned i=0;i<32;++i) p.ObserveSleep(1000000,20000000);
  SR_CHECK_EQ(p.WakeMargin(),1000000u);
  for (unsigned i=0;i<32;++i) p.ObserveSleep(1000000,1000000);
  SR_CHECK_EQ(p.WakeMargin(),100000u);
}
SR_TEST(presentation_expired_fractional_deadline_counts_the_skipped_slot) {
  PresentationDeadline p;
  p.Configure(60,0);p.Complete(0);
  p.Complete(33333333);
  SR_CHECK_EQ(p.deadline(),50000000u);
  SR_CHECK_EQ(p.skipped(),1u);
  PresentationDeadline punctual;
  punctual.Configure(60,0);punctual.Complete(0);
  punctual.Complete(16666666);
  SR_CHECK_EQ(punctual.deadline(),33333333u);
  SR_CHECK_EQ(punctual.skipped(),0u);
}
SR_TEST(presentation_csv_records_api_failures_without_counting_them_as_frames) {
  std::ostringstream out;
  PresentationCsv csv(out);
  csv.Record("d3d12",1,0,"real",100,200,0,true);
  csv.Record("d3d12",1,1,"repeat",300,400,1,false);
  csv.Record("d3d12",2,0,"real",500,600,0,true);
  const auto rows=out.str();
  SR_CHECK(rows.find("clock_domain,backend,call,source_serial")!=std::string::npos);
  SR_CHECK(rows.find("steady_cpu_api_return,d3d12,1,1,real,100,200,100,0,0,1,0")!=std::string::npos);
  SR_CHECK(rows.find("steady_cpu_api_return,d3d12,2,1,repeat,300,400,100,200,1,0,1")!=std::string::npos);
  SR_CHECK(rows.find("steady_cpu_api_return,d3d12,3,2,real,500,600,100,400,0,1,0")!=std::string::npos);
}

SR_TEST(cadence_evaluation_integer_divisor_modes) {
  const auto c60_on_60 = EvaluateDisplayCadence(60, 60);
  SR_CHECK_EQ(uint32_t(c60_on_60.mode), uint32_t(CadenceMode::kExact1to1));
  SR_CHECK_EQ(c60_on_60.divisor, 1u);
  SR_CHECK_EQ(c60_on_60.target_period_ns, 16666666ull);
  SR_CHECK(std::abs(c60_on_60.ratio - 1.0) < 0.001);

  const auto c30_on_60 = EvaluateDisplayCadence(30, 60);
  SR_CHECK_EQ(uint32_t(c30_on_60.mode), uint32_t(CadenceMode::kIntegerDivisor));
  SR_CHECK_EQ(c30_on_60.divisor, 2u);
  SR_CHECK_EQ(c30_on_60.target_period_ns, 33333333ull);
  SR_CHECK(std::abs(c30_on_60.ratio - 2.0) < 0.001);

  const auto c60_on_120 = EvaluateDisplayCadence(60, 120);
  SR_CHECK_EQ(uint32_t(c60_on_120.mode), uint32_t(CadenceMode::kIntegerDivisor));
  SR_CHECK_EQ(c60_on_120.divisor, 2u);
  SR_CHECK_EQ(c60_on_120.target_period_ns, 16666666ull);
  SR_CHECK(std::abs(c60_on_120.ratio - 2.0) < 0.001);

  const auto c30_on_120 = EvaluateDisplayCadence(30, 120);
  SR_CHECK_EQ(uint32_t(c30_on_120.mode), uint32_t(CadenceMode::kIntegerDivisor));
  SR_CHECK_EQ(c30_on_120.divisor, 4u);
  SR_CHECK_EQ(c30_on_120.target_period_ns, 33333333ull);
  SR_CHECK(std::abs(c30_on_120.ratio - 4.0) < 0.001);
}

SR_TEST(cadence_evaluation_fractional_vrr_and_unlocked_modes) {
  const auto c60_on_144 = EvaluateDisplayCadence(60, 144);
  SR_CHECK_EQ(uint32_t(c60_on_144.mode), uint32_t(CadenceMode::kFractionalVRR));
  SR_CHECK_EQ(c60_on_144.divisor, 0u);
  SR_CHECK_EQ(c60_on_144.target_period_ns, 16666666ull);
  SR_CHECK(std::abs(c60_on_144.ratio - 2.4) < 0.001);

  const auto c30_on_144 = EvaluateDisplayCadence(30, 144);
  SR_CHECK_EQ(uint32_t(c30_on_144.mode), uint32_t(CadenceMode::kFractionalVRR));
  SR_CHECK_EQ(c30_on_144.divisor, 0u);
  SR_CHECK(std::abs(c30_on_144.ratio - 4.8) < 0.001);

  const auto c_unlocked = EvaluateDisplayCadence(0, 144);
  SR_CHECK_EQ(uint32_t(c_unlocked.mode), uint32_t(CadenceMode::kUnlocked));
  SR_CHECK_EQ(c_unlocked.target_period_ns, 0ull);
  SR_CHECK_EQ(c_unlocked.ratio, 0.0);
}

SR_TEST(cadence_host_display_query_sane_range) {
  const uint32_t host_hz = QueryHostDisplayRefreshRate();
  SR_CHECK(host_hz >= 24u);
  SR_CHECK(host_hz <= 1000u);
}
