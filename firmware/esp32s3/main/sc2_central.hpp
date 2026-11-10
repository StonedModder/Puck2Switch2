// sc2_central.hpp — BLE central that finds a Steam Controller 2 ("Triton") in
// Bluetooth mode, subscribes to Valve's proprietary GATT service, keeps it in
// "no lizard" mode with IMU on, and streams decoded input.
//
// Protocol facts (all from permissively-licensed sources):
//   service  100F6C32-1735-4313-B402-38567131E5F3
//   input45  100F6C7A-...  (notify, report id 0x45, body WITHOUT the id byte)
//   input47  100F6C7C-...  (notify, report id 0x47, "Ibex"/timestamp variant)
//   feature  100F6C34-...  (write feature reports: lizard-off + IMU enable)
//   battery  100F6C78-...  (notify, byte0 charge state, byte1 level %)
//   lizard-off  {0x87,0x03,0x09,0x00,0x00}
//   imu-on      {0x87,0x03,0x30,0x18,0x00}
//   keepalive   3 s (the controller re-enables lizard mode on a watchdog)
// Ref: joypad-os steam_controller_2_ble.[ch] + btstack_host.c (Apache-2.0),
//      cross-checked against CouchTurtle/sc2-research (MIT) and SDL3.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "NimBLEDevice.h"
#include "ble_radio_policy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sc2_core.h"

class Sc2Central : public NimBLEScanCallbacks, public NimBLEClientCallbacks {
public:
  using InputCb = std::function<void(const s2s::Sc2Input &)>;
  using BatteryCb = std::function<void(const s2s::Sc2Battery &)>;

  Sc2Central() = default;

  bool begin();
  /// Drive connection state machine + lizard-off keepalive. Call from one task.
  void loop();
  /// Compatibility shim for the historical single-bool call: equivalent to
  /// set_link_state(sc2_linked, <last reported console state>).
  void set_scan_paused(bool sc2_linked);

  /// Report BOTH link states and pick the scan duty from them
  /// (s2s::scan_duty_for, host-tested in host_tests/test_ble_radio_policy.cpp).
  /// There are three regimes, not two: a scan that is fine while nothing else
  /// uses the radio is NOT fine while the peripheral still has to be discovered
  /// by an idle console — the old code ran 80 % duty in exactly that state.
  void set_link_state(bool sc2_linked, bool console_linked);

  bool connected() const { return connected_.load(); }
  bool ever_seen() const { return seen_count_.load() > 0; }
  uint32_t reports() const { return report_count_.load(); }

  void set_input_cb(InputCb cb) { input_cb_ = std::move(cb); }
  void set_battery_cb(BatteryCb cb) { battery_cb_ = std::move(cb); }

  /// HD-rumble output. Writes output report 0x80 (report-id byte stripped) to the
  /// Valve haptic characteristic 100f6cb5 without response:
  ///   [type=0][intensity u16=0][left u16][left gain=0][right u16][right gain=0]
  /// Speeds are 0..65535. The pad stops on its own ~50 ms after the last write, so
  /// while either speed is non-zero loop() re-sends every 25 ms; if no new value
  /// arrives for 150 ms the output is forced to zero (a game that stops streaming
  /// mid-effect must not leave the motors running). Safe to call from any task.
  void set_rumble(uint16_t left_speed, uint16_t right_speed);

private:
  void onResult(const NimBLEAdvertisedDevice *dev) override;
  void onScanEnd(const NimBLEScanResults &results, int reason) override;
  void onDisconnect(NimBLEClient *client, int reason) override;

  void start_scan();
  bool try_connect(const NimBLEAddress &addr);
  bool setup_gatt();
  void handle_notification(uint8_t report_id, const uint8_t *data, size_t len);
  void send_keepalive(bool include_imu);
  bool request_fast_interval();
  void apply_scan_duty(const s2s::ScanDuty &d);

  NimBLEClient *client_{nullptr};
  NimBLERemoteCharacteristic *feat_chr_{nullptr};
  NimBLERemoteCharacteristic *rumble_chr_{nullptr};
  std::atomic<uint16_t> rumble_left_{0};
  std::atomic<uint16_t> rumble_right_{0};
  std::atomic<int64_t> rumble_last_us_{0};
  uint16_t rumble_sent_left_{0};
  uint16_t rumble_sent_right_{0};
  int64_t last_rumble_us_{0};
  uint32_t rumble_writes_{0};
  uint32_t rumble_failures_{0};
  // Connection-parameter upgrade is retried after the link settles: requesting it
  // immediately after subscribing is refused by the stack, leaving the pad on a
  // ~45 ms default interval (22 Hz).
  int params_attempts_{0};
  int64_t last_params_us_{0};

  std::atomic<bool> connected_{false};
  std::atomic<bool> want_connect_{false};
  std::atomic<uint32_t> seen_count_{0};
  std::atomic<uint32_t> report_count_{0};
  std::atomic<bool> scanning_{false};

  std::mutex addr_mtx_;
  NimBLEAddress pending_addr_{};
  bool have_pending_{false};

  InputCb input_cb_{};
  BatteryCb battery_cb_{};

  std::vector<std::string> seen_addrs_{}; // diagnostics: distinct devices logged
  std::string last_steam_log_{};
  s2s::MacFilter mac_filter_{};

  // Rolling "what is visible right now" table. onResult only logs a device the
  // first time it is seen, so a controller that appeared earlier under an
  // unnamed address would otherwise be invisible in the log. This table is
  // refreshed periodically so the live scan can actually be inspected.
  struct DevInfo {
    std::string name;
    int rssi = 0;
    int64_t last_us = 0;
    uint8_t svc = 0;
  };
  std::mutex dev_mtx_;
  std::map<std::string, DevInfo> recent_{};
  int64_t last_report_us_{0};
  void report_visible(int64_t now_us);

  std::atomic<bool> scan_started_{false};
  std::atomic<bool> scan_paused_{false};
  /// Last console-link state reported by the app, so the one-argument
  /// set_scan_paused() shim can still pick the right three-way duty.
  std::atomic<bool> console_linked_{false};
  /// Scan duty currently programmed into the controller, so a repeated
  /// state report never stops/restarts the scan for nothing.
  s2s::ScanDuty duty_{0, 0, "unset"};
  bool imu_enabled_{false};
  int64_t last_keepalive_us_{0};
  uint32_t raw_dumps_{0};
};
