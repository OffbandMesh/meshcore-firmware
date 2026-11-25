#!/usr/bin/env python
"""Offband #2286: the bootloader-rollback deferral is present, and only where it is safe.

Why this exists
---------------
Both Arduino cores call esp_ota_mark_app_valid_cancel_rollback() inside
initArduino(), before setup() runs, whenever the running image is PENDING_VERIFY,
unless the sketch overrides the weak hook verifyRollbackLater() to return true.
With the default, an OTA'd image is confirmed before it has run a single line of
ours, so the bootloader can never roll a bad one back.

The override is only safe in a role that later confirms the image itself, through
board.beginBootSafety() in setup() and board.markBootValid() in loop(). A role
that defers and never confirms leaves its OTA'd image pending, and the bootloader
reverts it at the next reboot, whenever that is. Nothing reports that: the board
just comes back on the old firmware.

So this check enforces three things:

1. Every definition of verifyRollbackLater() is declared extern "C". The core's
   default is a C symbol, and a C++ definition gets a mangled name that the linker
   never matches. The build would succeed and the deferral would silently not
   happen.
2. Every file that defines it also calls beginBootSafety() and markBootValid().
3. The repeater, the role that confirms its image today, does define it. Removing
   it would quietly disarm the rollback again, so its absence is a finding.

Matching
--------
Comments are removed first (string literals are kept, so `extern "C"` survives),
because the explanation next to the override names these functions. Line numbers
in the report are the file's real ones.

Run:  python scripts/check_rollback_deferral.py
Exit: 0 clean, 1 on any finding.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_no_rtc_autodiscover import strip_comments  # noqa: E402

# The files that must defer, and why each is safe to.
REQUIRED = {
    os.path.join("examples", "simple_repeater", "main.cpp"):
        "the repeater calls beginBootSafety() in setup() and markBootValid() after "
        "ROLLBACK_VALIDATION_MS",
}

SEARCH_DIRS = ("src", "examples", "variants")
_SOURCE_SUFFIXES = (".cpp", ".c", ".h", ".hpp", ".ino")

_DEFINITION = re.compile(r"\bverifyRollbackLater\s*\(\s*(?:void\s*)?\)\s*\{")
_EXTERN_C = re.compile(r'extern\s+"C"\s+bool\s*$')
_CONFIRMERS = ("beginBootSafety", "markBootValid")


def definitions(code):
    """Yield (offset, is_extern_c) for each definition in comment-stripped code."""
    for m in _DEFINITION.finditer(code):
        line_start = code.rfind("\n", 0, m.start()) + 1
        prefix = code[line_start:m.start()].rstrip()
        yield m.start(), bool(_EXTERN_C.search(prefix))


def check_file(rel, code):
    """Return (defines_it, findings) for one comment-stripped source."""
    findings = []
    defs = list(definitions(code))
    for off, is_c in defs:
        line = code.count("\n", 0, off) + 1
        if not is_c:
            findings.append(
                '%s:%d: verifyRollbackLater() is not declared extern "C". The core\'s '
                "default is a C symbol, so this mangled definition would not replace it "
                "and the deferral would silently not happen." % (rel, line))
        missing = [f for f in _CONFIRMERS if not re.search(r"\b%s\s*\(" % f, code)]
        if missing:
            findings.append(
                "%s:%d: defers bootloader verification but never calls %s, so an OTA'd "
                "image here would stay pending and roll back at the next reboot."
                % (rel, line, " or ".join("%s()" % f for f in missing)))
    return bool(defs), findings


def check_tree(repo_root, required=REQUIRED):
    """Return a list of human-readable findings; empty means clean."""
    findings, defining = [], set()
    for top in SEARCH_DIRS:
        base = os.path.join(repo_root, top)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d != ".pio"]
            for fname in sorted(filenames):
                if not fname.endswith(_SOURCE_SUFFIXES):
                    continue
                path = os.path.join(dirpath, fname)
                rel = os.path.relpath(path, repo_root)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    code = strip_comments(fh.read())
                defines, found = check_file(rel.replace(os.sep, "/"), code)
                findings.extend(found)
                if defines:
                    defining.add(os.path.normpath(rel))
    for rel, why in sorted(required.items()):
        if os.path.normpath(rel) not in defining:
            findings.append(
                "%s: does not define verifyRollbackLater(), so the bootloader rollback is "
                "disarmed again for this role (%s)." % (rel.replace(os.sep, "/"), why))
    return findings


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    findings = check_tree(repo_root)
    if findings:
        for f in findings:
            print("FAIL  " + f)
        print("%d finding(s)." % len(findings))
        return 1
    print("OK  bootloader verification is deferred in %d role(s), each of which confirms "
          "its image: %s" % (len(REQUIRED), ", ".join(
              r.replace(os.sep, "/") for r in sorted(REQUIRED))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
