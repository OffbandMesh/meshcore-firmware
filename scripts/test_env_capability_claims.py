# scripts/test_env_capability_claims.py
#
# Host-runnable tests for check_env_capability_claims.py (Offband #649). Run:
#
#   python scripts/test_env_capability_claims.py
#
# Exit 0 on pass, nonzero with a clear failure message otherwise.
# No external deps -- stdlib only, matching the other scripts/test_*.py.
#
# These are deliberately NEGATIVE tests. A checker exercised only against a
# clean tree proves nothing about what it CATCHES -- only that it stays quiet.
# Every case below is either a mismatch the tool must fail on, or a real trap
# the first implementation fell into and was corrected for.

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_env_capability_claims import (  # noqa: E402
    CAPABILITIES,
    MISSING,
    OK,
    UNCLAIMED,
    UNEXPECTED,
    UNSUPPORTED,
    classify,
    defines,
    name_claim,
)

BLE = CAPABILITIES[0]

ESP32 = "platformio/espressif32@6.11.0"
NRF52 = "nordicnrf52"
NATIVE = "native"

# Resolved build_flags fragments. PlatformIO strips `;` comments during
# resolution, so a commented-out flag simply is not here -- verified against
# Heltec_t114_companion_radio_usb.
F_BLE = "-D MAX_CONTACTS=350 -D BLE_PIN_CODE=123456 -D OFFLINE_QUEUE_SIZE=256"
F_WIFI = "-D MAX_CONTACTS=350 -D WIFI_SSID='\"myssid\"' -D WIFI_PWD='\"mypwd\"'"
F_BOTH = F_BLE + " " + F_WIFI
F_PLAIN = "-D MAX_CONTACTS=350 -D ENABLE_USB_INTERFACE"

# nRF52 companion envs pull the whole helpers dir in with a greedy wildcard, so
# the BLE interface is COMPILED even when BLE_PIN_CODE is absent.
NRF_WILDCARD = "+<*> +<helpers/nrf52/*.cpp> +<../examples/companion_radio/*.cpp>"

failures = []


def check(label, got, want):
    if got != want:
        failures.append("%s\n    expected: %s\n    got:      %s" % (label, want, got))


def verdict(env, platform, flags, src="", deps=""):
    return classify(env, platform, flags, src, deps, BLE)["verdict"]


# --------------------------------------------------------------------------
# defines() -- word anchoring
# --------------------------------------------------------------------------

check("BLE_PIN_CODE is detected", defines(F_BLE, "BLE_PIN_CODE"), True)
check("absent macro is not detected", defines(F_PLAIN, "BLE_PIN_CODE"), False)
check("a longer macro must not match",
      defines("-D NO_BLE_PIN_CODE=1", "BLE_PIN_CODE"), False)
check("a prefixed macro must not match",
      defines("-D BLE_PIN_CODE_EXTRA=1", "BLE_PIN_CODE"), False)

# --------------------------------------------------------------------------
# The claim parser
# --------------------------------------------------------------------------

# THE trap that broke the first implementation. `heltec_v4_companion_observer_wifi`
# is a BLE companion that ships observer telemetry over WiFi -- it genuinely
# defines BLE_PIN_CODE. Anchoring on the trailing token alone read it as
# "claims no BLE" and reported all four observer envs as violations.
check("observer_wifi makes no claim about the companion transport",
      name_claim("heltec_v4_companion_observer_wifi", BLE), None)
check("observer_wifi is therefore not failed",
      verdict("heltec_v4_companion_observer_wifi", ESP32, F_BLE), UNCLAIMED)

# "nibble_zero_connect" contains the letters "ble"; a substring test reads every
# env on that board as claiming BLE.
check("nibble wifi env must not be read as claiming BLE",
      name_claim("nibble_zero_connect_companion_radio_wifi_", BLE), False)
check("nibble ble env does claim BLE",
      name_claim("nibble_zero_connect_companion_radio_ble_", BLE), True)

check("trailing underscore still claims BLE",
      name_claim("Xiao_C6_companion_radio_ble_", BLE), True)
check("ethernet transport claims no BLE",
      name_claim("ThinkNode_M7_companion_radio_ethernet", BLE), False)
check("repeater makes no claim", name_claim("heltec_v4_repeater", BLE), None)
check("room_server makes no claim",
      name_claim("Heltec_t096_room_server", BLE), None)
check("kiss_modem makes no claim",
      name_claim("ThinkNode_M7_kiss_modem", BLE), None)

# --------------------------------------------------------------------------
# ESP32 -- WIFI_SSID wins the #if chain
# --------------------------------------------------------------------------

check("ESP32 ble env with BLE_PIN_CODE is OK",
      verdict("heltec_v4_companion_radio_ble", ESP32, F_BLE), OK)

# The case #199 cannot see: links cleanly, ships with no BLE.
check("ESP32 ble env with no BLE_PIN_CODE is MISSING",
      verdict("heltec_v4_companion_radio_ble", ESP32, F_PLAIN), MISSING)

# Subtle: BLE_PIN_CODE is present, but WIFI_SSID beats it in the #if chain, so
# the firmware is a WiFi companion despite the name and the flag.
check("ESP32 ble env where WIFI_SSID overrides BLE is MISSING",
      verdict("heltec_v4_companion_radio_ble", ESP32, F_BOTH), MISSING)

check("ESP32 wifi env with WIFI_SSID is OK",
      verdict("heltec_v4_companion_radio_wifi", ESP32, F_WIFI), OK)
check("ESP32 usb env with no BLE flags is OK",
      verdict("Xiao_S3_WIO_companion_radio_usb", ESP32, F_PLAIN), OK)
check("ESP32 usb env that defines BLE_PIN_CODE is UNEXPECTED",
      verdict("Xiao_S3_WIO_companion_radio_usb", ESP32, F_BLE), UNEXPECTED)

# --------------------------------------------------------------------------
# nRF52 -- compiling the interface is NOT the capability
# --------------------------------------------------------------------------

check("nRF52 ble env with BLE_PIN_CODE is OK",
      verdict("RAK_4631_companion_radio_ble", NRF52, F_BLE, NRF_WILDCARD), OK)

# The false positive that the source-filter detector produced on 13 real envs:
# the wildcard compiles helpers/nrf52/SerialBLEInterface.cpp, but BLE_PIN_CODE
# is absent so main.cpp never instantiates it. Compiled != enabled.
check("nRF52 usb env compiling the BLE interface without BLE_PIN_CODE is OK",
      verdict("Heltec_t114_companion_radio_usb", NRF52, F_PLAIN, NRF_WILDCARD), OK)

check("nRF52 usb env that defines BLE_PIN_CODE is UNEXPECTED",
      verdict("Heltec_t114_companion_radio_usb", NRF52, F_BLE, NRF_WILDCARD),
      UNEXPECTED)

# WIFI_SSID has no meaning on nRF52 -- it must NOT suppress BLE there.
check("nRF52 BLE is not suppressed by a stray WIFI_SSID",
      verdict("RAK_4631_companion_radio_ble", NRF52, F_BOTH, NRF_WILDCARD), OK)

# --------------------------------------------------------------------------
# Nothing to say
# --------------------------------------------------------------------------

check("unclaimed role is reported, not failed",
      verdict("heltec_v4_repeater", ESP32, F_PLAIN), UNCLAIMED)
check("platform with no detector is reported, not failed",
      verdict("native_kiss_modem", NATIVE, ""), UNSUPPORTED)

# --------------------------------------------------------------------------

if failures:
    print("FAIL: %d assertion(s)\n" % len(failures))
    for f in failures:
        print("  %s\n" % f)
    sys.exit(1)

print("OK: %d env capability-claim checks pass (negative cases included)."
      % 24)
sys.exit(0)
