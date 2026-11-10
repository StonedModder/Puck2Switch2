#include "sc2_central.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "SC2";

// Valve (Steam Controller 2 "Triton") proprietary GATT service.
static const char *VALVE_SVC = "100f6c32-1735-4313-b402-38567131e5f3";
static const char *VALVE_IN45 = "100f6c7a-1735-4313-b402-38567131e5f3";
static const char *VALVE_IN47 = "100f6c7c-1735-4313-b402-38567131e5f3";
static const char *VALVE_FEAT = "100f6c34-1735-4313-b402-38567131e5f3";
static const char *VALVE_BATT = "100f6c78-1735-4313-b402-38567131e5f3";
static const char *VALVE_RUMBLE = "100f6cb5-1735-4313-b402-38567131e5f3";

// Feature-report payloads (report-id byte stripped, as sent on air).
// {0x87 = ID_SET_SETTINGS_VALUES, 0x03 = sizeof(ControllerSetting), setting, u16 value}
static uint8_t LIZARD_OFF[5] = {0x87, 0x03, 0x09, 0x00, 0x00}; // lizard mode OFF
static uint8_t IMU_ON[5] = {0x87, 0x03, 0x30, 0x18, 0x00};     // raw accel | raw gyro

static constexpr int64_t KEEPALIVE_US = 3000 * 1000; // controller watchdog ~3 s

bool Sc2Central::begin() {
  // Optional MAC allowlist (menuconfig: Steam2Switch2 adapter). Works alongside
  // the name/Valve-UUID match, or alone in strict mode.
  mac_filter_.parse(std::string(CONFIG_S2S_SC2_MAC));
#ifdef CONFIG_S2S_SC2_MAC_ONLY
  mac_filter_.only = true;
#else
  mac_filter_.only = false;
#endif
  if (!mac_filter_.empty()) {
    ESP_LOGW(TAG, "MAC filter active (%u address(es), strict=%d): %s",
             (unsigned)mac_filter_.list.size(), (int)mac_filter_.only,
             CONFIG_S2S_SC2_MAC);
  }

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(this, false);
  scan->setActiveScan(true); // need the scan response for the device name
  const auto duty = s2s::sc2_scan_params();
  scan->setInterval(duty.interval_ms);
  scan->setWindow(duty.window_ms);
  scan->setDuplicateFilter(0);

  // The Steam Controller 2 will not stream notifications until the link is
  // bonded with LE Secure Connections (joypad-os configures exactly this:
  // SM_AUTHREQ_BONDING | SM_AUTHREQ_SECURE_CONNECTION). NimBLE keeps these in
  // ble_hs_cfg, which is host-global — but espp's Switch2Pro only turns bonding
  // off so the *peripheral* never creates a bond with the console, and the
  // console never initiates SMP (it pairs at the application layer over 0x15).
  // So enabling bonding here does not change what the console sees.
  NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  // Exchange identity keys (IRK) as well as the LTK, so the controller's rotating
  // resolvable private address can be resolved back to our bond. Without this a
  // bonded reconnect is unrecognisable — and the SC2 advertises with NO NAME on
  // reconnect (joypad-os documents the same behaviour).
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  ESP_LOGI(TAG, "central ready; %d stored bond(s); scanning for Steam Controller 2",
           NimBLEDevice::getNumBonds());
  start_scan();
  return true;
}

void Sc2Central::set_scan_paused(bool /*paused*/) {
  // Fixed low duty from begin(): do not cancel a running scan merely because a
  // link changed state. NimBLEScan::stop() clears internal scan lists and has
  // caused link teardown races. A scan is still cancelled to initiate the SC2
  // connection in onResult() (existing behavior; see NimBLEClient::connect()).
}

void Sc2Central::start_scan() {
  if (connected_.load()) return;
  // Use our own flag: ble_gap_disc_active() can report a stale true after the
  // controller completes a scan, which would wedge us with no scan running.
  if (scan_started_.load()) return;

  NimBLEScan *scan = NimBLEDevice::getScan();
  // duration 0 = scan continuously. With BLE_EXT_ADV the duration is divided by
  // 10 (10 ms units), so a small nonzero value like 3 collapses to 0 and the
  // controller completes the scan immediately — request continuous explicitly.
  bool ok = scan->start(0, true, false);
  scan_started_.store(ok);
  ESP_LOGI(TAG, "scan start(continuous) -> %d", (int)ok);
}

void Sc2Central::onScanEnd(const NimBLEScanResults &results, int reason) {
  scan_started_.store(false);
  scanning_.store(false);
  ESP_LOGI(TAG, "scan end reason=%d results=%d seen=%u", reason, results.getCount(),
           (unsigned)seen_count_.load());
}

void Sc2Central::onResult(const NimBLEAdvertisedDevice *dev) {
  const std::string addr = dev->getAddress().toString();
  const std::string nm = dev->haveName() ? dev->getName() : std::string("?");

  // Feed the rolling visibility table (throttled to once per second per device
  // by only overwriting when this result is newer).
  {
    int64_t now = esp_timer_get_time();
    std::lock_guard<std::mutex> lk(dev_mtx_);
    auto &e = recent_[addr];
    e.name = nm;
    e.rssi = dev->getRSSI();
    e.svc = (uint8_t)dev->getServiceUUIDCount();
    e.last_us = now;
  }

  // Case-insensitive Steam check, independent of the diagnostic cap below, so a
  // controller that appears during its ~30 s pairing window is never missed.
  std::string lower = nm;
  for (auto &c : lower) c = (char)tolower((unsigned char)c);
  const bool steam_name = lower.find("steam") != std::string::npos;

  if (steam_name && last_steam_log_ != addr) {
    last_steam_log_ = addr;
    ESP_LOGW(TAG, ">>> STEAM DEVICE: %s rssi=%d name='%s' svc=%u", addr.c_str(), dev->getRSSI(),
             nm.c_str(), (unsigned)dev->getServiceUUIDCount());
  }

  // Diagnostics: log each distinct device once. Cap is generous (200) so a
  // fresh device always gets logged even after a long uptime.
  if (seen_addrs_.size() < 200 && !steam_name) {
    bool known = false;
    for (const auto &a : seen_addrs_)
      if (a == addr) { known = true; break; }
    if (!known) {
      seen_addrs_.push_back(addr);
      char mfr[40] = "-";
      if (dev->haveManufacturerData()) {
        const std::string md = dev->getManufacturerData();
        uint16_t cid = 0;
        if (md.size() >= 2) cid = (uint16_t)((uint8_t)md[1] << 8 | (uint8_t)md[0]);
        snprintf(mfr, sizeof(mfr), "cid=0x%04x len=%u", cid, (unsigned)md.size());
      }
      ESP_LOGI(TAG, "seen: %-17s rssi=%-4d name='%s' svc=%u %s", addr.c_str(), dev->getRSSI(),
               nm.c_str(), (unsigned)dev->getServiceUUIDCount(), mfr);
    }
  }

  // Match priority:
  //   1. MAC allowlist  -> accept immediately (works regardless of advertising
  //      name/service, which is why it exists).
  //   2. In strict mode, nothing else is accepted.
  //   3. Otherwise: Valve service UUID, or a name containing "Steam".
  const bool mac_match = mac_filter_.matches(addr);
  // A bonded controller re-advertises with NO NAME after the first pairing, so
  // identity (bond membership) is the reliable reconnect signal, not the name.
  const bool bonded = NimBLEDevice::isBonded(dev->getAddress());
  if ((mac_match || bonded) && last_steam_log_ != addr) {
    last_steam_log_ = addr;
    ESP_LOGW(TAG, ">>> %s: %s rssi=%d name='%s'",
             bonded ? "BONDED DEVICE" : "MAC MATCH", addr.c_str(), dev->getRSSI(), nm.c_str());
  }

  bool match = false;
  if (mac_match || bonded) {
    match = true;
  } else if (!mac_filter_.only) {
    if (dev->isAdvertisingService(NimBLEUUID(std::string(VALVE_SVC)))) {
      match = true;
    } else if (steam_name) {
      match = true;
    }
  }
  if (!match) return;

  seen_count_.fetch_add(1);
  if (want_connect_.load() || connected_.load()) return;

  {
    std::lock_guard<std::mutex> lk(addr_mtx_);
    pending_addr_ = dev->getAddress();
    have_pending_ = true;
  }
  want_connect_.store(true);
  ESP_LOGI(TAG, "found SC2 '%s' [%s] rssi=%d", dev->haveName() ? dev->getName().c_str() : "?",
           dev->getAddress().toString().c_str(), dev->getRSSI());
  scan_started_.store(false);
  NimBLEDevice::getScan()->stop();
}

void Sc2Central::onDisconnect(NimBLEClient *client, int reason) {
  ESP_LOGW(TAG, "SC2 disconnected (reason=0x%02x)", reason);
  connected_.store(false);
  feat_chr_ = nullptr;
  imu_enabled_ = false;
  want_connect_.store(false);
  // Keep the client object alive. Deleting a NimBLE client from inside its own
  // disconnect callback corrupts the heap ("free() target pointer is outside
  // heap areas"), and deleting it from the app thread still raced with the
  // stack's own teardown (LoadProhibited). A single long-lived client is reused:
  // once the link is fully down its status returns to DISCONNECTED and connect()
  // works again.
  start_scan();
}

bool Sc2Central::setup_gatt() {
  NimBLERemoteService *svc = client_->getService(NimBLEUUID(std::string(VALVE_SVC)));
  if (!svc) {
    ESP_LOGE(TAG, "Valve service not found — is the controller in Bluetooth mode?");
    return false;
  }

  // Only ONE of the two input characteristics exists on a given unit.
  NimBLERemoteCharacteristic *in45 = svc->getCharacteristic(NimBLEUUID(std::string(VALVE_IN45)));
  NimBLERemoteCharacteristic *in47 = svc->getCharacteristic(NimBLEUUID(std::string(VALVE_IN47)));
  NimBLERemoteCharacteristic *in_chr = in45 ? in45 : in47;
  const uint8_t report_id = in45 ? 0x45 : 0x47;
  if (!in_chr) {
    ESP_LOGE(TAG, "no Valve input characteristic (0x45/0x47) found");
    return false;
  }
  ESP_LOGI(TAG, "input characteristic: report 0x%02X", report_id);

  auto notify_cb = [this, report_id](NimBLERemoteCharacteristic *, uint8_t *data, size_t len,
                                     bool) { handle_notification(report_id, data, len); };

  // Subscribe (write CCCD). Try with response first (what joypad-os does after
  // bonding); if the controller rejects that write, retry without response.
  bool subbed = in_chr->subscribe(true, notify_cb, true);
  if (!subbed) {
    ESP_LOGW(TAG, "subscribe(with response) failed — retrying without response");
    subbed = in_chr->subscribe(true, notify_cb, false);
  }
  if (!subbed) {
    ESP_LOGE(TAG, "subscribe failed on report 0x%02X characteristic", report_id);
    return false;
  }
  ESP_LOGI(TAG, "subscribed to report 0x%02X notifications", report_id);

  feat_chr_ = svc->getCharacteristic(NimBLEUUID(std::string(VALVE_FEAT)));
  if (!feat_chr_) {
    // Without this characteristic the lizard-off keepalive cannot be sent and the
    // controller falls back to lizard mode ~3 s in (which looks like a dead pad).
    // After a re-pair the service can be re-enumerated under a new instance, so
    // search the whole GATT tree by UUID before giving up.
    for (auto *s : client_->getServices(/*refresh=*/true)) {
      if (!s) continue;
      auto *c = s->getCharacteristic(NimBLEUUID(std::string(VALVE_FEAT)));
      if (c) {
        feat_chr_ = c;
        ESP_LOGW(TAG, "feature characteristic found via fallback scan (handle 0x%04x)",
                 c->getHandle());
        break;
      }
    }
    if (!feat_chr_) ESP_LOGE(TAG, "feature characteristic missing — lizard-off unavailable");
  }

  // Haptic output (rumble). Same fallback treatment: after a re-pair the service
  // can come back under a new instance, so search the tree by UUID.
  rumble_chr_ = svc->getCharacteristic(NimBLEUUID(std::string(VALVE_RUMBLE)));
  if (!rumble_chr_) {
    for (auto *s : client_->getServices(/*refresh=*/true)) {
      if (!s) continue;
      auto *c = s->getCharacteristic(NimBLEUUID(std::string(VALVE_RUMBLE)));
      if (c) {
        rumble_chr_ = c;
        break;
      }
    }
  }
  if (rumble_chr_) {
    // A write that returns true only means "queued", not "delivered", so record
    // what the characteristic actually advertises — if the pad stays silent this
    // is the first thing to check (Write Request vs Write Without Response).
    ESP_LOGI(TAG, "haptic characteristic 100f6cb5 ready (handle 0x%04x, write=%d write_nr=%d)",
             rumble_chr_->getHandle(), (int)rumble_chr_->canWrite(),
             (int)rumble_chr_->canWriteNoResponse());
  } else {
    ESP_LOGE(TAG, "haptic characteristic missing — rumble unavailable");
  }

  NimBLERemoteCharacteristic *batt = svc->getCharacteristic(NimBLEUUID(std::string(VALVE_BATT)));
  if (batt) {
    batt->subscribe(true, [this](NimBLERemoteCharacteristic *, uint8_t *d, size_t len, bool) {
      if (len < 2) return;
      s2s::Sc2Battery b = s2s::parse_sc2_battery(d, len);
      // Log every battery notification so the encoding can be confirmed against
      // the real device rather than assumed (the console reads this level).
      ESP_LOGI(TAG, "battery notify len=%u raw=%02x %02x %02x -> level=%u%% state=%u%s", (unsigned)len,
               len > 0 ? d[0] : 0, len > 1 ? d[1] : 0, len > 2 ? d[2] : 0, b.level_pct,
               b.charge_state, b.valid ? "" : " (INVALID)");
      if (battery_cb_) battery_cb_(b);
    });
  }
  return true;
}

void Sc2Central::handle_notification(uint8_t report_id, const uint8_t *data, size_t len) {
  // The Valve characteristic omits the report-id byte; prepend it to form the
  // "assembled" report the parser expects.
  if (len + 1 > 64) return;
  uint8_t buf[64];
  buf[0] = report_id;
  std::memcpy(buf + 1, data, len);
  const size_t n = len + 1;

  // First few raw frames dumped so the live bring-up is debuggable over serial.
  if (raw_dumps_ < 4) {
    char hex[3 * 64 + 1] = {0};
    size_t lim = n < 48 ? n : 48;
    for (size_t i = 0; i < lim; ++i) snprintf(hex + i * 3, 4, "%02x ", buf[i]);
    ESP_LOGI(TAG, "raw[%u] len=%u: %s", (unsigned)raw_dumps_, (unsigned)n, hex);
    ++raw_dumps_;
  }

  s2s::Sc2Input in;
  if (!s2s::parse_sc2(buf, n, in)) return;
  report_count_.fetch_add(1);
  if (input_cb_) input_cb_(in);
}

bool Sc2Central::try_connect(const NimBLEAddress &addr) {
  ESP_LOGI(TAG, "try_connect(%s) connected=%d client=%p", addr.toString().c_str(),
           (int)connected_.load(), (void *)client_);
  // One long-lived client, reused for every attempt. Never delete it: NimBLE's
  // client teardown is not safe to drive from here (see onDisconnect).
  if (!client_) {
    client_ = NimBLEDevice::createClient();
    if (!client_) {
      ESP_LOGE(TAG, "createClient failed");
      return false;
    }
    client_->setClientCallbacks(this);
    client_->setConnectTimeout(8000);
  }

  ESP_LOGI(TAG, "connecting to %s ...", addr.toString().c_str());
  if (!client_->connect(addr)) {
    ESP_LOGW(TAG, "connect failed");
    return false;
  }
  connected_.store(true);
  ESP_LOGI(TAG, "connected; requesting bonded LE Secure Connections");

  // Bond/encrypt BEFORE touching the GATT tree: the controller rejects CCCD
  // writes on an unencrypted link (observed: subscribe failed / HCI 0x16).
  if (!client_->secureConnection(/*async=*/false)) {
    ESP_LOGW(TAG, "secureConnection() failed — attempting GATT anyway");
  } else {
    ESP_LOGI(TAG, "link secured (encrypted+bonded)");
  }

  ESP_LOGI(TAG, "discovering Valve GATT");

  if (!setup_gatt()) {
    ESP_LOGE(TAG, "setup_gatt failed — disconnecting (local terminate 0x16)");
    client_->disconnect();
    connected_.store(false);
    return false;
  }

  send_keepalive(/*include_imu=*/true);
  last_keepalive_us_ = esp_timer_get_time();
  ESP_LOGI(TAG, "SC2 ready (lizard-off + IMU on)");

  // 7.5 ms = 133 Hz sampling, as close to the console's 5 ms poll as the link
  // will give. Attempted from loop(), not here: requesting it this early is
  // refused by the stack while the connection is still settling.
  params_attempts_ = 0;
  last_params_us_ = esp_timer_get_time();
  return true;
}

void Sc2Central::send_keepalive(bool include_imu) {
  if (!feat_chr_) return;
  // Lizard-off must be refreshed: the controller re-enables lizard mode on a
  // watchdog, which would suppress the gamepad reports we need.
  feat_chr_->writeValue(LIZARD_OFF, sizeof(LIZARD_OFF), false);
  if (include_imu && !imu_enabled_) {
    feat_chr_->writeValue(IMU_ON, sizeof(IMU_ON), false);
    imu_enabled_ = true;
  }
}

void Sc2Central::report_visible(int64_t now_us) {
  if (now_us - last_report_us_ < 5'000'000) return; // every 5 s
  last_report_us_ = now_us;

  struct Row {
    std::string addr, name;
    int rssi;
    uint8_t svc;
  };
  std::vector<Row> rows;
  {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    for (auto it = recent_.begin(); it != recent_.end();) {
      if (now_us - it->second.last_us > 30'000'000) { // stale after 30 s
        it = recent_.erase(it);
        continue;
      }
      rows.push_back({it->first, it->second.name, it->second.rssi, it->second.svc});
      ++it;
    }
  }
  std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.rssi > b.rssi; });

  std::string line = "visible(" + std::to_string(rows.size()) + "):";
  const size_t lim = rows.size() < 8 ? rows.size() : 8;
  for (size_t i = 0; i < lim; ++i) {
    line += " " + rows[i].addr + "/" + std::to_string(rows[i].rssi);
    if (rows[i].name != "?") line += "'" + rows[i].name + "'";
  }
  ESP_LOGI(TAG, "%s", line.c_str());
}

bool Sc2Central::request_fast_interval() {
  if (!client_ || !connected_.load()) return false;
  // Ladder from the BLE minimum upward. 7.5 ms is as close to the console's 5 ms
  // poll as a BLE link gets; if the controller or stack refuses it, step up
  // rather than hammering one value. 6/9/12/16 units = 7.5/11.25/15/20 ms.
  static constexpr uint16_t kLadder[] = {6, 9, 12, 16};
  const int idx = (params_attempts_ - 1) < 0 ? 0 : (params_attempts_ - 1);
  const uint16_t itvl = kLadder[idx < 4 ? idx : 3];
  if (client_->updateConnParams(itvl, itvl, /*latency=*/0, /*timeout=*/600)) {
    ESP_LOGI(TAG, "SC2 interval set to %u units (%u Hz) on attempt %d", itvl,
             (unsigned)(1000u / ((unsigned)itvl * 5u / 4u)), params_attempts_);
    return true;
  }
  ESP_LOGW(TAG, "interval %u units refused (rc=%d); retrying", itvl, client_->getLastError());
  return false;
}

void Sc2Central::set_rumble(uint16_t left_speed, uint16_t right_speed) {
  rumble_left_.store(left_speed);
  rumble_right_.store(right_speed);
  rumble_last_us_.store(esp_timer_get_time());
}

void Sc2Central::loop() {
  report_visible(esp_timer_get_time());
  if (connected_.load()) {
    int64_t now = esp_timer_get_time();
    // Ask for the fastest interval the link accepts, retrying until it sticks.
    if (params_attempts_ < 4 && now - last_params_us_ >= 1000000) {
      last_params_us_ = now;
      ++params_attempts_;
      if (request_fast_interval()) {
        params_attempts_ = 99; // done
      }
    }
    // HD rumble: the pad's haptic watchdog drops the motors ~50 ms after the last
    // output report. loop() ticks at 10 ms, so a 40 ms gate can actually fire at
    // 45-50 ms — right at the timeout. 25 ms keeps a real margin. A value that
    // stops updating for 150 ms is forced to zero, so a game that stops streaming
    // mid-effect cannot leave the motors running.
    uint16_t rl = rumble_left_.load();
    uint16_t rr = rumble_right_.load();
    if ((rl || rr) && now - rumble_last_us_.load() > 150000) {
      rl = rr = 0;
      rumble_left_.store(0);
      rumble_right_.store(0);
    }
    const bool active = rl != 0 || rr != 0;
    const bool dirty = rl != rumble_sent_left_ || rr != rumble_sent_right_;
    if (rumble_chr_ && (active ? (now - last_rumble_us_ >= 25000) : dirty)) {
      uint8_t rmb[9] = {0x00, 0x00, 0x00,
                        (uint8_t)(rl & 0xFF), (uint8_t)(rl >> 8), 0x00,
                        (uint8_t)(rr & 0xFF), (uint8_t)(rr >> 8), 0x00};
      const bool ok = rumble_chr_->writeValue(rmb, sizeof(rmb), /*response=*/false);
      if (ok) {
        ++rumble_writes_;
        if (rumble_writes_ <= 3 || (!active && rumble_writes_ % 50 == 0)) {
          ESP_LOGI(TAG, "rumble write #%u left=%u right=%u", (unsigned)rumble_writes_,
                   (unsigned)rl, (unsigned)rr);
        }
      } else {
        ++rumble_failures_;
        if (rumble_failures_ <= 5) {
          ESP_LOGW(TAG, "rumble write failed (%u)", (unsigned)rumble_failures_);
        }
      }
      rumble_sent_left_ = rl;
      rumble_sent_right_ = rr;
      last_rumble_us_ = now;
    }

    if (now - last_keepalive_us_ >= KEEPALIVE_US) {
      send_keepalive(/*include_imu=*/false);
      last_keepalive_us_ = now;
    }
    return;
  }

  if (want_connect_.load()) {
    want_connect_.store(false);
    NimBLEAddress addr{};
    {
      std::lock_guard<std::mutex> lk(addr_mtx_);
      if (!have_pending_) return;
      addr = pending_addr_;
      have_pending_ = false;
    }
    try_connect(addr);
    return;
  }

  // Not connected and nothing pending: keep a scan running.
  start_scan();
}
