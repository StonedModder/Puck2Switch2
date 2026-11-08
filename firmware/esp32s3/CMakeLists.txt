cmake_minimum_required(VERSION 3.20)

set(ENV{IDF_COMPONENT_MANAGER} "0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# espp's switch2_pro + ble_gatt_server components (esp-nimble-cpp is vendored
# inside espp/components, so the build needs no network access), plus our own
# portable mapping core.
set(EXTRA_COMPONENT_DIRS
  "${CMAKE_CURRENT_LIST_DIR}/../../_refs/espp/components"
  "${CMAKE_CURRENT_LIST_DIR}/../core")

set(COMPONENTS "main esptool_py switch2_pro ble_gatt_server core")

project(puck2switch2)

set(CMAKE_CXX_STANDARD 23)
