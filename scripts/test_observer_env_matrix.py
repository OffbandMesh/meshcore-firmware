# scripts/test_observer_env_matrix.py
#
# Host-runnable test asserting Plan-1 companion-observer env matrix is
# correctly declared across the three Plan-1 hardware variants. Run with:
#
#   python scripts/test_observer_env_matrix.py
#
# Exit 0 on all envs present + correctly flagged; nonzero with a clear
# failure message otherwise.
#
# No external deps -- uses configparser from stdlib.
#
# Plan 1 is companion-only. Repeater observer envs are deferred to a
# future plan; the EXPECTED list extends naturally when they land.

import configparser
import sys
from pathlib import Path

# 3 companion envs Plan 1 introduces.
EXPECTED = [
    ("variants/heltec_v3/platformio.ini",
     ["env:Heltec_v3_companion_observer_wifi"]),
    ("variants/heltec_v4/platformio.ini",
     ["env:heltec_v4_companion_observer_wifi"]),
    ("variants/xiao_s3_wio/platformio.ini",
     ["env:Xiao_S3_WIO_companion_observer_wifi"]),
]

# Every Plan-1 env MUST set both flags (Plan 1 = companion-only,
# so OFFBAND_OBSERVER_BLE_COMPANION is always on). When a repeater
# variant lands later, only OFFBAND_OBSERVER will be required and
# this assertion splits.
REQUIRED_FLAGS_ALL = [
    "-D OFFBAND_OBSERVER",
    "-D OFFBAND_OBSERVER_BLE_COMPANION",
]
REQUIRED_SRC_FILTER = "+<helpers/wifi_observer/*.cpp>"

# Phase-1 strip (#42): observer envs override the inherited
# companion defaults down to observer minima to free static .bss. The strings
# below must match the platformio.ini byte-for-byte -- MAX_GROUP_CHANNELS uses
# the no-space second-D form; MAX_CONTACTS / OFFLINE_QUEUE_SIZE use the spaced
# form (matching the _ble base flags they override).
REQUIRED_STRIP_FLAGS = [
    "-D MAX_CONTACTS=50",
    "-DMAX_GROUP_CHANNELS=11",
    "-D OFFLINE_QUEUE_SIZE=16",
]

failures = []

for ini_path, env_names in EXPECTED:
    p = Path(ini_path)
    if not p.exists():
        failures.append(f"MISSING FILE: {ini_path}")
        continue
    cp = configparser.ConfigParser(strict=False, interpolation=None)
    cp.read(p, encoding="utf-8")
    for env in env_names:
        if env not in cp.sections():
            failures.append(f"{ini_path}: missing section [{env}]")
            continue
        flags = cp[env].get("build_flags", "")
        src_filter = cp[env].get("build_src_filter", "")
        for flag in REQUIRED_FLAGS_ALL:
            if flag not in flags:
                failures.append(
                    f"{ini_path} [{env}]: missing build_flag '{flag}'")
        for flag in REQUIRED_STRIP_FLAGS:
            if flag not in flags:
                failures.append(
                    f"{ini_path} [{env}]: missing strip flag '{flag}' (#42)")
        if REQUIRED_SRC_FILTER not in src_filter:
            failures.append(
                f"{ini_path} [{env}]: build_src_filter missing "
                f"'{REQUIRED_SRC_FILTER}'")

if failures:
    print("FAIL: observer env matrix has issues:")
    for f in failures:
        print(f"  - {f}")
    sys.exit(1)

print(f"OK: {sum(len(envs) for _, envs in EXPECTED)} observer envs "
      f"present and correctly flagged.")
sys.exit(0)
