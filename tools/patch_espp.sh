#!/usr/bin/env bash
# Patch the vendored espp switch2_pro component for Puck2Switch2.
#
# Two independent patches, both idempotent. Re-run after re-cloning _refs/espp.
#
# 1) BOND SWEEP. Switch2Pro::configure_security() calls unpair_all() whenever
#    there is no Switch-2 console bond (reconnect_mode_ == false). That deletes
#    EVERY BLE bond in NimBLE's store, including the Steam Controller 2 bond our
#    central role depends on, so the controller had to be re-paired after every
#    reboot. Safe to skip: the Switch 2 never uses BLE SMP bonding (it pairs at
#    the application layer over 0x15), so there are no stale console bonds.
#
# 2) MOTION BLOCK. send_input_report() hard-codes buf[0x0e] = 0x28 (40-byte
#    motion) and Pro2InputReport has no way to supply a motion block. We emit the
#    hardware-validated 30-byte (0x1E) quaternion carrier instead, so the length
#    must be app-controllable and the payload must be settable.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="${ROOT}/_refs/espp/components/switch2_pro/src/switch2_pro.cpp"
HPP="${ROOT}/_refs/espp/components/switch2_pro/include/switch2_pro_report.hpp"

for f in "$CPP" "$HPP"; do
  [[ -f "$f" ]] || { echo "[patch_espp] missing $f — clone espp into _refs/ first" >&2; exit 1; }
done

python3 - "$CPP" "$HPP" <<'PY'
import sys

cpp, hpp = sys.argv[1], sys.argv[2]
fail = 0

# ---------------------------------------------------------------- patch 1
s = open(cpp).read()
old1 = "  if (!reconnect_mode_) {\n    size_t cleared = ble_gatt_server_.unpair_all().size();"
new1 = ("  // Puck2Switch2: do NOT wipe all BLE bonds here — the Steam Controller 2\n"
        "  // central keeps its bond in the same store, and the Switch 2 never uses\n"
        "  // SMP bonding (it pairs at the application layer over 0x15), so there is\n"
        "  // nothing stale for this sweep to clean up.\n"
        "  if (false && !reconnect_mode_) {\n    size_t cleared = ble_gatt_server_.unpair_all().size();")
if new1 in s:
    print("[patch_espp] 1/2 bond sweep: already applied")
elif old1 in s:
    s = s.replace(old1, new1, 1)
    print("[patch_espp] 1/2 bond sweep: disabled")
else:
    print("[patch_espp] 1/2 bond sweep: ANCHOR NOT FOUND", file=sys.stderr); fail = 1

# ---------------------------------------------------------------- patch 2a
old2 = "    buf[0x0e] = 0x28; // motion data length (40) — always present once IMU is enabled"
new2 = ("    // Puck2Switch2: respect an app-supplied motion length (0x1E = the 30-byte\n"
        "    // quaternion carrier this project emits); default to 0x28 as upstream did.\n"
        "    if (buf[0x0e] == 0) buf[0x0e] = 0x28;")
if new2 in s:
    print("[patch_espp] 2a motion length: already applied")
elif old2 in s:
    s = s.replace(old2, new2, 1)
    print("[patch_espp] 2a motion length: app-controllable")
else:
    print("[patch_espp] 2a motion length: ANCHOR NOT FOUND", file=sys.stderr); fail = 1

open(cpp, "w").write(s)

# ---------------------------------------------------------------- patch 2b
h = open(hpp).read()
anchor = "  const std::array<uint8_t, SIZE> &data() const { return data_; }"
addition = """  const std::array<uint8_t, SIZE> &data() const { return data_; }

  /// Puck2Switch2 addition: copy a raw motion block into the report.
  /// @param blk         motion payload bytes
  /// @param len         payload length (<= 0x28)
  /// @param motion_len  value for report byte 0x0E (e.g. 0x1E = 30-byte carrier)
  void set_motion_block(const uint8_t *blk, size_t len, uint8_t motion_len) {
    if (len > 0x28) len = 0x28;
    data_[0x0e] = motion_len;
    std::memcpy(&data_[0x0f], blk, len);
  }"""
if "set_motion_block" in h:
    print("[patch_espp] 2b set_motion_block: already applied")
elif anchor in h:
    h = h.replace(anchor, addition, 1)
    print("[patch_espp] 2b set_motion_block: added")
else:
    print("[patch_espp] 2b set_motion_block: ANCHOR NOT FOUND", file=sys.stderr); fail = 1

open(hpp, "w").write(h)
sys.exit(fail)
PY
