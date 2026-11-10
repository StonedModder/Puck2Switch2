// Compile: g++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/esp32s3/main
//          host_tests/test_ble_radio_policy.cpp -o "$TMPDIR/test_ble_radio_policy"
#define S2S_SCAN_POLICY_ONLY
#include "sc2_central.hpp"
#include <cassert>
#include <cstdio>

int main() {
  constexpr auto p = s2s::sc2_scan_params();
  static_assert(p.interval_ms == 100 && p.window_ms == 80);
  static_assert(s2s::ble_scan_units(p.interval_ms) == 160);
  static_assert(s2s::ble_scan_units(p.window_ms) == 128);
  assert(p.window_ms * 100 / p.interval_ms == 80);
  std::printf("v1.1.0 initial scan policy: %u ms / %u ms (%u%%)\n",
              p.interval_ms, p.window_ms, p.window_ms * 100 / p.interval_ms);
  std::puts("PASS scan policy");
}
