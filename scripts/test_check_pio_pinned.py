#!/usr/bin/env python
"""Self-test for check_pio_pinned.py (#2274). Positive and negative cases."""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_pio_pinned as chk  # noqa: E402

FAILURES = []
README_OK = "install PlatformIO (`pip install platformio==6.1.19`)\n"


def expect(condition, label):
    print("  %s  %s" % ("pass" if condition else "FAIL", label))
    if not condition:
        FAILURES.append(label)


def run(files):
    root = tempfile.mkdtemp(prefix="piopin_")
    try:
        for rel, text in files.items():
            path = os.path.join(root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(text)
        return chk.check_tree(root)
    finally:
        shutil.rmtree(root, ignore_errors=True)


def wf(line):
    return "jobs:\n  b:\n    steps:\n      - run: %s\n" % line


def main():
    print("clean trees that must pass")
    f, v = run({".github/workflows/ci.yml": wf("pip install platformio==6.1.19"), "README.md": README_OK})
    expect(f == [] and v == "6.1.19", "one pinned install (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("pip install platformio==6.1.19"),
                ".github/actions/setup/action.yml": "runs:\n  steps:\n    - run: |\n        pip install platformio==6.1.19\n",
                "README.md": README_OK})
    expect(f == [], "a workflow and an action on the same pin (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("pip install platformio==6.1.19") +
                "      # pip install --upgrade platformio was the old way\n", "README.md": README_OK})
    expect(f == [], "a commented-out old install is ignored (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("pip install platformio==6.1.19") + wf("pip install pyserial"),
                "README.md": README_OK})
    expect(f == [], "installs of other packages are ignored (got %r)" % f)

    print("defects that must fail")
    f, v = run({".github/workflows/ci.yml": wf("pip install --upgrade platformio"), "README.md": README_OK})
    expect(any("without an exact version" in x for x in f), "--upgrade with no version (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("pip install platformio>=6.1"), "README.md": README_OK})
    expect(any("without an exact version" in x for x in f), "a range instead of a pin (got %r)" % f)
    f, v = run({".github/workflows/a.yml": wf("pip install platformio==6.1.19"),
                ".github/workflows/b.yml": wf("pip install platformio==6.2.0"), "README.md": README_OK})
    expect(any("more than one version" in x for x in f), "two workflows on different pins (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("pip3 install -U platformio"), "README.md": README_OK})
    expect(any("without an exact version" in x for x in f), "pip3 -U unpinned (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("pip install platformio==6.1.19"), "README.md": "no pin here\n"})
    expect(any("README.md names no" in x for x in f), "a README with no version (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("pip install platformio==6.1.19"),
                "README.md": "pip install platformio==6.1.18\n"})
    expect(any("README.md names PlatformIO 6.1.18" in x for x in f), "a README on a different version (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("pip install platformio==6.1.19"),
                "tools/requirements-build.txt": "pyserial==3.5\nplatformio>=6.1\n", "README.md": README_OK})
    expect(any("requirements-build.txt:2" in x and "without an exact version" in x for x in f),
           "an unpinned platformio in a requirements file (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("pip install platformio==6.1.19"),
                "requirements.txt": "platformio==6.2.0\n", "README.md": README_OK})
    expect(any("more than one version" in x for x in f), "a requirements file on a different pin (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("pip install platformio==6.1.19"),
                "requirements.txt": "platformio==6.1.19  # matches CI\nmkdocs\n", "README.md": README_OK})
    expect(f == [], "a requirements file on the same pin passes (got %r)" % f)
    f, v = run({".github/workflows/ci.yml": wf("echo hi"), "README.md": README_OK})
    expect(any("no PlatformIO install found" in x for x in f), "no install anywhere fails closed (got %r)" % f)

    print("the real tree")
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    f, v = chk.check_tree(root)
    expect(f == [], "this tree is clean (got %r)" % f)

    if FAILURES:
        print("\n%d failure(s)." % len(FAILURES))
        return 1
    print("\nall cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
