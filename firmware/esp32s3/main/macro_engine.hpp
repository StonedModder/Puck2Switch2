// macro_engine.hpp — macro playback and recording.
//
// Playback owns the input report while it runs: a 2 ms task samples the macro
// timeline and hands a complete report to the BLE layer, so the console sees a
// new state on every 5 ms connection event instead of waiting for a Steam
// Controller notification. Recording taps the report that is already being sent
// to the console, so what you record is exactly what the game received.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "switch2_pro_report.hpp"

#include "macro_core.h"

class MacroEngine {
public:
  using Sink = std::function<void(const espp::switch2::Pro2InputReport &)>;

  void set_sink(Sink s) { sink_ = std::move(s); }
  void set_battery(uint8_t level_0_9, bool charging) {
    battery_level_ = level_0_9;
    charging_ = charging;
  }

  void begin();
  void end();

  // ---- playback -----------------------------------------------------------
  bool start(const std::vector<s2s::MacroStep> &steps);
  void stop();
  bool playing() const { return playing_.load(); }
  uint32_t position_ms() const;
  uint32_t duration_ms() const;

  // ---- recording ----------------------------------------------------------
  void record_start();
  bool recording() const { return recording_.load(); }
  std::vector<s2s::MacroStep> record_stop();
  uint32_t record_ms() const;
  // Feed the report that was just sent to the console.
  void record_feed(const uint8_t *report63, uint32_t now_ms);

private:
  void run();
  void emit(const s2s::MacroStepView &v);

  Sink sink_{};
  std::thread task_;
  std::mutex mtx_;
  std::condition_variable cv_;
  bool quit_ = false;

  std::vector<s2s::MacroStep> steps_;
  std::atomic<bool> playing_{false};
  int64_t start_us_ = 0;
  uint32_t duration_ms_ = 0;

  std::atomic<bool> recording_{false};
  s2s::MacroRecorder recorder_;
  int64_t record_start_us_ = 0;

  uint8_t battery_level_ = 9;
  bool charging_ = false;
};
