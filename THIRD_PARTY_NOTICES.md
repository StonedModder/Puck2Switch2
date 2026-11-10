# Third-party notices

This project uses or adapts third-party software. The following list identifies
major direct dependencies and retains attribution; it is not a grant of rights
in the project as a whole.

- **PicoSwitch2** by notsosaelin and contributors: the Switch 2 motion-carrier
  encoder in `firmware/core/include/ns2_motion30.h` is adapted from
  `firmware/src/motion/ns2_ds5_motion.c` and `ns2_motion_pdu.c`.
  Apache License 2.0; source: https://github.com/notsosaelin/PicoSwitch2
- **espp** by esp-cpp and contributors, including `switch2_pro`: MIT License;
  source: https://github.com/esp-cpp/espp . The pinned espp commit and local
  modifications are recorded in `_refs/espp` and `tools/patches/`.
- **esp-nimble-cpp** by its contributors: Apache License 2.0; source:
  https://github.com/esp-cpp/esp-nimble-cpp . Its upstream NOTICE is preserved
  in the release third-party license bundle.
- **fmt** by Victor Zverovich and contributors: MIT License; source:
  https://github.com/fmtlib/fmt .
- **CLI** by Daniele Pallastrelli and contributors: Boost Software License
  1.0; source: https://github.com/daniele77/cli .
- **cJSON** by Dave Gamble and contributors: MIT License. Its license is
  retained at `firmware/components/cjson/LICENSE`.
- **ESP-IDF** by Espressif Systems and contributors: Apache License 2.0 and
  additional component-specific licenses; source:
  https://github.com/espressif/esp-idf . ESP-IDF's bundled components include
  software distributed under other terms. Their license and notice files from
  the build's component trees are included in the release license bundle.

See the release asset `Puck2Switch2-v1.1.1-third-party-licenses.zip` for the
license and notice texts collected from the actual build dependency trees.
The release notes identify the tested firmware binary and build configuration.
