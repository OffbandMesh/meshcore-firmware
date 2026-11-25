"""Offband #2277: the size decision for the real-image gate, kept free of PlatformIO
so it can be tested on a host.

PlatformIO's own size check adds up the sections it knows and leaves out
`.eh_frame`, which the binary carries. The C3 repeater's real image was 97.4% of its
slot while PlatformIO reported 90.7%. So the gate measures `firmware.bin`, the file
an OTA writes, against the app slot from the env's partition table.

The budget lives in scripts/size_budget.json: a default and optional per-MCU
entries, each with `fail_percent` (the build fails above it) and `report_percent`
(the build says so above it).
"""

import json
import os

BUDGET_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "size_budget.json")


class GateError(Exception):
    """The gate couldn't measure. A missing measurement is never a pass (#2198 §6)."""


def load_budget(mcu, path=BUDGET_FILE):
    """Return (fail_percent, report_percent) for this MCU. Raises GateError if the
    file or its values can't be read: an unreadable budget must not pass a build."""
    try:
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
        entry = dict(doc["default"])
        entry.update(doc.get("mcu", {}).get(mcu or "", {}))
        fail, report = float(entry["fail_percent"]), float(entry["report_percent"])
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise GateError("could not read the size budget %s (%s)" % (path, exc))
    if not (0 < report <= fail <= 100):
        raise GateError("size budget for %s is not 0 < report_percent <= fail_percent <= 100 (got %s, %s)"
                        % (mcu or "default", report, fail))
    return fail, report


def evaluate(image_bytes, slot_bytes, fail_percent, report_percent):
    """Return (ok, line). ok is False when the image is over the fail line."""
    if not isinstance(image_bytes, int) or image_bytes <= 0:
        raise GateError("no firmware image to measure")
    if not isinstance(slot_bytes, int) or slot_bytes <= 0:
        raise GateError("the app slot size could not be read from the partition table")
    pct = 100.0 * image_bytes / slot_bytes
    left = slot_bytes - image_bytes
    line = "image %s bytes of a %s-byte app slot: %.1f%%, %s bytes %s" % (
        format(image_bytes, ","), format(slot_bytes, ","), pct, format(abs(left), ","),
        "left" if left >= 0 else "OVER")
    if pct > fail_percent:
        return False, "FAIL  %s (fail line %.0f%%)" % (line, fail_percent)
    if pct > report_percent:
        return True, "HIGH  %s (report line %.0f%%)" % (line, report_percent)
    return True, "OK    %s" % line
