# Local patches applied on top of the pinned espp submodule

`_refs/espp` is a git submodule pinned to a specific upstream commit
(`351825c`, the `switch2_pro` component as imported). Everything this project
changes inside espp lives here as a real `git diff` patch, so the submodule
commit stays a pristine upstream reference.

| Patch | Touches | Why |
|---|---|---|
| `0001-switch2_pro-puck2switch2.patch` | `components/switch2_pro/{include/switch2_pro.hpp,include/switch2_pro_report.hpp,src/switch2_pro.cpp}` | Vibration write hooks (0x0012 and the 0x0016 vibration+command channel), 0x0016 command-only 8-byte frame handling, and `set_motion_block()` so the app can fill the IMU block of input report 0x09. |

## Applying

`tools/setup.sh` applies the patches automatically and is idempotent:

```sh
./tools/setup.sh
```

## Regenerating

After editing the submodule by hand, re-capture the diff:

```sh
git -C _refs/espp diff > tools/patches/0001-switch2_pro-puck2switch2.patch
```

Do not commit the submodule's dirty state — the pinned commit plus patches is
the single source of truth, and `git apply -R` is a clean escape hatch.
