#!/usr/bin/env python
"""Self-test for the real-image size gate (#2277). Positive and negative cases."""

import json
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import size_gate_core as core  # noqa: E402

FAILURES = []
SLOT = 1310720  # the Xiao C3's 0x140000 app slot


def expect(condition, label):
    print("  %s  %s" % ("pass" if condition else "FAIL", label))
    if not condition:
        FAILURES.append(label)


def raises(fn):
    try:
        fn()
    except core.GateError as exc:
        return str(exc)
    return None


def budget(doc):
    d = tempfile.mkdtemp(prefix="sizegate_")
    p = os.path.join(d, "b.json")
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(doc if isinstance(doc, str) else json.dumps(doc))
    return d, p


def main():
    print("images that must pass")
    ok, line = core.evaluate(700000, SLOT, 100, 90)
    expect(ok and line.startswith("OK"), "an image well under the slot (got %r)" % line)
    ok, line = core.evaluate(1276720, SLOT, 100, 90)
    expect(ok and line.startswith("HIGH"), "today's C3 repeater, 97.4%%, passes and is reported (got %r)" % line)
    ok, line = core.evaluate(SLOT, SLOT, 100, 90)
    expect(ok, "an image exactly filling the slot passes at a 100%% line (got %r)" % line)

    print("images that must fail")
    ok, line = core.evaluate(1416055, SLOT, 100, 90)
    expect(not ok and "OVER" in line, "an image over the slot (got %r)" % line)
    pio_figure, eh_frame = 1300000, 80000  # PlatformIO's figure fits; the real image doesn't
    ok, line = core.evaluate(pio_figure + eh_frame, SLOT, 100, 90)
    expect(pio_figure < SLOT and not ok, "fits PlatformIO's figure but not the real image (got %r)" % line)
    ok, line = core.evaluate(1276720, SLOT, 95, 90)
    expect(not ok, "the C3 repeater fails once the line moves to 95%% (got %r)" % line)

    print("can't measure: must fail, never pass")
    expect(raises(lambda: core.evaluate(0, SLOT, 100, 90)) is not None, "no image")
    expect(raises(lambda: core.evaluate(700000, 0, 100, 90)) is not None, "no slot size")
    d, p = budget("{not json")
    expect(raises(lambda: core.load_budget("esp32c3", p)) is not None, "an unreadable budget file")
    shutil.rmtree(d)
    expect(raises(lambda: core.load_budget("esp32c3", os.path.join(tempfile.gettempdir(), "nope.json")))
           is not None, "a missing budget file")
    d, p = budget({"default": {"fail_percent": 80, "report_percent": 90}})
    expect(raises(lambda: core.load_budget("esp32c3", p)) is not None, "report line above fail line")
    shutil.rmtree(d)

    print("the budget file")
    d, p = budget({"default": {"fail_percent": 100, "report_percent": 90},
                   "mcu": {"esp32c3": {"fail_percent": 95}}})
    expect(core.load_budget("esp32c3", p) == (95.0, 90.0), "a per-MCU entry overrides the default")
    expect(core.load_budget("esp32s3", p) == (100.0, 90.0), "other MCUs take the default")
    shutil.rmtree(d)
    expect(core.load_budget("esp32c3") == (100.0, 90.0), "the repo's budget loads: fail 100%, report 90%")

    if FAILURES:
        print("\n%d failure(s)." % len(FAILURES))
        return 1
    print("\nall cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
