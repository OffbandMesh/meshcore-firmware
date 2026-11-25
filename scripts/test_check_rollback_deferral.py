#!/usr/bin/env python
"""Offband #2286: tests for check_rollback_deferral.py.

Run:  python scripts/test_check_rollback_deferral.py
Exit: 0 on pass, nonzero with a clear failure message otherwise.
Stdlib only, no build, matching the other scripts/test_*.py.

Every case is either a defect the check must fail on or a legitimate pattern it
must pass. They run the REAL check_tree() over fixture trees in a temp dir, with
the required list passed in. Nothing is re-implemented here.
"""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_rollback_deferral as chk  # noqa: E402

FAILURES = []
ROLE = os.path.join("examples", "role_x", "main.cpp")
REQUIRED = {ROLE: "fixture role that confirms its image"}

GOOD = ('extern "C" bool verifyRollbackLater() { return true; }\n'
        "void setup() { board.beginBootSafety(); }\n"
        "void loop() { if (millis() > 60000) board.markBootValid(); }\n")


def expect(condition, label):
    if condition:
        print("  pass  %s" % label)
    else:
        print("  FAIL  %s" % label)
        FAILURES.append(label)


def run(files, required=REQUIRED):
    root = tempfile.mkdtemp(prefix="rollback-deferral-")
    try:
        for rel, text in files.items():
            path = os.path.join(root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(text)
        return chk.check_tree(root, required)
    finally:
        shutil.rmtree(root)


def main():
    print("positive: the patterns that must pass")
    expect(run({ROLE: GOOD}) == [], "a role that defers and confirms is clean")
    expect(run({ROLE: GOOD.replace("()", "(void)", 1)}) == [],
           "the (void) spelling of the definition is recognized")
    only_comment = GOOD + "// verifyRollbackLater() { return true; } in a comment\n"
    other = os.path.join("src", "helpers", "Other.cpp")
    expect(run({ROLE: GOOD, other: "// verifyRollbackLater() {} mentioned here\n"}) == [],
           "a mention in a comment elsewhere is not a definition")
    expect(run({ROLE: only_comment}) == [], "a commented-out copy beside the real one is ignored")
    expect(run({ROLE: GOOD,
                os.path.join("examples", "role_x", ".pio", "x.cpp"): "bool verifyRollbackLater() {}"})
           == [], "build output under .pio is not scanned")

    print("negative: the defects that must fail")
    f = run({ROLE: GOOD.replace('extern "C" ', "")})
    expect(len(f) == 1 and 'extern "C"' in f[0], "a C++-linkage override is a finding")

    f = run({ROLE: GOOD.replace("board.markBootValid();", "")})
    expect(len(f) == 1 and "markBootValid()" in f[0] and "roll back" in f[0],
           "deferring without markBootValid() is a finding")

    f = run({ROLE: GOOD.replace("board.beginBootSafety();", "")})
    expect(len(f) == 1 and "beginBootSafety()" in f[0], "deferring without beginBootSafety() is a finding")

    f = run({ROLE: GOOD.replace("board.markBootValid();", "// board.markBootValid();")})
    expect(len(f) == 1 and "markBootValid()" in f[0],
           "a confirm call that is only in a comment does not count")

    f = run({ROLE: GOOD, other: 'extern "C" bool verifyRollbackLater() { return true; }\n'})
    expect(len(f) == 1 and "Other.cpp" in f[0],
           "a second role that defers without confirming is a finding")

    f = run({ROLE: "void setup() { board.beginBootSafety(); board.markBootValid(); }\n"})
    expect(len(f) == 1 and "does not define" in f[0],
           "the required role losing its override is a finding")

    f = run({})
    expect(len(f) == 1 and "does not define" in f[0], "a missing required file fails closed")

    print("live tree")
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    live = chk.check_tree(root)
    expect(live == [], "the repository passes: %s" % (live or "clean"))

    if FAILURES:
        print("%d FAILED" % len(FAILURES))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
