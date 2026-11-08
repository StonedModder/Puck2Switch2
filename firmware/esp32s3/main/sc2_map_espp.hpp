// sc2_map_espp.hpp — apply a decoded SC2 state to espp's real Pro2InputReport
// using its public setters.
//
// This intentionally mirrors s2s::map_sc2_to_sw2() (the raw-byte reference
// implementation in sc2_core.h) so both paths produce identical bytes.
#pragma once

#include "sc2_core.h"
#include "switch2_pro_report.hpp"

namespace s2s {

inline void apply_sc2_to_pro2(const Sc2Input &in, const Sc2Battery &bat, const MapConfig &cfg,
                              espp::switch2::Pro2InputReport &ref) {
  const bool south = in.pressed(SC2_A);
  const bool east = in.pressed(SC2_B);
  const bool west = in.pressed(SC2_X);
  const bool north = in.pressed(SC2_Y);

  if (cfg.face_label_mode == 0) {
    // position-based: SC2 south->Switch B, east->A, west->Y, north->X
    ref.set_b(south);
    ref.set_a(east);
    ref.set_y(west);
    ref.set_x(north);
  } else {
    ref.set_a(south);
    ref.set_b(east);
    ref.set_x(west);
    ref.set_y(north);
  }

  // Bumper -> L/R only; trigger full-pull click -> ZL/ZR only. These are
  // separate digital inputs on the Switch 2 and must not be conflated.
  ref.set_r(in.pressed(SC2_RB));
  ref.set_zr(in.pressed(SC2_RTRIGGER_CLICK));
  ref.set_l(in.pressed(SC2_LB));
  ref.set_zl(in.pressed(SC2_LTRIGGER_CLICK));

  // plus/minus follow SDL's SC2 mapping: View->START(Plus), Menu->BACK(Minus)
  ref.set_plus(in.pressed(SC2_VIEW));
  ref.set_minus(in.pressed(SC2_MENU));

  ref.set_rstick(in.pressed(SC2_R3));
  ref.set_lstick(in.pressed(SC2_L3));

  ref.set_up(in.pressed(SC2_DPAD_UP));
  ref.set_down(in.pressed(SC2_DPAD_DOWN));
  ref.set_left(in.pressed(SC2_DPAD_LEFT));
  ref.set_right(in.pressed(SC2_DPAD_RIGHT));

  ref.set_home(in.pressed(SC2_STEAM));
  ref.set_capture(in.pressed(SC2_QAM));
  ref.set_gl(in.pressed(SC2_L4));
  ref.set_gr(in.pressed(SC2_R4));
  // L5/R5 lower paddles: no Switch 2 equivalent, unmapped.

  ref.set_left_stick(norm_stick(in.lx, cfg.deadzone), norm_stick(in.ly, cfg.deadzone));
  ref.set_right_stick(norm_stick(in.rx, cfg.deadzone), norm_stick(in.ry, cfg.deadzone));

  // Battery: 0..9 bars. Never report 0 while the level is merely UNKNOWN — until
  // the first notification arrives bat.valid is false, and advertising an empty
  // battery made the console treat the pad as dying (battery warnings / drops).
  // Report full until a real reading lands, then scale it.
  uint8_t lvl9 = 9;
  if (bat.valid) {
    lvl9 = static_cast<uint8_t>((bat.level_pct * 9u + 50u) / 100u);
    if (lvl9 == 0 && bat.level_pct > 0) lvl9 = 1; // not empty unless truly empty
  }
  ref.set_power(lvl9, bat.charging, false);
}

} // namespace s2s
