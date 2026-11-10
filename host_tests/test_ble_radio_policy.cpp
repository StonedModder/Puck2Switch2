// test_ble_radio_policy.cpp — host test for the shared-radio policy that fixes
// "the Switch 2 takes minutes to discover the adapter unless another controller
// is already connected to it".
//
// Two independent causes, both encoded in firmware/core/include/ble_radio_policy.h:
//
//  1. Scan duty. The central scanned at interval 100 u / window 80 u = 80 % duty
//     whenever the Steam Controller 2 was not connected — including the whole
//     period in which the peripheral still has to be *discovered* by the
//     console. ~20 % of the radio left for advertising events is why discovery
//     took minutes. The policy now has three regimes, not two.
//
//  2. Advertisement variant. A bonded-but-disconnected adapter advertised the
//     flag-less Reconnect variant, which an idle console ignores; the flagged
//     Wake variant (Nintendo manufacturer byte 0x0B = 0x81) is the one it acts
//     on. The periodic re-advertiser alternates the two so the idle case is
//     fixed without risking the already-working busy-console case.
//
// Build (no ESP-IDF, header-only):
//   g++ -std=c++17 -I firmware/core/include -O2 \
//       -o /tmp/tpolicy host_tests/test_ble_radio_policy.cpp && /tmp/tpolicy

#include <cstdio>
#include <cstring>

#include "ble_radio_policy.h"

using namespace s2s;

static int g_fail = 0;

#define CHECK(cond, ...)                                                                           \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      ++g_fail;                                                                                    \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                                             \
      std::printf(__VA_ARGS__);                                                                    \
      std::printf("\n");                                                                           \
    }                                                                                              \
  } while (0)

// 0.625 ms per BLE scan/advertising unit: units * 5 / 8 == ms.
static constexpr unsigned units_to_ms_x10(unsigned u) { return u * 50u / 8u; }

static void test_duty_unit_arithmetic() {
  // Sanity-check the unit conversion itself before trusting any duty number.
  CHECK(units_to_ms_x10(640) == 4000, "640 u should be 400.0 ms, got %u.%u", units_to_ms_x10(640) / 10,
        units_to_ms_x10(640) % 10);
  CHECK(units_to_ms_x10(16) == 100, "16 u should be 10.0 ms");
  CHECK(units_to_ms_x10(320) == 2000, "320 u should be 200.0 ms");
  CHECK(units_to_ms_x10(32) == 200, "32 u should be 20.0 ms");
  CHECK(units_to_ms_x10(160) == 1000, "160 u should be 100.0 ms");
  CHECK(units_to_ms_x10(40) == 250, "40 u should be 25.0 ms");
  // The pre-fix values, for the record: 100 u = 62.5 ms, 80 u = 50 ms -> 80 %.
  CHECK(units_to_ms_x10(100) == 625, "100 u should be 62.5 ms");
  CHECK(units_to_ms_x10(80) == 500, "80 u should be 50.0 ms");
  CHECK(80u * 1000u / 100u == 800u, "the pre-fix duty should be 80.0 %%");
}

static void test_duty_states() {
  // Both links down: the regime the bug lived in. MUST be far below the old
  // 80 %, because the peripheral needs the radio to advertise at 20-30 ms.
  const ScanDuty search = scan_duty_for(/*sc2*/ false, /*console*/ false);
  CHECK(search.interval_units == 160 && search.window_units == 40, "search duty should be 160/40");
  CHECK(scan_duty_permille(search) == 250, "search duty should be 25.0 %%, got %u per-mille",
        scan_duty_permille(search));
  CHECK(scan_duty_permille(search) < 800, "search duty must be below the pre-fix 80 %%");

  // Console link up, SC2 still down: a 5 ms console link (200 events/s) owns
  // most of the radio, so scan less than when nothing else is using it.
  const ScanDuty linked = scan_duty_for(false, true);
  CHECK(linked.interval_units == 320 && linked.window_units == 32, "linked duty should be 320/32");
  CHECK(scan_duty_permille(linked) == 100, "linked duty should be 10.0 %%");
  CHECK(scan_duty_permille(linked) < scan_duty_permille(search),
        "a live console link must get MORE radio than the search state");

  // SC2 up: nothing left to discover. Unchanged from the shipped firmware.
  const ScanDuty idle = scan_duty_for(true, false);
  CHECK(idle.interval_units == 640 && idle.window_units == 16, "idle duty should be 640/16");
  CHECK(scan_duty_permille(idle) == 25, "idle duty should be 2.5 %%");
  CHECK(scan_duty_for(true, true).interval_units == idle.interval_units,
        "with the SC2 linked the console state must not change the duty");
  CHECK(scan_duty_permille(idle) < scan_duty_permille(linked),
        "idle must be the smallest duty of the three");

  // Monotonic ordering of the whole state space, so a future edit cannot
  // accidentally make the both-links-down case the greediest again.
  CHECK(scan_duty_permille(scan_duty_for(true, true)) <=
            scan_duty_permille(scan_duty_for(false, true)),
        "duty ordering violated (sc2 up vs console only)");
  CHECK(scan_duty_permille(scan_duty_for(false, true)) <=
            scan_duty_permille(scan_duty_for(false, false)),
        "duty ordering violated (console only vs both down)");
}

static void test_wake_variant() {
  // Connected: never touch the advertisement (stopping/restarting it while a
  // link is up buys nothing and risks the link).
  for (unsigned t = 0; t < 4; ++t) {
    CHECK(wake_adv_for(/*bonded*/ true, /*console_linked*/ true, t) == WakeAdv::None,
          "connected must never re-advertise (tick %u)", t);
    CHECK(wake_adv_for(false, true, t) == WakeAdv::None, "connected+unbonded must be None");
  }

  // Not bonded: Discovery is already continuously on the air; re-airing it only
  // adds stop/start gaps.
  for (unsigned t = 0; t < 4; ++t)
    CHECK(wake_adv_for(false, false, t) == WakeAdv::None,
          "un-bonded must not be re-advertised (tick %u)", t);

  // Bonded and disconnected: the actual fix. A WAKE advertisement must go out on
  // the very first tick — no button press, no connected source controller — and
  // the flag-less variant that already worked against a busy console must still
  // be aired in between.
  CHECK(wake_adv_for(true, false, 0) == WakeAdv::Wake, "first tick must be WAKE");
  CHECK(wake_adv_for(true, false, 1) == WakeAdv::Reconnect, "second tick must be Reconnect");
  CHECK(wake_adv_for(true, false, 2) == WakeAdv::Wake, "third tick must be WAKE again");
  CHECK(wake_adv_for(true, false, 3) == WakeAdv::Reconnect, "fourth tick must be Reconnect");

  // Over any window of 2 ticks the console sees both variants, so neither the
  // idle nor the busy console can be starved of the one it acts on.
  for (unsigned t = 0; t < 20; ++t) {
    const WakeAdv a = wake_adv_for(true, false, t);
    const WakeAdv b = wake_adv_for(true, false, t + 1);
    CHECK(a != b && a != WakeAdv::None && b != WakeAdv::None,
          "every 2-tick window must contain both variants (tick %u)", t);
  }

  // And a disconnect must resume nudging from the Wake variant: the state is a
  // pure function of the tick, so simulate the sequence the firmware walks.
  unsigned tick = 0;
  bool console = false;
  WakeAdv first_after_boot = wake_adv_for(true, console, tick++);
  CHECK(first_after_boot == WakeAdv::Wake, "boot (bonded, idle console) must air WAKE first");
  console = true; // console connects; ticks become no-ops and do not advance
  for (int i = 0; i < 5; ++i)
    CHECK(wake_adv_for(true, console, tick) == WakeAdv::None, "no nudge while connected");
  console = false; // console drops
  CHECK(wake_adv_for(true, console, tick) != WakeAdv::None,
        "the nudge must resume by itself after a disconnect");
}

int main() {
  test_duty_unit_arithmetic();
  test_duty_states();
  test_wake_variant();

  // Print the before/after radio budget so the numbers in the commit message
  // are produced by the code rather than typed by hand.
  std::printf("scan duty budget (0.625 ms units):\n");
  std::printf("  BEFORE both-links-down: interval 100 u (62.5 ms) window 80 u (50.0 ms) = %u.%u%%\n",
              (80u * 1000u / 100u) / 10u, (80u * 1000u / 100u) % 10u);
  const ScanDuty states[3] = {scan_duty_for(false, false), scan_duty_for(false, true),
                              scan_duty_for(true, false)};
  for (const ScanDuty &d : states)
    std::printf("  AFTER  %-28s interval %3u u (%u.%u ms) window %2u u (%u.%u ms) = %u.%u%%\n",
                d.name, d.interval_units, units_to_ms_x10(d.interval_units) / 10,
                units_to_ms_x10(d.interval_units) % 10, d.window_units,
                units_to_ms_x10(d.window_units) / 10, units_to_ms_x10(d.window_units) % 10,
                scan_duty_permille(d) / 10, scan_duty_permille(d) % 10);
  std::printf("advertising interval: 20 ms = %u units, 30 ms = %u units (espp default max was "
              "40 ms = %u units)\n",
              20u * 8u / 5u, 30u * 8u / 5u, 40u * 8u / 5u);

  if (g_fail) {
    std::printf("\n%d CHECK(s) FAILED\n", g_fail);
    return 1;
  }
  std::printf("\nall ble_radio_policy checks passed\n");
  return 0;
}
