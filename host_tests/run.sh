#!/usr/bin/env bash
# Build and run the host-side tests. No ESP-IDF needed: everything under test is
# header-only portable C++ (the report encoder, the SC2->Switch2 mapper, the
# macro core). Run from anywhere.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
out="${TMPDIR:-/tmp}/p2s2_host_tests"
mkdir -p "$out"

inc=(-I "$root/_refs/espp/components/switch2_pro/include"
     -I "$root/firmware/core/include"
     -I "$root/firmware/esp32s3/main")

fail=0
for src in "$here"/test_*.cpp; do
  name="$(basename "$src" .cpp)"
  echo "=== $name ==="
  g++ -std=c++17 "${inc[@]}" -O2 -Wall -o "$out/$name" "$src"
  if "$out/$name"; then :; else
    echo "!! $name FAILED"
    fail=1
  fi
  echo
done

if [ "$fail" -ne 0 ]; then
  echo "HOST TESTS: FAIL"
  exit 1
fi
echo "HOST TESTS: PASS"
