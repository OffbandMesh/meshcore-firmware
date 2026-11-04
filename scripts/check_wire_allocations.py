#!/usr/bin/env python3
"""Fail the build when two efforts claim one wire number, or when anything is appended
to upstream's device-info frame.

WHY THIS SCANS UNMERGED WORK
----------------------------
On 2026-11-04 three features independently claimed device-info offset 87 and two claimed
0xC5 sub-codes 0x18/0x19. Every one of those claims lived on a branch nobody had pushed,
so `git ls-remote` could not see them and each session, reading the merged tree, correctly
concluded the number was free. Scanning the merged tree is what let it happen; scanning
LOCAL branches and worktrees is what finds it.

That insight is WhiteFalcon's, from scripts/check_subcode_uniqueness.py on their #1990
work. This check covers the same ground plus advert tags, capability bits and the
device-info frame, in one place rather than two overlapping guards.

WHY A DEVICE-INFO APPEND IS AN ABSOLUTE FAILURE
-----------------------------------------------
Byte 1 of RESP_CODE_DEVICE_INFO is upstream's FIRMWARE_VER_CODE and upstream uses it to
discriminate upstream's own fields. Every Offband field appended there was announced by
incrementing upstream's counter, so when upstream ships one field and bumps their code,
one number means two things and every absolute offset shifts under every deployed client
at once -- with nothing surfaced, because a client reading a plausible wrong number does
not error. docs/offband_info_frame_contract.md §2 therefore forbids ANY new Offband field
in that frame. Capabilities go in the 0xC5 0x1C advert, which cannot collide on position.

A duplicate sub-code is merely refused at runtime. A duplicate device-info offset is read
as a real value. That is why the second check has no allowance and no warning level.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

PROTO = "examples/companion_radio/OffbandConfigProtocol.h"
MESH = "examples/companion_radio/MyMesh.cpp"
BASELINE_REF = "origin/firmware-base"

# namespace -> regex capturing (name, value). Each namespace is checked for two names
# sharing one value.
NAMESPACES = {
    "0xC5 sub-code": re.compile(
        r"^\s*constexpr\s+uint8_t\s+(OFFBAND_UI_(?!ERR_)[A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]{1,2})\s*;"),
    "0xC5 error reason": re.compile(
        r"^\s*constexpr\s+uint8_t\s+(OFFBAND_UI_ERR_[A-Z0-9_]+)\s*=\s*(\d{1,3})\s*;"),
    "advert tag": re.compile(
        r"^\s*constexpr\s+uint8_t\s+(OFFBAND_INFO_TAG_[A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]{1,2})\s*;"),
    "caps byte 1 bit": re.compile(
        r"^\s*constexpr\s+uint8_t\s+(OFFBAND_CAP_[A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]{1,2})\s*;"),
    "caps byte 2 bit": re.compile(
        r"^\s*constexpr\s+uint8_t\s+(OFFBAND_CAP2_[A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]{1,2})\s*;"),
}

# The device-info builder: from the line that writes RESP_CODE_DEVICE_INFO to its
# writeFrame(). Appends inside it are what the frame is made of.
DEVICE_INFO_START = re.compile(r"out_frame\[i\+\+\]\s*=\s*RESP_CODE_DEVICE_INFO")
APPEND = re.compile(r"out_frame\[i\+\+\]\s*=\s*([^;]+);")
WRITE_FRAME = re.compile(r"writeFrame\s*\(")


# Refs kept on purpose and NOT on the build line. CLAUDE.md is explicit that preserved
# artifacts must never be flagged as stranded or in need of cleanup, and the same applies
# here: a pre-rebase backup legitimately holds an allocation that was later renamed, and
# reporting it would train readers to ignore this check. Verified against a live finding --
# backup/325-pre-rebase-8acea035 still calls caps1 0x08 OFFBAND_CAP_WIFI_CONFIG where
# epic/365 now calls it OFFBAND_CAP_WIFI_COMPANION. Same bit, same feature, renamed; a
# collision in form only.
HISTORICAL_PREFIXES = ("backup/", "archive/", "verify/", "spike/")


def historical(ref: str) -> bool:
    return ref.startswith(HISTORICAL_PREFIXES)


def run(args: list[str], cwd: Path) -> str | None:
    try:
        r = subprocess.run(args, cwd=cwd, capture_output=True, text=True, timeout=120)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return r.stdout if r.returncode == 0 else None


def sources(repo: Path) -> list[tuple[str, str, str]]:
    """Every (label, path, text) worth checking: the merged baseline, every local branch,
    and every worktree's WORKING TREE -- the last of which is the point, because
    uncommitted work is exactly what the collisions lived in."""
    out: list[tuple[str, str, str]] = []

    refs = [BASELINE_REF]
    listed = run(["git", "for-each-ref", "--format=%(refname:short)", "refs/heads"], repo)
    if listed:
        refs += [r for r in listed.split() if r and not historical(r)]

    for ref in refs:
        for path in (PROTO, MESH):
            text = run(["git", "show", f"{ref}:{path}"], repo)
            if text is not None:
                out.append((ref, path, text))

    # Worktrees, read off disk so uncommitted edits count.
    wt = run(["git", "worktree", "list", "--porcelain"], repo)
    if wt:
        for line in wt.splitlines():
            if not line.startswith("worktree "):
                continue
            root = Path(line[9:])
            for path in (PROTO, MESH):
                f = root / path
                try:
                    out.append((f"worktree:{root.name}", path, f.read_text(
                        encoding="utf-8", errors="replace")))
                except OSError:
                    pass
    return out


def check_uniqueness(srcs) -> list[str]:
    """Two different NAMES on one value, in one namespace, is a collision. The same name
    on the same value across refs is just the same claim seen twice."""
    seen: dict[tuple[str, int], dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    for label, path, text in srcs:
        if not path.endswith("OffbandConfigProtocol.h"):
            continue
        for line in text.splitlines():
            for ns, pat in NAMESPACES.items():
                m = pat.match(line)
                if m:
                    value = int(m.group(2), 0)
                    seen[(ns, value)][m.group(1)].add(label)

    errors = []
    for (ns, value), names in sorted(seen.items()):
        if len(names) > 1:
            shown = ", ".join(
                f"{n} [{', '.join(sorted(where))}]" for n, where in sorted(names.items()))
            errors.append(f"{ns} {value:#04x} claimed by {len(names)} names: {shown}")
    return errors


def device_info_appends(text: str) -> list[str]:
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if DEVICE_INFO_START.search(line):
            # From the line AFTER the response code. The code byte is an append too, and
            # counting it made the reported field count one too high -- a small lie in a
            # message whose whole job is to be believed.
            body = []
            for line2 in lines[i + 1:]:
                m = APPEND.search(line2)
                if m:
                    body.append(m.group(1).strip())
                if WRITE_FRAME.search(line2) and body:
                    return body
            return body
    return []


def check_device_info(srcs, repo: Path) -> list[str]:
    baseline = None
    for label, path, text in srcs:
        if label == BASELINE_REF and path.endswith("MyMesh.cpp"):
            baseline = device_info_appends(text)
            break
    if baseline is None:
        return [f"cannot read {BASELINE_REF}:{MESH} -- the baseline frame is unknown, so a "
                f"new append cannot be detected. Fetch the remote and re-run."]

    # Grouped by WHAT is appended, not by ref. A branch and its worktree report the same
    # append, and so does every ref carrying one feature's work -- listing each separately
    # buries one problem in five lines and makes the check look noisier than the repo is.
    by_extra: dict[tuple[str, ...], set[str]] = defaultdict(set)
    for label, path, text in srcs:
        if label == BASELINE_REF or not path.endswith("MyMesh.cpp"):
            continue
        if label.startswith("worktree:") or not historical(label):
            appends = device_info_appends(text)
            if len(appends) > len(baseline):
                by_extra[tuple(appends[len(baseline):])].add(label)

    errors = []
    for extra, labels in sorted(by_extra.items()):
        where = ", ".join(sorted(labels))
        errors.append(
            f"appends {len(extra)} field(s) to upstream's device-info frame: "
            + ", ".join(extra)
            + f" -- on {where}. The frame is frozen at {len(baseline)} fields "
              "(contract §2). Capabilities go in the 0xC5 0x1C advert as tags; a value "
              "gets its own sub-code. An appended byte lands on an absolute offset another "
              "effort may also be using, and a wrong value read there raises no error -- "
              "which is why this has no warning level.")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--repo-root", default=".")
    ap.add_argument("--strict", action="store_true",
                    help="fail on a finding anywhere, not only on the current branch. For "
                         "a pre-push hook or a deliberate repo-wide audit.")
    args = ap.parse_args()
    repo = Path(args.repo_root).resolve()

    srcs = sources(repo)
    if not srcs:
        print("ERROR: no sources found -- not a git repo, or the protocol header moved")
        return 1

    head = (run(["git", "rev-parse", "--abbrev-ref", "HEAD"], repo) or "").strip()
    findings = check_uniqueness(srcs) + check_device_info(srcs, repo)
    refs = len({label for label, _, _ in srcs})

    # WHAT FAILS THE BUILD, AND WHY IT IS NOT EVERYTHING.
    #
    # In CI, refs/heads holds only the branch under test, so the cross-branch detection
    # that found tonight's collisions is a LOCAL capability -- it works on a developer
    # machine and in a pre-push hook, not on a fresh clone. And on a local machine,
    # failing the build for somebody else's stale branch makes the check something people
    # disable rather than read.
    #
    # So: a finding that names the CURRENT branch or its worktree is fatal, because that
    # is the change being made. A finding elsewhere is reported in full and does not fail,
    # because it is information about work that is not yours. --strict makes everything
    # fatal for a pre-push hook or an audit.
    # "Mine" is the current branch or THIS worktree -- not any worktree. An earlier version
    # matched the substring "worktree:" and so treated every other checkout's finding as
    # fatal, which would fail a developer's build for a branch they have never touched.
    here = f"worktree:{repo.name}"
    mine, theirs = [], []
    for f in findings:
        is_mine = (head and head in f) or (here in f)
        (mine if is_mine else theirs).append(f)

    for f in theirs:
        print(f"  NOTE (other work): {f}")
    if theirs and not args.strict:
        print(f"  ^ {len(theirs)} finding(s) on refs that are not {head or 'HEAD'} -- "
              "reported, not failed. Re-run with --strict to treat them as errors.")

    fatal = mine + (theirs if args.strict else [])
    if fatal:
        print(f"FAIL: wire-allocation conflicts across {refs} ref(s)/worktree(s):")
        for f in fatal:
            print(f"  - {f}")
        return 1

    print(f"PASS: no wire-allocation conflicts on {head or 'HEAD'} "
          f"(scanned {refs} ref(s)/worktree(s)); nothing appended to upstream's "
          "device-info frame")
    return 0


if __name__ == "__main__":
    sys.exit(main())
