"""Host tests for the #273/#468 bridge-identity logic in pio-flash.py.

Run standalone:  python scripts/test_pio_flash_bridge_identity.py
No hardware, no PowerShell -- exercises the pure matcher/normalisation layer
(find_in_registry, entry_usb_serials, norm_serial, bridge_chip, and the
default-serial neutralisation rule) the same way the wrapper composes them.
"""
import importlib.util
import sys
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "pio_flash", Path(__file__).parent / "pio-flash.py"
)
pf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pf)

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"  PASS {name}")
    else:
        print(f"  FAIL {name} {detail}")
        FAILURES.append(name)


# --- bridge_chip -----------------------------------------------------------
check("cp2102 is bridge", pf.bridge_chip("10C4:EA60") == "CP2102")
check("ch340 is bridge", pf.bridge_chip("1A86:7522") == "CH340")
check("esp32s3 native not bridge", pf.bridge_chip("303A:0002") is None)
check("nrf52 not bridge", pf.bridge_chip("239A:8029") is None)
check("empty vid_pid safe", pf.bridge_chip("") is None)

# --- default-serial neutralisation (#468 guard 1) --------------------------
# A recorded factory default must never act as Tier-1 identity.
entry_default = {"usb_serial": "0001", "vid_pid": ["10C4:EA60"]}
check("default serial filtered from entry", pf.entry_usb_serials(entry_default) == [])

entry_real = {"usb_serial": "3C0F02EBF90C", "vid_pid": ["10C4:EA60"]}
check("real serial kept", pf.entry_usb_serials(entry_real) == ["3C0F02EBF90C"])

# norm_serial folds separators/case, so a colon-form MAC equals its bare form.
check(
    "norm folds mac forms",
    pf.norm_serial("3c:0f:02:eb:f9:0c") == pf.norm_serial("3C0F02EBF90C"),
)

# --- find_in_registry: default serial cannot Tier-1 match ------------------
registry = {
    "devices": {
        "cp2102-a": {"usb_serial": "0001", "vid_pid": ["10C4:EA60"], "mac": "aa:bb:cc:dd:ee:01"},
        "cp2102-b": {"vid_pid": ["10C4:EA60"], "mac": "aa:bb:cc:dd:ee:02"},
        "real-serial-board": {"usb_serial": "F85B1BA61450", "vid_pid": ["303A:0002"]},
    },
    "foreign_devices": {},
}
# A port presenting the default serial must not match cp2102-a by serial.
kind, name, _ = pf.find_in_registry(registry, "10C4:EA60", "", "0001")
check("port with default serial does not Tier-1 match", (kind, name) == (None, None),
      f"got {(kind, name)}")

# Real serial still matches its entry.
kind, name, _ = pf.find_in_registry(registry, "303A:0002", "", "F85B1BA61450")
check("real serial Tier-1 matches", (kind, name) == ("device", "real-serial-board"),
      f"got {(kind, name)}")

# Tier-3 discipline unchanged: class-only entries never match passively.
kind, name, _ = pf.find_in_registry(registry, "10C4:EA60", "8&DEADBEEF", "")
check("bridge class never passive-matches", (kind, name) == (None, None),
      f"got {(kind, name)}")

# --- resolve_device provisional path (#273/#468) ---------------------------
# Monkeypatch enumeration; capture refusals as exceptions instead of sys.exit.
class Refusal(Exception):
    pass


def fake_refuse(msg, *, exit_code=1):
    raise Refusal(msg)


pf.refuse = fake_refuse
pf.err = lambda msg: None  # silence warnings for the test run

BRIDGE_PORT = {
    "com": "COM6", "vid_pid": "10C4:EA60", "instance_hash": "",
    "deviceid_full": "USB\\VID_10C4&PID_EA60\\0001", "usb_serial": "",
    "description": "CP210x",
}


def with_ports(ports):
    pf.enumerate_ports = lambda: ports


reg1 = {
    "devices": {
        "hv3": {"vid_pid": ["10C4:EA60"], "mac": "3c:0f:02:eb:f9:0c",
                 "discriminators": {"windows": {}}},
    },
    "foreign_devices": {},
}

with_ports([BRIDGE_PORT])
port, entry = pf.resolve_device("hv3", reg1)
check("sole bridge candidate resolves provisionally",
      port.get("bridge_provisional") is True and port["com"] == "COM6")

# Two bridge ports present -> refuse (can't tell apart).
with_ports([BRIDGE_PORT, dict(BRIDGE_PORT, com="COM9")])
try:
    pf.resolve_device("hv3", reg1)
    check("two bridge ports refuse", False, "no refusal raised")
except Refusal as e:
    check("two bridge ports refuse", "2 ports" in str(e) or "cannot be told apart" in str(e)
          or "bridge-class" in str(e))

# Rival serial-less bridge entry -> refuse (registry ambiguity).
reg2 = {
    "devices": {
        "hv3": {"vid_pid": ["10C4:EA60"], "mac": "3c:0f:02:eb:f9:0c",
                 "discriminators": {"windows": {}}},
        "hv3-other": {"vid_pid": ["10C4:EA60"], "mac": "aa:bb:cc:dd:ee:ff",
                       "discriminators": {"windows": {}}},
    },
    "foreign_devices": {},
}
with_ports([BRIDGE_PORT])
try:
    pf.resolve_device("hv3", reg2)
    check("rival bridge entry refuses", False, "no refusal raised")
except Refusal as e:
    check("rival bridge entry refuses", "hv3-other" in str(e))

# Bridge entry without mac: -> falls through to the #323 refusal (no guess).
reg3 = {
    "devices": {
        "hv3": {"vid_pid": ["10C4:EA60"], "discriminators": {"windows": {}}},
    },
    "foreign_devices": {},
}
with_ports([BRIDGE_PORT])
try:
    pf.resolve_device("hv3", reg3)
    check("bridge without mac refuses", False, "no refusal raised")
except Refusal as e:
    check("bridge without mac refuses", "neither usb_serial nor a DeviceID hash" in str(e))

# Non-bridge entry with no discriminators -> unchanged #323 refusal.
reg4 = {
    "devices": {
        "s3-board": {"vid_pid": ["303A:0002"], "mac": "11:22:33:44:55:66",
                      "discriminators": {"windows": {}}},
    },
    "foreign_devices": {},
}
with_ports([dict(BRIDGE_PORT, vid_pid="303A:0002")])
try:
    pf.resolve_device("s3-board", reg4)
    check("native-USB class never provisional", False, "no refusal raised")
except Refusal as e:
    check("native-USB class never provisional",
          "neither usb_serial nor a DeviceID hash" in str(e))

print()
if FAILURES:
    print(f"{len(FAILURES)} FAILURE(S): {FAILURES}")
    sys.exit(1)
print("ALL PASS")

# --- #503: serial-first, global, no VID:PID precondition ------------------
reg503 = {
    "devices": {
        "t1000e": {"usb_serial": "ACEDFAE4BC94D153", "vid_pid": ["239A:8029"], "mac": None},
        "legacy-hash-only": {"vid_pid": ["303A:0002"], "mac": "aa:bb:cc:00:11:22",
                              "discriminators": {"windows": {"runtime_deviceid_instance": "8&AAAA"}}},
    },
    "foreign_devices": {
        "desk-unit": {"usb_serial": "F0F0F0F0F0F0", "vid_pid": ["1A86:7522"]},
    },
}
# 1. Cross-VID serial match: board flips to its app identity (2886:0057) --
#    entry lists only 239A:8029. Serial must still win, VID:PID irrelevant.
kind, name, _ = pf.find_in_registry(reg503, "2886:0057", "", "ACEDFAE4BC94D153")
check("503: serial matches across VID:PID change", (kind, name) == ("device", "t1000e"),
      f"got {(kind, name)}")
# 2. Foreign matched by serial cross-VID still returns foreign (callers refuse).
kind, name, _ = pf.find_in_registry(reg503, "303A:1001", "", "F0F0F0F0F0F0")
check("503: foreign refusal survives serial-first", (kind, name) == ("foreign", "desk-unit"),
      f"got {(kind, name)}")
# 3. A serial-bearing port that matches nothing NEVER falls to class/hash match.
kind, name, _ = pf.find_in_registry(reg503, "303A:0002", "8&AAAA", "DEADBEEF0001AA")
check("503: unknown serial never class-matches", (kind, name) == (None, None),
      f"got {(kind, name)}")
# 4. Serial-less port still hash-matches the legacy entry (bridge-class fallback).
kind, name, _ = pf.find_in_registry(reg503, "303A:0002", "8&AAAA", "")
check("503: serial-less hash fallback intact", (kind, name) == ("device", "legacy-hash-only"),
      f"got {(kind, name)}")
# 5. #323 preserved: entry with serial never matches a port with different serial.
kind, name, _ = pf.find_in_registry(reg503, "239A:8029", "", "1111222233334444")
check("503: no impersonation across serials", (kind, name) == (None, None),
      f"got {(kind, name)}")

# 6. resolve_device: legacy-hash entry vs serial-bearing port -> refuse without
#    approval; matches with CLASS_MATCH_APPROVED.
S3_PORT = {"com": "COM8", "vid_pid": "303A:0002", "instance_hash": "8&AAAA",
           "deviceid_full": "USB\VID_303A&PID_0002&MI_00\8&AAAA&0&0000",
           "usb_serial": "F85B1BA61450", "description": "USB Serial Device"}
reg_legacy = {"devices": {"legacy-hash-only": reg503["devices"]["legacy-hash-only"]},
              "foreign_devices": {}}
with_ports([S3_PORT])
pf.CLASS_MATCH_APPROVED = False
try:
    pf.resolve_device("legacy-hash-only", reg_legacy)
    check("503: class-decided match refused w/o approval", False, "no refusal")
except Refusal as e:
    check("503: class-decided match refused w/o approval", "--approve-class-match" in str(e))
pf.CLASS_MATCH_APPROVED = True
port, _ = pf.resolve_device("legacy-hash-only", reg_legacy)
check("503: approval flag unlocks legacy match", port["com"] == "COM8")
pf.CLASS_MATCH_APPROVED = False

# 7. Bridge provisional requires a SERIAL-LESS port: a bridge entry may not
#    claim a serial-bearing port of its class.
reg_bridge = {"devices": {"hv3": {"vid_pid": ["10C4:EA60"], "mac": "3c:0f:02:eb:f9:0c",
                                    "discriminators": {"windows": {}}}},
              "foreign_devices": {}}
SERIAL_CP2102 = dict(BRIDGE_PORT, usb_serial="REPROGRAMMED123")
with_ports([SERIAL_CP2102])
try:
    pf.resolve_device("hv3", reg_bridge)
    check("503: bridge cannot claim serial-bearing port", False, "no refusal")
except Refusal:
    check("503: bridge cannot claim serial-bearing port", True)

# --- #808: --known-port operator port-assertion ---------------------------
# When a bridge is passively unidentifiable (serial-less + a serial-less
# registry rival, or >1 present candidate), the operator may name the exact
# port. This bypasses ONLY the passive-guess refusals; the port must still be
# present + a serial-less candidate of the entry's bridge class, and confirm
# still MAC-verifies (bridge_provisional stays set). reg1 = no rival, reg2 =
# rival "hv3-other" present (the case that refuses passively, above).

# 1. --known-port bypasses the rival refusal and marks the match operator_asserted.
with_ports([BRIDGE_PORT])
port, entry = pf.resolve_device("hv3", reg2, known_port="COM6")
check("808: known-port bypasses rival refusal",
      port.get("bridge_provisional") is True and port["com"] == "COM6",
      f"got {port}")
check("808: known-port sets operator_asserted",
      port.get("operator_asserted") is True, f"got {port}")

# 2. --known-port disambiguates when >1 class candidate is present (the
#    two-port case that otherwise refuses).
with_ports([BRIDGE_PORT, dict(BRIDGE_PORT, com="COM9")])
port, _ = pf.resolve_device("hv3", reg1, known_port="COM9")
check("808: known-port picks the named port among candidates",
      port["com"] == "COM9", f"got {port.get('com')}")

# 3. Case-insensitive: lowercase "com6" resolves COM6.
with_ports([BRIDGE_PORT])
port, _ = pf.resolve_device("hv3", reg1, known_port="com6")
check("808: known-port is case-insensitive", port["com"] == "COM6")

# 4. --known-port naming an ABSENT port refuses (no blind trust).
with_ports([BRIDGE_PORT])
try:
    pf.resolve_device("hv3", reg1, known_port="COM7")
    check("808: known-port absent refuses", False, "no refusal")
except Refusal as e:
    check("808: known-port absent refuses",
          "not a present" in str(e).lower() or "COM7" in str(e), f"got {e}")

# 5. --known-port may NOT claim a serial-BEARING port (identity exists).
with_ports([dict(BRIDGE_PORT, usb_serial="REPROGRAMMED123")])
try:
    pf.resolve_device("hv3", reg1, known_port="COM6")
    check("808: known-port cannot claim serial-bearing port", False, "no refusal")
except Refusal:
    check("808: known-port cannot claim serial-bearing port", True)

# 6. The MAC gate stays armed: known-port returns bridge_provisional, so
#    _verify_bridge_mac (gated on it) still fires in confirm before any write.
with_ports([BRIDGE_PORT])
port, _ = pf.resolve_device("hv3", reg1, known_port="COM6")
check("808: known-port stays MAC-gated (bridge_provisional set)",
      port.get("bridge_provisional") is True)

print()
if FAILURES:
    print(f"{len(FAILURES)} FAILURE(S): {FAILURES}")
    sys.exit(1)
print("ALL PASS (503)")
