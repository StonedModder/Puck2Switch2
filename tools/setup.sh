#!/usr/bin/env bash
# Prepare the espp component tree this firmware builds against.
#
# espp is a pinned git submodule at _refs/espp. The build resolves components
# straight off disk (EXTRA_COMPONENT_DIRS with IDF_COMPONENT_MANAGER=0), so both
# the submodule AND the nested submodules it vendors must actually be populated
# on disk — a plain checkout leaves them empty and the build dies with
# "Failed to resolve component 'esp-nimble-cpp'".
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESPP="${ROOT}/_refs/espp"
PATCH="${ROOT}/tools/patches/0001-switch2_pro-puck2switch2.patch"

# Only what switch2_pro + ble_gatt_server pull in (nimble, CLI11 via cli, {fmt}
# via logger->format). espp has 16 nested submodules; the rest (lvgl, esp-dsp,
# littlefs, ...) are ~800 MB of dead weight for this project.
NESTED=(
  components/esp-nimble-cpp
  components/cli/detail/cli
  components/format/detail/fmt
)

echo "==> [1/4] espp submodule"
git -C "${ROOT}" submodule update --init _refs/espp

echo "==> [2/4] nested dependencies"
git -C "${ESPP}" submodule update --init "${NESTED[@]}"

echo "==> [3/4] verifying nested dependencies"
for p in "${NESTED[@]}"; do
  if [[ ! -f "${ESPP}/${p}/CMakeLists.txt" ]]; then
    echo "ERROR: ${p} is empty — submodule init did not populate it." >&2
    echo "       Try: git -C _refs/espp submodule update --init --recursive" >&2
    exit 1
  fi
done

echo "==> [4/4] local espp patches"
if git -C "${ESPP}" apply --reverse --check "${PATCH}" 2>/dev/null; then
  echo "    already applied"
else
  # Reset tracked files first so a half-applied or hand-edited tree cannot wedge
  # the build. _refs/espp is a build dependency: local edits belong in the patch.
  git -C "${ESPP}" checkout -- . 2>/dev/null || true
  git -C "${ESPP}" apply "${PATCH}"
  echo "    applied $(basename "${PATCH}")"
fi

echo "==> ready. build with: idf.py -C firmware/esp32s3 build"
