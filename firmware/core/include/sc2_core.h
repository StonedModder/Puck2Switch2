// sc2_core.h — Steam Controller 2 ("Triton") BLE input parsing + Switch 2
// Pro Controller 2 mapping.  Pure C++ , no ESP-IDF / no BLE deps, so it is
// host-testable with g++/clang.
//
// Sources (both permissively licensed, no AGPL code used):
//   SC2 report 0x45 layout : joypad-os steam_controller_2_ble.h/.c (Apache-2.0)
//                            cross-checked vs CouchTurtle/sc2-research docs (MIT)
//                            and SDL3 src/joystick/hidapi/steam/controller_structs.h
//   Switch 2 report 0x09   : espp switch2_pro_report.hpp (MIT)
//
// Over BLE the SC2 notification body does NOT include the report-id byte; the
// GATT client prepends the implied id (0x45 or 0x47).  parse() therefore expects
// r[0] == report id, matching joypad-os's "assembled report" convention.
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s2s {

// ---------------------------------------------------------------------------
// SC2 button bits (u32 at report offset 2, little-endian)
// 30 bits.  Identical in joypad-os (Apache-2.0) and sc2-research (MIT).
// ---------------------------------------------------------------------------
enum Sc2Button : uint32_t {
  SC2_A = 0,               // South face
  SC2_B = 1,               // East face
  SC2_X = 2,               // West face
  SC2_Y = 3,               // North face
  SC2_QAM = 4,             // Quick-access menu ("...")
  SC2_R3 = 5,              // Right stick click
  SC2_VIEW = 6,            // View (left of Steam)
  SC2_R4 = 7,              // Right upper grip paddle
  SC2_R5 = 8,              // Right lower grip paddle
  SC2_RB = 9,              // Right bumper
  SC2_DPAD_DOWN = 10,
  SC2_DPAD_RIGHT = 11,
  SC2_DPAD_LEFT = 12,
  SC2_DPAD_UP = 13,
  SC2_MENU = 14,           // Menu (right of Steam)
  SC2_L3 = 15,             // Left stick click
  SC2_STEAM = 16,          // Steam / Guide
  SC2_L4 = 17,             // Left upper grip paddle
  SC2_L5 = 18,             // Left lower grip paddle
  SC2_LB = 19,             // Left bumper
  SC2_RSTICK_TOUCH = 20,
  SC2_RPAD_TOUCH = 21,
  SC2_RPAD_CLICK = 22,
  SC2_RTRIGGER_CLICK = 23, // Right trigger full-pull
  SC2_LSTICK_TOUCH = 24,
  SC2_LPAD_TOUCH = 25,
  SC2_LPAD_CLICK = 26,
  SC2_LTRIGGER_CLICK = 27, // Left trigger full-pull
  SC2_RGRIP_TOUCH = 28,
  SC2_LGRIP_TOUCH = 29,
};

// SC2 report byte offsets (assembled report: byte 0 = report id)
static constexpr int SC2_OFF_BUTTONS = 2;
static constexpr int SC2_OFF_LTRIGGER = 6;
static constexpr int SC2_OFF_RTRIGGER = 8;
static constexpr int SC2_OFF_LX = 10;
static constexpr int SC2_OFF_LY = 12; // +up  (must invert for HID/Switch up=+)
static constexpr int SC2_OFF_RX = 14;
static constexpr int SC2_OFF_RY = 16; // +up
static constexpr int SC2_OFF_ACCEL = 34;
static constexpr int SC2_OFF_GYRO = 40;
static constexpr int SC2_MIN_REPORT_LEN = 18;    // buttons + sticks
static constexpr int SC2_MOTION_REPORT_LEN = 46; // + IMU

static constexpr uint8_t SC2_REPORT_45 = 0x45; // TritonMTUNoQuat
static constexpr uint8_t SC2_REPORT_47 = 0x47; // TritonMTUNoQuat32TS ("Ibex")

// ---------------------------------------------------------------------------
// Normalized SC2 state (raw, as decoded from the wire)
// ---------------------------------------------------------------------------
struct Sc2Input {
  uint32_t buttons = 0;   // bitfield of Sc2Button
  int16_t lx = 0, ly = 0; // -32768..32767, ly +up
  int16_t rx = 0, ry = 0; // ry +up
  uint16_t lt = 0, rt = 0; // 0..32767
  int16_t accel[3] = {0, 0, 0};
  int16_t gyro[3] = {0, 0, 0};
  // Device IMU time is retained for diagnostics; current fusion is unchanged.
  uint32_t imu_timestamp = 0;
  uint8_t imu_report_id = 0;
  bool has_motion = false;
  bool valid = false;

  bool pressed(Sc2Button b) const { return (buttons >> b) & 1u; }
};

// Battery from the 100F6C78 characteristic: byte0 charge state, byte1 level%.
struct Sc2Battery {
  uint8_t charge_state = 0; // 2=charging, 4=charge_done
  uint8_t level_pct = 0;
  bool charging = false;
  bool valid = false; // set once a real notification has been seen
};

// ---------------------------------------------------------------------------
// Parse an assembled SC2 report (r[0] = report id).  Returns false if the
// buffer is not a usable SC2 state report.
// ---------------------------------------------------------------------------
inline int16_t rd_s16(const uint8_t *r, int off) {
  return static_cast<int16_t>(static_cast<uint16_t>(r[off]) |
                              (static_cast<uint16_t>(r[off + 1]) << 8));
}
inline uint16_t rd_u16(const uint8_t *r, int off) {
  return static_cast<uint16_t>(static_cast<uint16_t>(r[off]) |
                               (static_cast<uint16_t>(r[off + 1]) << 8));
}

inline bool parse_sc2(const uint8_t *r, size_t len, Sc2Input &out) {
  out.valid = false;
  if (len < SC2_MIN_REPORT_LEN) return false;
  if (r[0] != SC2_REPORT_45 && r[0] != SC2_REPORT_47) return false;

  out.buttons = static_cast<uint32_t>(rd_u16(r, SC2_OFF_BUTTONS)) |
                (static_cast<uint32_t>(rd_u16(r, SC2_OFF_BUTTONS + 2)) << 16);
  out.lt = rd_u16(r, SC2_OFF_LTRIGGER);
  out.rt = rd_u16(r, SC2_OFF_RTRIGGER);
  out.lx = rd_s16(r, SC2_OFF_LX);
  out.ly = rd_s16(r, SC2_OFF_LY);
  out.rx = rd_s16(r, SC2_OFF_RX);
  out.ry = rd_s16(r, SC2_OFF_RY);

  out.has_motion = (len >= SC2_MOTION_REPORT_LEN);
  if (out.has_motion) {
    out.imu_report_id = r[0];
    if (r[0] == SC2_REPORT_45) {
      out.imu_timestamp = static_cast<uint32_t>(rd_u16(r, 30)) |
                          (static_cast<uint32_t>(rd_u16(r, 32)) << 16);
    } else {
      out.imu_timestamp = rd_u16(r, 32);
    }
    // RAW device axes, deliberately NOT remapped. The Switch 2 motion carrier
    // expects Nintendo's frame (Z out of the face). Measured on hardware, a
    // controller resting face-up reports gravity on raw Z (~16221 counts ~ 1 g),
    // so raw Z already IS the out-of-face axis. joypad-os remaps to SDL's frame
    // (+X, +Z, -Y) which is correct for SDL but wrong here: it moved the vertical
    // axis into slot 1 and produced motion in the wrong directions.
    out.accel[0] = rd_s16(r, SC2_OFF_ACCEL + 0);
    out.accel[1] = rd_s16(r, SC2_OFF_ACCEL + 2);
    out.accel[2] = rd_s16(r, SC2_OFF_ACCEL + 4);
    out.gyro[0] = rd_s16(r, SC2_OFF_GYRO + 0);
    out.gyro[1] = rd_s16(r, SC2_OFF_GYRO + 2);
    out.gyro[2] = rd_s16(r, SC2_OFF_GYRO + 4);
  }
  out.valid = true;
  return true;
}

inline Sc2Battery parse_sc2_battery(const uint8_t *v, size_t len) {
  Sc2Battery b;
  if (len < 2) return b;
  // Over BLE the report-id byte is stripped by the GATT client, so byte0 is the
  // charge state. Tolerate a payload that still carries the 0x43 id.
  size_t off = (len >= 3 && v[0] == 0x43) ? 1 : 0;
  if (len < off + 2) return b;
  b.charge_state = v[off];
  b.level_pct = v[off + 1];
  if (b.level_pct > 100) b.level_pct = 100;
  b.charging = (b.charge_state == 2 || b.charge_state == 4);
  b.valid = true;
  return b;
}

// ---------------------------------------------------------------------------
// MAC allowlist: connect to a known controller by address even when its
// advertisement carries no "Steam" name and no Valve service UUID.
// Accepts "aa:bb:cc:dd:ee:ff" (':' or '-'), comma/space separated, and is
// compared case-insensitively. Pure string logic so it is host-testable.
// ---------------------------------------------------------------------------
inline std::string normalize_mac(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    if (c == '-' || c == ':' || c == '.' || c == ' ') {
      // separators collapse to ':'
      if (!out.empty() && out.back() != ':') out.push_back(':');
      continue;
    }
    out.push_back((char)tolower((unsigned char)c));
  }
  while (!out.empty() && out.back() == ':') out.pop_back();
  return out;
}

struct MacFilter {
  std::vector<std::string> list; // normalized "aa:bb:cc:dd:ee:ff"
  bool only = false;             // strict: ignore name/service matching

  bool empty() const { return list.empty(); }

  void parse(const std::string &csv) {
    list.clear();
    std::string cur;
    for (char c : csv) {
      if (c == ',' || c == ';' || c == ' ' || c == '\n' || c == '\t') {
        if (!cur.empty()) { list.push_back(normalize_mac(cur)); cur.clear(); }
      } else {
        cur.push_back(c);
      }
    }
    if (!cur.empty()) list.push_back(normalize_mac(cur));
    // drop obviously invalid entries (a BLE address has 6 bytes = 17 chars)
    std::vector<std::string> keep;
    for (auto &m : list)
      if (m.size() == 17) keep.push_back(m);
    list.swap(keep);
  }

  bool matches(const std::string &addr) const {
    if (list.empty()) return false;
    const std::string n = normalize_mac(addr);
    for (const auto &m : list)
      if (m == n) return true;
    return false;
  }
};


// ---------------------------------------------------------------------------
// Mapping configuration
// ---------------------------------------------------------------------------
struct MapConfig {
  // Face buttons: 0 = position-based (SC2 south -> Switch B south; recommended,
  // preserves muscle memory, consensus for cross-brand adapters).
  // 1 = label-based (SC2 'A' -> Switch 'A').
  int face_label_mode = 0;
  // Stick deadzone in normalized units (SC2 idles with a small bias ~±300).
  float deadzone = 0.06f;
};

// Normalized stick: -1..+1 with +Y = up (identical convention both sides).
inline float norm_stick(int16_t v, float deadzone) {
  float f = static_cast<float>(v) / 32767.0f;
  if (f < -1.0f) f = -1.0f;
  if (f > 1.0f) f = 1.0f;
  if (f > -deadzone && f < deadzone) f = 0.0f;
  return f;
}

// 12-bit axis encoding, identical to espp's Pro2InputReport::axis_to_u12():
//   u12 = (clamp(v,-1,1) * 0.5 + 0.5) * 4095, rounded.
inline uint16_t axis_to_u12(float v) {
  if (v < -1.f) v = -1.f;
  if (v > 1.f) v = 1.f;
  return static_cast<uint16_t>((v * 0.5f + 0.5f) * 4095.0f + 0.5f);
}

// Pack a 12-bit (x,y) pair into 3 bytes, identical to espp pack_stick().
inline void pack_stick(uint8_t *out, uint16_t xv, uint16_t yv) {
  out[0] = static_cast<uint8_t>(xv & 0xff);
  out[1] = static_cast<uint8_t>(((yv & 0x0f) << 4) | ((xv >> 8) & 0x0f));
  out[2] = static_cast<uint8_t>((yv >> 4) & 0xff);
}

// ---------------------------------------------------------------------------
// Switch 2 Pro Controller 2 input report (id 0x09), 63 bytes.
// This mirrors espp's Pro2InputReport byte-for-byte (offsets 2/3/4 buttons,
// 5 and 8 sticks, byte 1 power, byte 0x0b = 0x30).
// ---------------------------------------------------------------------------
static constexpr size_t SW2_SIZE = 63;
static constexpr uint16_t SW2_STICK_CENTER = 0x800; // 2048
static constexpr uint16_t SW2_STICK_MAX = 0xfff;

inline void sw2_set_bit(uint8_t *d, size_t byte, uint8_t mask, bool v) {
  if (v) d[byte] |= mask;
  else d[byte] &= static_cast<uint8_t>(~mask);
}

inline void sw2_reset(uint8_t *d) {
  std::memset(d, 0, SW2_SIZE);
  d[0x0b] = 0x30;
  pack_stick(d + 5, SW2_STICK_CENTER, SW2_STICK_CENTER);
  pack_stick(d + 8, SW2_STICK_CENTER, SW2_STICK_CENTER);
}

inline void sw2_set_power(uint8_t *d, uint8_t level_0_9, bool charging, bool external) {
  const uint8_t level = level_0_9 > 9 ? 9 : level_0_9;
  d[1] = static_cast<uint8_t>((level << 2) | (charging ? 0x02 : 0x00) | (external ? 0x01 : 0x00));
}

// Map one SC2 state into a 63-byte Switch 2 0x09 report body.
inline void map_sc2_to_sw2(const Sc2Input &in, const Sc2Battery &bat, const MapConfig &cfg,
                           uint8_t *out) {
  sw2_reset(out);

  // ---- face buttons -------------------------------------------------------
  const bool south = in.pressed(SC2_A);
  const bool east = in.pressed(SC2_B);
  const bool west = in.pressed(SC2_X);
  const bool north = in.pressed(SC2_Y);

  bool sw_a, sw_b, sw_x, sw_y;
  if (cfg.face_label_mode == 0) {
    // position: south->B(offset2 0x01), east->A(0x02), west->Y(0x04), north->X(0x08)
    sw_b = south;
    sw_a = east;
    sw_y = west;
    sw_x = north;
  } else {
    // label: SC2 A->Switch A, B->B, X->X, Y->Y
    sw_a = south;
    sw_b = east;
    sw_x = west;
    sw_y = north;
  }
  sw2_set_bit(out, 2, 0x02, sw_a);
  sw2_set_bit(out, 2, 0x01, sw_b);
  sw2_set_bit(out, 2, 0x08, sw_x);
  sw2_set_bit(out, 2, 0x04, sw_y);

  // ---- shoulders / triggers ----------------------------------------------
  // Bumper -> L/R ONLY. Trigger full-pull click -> ZL/ZR ONLY. The bumper must
  // not also fire the trigger: they are separate digital inputs on the Switch 2.
  sw2_set_bit(out, 2, 0x10, in.pressed(SC2_RB));                // R  <- right bumper
  sw2_set_bit(out, 2, 0x20, in.pressed(SC2_RTRIGGER_CLICK));    // ZR <- right trigger click
  sw2_set_bit(out, 3, 0x10, in.pressed(SC2_LB));                // L  <- left bumper
  sw2_set_bit(out, 3, 0x20, in.pressed(SC2_LTRIGGER_CLICK));    // ZL <- left trigger click

  // ---- plus / minus / stick clicks ---------------------------------------
  sw2_set_bit(out, 2, 0x40, in.pressed(SC2_VIEW));  // Plus  <- View (SDL: View->START)
  sw2_set_bit(out, 3, 0x40, in.pressed(SC2_MENU));  // Minus <- Menu (SDL: Menu->BACK)
  sw2_set_bit(out, 2, 0x80, in.pressed(SC2_R3));    // RStick click
  sw2_set_bit(out, 3, 0x80, in.pressed(SC2_L3));    // LStick click

  // ---- d-pad --------------------------------------------------------------
  sw2_set_bit(out, 3, 0x08, in.pressed(SC2_DPAD_UP));
  sw2_set_bit(out, 3, 0x01, in.pressed(SC2_DPAD_DOWN));
  sw2_set_bit(out, 3, 0x04, in.pressed(SC2_DPAD_LEFT));
  sw2_set_bit(out, 3, 0x02, in.pressed(SC2_DPAD_RIGHT));

  // ---- system -------------------------------------------------------------
  sw2_set_bit(out, 4, 0x01, in.pressed(SC2_STEAM));  // Home <- Steam
  sw2_set_bit(out, 4, 0x02, in.pressed(SC2_QAM));    // Capture <- QAM
  sw2_set_bit(out, 4, 0x08, in.pressed(SC2_L4));     // GL <- left upper paddle
  sw2_set_bit(out, 4, 0x04, in.pressed(SC2_R4));     // GR <- right upper paddle
  // L5/R5 (lower paddles) have no Switch 2 equivalent: unmapped by default.

  // ---- sticks -------------------------------------------------------------
  const float lx = norm_stick(in.lx, cfg.deadzone);
  const float ly = norm_stick(in.ly, cfg.deadzone); // +up -> +up (no invert needed)
  const float rx = norm_stick(in.rx, cfg.deadzone);
  const float ry = norm_stick(in.ry, cfg.deadzone); // +up -> +up
  pack_stick(out + 5, axis_to_u12(lx), axis_to_u12(ly));
  pack_stick(out + 8, axis_to_u12(rx), axis_to_u12(ry));

  // ---- power --------------------------------------------------------------
  // SC2 reports 0..100%; Switch 2 wants 0..9. Until the first real battery
  // notification arrives, report FULL rather than empty: level 0 means "empty"
  // to the console, and defaulting to it makes the pad flash a false low-battery
  // warning on every connect.
  uint8_t lvl9 = 9;
  if (bat.valid) lvl9 = static_cast<uint8_t>((bat.level_pct * 9u + 50u) / 100u);
  sw2_set_power(out, lvl9, bat.charging, false);
}

} // namespace s2s
