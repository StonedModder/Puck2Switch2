// macro_engine.cpp
#include "macro_engine.hpp"

#include <algorithm>

#include "esp_timer.h"

using namespace s2s;

namespace {
inline float u12_to_axis(uint16_t v) {
  if (v > MACRO_STICK_MAX) v = MACRO_STICK_MAX;
  return (static_cast<float>(v) - 2048.0f) / 2047.0f;
}

// Decode the 12-bit stick pair packed at `p` (see pack_stick in sc2_core.h).
inline void unpack_stick(const uint8_t *p, uint16_t &x, uint16_t &y) {
  x = static_cast<uint16_t>(p[0] | ((p[1] & 0x0F) << 8));
  y = static_cast<uint16_t>(((p[1] >> 4) & 0x0F) | (p[2] << 4));
}

// Read back the Switch-visible button mask from a report body.
inline uint32_t buttons_from_report(const uint8_t *r) {
  uint32_t b = 0;
  if (r[2] & 0x02) b |= MB_A;
  if (r[2] & 0x01) b |= MB_B;
  if (r[2] & 0x08) b |= MB_X;
  if (r[2] & 0x04) b |= MB_Y;
  if (r[2] & 0x10) b |= MB_R;
  if (r[2] & 0x20) b |= MB_ZR;
  if (r[2] & 0x40) b |= MB_PLUS;
  if (r[2] & 0x80) b |= MB_RSTICK;
  if (r[3] & 0x10) b |= MB_L;
  if (r[3] & 0x20) b |= MB_ZL;
  if (r[3] & 0x40) b |= MB_MINUS;
  if (r[3] & 0x80) b |= MB_LSTICK;
  if (r[3] & 0x08) b |= MB_DUP;
  if (r[3] & 0x01) b |= MB_DDOWN;
  if (r[3] & 0x04) b |= MB_DLEFT;
  if (r[3] & 0x02) b |= MB_DRIGHT;
  if (r[4] & 0x01) b |= MB_HOME;
  if (r[4] & 0x02) b |= MB_CAPTURE;
  if (r[4] & 0x08) b |= MB_GL;
  if (r[4] & 0x04) b |= MB_GR;
  return b;
}
} // namespace

void MacroEngine::begin() {
  quit_ = false;
  task_ = std::thread([this] { run(); });
}

void MacroEngine::end() {
  {
    std::lock_guard<std::mutex> lk(mtx_);
    quit_ = true;
  }
  cv_.notify_all();
  if (task_.joinable()) task_.join();
}

bool MacroEngine::start(const std::vector<MacroStep> &steps) {
  if (steps.empty()) return false;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    steps_ = steps;
    duration_ms_ = macro_total_ms(steps_);
    start_us_ = esp_timer_get_time();
  }
  playing_.store(true);
  cv_.notify_all();
  return true;
}

void MacroEngine::stop() { playing_.store(false); }

uint32_t MacroEngine::position_ms() const {
  if (!playing_.load()) return 0;
  const int64_t dt = esp_timer_get_time() - start_us_;
  return dt > 0 ? static_cast<uint32_t>(dt / 1000) : 0;
}

uint32_t MacroEngine::duration_ms() const {
  std::lock_guard<std::mutex> lk(const_cast<std::mutex &>(mtx_));
  return duration_ms_;
}

void MacroEngine::emit(const MacroStepView &v) {
  if (!sink_) return;
  espp::switch2::Pro2InputReport r;
  r.reset();
  const uint32_t b = v.buttons;
  r.set_a(b & MB_A);
  r.set_b(b & MB_B);
  r.set_x(b & MB_X);
  r.set_y(b & MB_Y);
  r.set_l(b & MB_L);
  r.set_r(b & MB_R);
  r.set_zl(b & MB_ZL);
  r.set_zr(b & MB_ZR);
  r.set_plus(b & MB_PLUS);
  r.set_minus(b & MB_MINUS);
  r.set_home(b & MB_HOME);
  r.set_capture(b & MB_CAPTURE);
  r.set_lstick(b & MB_LSTICK);
  r.set_rstick(b & MB_RSTICK);
  r.set_up(b & MB_DUP);
  r.set_down(b & MB_DDOWN);
  r.set_left(b & MB_DLEFT);
  r.set_right(b & MB_DRIGHT);
  r.set_gl(b & MB_GL);
  r.set_gr(b & MB_GR);
  r.set_left_stick(u12_to_axis(v.lx), u12_to_axis(v.ly));
  r.set_right_stick(u12_to_axis(v.rx), u12_to_axis(v.ry));
  r.set_power(battery_level_, charging_, false);
  sink_(r);
}

void MacroEngine::run() {
  constexpr auto kTick = std::chrono::microseconds(2000); // 2 ms
  auto next = std::chrono::steady_clock::now();
  while (true) {
    {
      std::unique_lock<std::mutex> lk(mtx_);
      if (quit_) return;
      if (!playing_.load()) {
        cv_.wait_for(lk, std::chrono::milliseconds(50), [this] {
          return quit_ || playing_.load();
        });
        if (quit_) return;
        next = std::chrono::steady_clock::now();
        continue;
      }
    }

    std::vector<MacroStep> snapshot;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      snapshot = steps_;
    }
    if (snapshot.empty()) {
      playing_.store(false);
      continue;
    }

    const int64_t elapsed_us = esp_timer_get_time() - start_us_;
    const uint32_t t = elapsed_us > 0 ? static_cast<uint32_t>(elapsed_us / 1000) : 0;

    MacroStepView v;
    if (macro_sample(snapshot, t, v)) {
      emit(v);
    } else {
      // Release everything once when the timeline ends, otherwise the last held
      // button stays pressed on the console forever.
      MacroStepView neutral;
      emit(neutral);
      playing_.store(false);
      continue;
    }

    next += kTick;
    std::this_thread::sleep_until(next);
    // If the task fell behind (Wi-Fi burst), do not try to catch up.
    if (std::chrono::steady_clock::now() - next > std::chrono::milliseconds(50)) {
      next = std::chrono::steady_clock::now();
    }
  }
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------
void MacroEngine::record_start() {
  {
    std::lock_guard<std::mutex> lk(mtx_);
    recorder_.clear();
  }
  record_start_us_ = esp_timer_get_time();
  recording_.store(true);
}

uint32_t MacroEngine::record_ms() const {
  if (!recording_.load()) return 0;
  const int64_t dt = esp_timer_get_time() - record_start_us_;
  return dt > 0 ? static_cast<uint32_t>(dt / 1000) : 0;
}

void MacroEngine::record_feed(const uint8_t *report63, uint32_t now_ms) {
  if (!recording_.load()) return;
  uint16_t lx, ly, rx, ry;
  unpack_stick(report63 + 5, lx, ly);
  unpack_stick(report63 + 8, rx, ry);
  std::lock_guard<std::mutex> lk(mtx_);
  recorder_.observe(buttons_from_report(report63), lx, ly, rx, ry, now_ms);
}

std::vector<MacroStep> MacroEngine::record_stop() {
  recording_.store(false);
  const int64_t dt = esp_timer_get_time() - record_start_us_;
  const uint32_t ms = dt > 0 ? static_cast<uint32_t>(dt / 1000) : 0;
  std::lock_guard<std::mutex> lk(mtx_);
  return recorder_.finish(ms);
}
