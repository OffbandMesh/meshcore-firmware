# scripts/test_offband_version_splash.py
#
# Verifies the companion OLED splash compact-version derivation
# (offband::shortVersionInto in src/helpers/ui/OffbandSplash.cpp; that C++ is now
# directly covered by test/test_splash, so this remains only as an independent
# spec mirror) by implementing
# the same pure parse in Python and testing against known-good fixtures.
#
# The C++ implementation is a direct translation of this algorithm; this test
# is the executable specification (#33). Portable: no g++.
#
# Run with:  python scripts/test_offband_version_splash.py

import sys

CW_PREFIX = "offband-"


def compact_offband_version(cw: str) -> str:
    """Python equivalent of the SplashScreen _offband_short derivation.

    OFFBAND_VERSION is `git describe` output: "<tag>[-<N>-g<sha>][-dirty]"
    where <tag> may itself contain dashes for a pre-release (e.g. v0.14.0-rc1).
    The commits-since suffix is identified by git's "-g<sha>" marker, NOT the
    first dash (which on a pre-release tag is the "-rcN" separator).
    """
    if cw.startswith(CW_PREFIX):
        cw = cw[len(CW_PREFIX):]
    cw_dirty = "-dirty" in cw
    gmark = cw.find("-g")           # start of "-g<sha>" iff commits>0
    commits = 0
    if gmark != -1:
        # commits = the integer immediately before "-g"; tag ends at the dash
        # that precedes that integer.
        n_start = gmark
        while n_start > 0 and cw[n_start - 1].isdigit():
            n_start -= 1
        commits = int(cw[n_start:gmark]) if gmark > n_start else 0
        tag_end = (n_start - 1) if (n_start > 0 and cw[n_start - 1] == "-") else n_start
        tag = cw[:tag_end]
    else:
        # No commits suffix: exact tag (may be a pre-release). Trim trailing -dirty.
        d = cw.find("-dirty")
        tag = cw[:d] if d != -1 else cw
    suffix = ("+%d" % commits) if commits > 0 else ""
    return tag + suffix + ("*" if cw_dirty else "")


FIXTURES = [
    # (label, OFFBAND_VERSION, expected compact form)
    ("exact release tag",        "offband-v0.14.0",                  "v0.14.0"),
    ("exact pre-release tag",    "offband-v0.14.0-rc1",              "v0.14.0-rc1"),
    ("rc2 exact pre-release",    "offband-v1.2.3-rc2",              "v1.2.3-rc2"),
    ("commits past release",     "offband-v0.14.0-4-g369714e",       "v0.14.0+4"),
    ("commits past pre-release", "offband-v0.14.0-rc1-4-g369714e",   "v0.14.0-rc1+4"),
    ("dirty exact pre-release",  "offband-v0.14.0-rc1-dirty",        "v0.14.0-rc1*"),
    ("dirty commits past tag",   "offband-v0.14.0-4-g369714e-dirty", "v0.14.0+4*"),
    ("untagged SHA fallback",    "369714e",                            "369714e"),
]

failures = []
for label, cw, expected in FIXTURES:
    got = compact_offband_version(cw)
    if got == expected:
        print(f"OK: '{cw}' -> '{got}' ({label})")
    else:
        print(f"FAIL: '{cw}' -> '{got}', expected '{expected}' ({label})")
        failures.append(label)

# Explicit regression guard for the #33 bug: a clean pre-release tag must NOT
# collapse to "v0.14.0+0".
if compact_offband_version("offband-v0.14.0-rc1") == "v0.14.0+0":
    print("FAIL: regression -- pre-release tag still renders the spurious '+0'")
    failures.append("rc1-plus0-regression")

if failures:
    print(f"\n{len(failures)} check(s) failed.")
    sys.exit(1)
print(f"\nAll {len(FIXTURES)} fixtures + regression guard passed.")
sys.exit(0)
