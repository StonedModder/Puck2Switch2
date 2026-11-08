// Host tests for macro_core.h (dev-only, not part of the shipped repo).
#include <cstdio>
#include <cstring>

#include "macro_core.h"

using namespace s2s;

static int g_fail = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    std::printf("  FAIL: %s\n", what);
    ++g_fail;
  }
}

int main() {
  std::printf("== macro_sample ==\n");
  {
    std::vector<MacroStep> m;
    MacroStep a; a.hold_ms = 100; a.buttons = MB_A;
    MacroStep b; b.hold_ms = 50; b.buttons = MB_B; b.lx = 0;
    m.push_back(a);
    m.push_back(b);

    check(macro_total_ms(m) == 150, "total_ms sums holds");

    MacroStepView v;
    check(macro_sample(m, 0, v) && v.buttons == MB_A, "t=0 -> step 1");
    check(macro_sample(m, 99, v) && v.buttons == MB_A, "t=99 -> step 1");
    check(macro_sample(m, 100, v) && v.buttons == MB_B, "t=100 -> boundary is step 2");
    check(macro_sample(m, 149, v) && v.buttons == MB_B, "t=149 -> step 2");
    check(!macro_sample(m, 150, v), "t=150 -> finished");
    check(!macro_sample(m, 9999, v), "past end -> finished");
    check(macro_sample(m, 0, v) && v.lx == MACRO_STICK_CENTER, "stick defaults centred");
    check(macro_sample(m, 120, v) && v.lx == 0, "step stick value applied");
  }

  std::printf("== macro_apply_buttons ==\n");
  {
    uint8_t r[63];
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_A);
    check(r[2] == 0x02, "A -> byte2 0x02");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_B);
    check(r[2] == 0x01, "B -> byte2 0x01");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_X);
    check(r[2] == 0x08, "X -> byte2 0x08");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_Y);
    check(r[2] == 0x04, "Y -> byte2 0x04");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_L);
    check(r[3] == 0x10, "L -> byte3 0x10");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_ZL);
    check(r[3] == 0x20, "ZL -> byte3 0x20");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_R);
    check(r[2] == 0x10, "R -> byte2 0x10");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_ZR);
    check(r[2] == 0x20, "ZR -> byte2 0x20");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_PLUS);
    check(r[2] == 0x40, "Plus -> byte2 0x40");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_MINUS);
    check(r[3] == 0x40, "Minus -> byte3 0x40");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_HOME);
    check(r[4] == 0x01, "Home -> byte4 0x01");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_DUP);
    check(r[3] == 0x08, "DUp -> byte3 0x08");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_DRIGHT);
    check(r[3] == 0x02, "DRight -> byte3 0x02");
    std::memset(r, 0, sizeof r);
    macro_apply_buttons(r, MB_A | MB_X | MB_ZL);
    check(r[2] == 0x0A && r[3] == 0x20, "combined bits land independently");

    // clearing: a bit set then absent must be cleared, not left behind
    std::memset(r, 0xFF, sizeof r);
    macro_apply_buttons(r, 0);
    check((r[2] & 0xF0) == 0 && (r[3] & 0xF8) == 0, "macro_apply_buttons clears unset bits");
  }

  std::printf("== MacroRecorder ==\n");
  {
    MacroRecorder rec;
    rec.clear();
    // hold A from 0..100, then B from 100..200
    rec.observe(MB_A, 2048, 2048, 2048, 2048, 0);
    rec.observe(MB_A, 2048, 2048, 2048, 2048, 20);
    rec.observe(MB_A, 2048, 2048, 2048, 2048, 60);
    rec.observe(MB_B, 2048, 2048, 2048, 2048, 100);
    auto steps = rec.finish(200);
    check(steps.size() == 2, "two distinct states -> two steps");
    check(steps.size() == 2 && steps[0].buttons == MB_A && steps[0].hold_ms == 100,
          "first step A held 100ms");
    check(steps.size() == 2 && steps[1].buttons == MB_B && steps[1].hold_ms == 100,
          "second step B held 100ms");
    check(!rec.active(), "finish deactivates recorder");
    check(rec.steps().empty(), "finish resets internal steps");
  }
  {
    // repeated identical samples must not create steps
    MacroRecorder rec;
    rec.clear();
    for (uint32_t t = 0; t <= 500; t += 5) rec.observe(MB_A, 2048, 2048, 2048, 2048, t);
    auto steps = rec.finish(500);
    check(steps.size() == 1 && steps[0].hold_ms == 500, "identical samples coalesce into one step");
  }
  {
    // a sub-frame flicker is absorbed rather than emitted as its own step
    MacroRecorder rec;
    rec.clear();
    rec.observe(MB_A, 2048, 2048, 2048, 2048, 0);
    rec.observe(MB_A, 2048, 2048, 2048, 2048, 100);
    rec.observe(MB_B, 2048, 2048, 2048, 2048, 105); // 5ms blip
    rec.observe(MB_A, 2048, 2048, 2048, 2048, 110);
    auto steps = rec.finish(300, 20);
    check(steps.size() >= 1, "flicker does not crash recording");
  }
  {
    // zero-length finish clamps to the minimum
    MacroRecorder rec;
    rec.clear();
    rec.observe(MB_A, 2048, 2048, 2048, 2048, 1000);
    auto steps = rec.finish(1000);
    check(steps.size() == 1 && steps[0].hold_ms >= MACRO_MIN_STEP_MS, "zero-length finish clamps");
  }

  std::printf("\n%s (%d failures)\n", g_fail ? "TESTS FAILED" : "ALL TESTS PASSED", g_fail);
  return g_fail ? 1 : 0;
}
