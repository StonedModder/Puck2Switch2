// macro_core.h — portable macro model for the web macro editor.
//
// A macro is a flat list of steps. Each step holds an absolute input state for a
// duration, so playback needs no interpolation and an edit never changes the
// meaning of a later step. Absolute states (rather than press/release events)
// also make the timeline trivially re-orderable and diffable as JSON.
//
// Pure C++: no ESP-IDF, no cJSON, no allocation beyond std::vector. The firmware
// layer owns JSON and storage; this layer owns timing and button semantics.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace s2s {

// Switch 2 Pro Controller buttons, independent of the Steam Controller layout.
// The editor and the JSON format both use these names.
enum MacroButton : uint32_t {
  MB_A = 1u << 0,
  MB_B = 1u << 1,
  MB_X = 1u << 2,
  MB_Y = 1u << 3,
  MB_L = 1u << 4,
  MB_R = 1u << 5,
  MB_ZL = 1u << 6,
  MB_ZR = 1u << 7,
  MB_PLUS = 1u << 8,
  MB_MINUS = 1u << 9,
  MB_HOME = 1u << 10,
  MB_CAPTURE = 1u << 11,
  MB_LSTICK = 1u << 12,
  MB_RSTICK = 1u << 13,
  MB_DUP = 1u << 14,
  MB_DDOWN = 1u << 15,
  MB_DLEFT = 1u << 16,
  MB_DRIGHT = 1u << 17,
  MB_GL = 1u << 18,
  MB_GR = 1u << 19,
};

static constexpr uint16_t MACRO_STICK_CENTER = 0x800; // 2048
static constexpr uint16_t MACRO_STICK_MAX = 0xFFF;
static constexpr uint16_t MACRO_MIN_STEP_MS = 10;
static constexpr uint16_t MACRO_MAX_STEP_MS = 60000;

struct MacroStep {
  uint16_t hold_ms = MACRO_MIN_STEP_MS;
  uint32_t buttons = 0;
  uint16_t lx = MACRO_STICK_CENTER;
  uint16_t ly = MACRO_STICK_CENTER;
  uint16_t rx = MACRO_STICK_CENTER;
  uint16_t ry = MACRO_STICK_CENTER;

  bool same_state(const MacroStep &o) const {
    return buttons == o.buttons && lx == o.lx && ly == o.ly && rx == o.rx && ry == o.ry;
  }
};

struct MacroStepView {
  uint32_t buttons = 0;
  uint16_t lx = MACRO_STICK_CENTER, ly = MACRO_STICK_CENTER;
  uint16_t rx = MACRO_STICK_CENTER, ry = MACRO_STICK_CENTER;
};

inline uint32_t macro_total_ms(const std::vector<MacroStep> &steps) {
  uint32_t t = 0;
  for (const auto &s : steps) t += s.hold_ms;
  return t;
}

// Write a button bitmask into the 63-byte Switch 2 0x09 report body. Kept in
// lockstep with map_sc2_to_sw2() in sc2_core.h (same byte/bit positions).
inline void macro_apply_buttons(uint8_t *out, uint32_t b) {
  auto set = [&](size_t byte, uint8_t mask, bool v) {
    if (v) out[byte] |= mask;
    else out[byte] &= static_cast<uint8_t>(~mask);
  };
  set(2, 0x02, b & MB_A);
  set(2, 0x01, b & MB_B);
  set(2, 0x08, b & MB_X);
  set(2, 0x04, b & MB_Y);
  set(2, 0x10, b & MB_R);
  set(2, 0x20, b & MB_ZR);
  set(2, 0x40, b & MB_PLUS);
  set(2, 0x80, b & MB_RSTICK);

  set(3, 0x10, b & MB_L);
  set(3, 0x20, b & MB_ZL);
  set(3, 0x40, b & MB_MINUS);
  set(3, 0x80, b & MB_LSTICK);
  set(3, 0x08, b & MB_DUP);
  set(3, 0x01, b & MB_DDOWN);
  set(3, 0x04, b & MB_DLEFT);
  set(3, 0x02, b & MB_DRIGHT);

  set(4, 0x01, b & MB_HOME);
  set(4, 0x02, b & MB_CAPTURE);
  set(4, 0x08, b & MB_GL);
  set(4, 0x04, b & MB_GR);
}

// Sample the macro at absolute time t (ms from its start). Returns false once t
// has passed the end, so a caller can stop cleanly instead of looping.
inline bool macro_sample(const std::vector<MacroStep> &steps, uint32_t t_ms, MacroStepView &out) {
  uint32_t acc = 0;
  for (const auto &s : steps) {
    acc += s.hold_ms;
    if (t_ms < acc) {
      out.buttons = s.buttons;
      out.lx = s.lx;
      out.ly = s.ly;
      out.rx = s.rx;
      out.ry = s.ry;
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Recorder
// ---------------------------------------------------------------------------
// Turns a stream of observed input states into steps. Consecutive samples with
// the same state extend the current step instead of creating one per sample, so
// a 30 s recording becomes tens of steps rather than thousands.
class MacroRecorder {
public:
  void clear() {
    steps_.clear();
    open_ = MacroStep{};
    open_start_ms_ = 0;
    have_open_ = false;
    last_ms_ = 0;
  }

  bool active() const { return have_open_; }
  const std::vector<MacroStep> &steps() const { return steps_; }

  // Feed one observation at t_ms. `min_hold_ms` coalesces flicker: a state that
  // lasted less than this is extended rather than emitted, because a press
  // shorter than a frame is not reliably seen by the console.
  void observe(uint32_t buttons, uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry, uint32_t t_ms,
               uint16_t min_hold_ms = 20) {
    if (!have_open_) {
      open_ = MacroStep{};
      open_.buttons = buttons;
      open_.lx = lx;
      open_.ly = ly;
      open_.rx = rx;
      open_.ry = ry;
      open_start_ms_ = t_ms;
      have_open_ = true;
      last_ms_ = t_ms;
      return;
    }
    last_ms_ = t_ms;
    MacroStep cur;
    cur.buttons = buttons;
    cur.lx = lx;
    cur.ly = ly;
    cur.rx = rx;
    cur.ry = ry;
    if (cur.same_state(open_)) return;

    const uint32_t held = t_ms - open_start_ms_;
    if (held < min_hold_ms) {
      // Too short to be a deliberate input: keep the previous state and let the
      // new one become the continuation, so the flicker does not create a step.
      open_ = cur;
      return;
    }
    open_.hold_ms = static_cast<uint16_t>(held > MACRO_MAX_STEP_MS ? MACRO_MAX_STEP_MS : held);
    steps_.push_back(open_);
    open_ = cur;
    open_start_ms_ = t_ms;
  }

  // Close the recording at t_ms and return the finished steps.
  std::vector<MacroStep> finish(uint32_t t_ms, uint16_t min_hold_ms = MACRO_MIN_STEP_MS) {
    if (have_open_) {
      uint32_t held = t_ms > open_start_ms_ ? t_ms - open_start_ms_ : 0;
      if (held < min_hold_ms) held = min_hold_ms;
      open_.hold_ms = static_cast<uint16_t>(held > MACRO_MAX_STEP_MS ? MACRO_MAX_STEP_MS : held);
      steps_.push_back(open_);
      have_open_ = false;
    }
    auto out = steps_;
    clear();
    return out;
  }

private:
  std::vector<MacroStep> steps_;
  MacroStep open_{};
  uint32_t open_start_ms_ = 0;
  uint32_t last_ms_ = 0;
  bool have_open_ = false;
};

} // namespace s2s
