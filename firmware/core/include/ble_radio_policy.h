// ble_radio_policy.h — pure, host-testable policy for sharing ONE radio between
// the BLE central (Steam Controller 2 scan) and the BLE peripheral (Switch 2 Pro
// advertising / link).
//
// Why this exists as a separate header: both decisions below were the root cause
// of "the Switch 2 takes minutes to discover the adapter unless another
// controller is already connected", and both are trivially wrong-able. Keeping
// them as constexpr functions means the transitions are asserted by a host test
// (host_tests/test_ble_radio_policy.cpp) instead of being re-derived by hand
// every time the firmware is touched.
//
// Unit convention: BLE scan interval/window and legacy advertising interval are
// all in units of 0.625 ms. units * 5 / 8 == milliseconds.
#pragma once

#include <cstdint>

namespace s2s {

/// Scan interval/window in BLE units of 0.625 ms.
struct ScanDuty {
  uint16_t interval_units;
  uint16_t window_units;
  const char *name;
};

/// Scan duty while the SC2 link is UP: there is nothing left to discover, and
/// the console drives its link at 5 ms (200 connection events/s), so the scan
/// must be a trickle. 640 u = 400 ms interval, 16 u = 10 ms window -> 2.5 %.
inline constexpr ScanDuty kScanDutyIdle{640, 16, "IDLE 2.5% (SC2 linked)"};

/// Scan duty while the SC2 is DOWN but the console link is UP: we still have to
/// find/reconnect the controller, but a 5 ms console link owns most of the
/// radio. 320 u = 200 ms interval, 32 u = 20 ms window -> 10 %.
inline constexpr ScanDuty kScanDutyLinked{320, 32, "LINKED 10% (console linked)"};

/// Scan duty while BOTH links are down: we must be *discoverable* (advertising
/// at 20-30 ms) at least as much as we must be *discovering*. 160 u = 100 ms
/// interval, 40 u = 25 ms window -> 25 %, leaving 75 % of the radio for
/// advertising events. The pre-fix value here was 100 u / 80 u = 80 %, which
/// starved advertising and is the radio half of the slow-discovery bug.
inline constexpr ScanDuty kScanDutySearch{160, 40, "SEARCH 25% (both links down)"};

/// Pick the scan duty for the current link state. Pure.
inline constexpr ScanDuty scan_duty_for(bool sc2_linked, bool console_linked) {
  if (sc2_linked)
    return kScanDutyIdle; // nothing to discover; protect the console link
  if (console_linked)
    return kScanDutyLinked; // 5 ms console link owns the radio
  return kScanDutySearch;   // being discoverable matters at least as much
}

/// Scan duty cycle in per-mille (window/interval), for logging and for the
/// arithmetic in the test. 2.5 % -> 25.
inline constexpr unsigned scan_duty_permille(const ScanDuty &d) {
  return d.interval_units ? (unsigned)d.window_units * 1000u / d.interval_units : 0u;
}

/// Which advertisement variant the periodic re-advertiser should put on the air.
enum class WakeAdv : uint8_t {
  None,      ///< leave the current advertisement alone
  Wake,      ///< Nintendo mfr byte 0x0B = 0x81 ("user wants in") + bonded console addr
  Reconnect, ///< same payload, no wake flag — the passive variant
};

/// Periodic re-advertisement policy while we are NOT connected to the console.
///
/// The wake variant is the ONLY one an idle console acts on (byte 0x0B = 0x81),
/// and the passive Reconnect variant is the only one observed to work on an
/// already-busy console, so while bonded-and-disconnected we alternate: an idle
/// console sees a flagged advertisement within one tick, and a console that is
/// awake with another pad attached still sees the flag-less variant it already
/// accepted before this fix. Nothing has to be pressed, and nothing depends on
/// the Steam Controller being connected first.
///
/// Un-bonded (first-ever pairing) needs no re-advertisement: the Discovery
/// variant is already continuously on the air and carries no console address,
/// so re-airing it would only add stop/start gaps.
inline constexpr WakeAdv wake_adv_for(bool bonded, bool console_linked, unsigned tick) {
  if (console_linked)
    return WakeAdv::None;
  if (!bonded)
    return WakeAdv::None;
  return (tick % 2u == 0u) ? WakeAdv::Wake : WakeAdv::Reconnect;
}

} // namespace s2s
