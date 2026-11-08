// app_main.cpp — Steam Controller 2 (BLE) -> Nintendo Switch 2 Pro Controller.
//
// One ESP32-S3, two BLE roles on one NimBLE host:
//   * central  -> connects to the Steam Controller 2 and reads report 0x45
//   * peripheral -> espp::Switch2Pro emulates a Pro Controller 2 (report 0x09)
//
// Latency design: the Switch 2 report is rebuilt and handed to espp from inside
// the Steam Controller notification callback. espp's streaming task snapshots
// the newest report under a mutex each connection interval, so this removes the
// extra poll delay a polling loop would add on top of the link interval.
//
// The SC2 runs a "lizard mode" watchdog, so the central refreshes a lizard-off
// feature report every 3 s; without it the controller stops sending gamepad
// state. See sc2_central.cpp.

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#include "esp_log.h"
#include "esp_timer.h"

#include "nvs.hpp"
#include "switch2_pro.hpp"

#include "ns2_motion30.h"
#include "macro_engine.hpp"
#include "macro_store.hpp"
#include "sc2_central.hpp"
#include "sc2_core.h"
#include "sc2_map_espp.hpp"
#include "web_server.hpp"

static const char *TAG = "S2S";

// --- shared input state (written by the NimBLE host task, read by the app) ---
static std::mutex g_mtx;
static s2s::Sc2Input g_in{};
static s2s::Sc2Battery g_bat{};
static std::atomic<int64_t> g_in_ts_us{0}; // arrival time of the newest SC2 sample

// --- report pipeline (NimBLE host task) -------------------------------------
static espp::Switch2Pro *g_ctl = nullptr;
static Sc2Central *g_sc2 = nullptr;
static s2s::MapConfig g_cfg{};
static std::atomic<bool> g_hold_lr{false}; // console is asking "press L + R"
static MacroEngine g_macro;

// Live IMU: fuse the SC2's gyro+accel into an orientation quaternion and emit
// the Switch 2's hardware-validated 30-byte motion carrier (report 0x0E = 0x1E).
static constexpr bool kLiveMotion = true;
static s2s::MahonyFilter g_attitude;
static s2s::Ns2MotionEncoder g_motion_enc;
static uint16_t g_tick800 = 0;
static int64_t g_last_imu_us = 0;

// Rebuild the Switch 2 report from one Steam Controller sample and hand it to
// espp. Runs on the NimBLE host task so the report reflects the sample that just
// arrived rather than the last one a polling loop happened to see.
static void push_from_sc2(const s2s::Sc2Input &in, const s2s::Sc2Battery &bat) {
  static espp::switch2::Pro2InputReport report;
  static uint8_t carrier[s2s::NS2_MOTION30_LEN];

  report.reset();
  s2s::apply_sc2_to_pro2(in, bat, g_cfg, report);

  if (g_hold_lr.load()) {
    report.set_l(true);
    report.set_r(true);
  }

  if (kLiveMotion && in.has_motion) {
    const int64_t now_us = esp_timer_get_time();
    float dt = (float)(now_us - g_last_imu_us) * 1e-6f;
    g_last_imu_us = now_us;
    if (dt > 0.0005f && dt < 0.5f) {
      // SC2 ranges: accel +/-2 g (16384 counts/g), gyro +/-2000 dps
      // (32767 counts => 16.3835 counts per deg/s).
      const float accel_g[3] = {in.accel[0] / 16384.0f, in.accel[1] / 16384.0f,
                                in.accel[2] / 16384.0f};
      const float gyro_dps[3] = {in.gyro[0] / 16.3835f, in.gyro[1] / 16.3835f,
                                 in.gyro[2] / 16.3835f};
      g_attitude.update(gyro_dps, accel_g, dt);

      // The timing byte packs a 12-bit 800 Hz tick plus a 4-bit "ticks since the
      // last report" nibble. A 20 ms gap needs 16, which cannot fit in 4 bits and
      // corrupts the timestamp field, so clamp to the field width.
      uint32_t ticks = (uint32_t)(dt * 800.0f + 0.5f);
      if (ticks > 15u) ticks = 15u;
      g_tick800 = (uint16_t)((g_tick800 + ticks) & 0x0FFFu);

      g_motion_enc.build(carrier, accel_g, g_tick800, (uint8_t)ticks, g_attitude.q);
      // 0x1E = 30-byte quaternion carrier (hardware-validated form).
      report.set_motion_block(carrier, s2s::NS2_MOTION30_LEN, 0x1E);
    }
  }

  if (g_macro.recording()) {
    // Record the exact report the console is about to receive, so what is
    // captured is what the game saw, not an intermediate representation.
    g_macro.record_feed(report.data().data(), (uint32_t)(esp_timer_get_time() / 1000));
  }

  // While a macro plays the engine owns the report; live input must not fight it.
  if (!g_macro.playing() && g_ctl) g_ctl->set_input_report(report);
}

// HD rumble: turn the console's vibration frame into Steam Controller 2 motor
// speeds. A 16-byte LRA block is [0x5n sequence][5-byte op][5-byte op][5-byte op]
// (1 + 3*5 = 16) — the console queues up to three ops per block, so decoding only
// the first one can read silence while a later slot is active. Each 5-byte op
// holds four 10-bit fields in little-endian bit order: hi_freq, hi_amp, lo_freq,
// lo_amp (0..1023).
static uint16_t sc2_amp_from_block(const uint8_t *block, size_t avail) {
  if (avail < 6) return 0;
  const size_t ops = (avail - 1) / 5;
  uint16_t best = 0;
  for (size_t i = 0; i < ops && i < 3; ++i) {
    const uint8_t *p = block + 1 + i * 5;
    const uint16_t hi_amp = (uint16_t)(((p[1] >> 2) & 0x3F) | ((p[2] & 0x0F) << 6));
    const uint16_t lo_amp = (uint16_t)(((p[3] >> 6) & 0x03) | (p[4] << 2));
    const uint16_t amp = hi_amp > lo_amp ? hi_amp : lo_amp;
    if (amp > best) best = amp;
  }
  return (uint16_t)((uint32_t)best * 65535u / 1023u);
}

extern "C" void app_main(void) {
  esp_log_level_set("NimBLE", ESP_LOG_WARN);

  ESP_LOGI(TAG, "Steam Controller 2 -> Switch 2 Pro adapter");
  ESP_LOGI(TAG, "build: %s %s", __DATE__, __TIME__);

  // --- NVS (Switch 2 bond persistence + NimBLE bond store) -------------------
  espp::Nvs nvs;
  std::error_code ec;
  nvs.init(ec);
  if (ec) {
    ESP_LOGE(TAG, "NVS init failed: %s", ec.message().c_str());
    return;
  }

  // --- Switch 2 Pro Controller peripheral -----------------------------------
  // wake_console_on_boot_enabled = false: waking is user-initiated (a button
  // press on the Steam Controller), like a real controller.
  //
  // continuous_stream_divisor = 1: send on every connection interval. The link
  // runs at 5 ms regardless of how often we notify (the console drives it), so
  // halving notifies only added latency without freeing any radio time.
  static espp::Switch2Pro controller({.device_name = "Pro Controller",
                                      .wake_console_on_boot_enabled = false,
                                      .imu_motion_enabled = false,
                                      .continuous_streaming_enabled = true,
                                      .continuous_stream_divisor = 1,
                                      .log_level = espp::Logger::Verbosity::INFO});
  if (!controller.init()) {
    ESP_LOGE(TAG, "Switch2Pro init failed");
    return;
  }
  g_ctl = &controller;
  ESP_LOGI(TAG, "Switch 2 Pro Controller peripheral up — advertising");

  // Rumble: the console streams HD-rumble frames on the vibration+command channel
  // whether or not a command rides along, so mirror them onto the Steam Controller.
  controller.on_vibration = [](const uint8_t *frame, size_t len) {
    if (len < 13 || !g_sc2) return;
    // The report-id byte is normally at [0], putting the block's 0x5n sequence at
    // [1]. If the sequence nibble is at [0] instead the host omitted the id, and
    // decoding from +1 would silently read garbage (~7% duty) instead of failing.
    size_t off = 1;
    if ((frame[1] & 0xF0) != 0x50 && (frame[0] & 0xF0) == 0x50) {
      off = 0;
      static bool warned = false;
      if (!warned) {
        warned = true;
        ESP_LOGW(TAG, "vibration frame without report id (len=%u) — decoding from 0",
                 (unsigned)len);
      }
    }
    const size_t rem = len - off;
    const size_t stride = (rem >= 33) ? 16 : 6; // 42-byte form vs compact 13-byte form
    const uint8_t *lb = frame + off;
    const uint8_t *rb = lb + stride;
    const size_t l_avail = rem < stride ? rem : stride;
    const size_t r_avail = (rem > stride) ? ((rem - stride) < stride ? (rem - stride) : stride) : 0;
    const uint16_t l = sc2_amp_from_block(lb, l_avail);
    const uint16_t r = r_avail ? sc2_amp_from_block(rb, r_avail) : 0;
    static uint32_t frames = 0;
    if (frames < 6 || frames % 300 == 0) {
      ESP_LOGI(TAG, "vib #%u len=%u off=%u left=%u right=%u | L %02x %02x %02x %02x %02x",
               (unsigned)frames, (unsigned)len, (unsigned)off, (unsigned)l, (unsigned)r, lb[0],
               lb[1], lb[2], lb[3], lb[4]);
    }
    ++frames;
    g_sc2->set_rumble(l, r);
  };

  // --- Steam Controller 2 central ------------------------------------------
  static Sc2Central sc2;
  g_sc2 = &sc2;
  sc2.set_input_cb([](const s2s::Sc2Input &in) {
    s2s::Sc2Battery bat;
    {
      std::lock_guard<std::mutex> lk(g_mtx);
      g_in = in;
      g_in_ts_us.store(esp_timer_get_time());
      bat = g_bat;
    }
    push_from_sc2(in, bat); // forward immediately, do not wait for the loop
  });
  sc2.set_battery_cb([](const s2s::Sc2Battery &b) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_bat = b;
  });
  sc2.begin();

  // --- macro engine + editor ----------------------------------------------
  // The engine pushes reports straight into the BLE layer, so a macro plays at
  // the connection interval rather than at the Steam Controller's sample rate.
  g_macro.set_sink([](const espp::switch2::Pro2InputReport &r) {
    if (g_ctl) g_ctl->set_input_report(r);
  });
  g_macro.begin();

#if CONFIG_S2S_WEB_ENABLE
  if (!macro_store::init()) {
    ESP_LOGW(TAG, "macro storage unavailable — macros cannot be saved");
  }
  WebHooks hooks;
  hooks.sc2_connected = [] { return sc2.connected(); };
  hooks.console_connected = [] { return controller.is_connected(); };
  hooks.console_streaming = [] { return controller.is_input_streaming(); };
  // Test rumble from the web UI. The pad stops itself ~50 ms after the last
  // write, so for a timed buzz we re-send nothing and simply stop early by
  // writing zeroes from a one-shot timer. The timer is created once and rearmed.
  hooks.rumble = [](uint16_t l, uint16_t r, uint32_t ms) {
    if (!g_sc2) return;
    g_sc2->set_rumble(l, r);
    if (ms == 0) return;
    static esp_timer_handle_t stop_timer = nullptr;
    if (!stop_timer) {
      const esp_timer_create_args_t args = {
          .callback = [](void *) {
            if (g_sc2) g_sc2->set_rumble(0, 0);
          },
          .arg = nullptr,
          .dispatch_method = ESP_TIMER_TASK,
          .name = "rumble_stop",
          .skip_unhandled_events = true,
      };
      if (esp_timer_create(&args, &stop_timer) != ESP_OK) {
        stop_timer = nullptr;
        return;
      }
    }
    esp_timer_stop(stop_timer); // harmless if it already fired
    esp_timer_start_once(stop_timer, (uint64_t)ms * 1000);
  };
  if (!web_server_begin(g_macro, hooks)) {
    ESP_LOGE(TAG, "macro editor failed to start");
  }
#endif

  int lr_ticks = 0;
  bool prev_streaming = false;
  uint32_t prev_buttons = 0;
  uint32_t last_status_ms = 0;
  bool prev_sc2 = false;

  while (true) {
    // Drive the SC2 central: connection state machine, lizard-off keepalive,
    // scan restart and the rolling visibility snapshot. Without this the
    // central can DISCOVER a controller and never actually connect to it.
    sc2.loop();

    s2s::Sc2Input in;
    s2s::Sc2Battery bat;
    {
      std::lock_guard<std::mutex> lk(g_mtx);
      in = g_in;
      bat = g_bat;
    }

    // Keep the macro engine's battery field in step with what the pad reports,
    // so a macro played while the console is up shows the right level.
    if (bat.valid) {
      g_macro.set_battery(static_cast<uint8_t>((bat.level_pct * 9u + 50u) / 100u), bat.charging);
    }

    // Any fresh button press while the console is not connected -> wake it,
    // matching "press a button on your controller to wake the console".
    const uint32_t new_presses = in.buttons & ~prev_buttons;
    prev_buttons = in.buttons;

    const bool sc2_up = sc2.connected();
    if (sc2_up != prev_sc2) {
      ESP_LOGI(TAG, "SC2 %s (%u reports so far)", sc2_up ? "CONNECTED" : "disconnected",
               (unsigned)sc2.reports());
      prev_sc2 = sc2_up;
    }

    // Any fresh button press on the Steam Controller wakes a sleeping console.
    // wake_console() only succeeds once a console has actually completed pairing
    // (it requires a persisted bond AND no active connection), so this is inert
    // until the console has been paired at least once.
    if (new_presses && !controller.is_connected()) {
      const bool woke = controller.wake_console();
      ESP_LOGI(TAG, "button 0x%08x -> wake %s (paired=%d)", (unsigned)new_presses,
               woke ? "ADVERT SENT" : "not sent (no bond / already connected)",
               (int)controller.is_paired());
    }

    // The console asks "press L + R to select this controller" while connecting.
    // Auto-hold L+R for ~1 s whenever streaming (re)starts.
    const bool streaming = controller.is_input_streaming();
    if (streaming && !prev_streaming) lr_ticks = 100; // 100 * 10 ms = 1 s
    prev_streaming = streaming;
    if (lr_ticks > 0) {
      --lr_ticks;
      g_hold_lr.store(true);
    } else {
      g_hold_lr.store(false);
    }

    // Scanning is radio time we cannot spare while both links are up: with the
    // controller attached there is nothing to discover. When it drops, the central
    // scans at full duty again so reconnection is fast.
    sc2.set_scan_paused(sc2_up);

    // Heartbeat every 5 s so the serial log shows liveness during live tests.
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (now_ms - last_status_ms >= 5000) {
      last_status_ms = now_ms;
      ESP_LOGI(TAG,
               "status: sc2=%s reports=%u | console conn=%d streaming=%d paired=%d | btn=0x%08x"
               " | imu m=%d acc=%d,%d,%d gyr=%d,%d,%d | age=%lldms",
               sc2_up ? "up" : "down", (unsigned)sc2.reports(), (int)controller.is_connected(),
               (int)controller.is_input_streaming(), (int)controller.is_paired(),
               (unsigned)in.buttons, (int)in.has_motion, in.accel[0], in.accel[1], in.accel[2],
               in.gyro[0], in.gyro[1], in.gyro[2],
               (long long)((esp_timer_get_time() - g_in_ts_us.load()) / 1000));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
