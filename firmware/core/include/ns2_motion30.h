// ns2_motion30.h — Nintendo Switch 2 Pro Controller report-0x09 motion carrier
// (30-byte / length-0x1E form).
//
// WHY THIS FORM: the 40-byte (0x28) block is decoded by the community but has no
// console-validated *encoder* — the only published 0x28 generator was rejected on
// hardware. The 30-byte quaternion carrier is hardware-validated on a real Switch 2
// (confirmed in Splatoon 3) by two independent projects, so it is the production path.
//
// Ported from notsosaelin/PicoSwitch2 (Apache-2.0):
//   firmware/src/motion/ns2_ds5_motion.c   (encode_switch2_g0/g1/g2, pack_switch2_smallest_three)
//   firmware/src/motion/ns2_motion_pdu.c   (ns2_motion_pdu30_set_orientation)
//   firmware/src/motion/ns2_ds5_motion.c   (ns2_ds5_motion_build)
// Constants and the smallest-three chart law are reproduced from that source; the
// sensor-fusion filter here is our own (the reference integrates its own quaternion).
//
// The "Switch 1 int16 accel/gyro" model is a documented dead end for this block:
// the lane that looks like gyro is the fractional low half of a Q16.16 accel.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace s2s {

static constexpr size_t NS2_MOTION30_LEN = 30;
static constexpr uint32_t NS2_ORIENT_MASK = 0x03FFFFFFu; // 26 bits per slot
static constexpr float NS2_INV_SQRT2 = 0.70710678118654752440f;
// Q16.16 value per g: 4096 counts/g (ICM-42670 @ +/-8g) * 68963 (reference cal).
static constexpr float NS2_Q16_PER_G = 4096.0f * 68963.0f;
static constexpr float NS2_COMPONENT_EPS = 1e-6f;

// Encode one quaternion component into its slot. All three slots share the
// +/-1/sqrt(2) physical range but have unequal numeric widths.
static inline uint32_t ns2_encode_g0(float v) {
  const float s = (v * NS2_INV_SQRT2 + 0.5f) * 67108864.0f;
  if (!(s > 0.0f)) return 0;
  if (s >= 67108863.0f) return 0x03FFFFFFu;
  return (uint32_t)(s + 0.5f);
}
static inline uint32_t ns2_encode_g1(float v) {
  const float s = (v * NS2_INV_SQRT2 + 0.5f) * 33554432.0f;
  if (!(s > 0.0f)) return 0;
  if (s >= 33554431.0f) return 0x01FFFFFFu;
  return (uint32_t)(s + 0.5f);
}
static inline uint32_t ns2_encode_g2(float v) {
  const float s = (v * NS2_INV_SQRT2 + 0.5f) * 16777216.0f;
  if (!(s > 0.0f)) return 0;
  if (s >= 16777215.0f) return 0x00FFFFFFu;
  return (uint32_t)(s + 0.5f);
}

// Pack a w/x/y/z quaternion into the three orientation slots.
// Chart law (grounded in genuine Pro Controller 2 captures): state k omits one
// component and carries the other three cyclically, with the omitted component
// canonicalised positive so the decoder can take the positive square root.
struct Ns2MotionEncoder {
  uint8_t omitted = 0;
  bool ok = true;

  void pack_orientation(float qw, float qx, float qy, float qz, uint32_t out[3]) {
    const float wire[4] = {qw, qx, qy, qz};
    unsigned om = omitted & 3u;

    // If any transmitted component has left +/-1/sqrt(2), re-select the chart:
    // dropping the largest component restores the smallest-three invariant.
    bool boundary = false;
    for (unsigned i = 0; i < 4; ++i)
      if (i != om && std::fabs(wire[i]) > NS2_INV_SQRT2) { boundary = true; break; }
    if (boundary) {
      om = 0;
      for (unsigned i = 1; i < 4; ++i)
        if (std::fabs(wire[i]) > std::fabs(wire[om])) om = i;
      omitted = (uint8_t)om;
    }

    const float sign = wire[om] < 0.0f ? -1.0f : 1.0f;
    const float c0 = wire[(om + 1u) & 3u] * sign;
    const float c1 = wire[(om + 2u) & 3u] * sign;
    const float c2 = wire[(om + 3u) & 3u] * sign;

    const float lim = NS2_INV_SQRT2 + NS2_COMPONENT_EPS;
    if (!std::isfinite(c0) || !std::isfinite(c1) || !std::isfinite(c2) || c0 < -lim ||
        c0 > lim || c1 < -lim || c1 > lim || c2 < -lim || c2 > lim) {
      ok = false;
      out[0] = out[1] = out[2] = 0;
      return;
    }
    ok = true;
    out[0] = ns2_encode_g0(c0);
    out[1] = ns2_encode_g1(c1);
    out[2] = ((uint32_t)om << 24) | ns2_encode_g2(c2);
  }

  // Build the full 30-byte carrier.
  //  tick800        : free-running 800 Hz tick (low 12 bits used)
  //  elapsed_ticks  : ticks since the previous report (high 4 bits)
  //  accel_g        : accelerometer in g (x,y,z)
  void build(uint8_t out[NS2_MOTION30_LEN], const float accel_g[3], uint16_t tick800,
             uint8_t elapsed_ticks, const float q[4]) {
    std::memset(out, 0, NS2_MOTION30_LEN);

    const uint16_t timing = (uint16_t)((tick800 & 0x0FFFu) | ((elapsed_ticks & 0x0Fu) << 12));
    out[0] = (uint8_t)(timing & 0xFF);
    out[1] = (uint8_t)(timing >> 8);
    out[2] = 0x00;
    out[3] = 0x0C; // nominal temperature 0x0C00

    uint32_t o[3];
    pack_orientation(q[0], q[1], q[2], q[3], o); // w,x,y,z

    const uint32_t g0 = o[0] & NS2_ORIENT_MASK;
    const uint32_t g1 = o[1] & NS2_ORIENT_MASK;
    const uint32_t g2 = o[2] & NS2_ORIENT_MASK;

    out[5] = (uint8_t)g0;
    out[6] = (uint8_t)(g0 >> 8);
    out[7] = (uint8_t)(g0 >> 16);
    out[8] = (uint8_t)((out[8] & 0xFCu) | ((g0 >> 24) & 0x03u));

    out[9] = (uint8_t)g1;
    out[10] = (uint8_t)(g1 >> 8);
    out[11] = (uint8_t)(g1 >> 16);
    out[12] = (uint8_t)((out[12] & 0xFCu) | ((g1 >> 24) & 0x03u));

    out[13] = (uint8_t)g2;
    out[14] = (uint8_t)(g2 >> 8);
    out[15] = (uint8_t)(g2 >> 16);
    out[4] = (uint8_t)((out[4] & 0xFCu) | ((g2 >> 24) & 0x03u));

    for (unsigned axis = 0; axis < 3; ++axis) {
      float v = accel_g[axis] * NS2_Q16_PER_G;
      if (v > 2147483647.0f) v = 2147483647.0f;
      if (v < -2147483648.0f) v = -2147483648.0f;
      const int32_t iv = (int32_t)v;
      out[16 + axis * 4 + 0] = (uint8_t)(iv & 0xFF);
      out[16 + axis * 4 + 1] = (uint8_t)((iv >> 8) & 0xFF);
      out[16 + axis * 4 + 2] = (uint8_t)((iv >> 16) & 0xFF);
      out[16 + axis * 4 + 3] = (uint8_t)((iv >> 24) & 0xFF);
    }

    out[28] = 0x00;
    out[29] = 0x02;
  }
};

// ---------------------------------------------------------------------------
// Mahony complementary attitude filter: gyro integration corrected by gravity.
// The SC2 is a real inertial sensor (verified: |accel| = 1.005 g at rest), and
// this block carries ORIENTATION, so raw gyro must be fused into a quaternion.
// ---------------------------------------------------------------------------
struct MahonyFilter {
  float q[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // w,x,y,z
  // Gyro-leaning gains. The accelerometer is only a gravity reference, and a
  // fast swing loads it with linear acceleration, so a high proportional gain
  // makes the correction fight the gyro and the orientation visibly lags behind
  // (then snaps back when the motion stops). Low kp trusts the gyro through the
  // fast part of a swing; the small integral term still kills drift at rest.
  float kp = 0.5f;
  float ki = 0.01f;
  float integral[3] = {0, 0, 0};

  void reset() {
    q[0] = 0; q[1] = 0; q[2] = 0; q[3] = 1;
    integral[0] = integral[1] = integral[2] = 0;
  }

  // gyro_dps in degrees/second, accel_g in g, dt in seconds
  void update(const float gyro_dps[3], const float accel_g[3], float dt) {
    if (dt <= 0.0f || dt > 0.5f) return;

    const float gx = gyro_dps[0] * 0.017453292519943295f; // deg -> rad
    const float gy = gyro_dps[1] * 0.017453292519943295f;
    const float gz = gyro_dps[2] * 0.017453292519943295f;

    float ax = accel_g[0], ay = accel_g[1], az = accel_g[2];
    const float an = std::sqrt(ax * ax + ay * ay + az * az);
    // Magnitude alone cannot detect all linear acceleration, but suppressing
    // implausible gravity observations prevents strong dynamic corrections.
    const float deviation = std::fabs(an - 1.0f);
    float trust = (0.20f - deviation) / 0.12f;
    if (trust < 0.0f) trust = 0.0f;
    if (trust > 1.0f) trust = 1.0f;
    if (an > 0.01f) { ax /= an; ay /= an; az /= an; }
    else { ax = ay = az = 0.0f; trust = 0.0f; }

    // Estimated gravity direction from the current attitude.
    const float qw = q[0], qx = q[1], qy = q[2], qz = q[3];
    const float vx = 2.0f * (qx * qz - qw * qy);
    const float vy = 2.0f * (qw * qx + qy * qz);
    const float vz = qw * qw - qx * qx - qy * qy + qz * qz;

    // Error = measured x estimated gravity.
    const float ex = trust * (ay * vz - az * vy);
    const float ey = trust * (az * vx - ax * vz);
    const float ez = trust * (ax * vy - ay * vx);

    if (ki > 0.0f) {
      integral[0] += ki * ex * dt;
      integral[1] += ki * ey * dt;
      integral[2] += ki * ez * dt;
    }

    const float wx = gx + kp * ex + integral[0];
    const float wy = gy + kp * ey + integral[1];
    const float wz = gz + kp * ez + integral[2];

    // Integrate quaternion: q += 0.5 * q * omega * dt
    const float h = 0.5f * dt;
    const float nw = qw + h * (-qx * wx - qy * wy - qz * wz);
    const float nx = qx + h * (qw * wx + qy * wz - qz * wy);
    const float ny = qy + h * (qw * wy - qx * wz + qz * wx);
    const float nz = qz + h * (qw * wz + qx * wy - qy * wx);

    const float n = std::sqrt(nw * nw + nx * nx + ny * ny + nz * nz);
    if (n > 0.0f) {
      q[0] = nw / n; q[1] = nx / n; q[2] = ny / n; q[3] = nz / n;
    }
  }
};

} // namespace s2s
