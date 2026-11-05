#!/usr/bin/env python3
"""Self-test for check_wire_allocations.py.

A guard that cannot fail is worse than no guard: it reports PASS forever and everyone
believes it. So every check here is exercised in BOTH directions -- a planted conflict must
be caught, and clean input must not be flagged.

The planted cases are the three real ones from 2026-11-04, so a future change that stops
detecting them fails here rather than on someone's radio.
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("cwa", HERE / "check_wire_allocations.py")
cwa = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(cwa)

PROTO = "examples/companion_radio/OffbandConfigProtocol.h"
MESH = "examples/companion_radio/MyMesh.cpp"

failures: list[str] = []


def check(name: str, cond: bool, detail: str = "") -> None:
    if cond:
        print(f"  ok   {name}")
    else:
        print(f"  FAIL {name} {detail}")
        failures.append(name)


def frame(fields: list[str]) -> str:
    """A minimal device-info builder with the given appended fields."""
    body = "\n".join(f"    out_frame[i++] = {f};" for f in fields)
    return ("  int i = 0;\n"
            "  out_frame[i++] = RESP_CODE_DEVICE_INFO;\n"
            f"{body}\n"
            "  _serial->writeFrame(out_frame, i);\n")


# ---------------------------------------------------------------- device-info appends

base = frame(["FIRMWARE_VER_CODE", "offband_caps", "offband_caps2"])
check("baseline frame parses to its field count",
      len(cwa.device_info_appends(base)) == 3,
      f"got {len(cwa.device_info_appends(base))}")

check("a builder with no device-info frame yields nothing",
      cwa.device_info_appends("int x = 0;\n") == [])

srcs_clean = [(cwa.BASELINE_REF, MESH, base), ("epic/mine", MESH, base)]
check("an identical frame is not flagged",
      cwa.check_device_info(srcs_clean, HERE) == [])

# The real #1001 case: one extra field appended.
extra = frame(["FIRMWARE_VER_CODE", "offband_caps", "offband_caps2", "EXPECTED_ACK_TABLE_SIZE"])
srcs_994 = [(cwa.BASELINE_REF, MESH, base), ("feat/994-delivery-observability", MESH, extra)]
found = cwa.check_device_info(srcs_994, HERE)
check("an appended field IS caught", len(found) == 1, f"got {found}")
check("the finding names the field", found and "EXPECTED_ACK_TABLE_SIZE" in found[0])
check("the finding names the ref", found and "feat/994-delivery-observability" in found[0])

# The real #606 case, and the grouping: two refs appending the SAME field is one finding.
caps3 = frame(["FIRMWARE_VER_CODE", "offband_caps", "offband_caps2", "offband_caps3"])
srcs_two = [(cwa.BASELINE_REF, MESH, base),
            ("epic/606-channel-notify", MESH, caps3),
            ("worktree:wt606", MESH, caps3)]
grouped = cwa.check_device_info(srcs_two, HERE)
check("two refs appending the same field group into one finding",
      len(grouped) == 1, f"got {len(grouped)}")
check("the grouped finding names both refs",
      grouped and "epic/606-channel-notify" in grouped[0] and "worktree:wt606" in grouped[0])

# Two DIFFERENT appended fields are two findings -- the offset-87 collision itself.
srcs_collide = [(cwa.BASELINE_REF, MESH, base),
                ("epic/606-channel-notify", MESH, caps3),
                ("feat/994-delivery-observability", MESH, extra)]
check("two different appended fields are two findings -- the offset-87 collision",
      len(cwa.check_device_info(srcs_collide, HERE)) == 2)

# A missing baseline must be loud, not silently clean.
check("a missing baseline is an error, not a pass",
      len(cwa.check_device_info([("epic/mine", MESH, extra)], HERE)) == 1)

# ---------------------------------------------------------------- constant uniqueness

def proto(lines: list[str]) -> str:
    return "\n".join(lines) + "\n"


one = proto(["constexpr uint8_t OFFBAND_UI_CHNOTIFY_GET = 0x18;"])
check("one name on one value is clean",
      cwa.check_uniqueness([("a", PROTO, one), ("b", PROTO, one)]) == [],
      "the same claim seen on two refs is not a collision")

# The real sub-code collision: two names, one value.
clash = [("epic/606", PROTO, proto(["constexpr uint8_t OFFBAND_UI_CHNOTIFY_GET = 0x18;"])),
         ("epic/1990", PROTO, proto(["constexpr uint8_t OFFBAND_UI_ROTATION_GET = 0x18;"]))]
u = cwa.check_uniqueness(clash)
check("two names on one sub-code IS caught", len(u) == 1, f"got {u}")
check("the sub-code finding names both constants",
      u and "CHNOTIFY_GET" in u[0] and "ROTATION_GET" in u[0])

# Error reasons are decimal, not hex -- a separate pattern, so exercised separately.
reasons = [("x", PROTO, proto(["constexpr uint8_t OFFBAND_UI_ERR_NO_ROTATION = 10;"])),
           ("y", PROTO, proto(["constexpr uint8_t OFFBAND_UI_ERR_SOMETHING = 10;"]))]
check("two names on one error reason IS caught",
      len(cwa.check_uniqueness(reasons)) == 1)

# Namespaces must not bleed: a sub-code and a tag may share a number legitimately.
mixed = [("x", PROTO, proto(["constexpr uint8_t OFFBAND_UI_SCOPE_GET = 0x01;"])),
         ("y", PROTO, proto(["constexpr uint8_t OFFBAND_INFO_TAG_CHAN_NOTIFY = 0x01;"]))]
check("a sub-code and an advert tag may share a value",
      cwa.check_uniqueness(mixed) == [],
      "different namespaces are different number spaces")

# ERR_ constants must not be swallowed by the sub-code pattern.
err_vs_sub = [("x", PROTO, proto(["constexpr uint8_t OFFBAND_UI_ERR_MALFORMED = 5;",
                                  "constexpr uint8_t OFFBAND_UI_SCOPE_GET = 0x05;"]))]
check("an error reason and a sub-code of the same number do not collide",
      cwa.check_uniqueness(err_vs_sub) == [],
      "the sub-code pattern must exclude OFFBAND_UI_ERR_*")

caps = [("x", PROTO, proto(["constexpr uint8_t OFFBAND_CAP2_TUNES = 0x10;"])),
        ("y", PROTO, proto(["constexpr uint8_t OFFBAND_CAP2_OTHER = 0x10;"]))]
check("two names on one caps2 bit IS caught", len(cwa.check_uniqueness(caps)) == 1)

check("caps byte 1 and caps byte 2 are separate spaces",
      cwa.check_uniqueness([("x", PROTO, proto(["constexpr uint8_t OFFBAND_CAP_BLOCK = 0x02;"])),
                            ("y", PROTO, proto(["constexpr uint8_t OFFBAND_CAP2_BUTTON = 0x02;"]))]) == [])

# ---------------------------------------------------------------- historical refs

check("backup refs are historical", cwa.historical("backup/325-pre-rebase-8acea035"))
check("archive refs are historical", cwa.historical("archive/plan3-web-ui-crash-fixture"))
check("an epic ref is NOT historical", not cwa.historical("epic/1994-offband-info-frame"))
check("a task ref is NOT historical", not cwa.historical("task/1997-contract-rev2"))

# ---------------------------------------------------------------- result

print()
if failures:
    print(f"FAIL: {len(failures)} self-test(s) failed: {', '.join(failures)}")
    sys.exit(1)
print("PASS: wire-allocation guard self-test")
sys.exit(0)
