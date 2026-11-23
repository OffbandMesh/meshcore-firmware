#!/usr/bin/env python
"""Offband #1054: a board with no RTC chip must not auto-discover one at boot.

Why this exists
---------------
`AutoDiscoverRTCClock` probes a short list of I2C addresses at boot and adopts
whatever answers as the board's clock. On a board that has no RTC chip, that probe
can only fail, or adopt a part that is not a clock and serve its registers as the
time (lilygo_techo_card, 0x68). A variant whose board has no RTC chip therefore
declares its clock directly.

Nothing else catches a regression here. Putting the probe back compiles, boots and
passes every other check; the board just does a needless bus walk at boot and is
one stray ACK away from a wrong clock.

The list is the only input
--------------------------
`NO_RTC_CHIP_VARIANTS` names each variant whose board is known to carry no RTC
chip, and how that is known. A variant goes on it from its schematic or from its
own bus scan, never by assumption.

Known gap, left off on purpose: `meshsmith_photon_nrf52` sits on the same carrier
board as the C6 and still auto-discovers. It is not listed until that variant is
brought up on hardware (#194). Listing it now would assert a fact nobody checked.

Matching
--------
Comments are removed first, because the fix itself explains in a comment what
`AutoDiscoverRTCClock` used to do. Any remaining mention counts, including an
`#include` of its header. Line numbers in the report are the file's real ones.

Fails closed: a listed variant with no directory, or a directory with no sources,
is reported, because the check could not confirm anything about it.

Run:  python scripts/check_no_rtc_autodiscover.py
Exit: 0 clean, 1 on any finding.
"""

import os
import re
import sys

NO_RTC_CHIP_VARIANTS = {
    "meshsmith_photon_esp32c6":
        "its only I2C device is the MAX17048 fuel gauge at 0x36 (#1050)",
}

# String and character literals are matched first and kept as they are, so a `//`
# or `/*` inside one is never taken for the start of a comment. Without that, the
# rest of a line after "http://..." would be dropped, and a use there missed.
_COMMENT = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/', re.S)
_USE = re.compile(r"\bAutoDiscoverRTCClock\b")
_SOURCE_SUFFIXES = (".cpp", ".c", ".h", ".hpp")


def strip_comments(text):
    """Blank out comments, keeping every newline so line numbers stay true.
    String and character literals pass through unchanged."""
    def blank(m):
        s = m.group(0)
        if s[0] in "\"'":
            return s
        return "\n" * s.count("\n") or " "
    return _COMMENT.sub(blank, text)


def check_tree(repo_root, variants=NO_RTC_CHIP_VARIANTS):
    """Return a list of human-readable findings; empty means clean."""
    findings = []
    for name, why in sorted(variants.items()):
        vdir = os.path.join(repo_root, "variants", name)
        if not os.path.isdir(vdir):
            findings.append(
                "variants/%s: directory not found, so nothing about this board could be "
                "confirmed" % name)
            continue
        sources = sorted(f for f in os.listdir(vdir) if f.endswith(_SOURCE_SUFFIXES))
        if not sources:
            findings.append(
                "variants/%s: no source files, so nothing about this board could be "
                "confirmed" % name)
            continue
        for fname in sources:
            path = os.path.join(vdir, fname)
            with open(path, encoding="utf-8", errors="replace") as fh:
                code = strip_comments(fh.read())
            for m in _USE.finditer(code):
                line = code.count("\n", 0, m.start()) + 1
                findings.append(
                    "variants/%s/%s:%d: AutoDiscoverRTCClock on a board with no RTC chip "
                    "-- %s. Declare the clock directly instead." % (name, fname, line, why))
    return findings


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    findings = check_tree(repo_root)
    if findings:
        for f in findings:
            print("FAIL  " + f)
        print("%d finding(s)." % len(findings))
        return 1
    print("OK  %d variant(s) with no RTC chip declare their clock directly: %s"
          % (len(NO_RTC_CHIP_VARIANTS), ", ".join(sorted(NO_RTC_CHIP_VARIANTS))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
