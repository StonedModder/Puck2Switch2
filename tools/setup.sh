#!/usr/bin/env bash
# Fetch and prepare the espp component tree this firmware builds against.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESPP_TAG="v1.4.2"
ESPP_DIR="${ROOT}/_refs/espp"

if [[ ! -d "${ESPP_DIR}/.git" ]]; then
  echo "==> cloning espp ${ESPP_TAG}"
  mkdir -p "${ROOT}/_refs"
  git clone --depth 1 --branch "${ESPP_TAG}" https://github.com/esp-cpp/espp.git "${ESPP_DIR}"
else
  echo "==> espp already present"
fi

"${ROOT}/tools/patch_espp.sh"
echo "==> ready. build with: idf.py -C firmware/esp32s3 build"
