# assets/drivers/ — kernel-matched backend files

The Driver section in the app (DriverFragment / LaunchMenuActivity) picks
one of 11 fixed backend **modes** (see `DriverInfo.wanbaiModes()` / native
`jni/src/wanbai.h`): `auto`, `qx1`, `gt`, `rt-dev`, `rt-proc`, `qx2`,
`tb-kpm`, `ditpro`, `kma`, `ldk`, `paradise`.

Selecting a mode always writes it to
`/data/adb/modules/invisiblex_loader/config/config.json` (`"driver"` key),
which is enough on its own for `auto`, `qx1`, `gt`, `rt-dev`, `rt-proc` and
`qx2` — those probe existing `/dev`/`/proc` nodes and need nothing extra.

The KPM-style modes (`tb-kpm`, `ditpro`, `kma`, `ldk`, `paradise`) need an
actual kernel module inserted first, and that module **must match the
device's running kernel version**. `DriverConfig.loadMatchingAsset()`
looks in this folder for a file named:

```
<mode>_<kernel-major.minor>.<ext>      e.g. paradise_6.6.ko, kma_5.15.ko
```

and falls back to a kernel-agnostic file if present:

```
<mode>.<ext>                           e.g. paradise.ko
```

`<ext>` can be:
- `.ko` — a real kernel module; the loader tries `insmod` first and falls
  back to direct execute if that fails.
- `.sh` — a self-extracting loader script (embeds a base64 `.ko` and runs
  `insmod` on it itself, e.g. the bundled Paradise driver packages
  `paradise_5.10.sh` / `paradise_5.15.sh` / `paradise_6.1.sh` /
  `paradise_6.6.sh` / `paradise_6.12.sh`). The loader runs these with
  `sh`, never `insmod`s the script file itself.
- anything else — treated as a plain executable.

Example: if the device reports kernel `6.6` and the user picks
**Paradise**, the loader looks for `paradise_6.6.ko` first, then
`paradise.ko`. If neither exists, driver selection still works via
config.json alone — a missing bundled module is reported as informational,
not an error.

Drop real compiled files in here using this naming scheme to wire them up;
nothing else needs to change.
