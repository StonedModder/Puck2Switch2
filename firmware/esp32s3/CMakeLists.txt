cmake_minimum_required(VERSION 3.20)

set(ENV{IDF_COMPONENT_MANAGER} "0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# espp's switch2_pro + ble_gatt_server components (esp-nimble-cpp is vendored
# inside espp/components, so the build needs no network access), plus our own
# portable mapping core.
set(EXTRA_COMPONENT_DIRS
  "${CMAKE_CURRENT_LIST_DIR}/../../_refs/espp/components"
  "${CMAKE_CURRENT_LIST_DIR}/../components"
  "${CMAKE_CURRENT_LIST_DIR}/../core")

set(COMPONENTS "main esptool_py switch2_pro ble_gatt_server core cjson")

# The build resolves dependencies straight off disk (IDF_COMPONENT_MANAGER=0), so
# the espp submodule and the nested submodules it vendors must be checked out.
# Fail here with an actionable message instead of a cryptic "Failed to resolve
# component 'esp-nimble-cpp'" three minutes later.
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../_refs/espp/components/switch2_pro/CMakeLists.txt")
  message(FATAL_ERROR
    "espp is missing at _refs/espp.\n"
    "Run ./tools/setup.sh from the repo root to init the submodule, its nested "
    "dependencies, and the local switch2_pro patches.")
endif()

project(puck2switch2)

set(CMAKE_CXX_STANDARD 23)
