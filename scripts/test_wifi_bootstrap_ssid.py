# scripts/test_wifi_bootstrap_ssid.py
#
# Verifies WifiBootstrap::deriveApSsid() algorithm correctness by
# implementing the same pure function in Python and testing against
# known-good fixtures.
#
# The C++ implementation in WifiBootstrap.cpp is a direct translation
# of this algorithm; this test serves as the executable specification.
# (Requires g++ for a compiled harness; a Python-native equivalent is
# used here for portability on Windows where g++ may not be present.)
#
# Run with:  python scripts/test_wifi_bootstrap_ssid.py

import sys

AP_SSID_PREFIX = "Offband-Observer-"


def derive_ap_ssid(mac_bytes: bytes) -> str:
    """
    Python equivalent of WifiBootstrap::deriveApSsid().

    Format: "Offband-Observer-XXXXXX" where XXXXXX = last 6 hex chars
    of MAC (last 3 bytes, upper-case). Mirrors the C++ bit-shift logic:

        hex[i*2]     = H[(tail[i] >> 4) & 0x0F];
        hex[i*2 + 1] = H[tail[i] & 0x0F];

    where H = "0123456789ABCDEF" and tail = mac_bytes[3:6].
    """
    assert len(mac_bytes) == 6, "mac_bytes must be 6 bytes"
    H = "0123456789ABCDEF"
    tail = mac_bytes[3:6]
    hex_chars = ""
    for b in tail:
        hex_chars += H[(b >> 4) & 0x0F]
        hex_chars += H[b & 0x0F]
    return AP_SSID_PREFIX + hex_chars


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------
FIXTURES = [
    # (label, mac_bytes, expected_ssid)
    (
        "ST-P MAC (88:56:A6:96:0D:0C)",
        bytes([0x88, 0x56, 0xA6, 0x96, 0x0D, 0x0C]),
        "Offband-Observer-960D0C",
    ),
    (
        "all-zeros MAC",
        bytes([0x00, 0x00, 0x00, 0x00, 0x00, 0x00]),
        "Offband-Observer-000000",
    ),
    (
        "all-0xFF MAC",
        bytes([0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]),
        "Offband-Observer-FFFFFF",
    ),
    (
        "mixed-nibble MAC (AB:CD:EF:12:34:56)",
        bytes([0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56]),
        "Offband-Observer-123456",
    ),
]

failures = []
for label, mac, expected in FIXTURES:
    got = derive_ap_ssid(mac)
    if got == expected:
        print(f"OK: deriveApSsid produced '{got}' for {label}.")
    else:
        print(f"FAIL: expected '{expected}', got '{got}' for {label}.")
        failures.append(label)

if failures:
    print(f"\n{len(failures)} fixture(s) failed.")
    sys.exit(1)

sys.exit(0)
