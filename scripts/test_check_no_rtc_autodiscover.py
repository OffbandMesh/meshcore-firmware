#!/usr/bin/env python
"""Offband #1054: tests for check_no_rtc_autodiscover.py.

Run:  python scripts/test_check_no_rtc_autodiscover.py
Exit: 0 on pass, nonzero with a clear failure message otherwise.
Stdlib only, no build, matching the other scripts/test_*.py.

A checker only ever run against a clean tree proves nothing about what it catches.
Every case below is either a defect the check must fail on or a legitimate pattern
it must not flag. The comment cases matter as much as the defect cases: the fix
itself names AutoDiscoverRTCClock in a comment, so a check that flagged comments
would fail on the very code it exists to protect.

Cases run the REAL check_tree() over fixture trees in a temp dir, with the variant
list passed in. Nothing is re-implemented here.
"""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_no_rtc_autodiscover as chk  # noqa: E402

FAILURES = []
LISTED = {"board_x": "fixture board with no RTC chip"}

DECLARED = "ESP32RTCClock rtc_clock;\nvoid radio_init() { rtc_clock.begin(); }\n"
PROBING = ("ESP32RTCClock fallback_clock;\n"
           "AutoDiscoverRTCClock rtc_clock(fallback_clock);\n")


def expect(condition, label):
    if condition:
        print("  pass  %s" % label)
    else:
        print("  FAIL  %s" % label)
        FAILURES.append(label)


def tree(files):
    """Write {relative path: text} under a fresh temp root and return the root."""
    root = tempfile.mkdtemp(prefix="norct_")
    for rel, text in files.items():
        path = os.path.join(root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
    return root


def run(files, variants=LISTED):
    root = tree(files)
    try:
        return chk.check_tree(root, variants)
    finally:
        shutil.rmtree(root, ignore_errors=True)


def main():
    print("clean patterns that must NOT be flagged")
    found = run({"variants/board_x/target.cpp": DECLARED})
    expect(found == [], "a declared clock passes")

    found = run({"variants/board_x/target.cpp":
                 "// AutoDiscoverRTCClock walked a probe here once.\n" + DECLARED})
    expect(found == [], "a line comment naming the class passes")

    found = run({"variants/board_x/target.cpp":
                 "/* AutoDiscoverRTCClock\n   used to live here */\n" + DECLARED})
    expect(found == [], "a block comment naming the class passes")

    found = run({"variants/board_x/target.cpp": DECLARED,
                 "variants/board_y/target.cpp": PROBING})
    expect(found == [], "a variant not on the list is left alone")

    print("defects that MUST be flagged")
    found = run({"variants/board_x/target.cpp": PROBING})
    expect(len(found) == 1 and "target.cpp:2:" in found[0],
           "the probe is caught on its real line (got %r)" % found)

    found = run({"variants/board_x/target.h":
                 "#include <helpers/AutoDiscoverRTCClock.h>\n"})
    expect(len(found) == 1 and "target.h:1:" in found[0],
           "an include of its header is caught (got %r)" % found)

    found = run({"variants/board_x/target.cpp":
                 "/* one\n   two\n   three */\n" + PROBING})
    expect(len(found) == 1 and "target.cpp:5:" in found[0],
           "a block comment above does not shift the reported line (got %r)" % found)

    found = run({"variants/board_x/target.cpp":
                 DECLARED + 'const char* url = "http://x"; AutoDiscoverRTCClock rtc_clock(fallback_clock);\n'})
    expect(len(found) == 1 and "target.cpp:3:" in found[0],
           "a // inside a string does not hide a use later on the line (got %r)" % found)

    found = run({"variants/board_x/target.cpp":
                 DECLARED + 'const char* name = "AutoDiscoverRTCClock";\n'})
    expect(len(found) == 1 and "target.cpp:3:" in found[0],
           "the name inside a string literal is caught (got %r)" % found)

    found = run({"variants/board_y/target.cpp": DECLARED})
    expect(len(found) == 1 and "directory not found" in found[0],
           "a listed variant missing from the tree fails closed (got %r)" % found)

    found = run({"variants/board_x/README.md": "notes\n"})
    expect(len(found) == 1 and "no source files" in found[0],
           "a listed variant with no sources fails closed (got %r)" % found)

    print("the real tree")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    found = chk.check_tree(repo_root)
    expect(found == [], "this tree is clean (got %r)" % found)

    if FAILURES:
        print("\n%d failure(s)." % len(FAILURES))
        return 1
    print("\nall cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
