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
