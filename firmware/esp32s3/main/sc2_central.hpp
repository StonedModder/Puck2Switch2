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
  /// Drop the scan to a low duty cycle while the Switch 2 link is active. An
  /// 80% duty scan competes with the console's 5 ms link for the single radio
  /// and shows up as dropped input, so scanning becomes a 2.5% trickle instead.
  void set_scan_paused(bool paused);

  bool connected() const { return connected_.load(); }
  bool ever_seen() const { return seen_count_.load() > 0; }
  uint32_t reports() const { return report_count_.load(); }

  void set_input_cb(InputCb cb) { input_cb_ = std::move(cb); }
  void set_battery_cb(BatteryCb cb) { battery_cb_ = std::move(cb); }

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

  NimBLEClient *client_{nullptr};
  NimBLERemoteCharacteristic *feat_chr_{nullptr};
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
  bool imu_enabled_{false};
  int64_t last_keepalive_us_{0};
  uint32_t raw_dumps_{0};
};
