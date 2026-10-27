#!/usr/bin/env python3
"""Pin the QCC badge's preflashed channel table: which channels ship quiet (#1625).

A tester asked for #test and #echo to be quiet by default. The badge has no per-channel
All/Mentions/Off -- that lives in GuiPrefs and only GUI builds carry it -- so the mechanism
is the per-channel TUNE, where tune id 0 is silence by construction.

This is a config-lint check rather than a native test because the table lives in a variant
header that the native envs do not compile, exactly like test_qcc_variant_pins.py.

Positive AND negative, per standing policy: the two traffic channels must be silent, the
conversational ones must NOT be, and the encoding must still be id+1 with 0 meaning
"inherit" -- because a silent channel and a channel that made no choice are one apart and
swapping them is invisible until a badge is quiet in the wrong room.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHANNELS = os.path.join(ROOT, "variants", "qcc_badge", "QccChannels.h")
SLOTS = os.path.join(ROOT, "src", "helpers", "ui", "TuneSlots.h")

SHOULD_BE_SILENT = {"#test", "#echo"}
SHOULD_INHERIT = {"#queencitycon", "#okimesh", "#offband",
                  "#qcchhv", "#qcchams"}   # #1861: conversation channels

failures = []


def fail(msg):
    failures.append(msg)


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def main():
    for p in (CHANNELS, SLOTS):
        if not os.path.exists(p):
            fail("missing file: %s" % p)
    if failures:
        return report()

    chan = read(CHANNELS)
    slots = read(SLOTS)

    # The encoding must still be id + 1 with 0 reserved for "unset". If that ever changes,
    # kPreflashTuneSilent silently becomes a different tune.
    if not re.search(r"\(uint8_t\)\(id \+ 1\)", slots):
        fail("encodeChannelTune no longer looks like id + 1; the seed table assumes it")
    m = re.search(r"kChannelTuneUnset\s*=\s*(\d+)", slots)
    if not m:
        fail("could not find kChannelTuneUnset in TuneSlots.h")
    elif m.group(1) != "0":
        fail("kChannelTuneUnset is %s, not 0; 'inherit' and 'silent' may have swapped"
             % m.group(1))

    # The table itself.
    rows = re.findall(
        r'\{\s*"(#[A-Za-z0-9_]+)"\s*,\s*"[^"]+"\s*,\s*(kPreflashTune\w+)\s*\}', chan)
    if not rows:
        fail("could not parse kQccPreflashChannels; did its shape change?")
        return report()

    seen = {name: tune for name, tune in rows}

    for name in sorted(SHOULD_BE_SILENT):
        if name not in seen:
            fail("%s is no longer preflashed at all" % name)
        elif seen[name] != "kPreflashTuneSilent":
            fail("%s should ship SILENT, found %s" % (name, seen[name]))

    for name in sorted(SHOULD_INHERIT):
        if name not in seen:
            fail("%s is no longer preflashed at all" % name)
        elif seen[name] != "kPreflashTuneInherit":
            fail("%s should INHERIT the global channel tune, found %s"
                 % (name, seen[name]))

    unexpected = set(seen) - SHOULD_BE_SILENT - SHOULD_INHERIT
    if unexpected:
        fail("preflash table gained channels this check does not know about: %s -- decide "
             "whether each ships quiet and add it here" % ", ".join(sorted(unexpected)))

    # Silent must be a CHOICE, not the absence of one.
    if "kPreflashTuneSilent = encodeChannelTune(kTuneSilent)" not in chan.replace("\n", " "):
        fail("kPreflashTuneSilent is no longer derived from encodeChannelTune(kTuneSilent)")

    return report()


def report():
    if failures:
        print("FAIL: QCC preflash channel tunes")
        for f in failures:
            print("  - %s" % f)
        return 1
    print("OK: QCC preflash channel tunes (%d silent, %d inherit)"
          % (len(SHOULD_BE_SILENT), len(SHOULD_INHERIT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
