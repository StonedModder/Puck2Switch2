// Host-testable motion conditioning. Raw axes stay in the Nintendo frame.
#pragma once
#include "ns2_motion30.h"
#include <cmath>
#include <cstdint>

namespace s2s {
// A quiet startup window is an assumption, not a proof of stillness: steady
// slow yaw and gyro bias are indistinguishable without an external reference.
// Once acquired, the estimate is frozen so a later slow pan is not learned.
struct GyroBiasEstimator {
  float bias[3] = {0, 0, 0};
  bool calibrated = false;
  float seconds = 0;
  float sum[3] = {0, 0, 0}, sumsq[3] = {0, 0, 0};
  unsigned count = 0;
  void clear_window() {
    seconds = 0; count = 0;
    for (int i=0; i<3; ++i) sum[i] = sumsq[i] = 0;
  }
  void reset() { *this = GyroBiasEstimator{}; }
  void update(const float gyro[3], const float accel[3], float dt) {
    if (calibrated) return;
    const float a = std::sqrt(accel[0]*accel[0]+accel[1]*accel[1]+accel[2]*accel[2]);
    if (!(std::fabs(a-1.0f)<0.06f) || dt <= 0 || dt > 0.10f) { clear_window(); return; }
    for (int i=0; i<3; ++i) {
      if (!std::isfinite(gyro[i]) || std::fabs(gyro[i]) > 4.0f) { clear_window(); return; }
    }
    for (int i=0; i<3; ++i) { sum[i] += gyro[i]; sumsq[i] += gyro[i]*gyro[i]; }
    ++count; seconds += dt;
    if (seconds < 0.60f || count < 15) return;
    for (int i=0; i<3; ++i) {
      const float mean = sum[i]/count;
      const float var = sumsq[i]/count-mean*mean;
      if (var > 0.25f) { clear_window(); return; }
    }
    for (int i=0; i<3; ++i) bias[i] = sum[i]/count;
    calibrated = true;
  }
};

struct MotionPipeline {
  MahonyFilter attitude;
  GyroBiasEstimator bias;
  // Absolute microseconds keep the 800 Hz clock independent of sample gaps and
  // avoid accumulating float rounding error. It naturally wraps at 4096 ticks.
  static uint16_t tick_at(int64_t us) {
    const uint64_t t = static_cast<uint64_t>(us);
    return static_cast<uint16_t>(((t / 1250u)) & 0x0fffu);
  }
  void update(const float gyro[3], const float accel[3], float dt) {
    bias.update(gyro, accel, dt);
    const float corrected[3] = {gyro[0]-bias.bias[0], gyro[1]-bias.bias[1], gyro[2]-bias.bias[2]};
    attitude.update(corrected, accel, dt);
  }
};
} // namespace s2s
