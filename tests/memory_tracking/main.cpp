#include "../native/test_main.h"
#include <string_view>
#if !defined(SR_TRACKING_STOCK_CLIENT)
int RunTrackingFailureCase(int failure);
#endif
int main(int argc, char** argv) {
#if !defined(SR_TRACKING_STOCK_CLIENT)
  if (argc == 2 && std::string_view(argv[1]) == "--lost-contract") return RunTrackingFailureCase(1);
  if (argc == 2 && std::string_view(argv[1]) == "--arm-failure") return RunTrackingFailureCase(0);
  if (argc == 2 && std::string_view(argv[1]) == "--uninitialized") return RunTrackingFailureCase(2);
#else
  (void)argc; (void)argv;
#endif
  return sr_test::RunAll();
}
