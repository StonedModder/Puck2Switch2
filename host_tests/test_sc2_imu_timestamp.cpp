// SDL's Triton layouts: report 0x45 uses a 32-bit microsecond timestamp at
// offset 30; 0x47 uses a 16-bit 32-us timestamp at offset 32.
#include "sc2_core.h"
#include <cassert>
#include <cstdio>
int main() {
  uint8_t a[46] = {};
  s2s::Sc2Input out{};
  a[0]=0x45; a[30]=0x78; a[31]=0x56; a[32]=0x34; a[33]=0x12;
  assert(s2s::parse_sc2(a,sizeof(a),out));
  assert(out.has_motion && out.imu_report_id==0x45 && out.imu_timestamp==0x12345678u);
  a[0]=0x47; a[32]=0xef; a[33]=0xbe;
  assert(s2s::parse_sc2(a,sizeof(a),out));
  assert(out.has_motion && out.imu_report_id==0x47 && out.imu_timestamp==0xbeefu);
  assert(s2s::parse_sc2(a,18,out));
  assert(!out.has_motion);
  std::puts("PASS SC2 timestamp offsets and short report");
}
