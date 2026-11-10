// test_power_field.cpp — host test for the Switch 2 Pro report power byte.
//
// Bug under test: the console connects, subscribes to the input characteristic,
// and espp's streaming task immediately begins notifying its stored
// Pro2InputReport once per connection interval. Until the FIRST Steam
// Controller 2 notification arrives, nothing has ever called set_power(), so
// the console reads whatever reset()/default-construction left in report byte 1.
// Pre-fix that byte was 0x00, which decodes to battery level 0 = EMPTY, and the
// console warns about a dying controller and drops the link.
//
// Byte 1 layout (switch2_pro_report.hpp set_power):
//   bits 5..2 = battery level 0..9, bit 1 = charging, bit 0 = external power.
//
// Build (no ESP-IDF, header-only):
//   g++ -std=c++17 -I _refs/espp/components/switch2_pro/include \
//       -I firmware/core/include -O2 -o /tmp/tpow host_tests/test_power_field.cpp && /tmp/tpow

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "sc2_core.h"
#include "sc2_map_espp.hpp"
#include "switch2_pro_report.hpp"

using espp::switch2::Pro2InputReport;

static int g_fail = 0;

static uint8_t raw_power(const Pro2InputReport &r) { return r.data()[1]; }
static uint8_t level_of(uint8_t raw) { return static_cast<uint8_t>((raw >> 2) & 0x0f); }
static bool charging_of(uint8_t raw) { return (raw & 0x02) != 0; }
static bool external_of(uint8_t raw) { return (raw & 0x01) != 0; }

static void check(const char *name, bool ok, const char *detail) {
  std::printf("[%s] %s — %s\n", ok ? "PASS" : "FAIL", name, detail);
  if (!ok) g_fail++;
}

// Byte-for-byte replica of the PRE-FIX reset() body, kept so the fix is a
// measured delta rather than a claim. If this ever stops reproducing 0x00 the
// bug description itself is wrong.
static uint8_t legacy_reset_power_byte() {
  std::array<uint8_t, Pro2InputReport::SIZE> d{};
  d.fill(0);
  d[0x0b] = 0x30; // the only byte old reset() set besides the sticks
  // old reset() then called set_left_stick/set_right_stick, which touch
  // offsets 5..10 only — never offset 1.
  return d[1];
}

// Minimum level that will not read as "dying" to the console. 9 = full bars.
static constexpr uint8_t kHealthyMin = 5;

int main() {
  std::printf("== Switch 2 Pro report power byte (report offset 1) ==\n\n");

  // ---- (f) BEFORE -------------------------------------------------------
  const uint8_t before = legacy_reset_power_byte();
  std::printf("BEFORE (pre-fix reset() replica): byte1=0x%02X level=%u charging=%d ext=%d\n",
              before, level_of(before), (int)charging_of(before), (int)external_of(before));
  check("f: pre-fix default really was a false zero", before == 0x00 && level_of(before) == 0,
        "pre-fix byte1 == 0x00 -> level 0 (EMPTY) streamed to the console");
  std::printf("\n");

  // ---- (a) default construction -----------------------------------------
  {
    Pro2InputReport r; // exactly what espp's `Pro2InputReport input_report_{}` yields
    const uint8_t b = raw_power(r);
    char d[160];
    std::snprintf(d, sizeof d, "default-constructed byte1=0x%02X level=%u (need >= %u)", b,
                  level_of(b), kHealthyMin);
    check("a: default-constructed report is healthy", level_of(b) >= kHealthyMin, d);
  }

  // ---- (b) after reset() -------------------------------------------------
  {
    Pro2InputReport r;
    r.set_power(0, false, false); // scribble a genuine empty reading in first
    r.reset();
    const uint8_t b = raw_power(r);
    char d[160];
    std::snprintf(d, sizeof d, "after reset() byte1=0x%02X level=%u (need >= %u)", b, level_of(b),
                  kHealthyMin);
    check("b: reset() restores a healthy level", level_of(b) >= kHealthyMin, d);
  }

  // ---- mapper paths ------------------------------------------------------
  s2s::Sc2Input in{};
  s2s::MapConfig cfg{};

  // ---- (c) invalid / no-data-yet battery ---------------------------------
  {
    s2s::Sc2Battery bat{}; // valid == false, level_pct == 0
    Pro2InputReport r;
    s2s::apply_sc2_to_pro2(in, bat, cfg, r);
    const uint8_t b = raw_power(r);
    char d[160];
    std::snprintf(d, sizeof d, "bat.valid=false -> byte1=0x%02X level=%u (need >= %u)", b,
                  level_of(b), kHealthyMin);
    check("c: unknown battery maps to healthy", level_of(b) >= kHealthyMin, d);
  }

  // ---- (d) genuine 0 percent is NOT masked -------------------------------
  {
    s2s::Sc2Battery bat{};
    bat.valid = true;
    bat.level_pct = 0;
    Pro2InputReport r;
    s2s::apply_sc2_to_pro2(in, bat, cfg, r);
    const uint8_t b = raw_power(r);
    char d[160];
    std::snprintf(d, sizeof d, "valid 0%% -> byte1=0x%02X level=%u (need == 0)", b, level_of(b));
    check("d: a real empty battery still reports 0", level_of(b) == 0, d);
  }

  // ---- (e) scaling -------------------------------------------------------
  struct {
    uint8_t pct;
    uint8_t want;
  } scale[] = {{100, 9}, {50, 5}, {1, 1}, {5, 1}, {11, 1}, {90, 8}};
  for (auto &s : scale) {
    s2s::Sc2Battery bat{};
    bat.valid = true;
    bat.level_pct = s.pct;
    Pro2InputReport r;
    s2s::apply_sc2_to_pro2(in, bat, cfg, r);
    const uint8_t b = raw_power(r);
    char name[64], d[160];
    std::snprintf(name, sizeof name, "e: %u%% -> %u bars", s.pct, s.want);
    std::snprintf(d, sizeof d, "byte1=0x%02X level=%u (want %u)", b, level_of(b), s.want);
    check(name, level_of(b) == s.want, d);
  }

  // ---- (e2) a low-but-nonzero reading must never round down to EMPTY -----
  for (unsigned pct = 1; pct <= 100; ++pct) {
    s2s::Sc2Battery bat{};
    bat.valid = true;
    bat.level_pct = static_cast<uint8_t>(pct);
    Pro2InputReport r;
    s2s::apply_sc2_to_pro2(in, bat, cfg, r);
    if (level_of(raw_power(r)) == 0) {
      char d[160];
      std::snprintf(d, sizeof d, "%u%% rounded down to level 0", pct);
      check("e2: nonzero pct never encodes EMPTY", false, d);
      break;
    }
    if (pct == 100) check("e2: nonzero pct never encodes EMPTY (1..100%%)", true,
                          "no pct in 1..100 maps to level 0");
  }

  // ---- charging flag passthrough ----------------------------------------
  {
    s2s::Sc2Battery bat{};
    bat.valid = true;
    bat.level_pct = 42;
    bat.charging = true;
    Pro2InputReport r;
    s2s::apply_sc2_to_pro2(in, bat, cfg, r);
    const uint8_t b = raw_power(r);
    char d[160];
    std::snprintf(d, sizeof d, "byte1=0x%02X level=%u charging=%d", b, level_of(b),
                  (int)charging_of(b));
    check("g: charging flag reaches byte1 bit1", charging_of(b), d);
  }

  // ---- no code path leaves byte1 at 0x00 without a real 0 reading --------
  {
    Pro2InputReport r;
    bool bad = false;
    // Hammer every non-power mutator and confirm none clears the power byte.
    for (int i = 0; i < 2; ++i) {
      const bool v = (i == 0);
      r.set_a(v); r.set_b(v); r.set_x(v); r.set_y(v);
      r.set_l(v); r.set_r(v); r.set_zl(v); r.set_zr(v);
      r.set_plus(v); r.set_minus(v); r.set_home(v); r.set_capture(v);
      r.set_gl(v); r.set_gr(v); r.set_c(v);
      r.set_up(v); r.set_down(v); r.set_left(v); r.set_right(v);
      r.set_lstick(v); r.set_rstick(v);
      r.set_left_stick(v ? 1.f : -1.f, v ? -1.f : 1.f);
      r.set_right_stick(0.f, 0.f);
      uint8_t blk[0x28];
      std::memset(blk, 0xAB, sizeof blk);
      r.set_motion_block(blk, sizeof blk, 0x1E);
      if (level_of(raw_power(r)) < kHealthyMin) bad = true;
    }
    char d[160];
    std::snprintf(d, sizeof d, "after all button/stick/motion writes byte1=0x%02X level=%u",
                  raw_power(r), level_of(raw_power(r)));
    check("h: no other setter can zero the power byte", !bad, d);
  }

  std::printf("\n%s (%d failure%s)\n", g_fail ? "RESULT: FAIL" : "RESULT: PASS", g_fail,
              g_fail == 1 ? "" : "s");
  return g_fail ? 1 : 0;
}
