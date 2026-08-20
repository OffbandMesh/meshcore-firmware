#!/usr/bin/env python3
"""Sweep the firmware's CLI surface and assert every key still answers (#852).

WHY THIS EXISTS

`get radio`, `get agc.reset.interval` and `get pwrmgt.bootmv` returned an empty
reply for ten months (#764). Nothing caught it: the code compiles, no compiler
calls it dead, and the serial console hides it because that caller zeroes its
buffer and suppresses empty replies. Only the radio transport showed it -- as
uninitialised stack (#765).

The static guard from #775 now catches the STRUCTURAL form of that defect on
every PR. It cannot catch a key that compiles fine and misbehaves at runtime:
a handler that returns early on device state, one whose reply depends on
hardware that is absent, or one that is simply slow. This is the runtime half.

HOW IT RUNS, AND WHY IT IS SPLIT

    enumerate-keys  ->  commands file  ->  [operator runs pio-flash]  ->  analyse

The sweep does NOT invoke `pio-flash` itself. The flash discipline is one human
approval per RUN of that tool, and a wrapper that shells out to it hides the run
behind another layer. The operator issues the run and sees it happen.

    python scripts/cli_sweep.py emit --out cmds.txt
    python scripts/pio-flash.py send-batch <device> --commands-file cmds.txt \\
        --json-out sweep.json
    python scripts/cli_sweep.py analyse --json sweep.json

THE KEY LIST COMES FROM SOURCE, EVERY RUN

Hand-listing is precisely how three keys hid for ten months while everyone
assumed the surface was covered. If a key exists in the dispatch chain, it gets
swept -- including the serial-only ones, and including the secret-bearing ones,
whose replies are redacted at source by #849 with length preserved so the shape
assertion still holds.

A deny-list that skipped the secret keys was proposed and rejected by the owner:
it keeps secrets out of logs by never testing them, which leaves three keys
permanently unverified. That is the failure this whole exercise exists to close.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# Matches a key literal in a dispatch arm, whichever matcher is used. `memcmp`
# arms are included deliberately: #299's conversion left some behind, and those
# are exactly the ones that go stale unnoticed.
_KEY = re.compile(
    r'\b(?:isKey|memcmp)\s*\(\s*config\s*,\s*"((?:[^"\\]|\\.)+)"'
)

class NoKeysFound(Exception):
    """Enumeration produced nothing. That is a failure, not an empty surface."""


DEFAULT_SOURCE = "src/helpers/CommonCLI.cpp"

# A reply slower than this is surfaced. Not a failure -- a question worth
# asking, and the thread #893 is pulling on.
DEFAULT_SLOW_S = 1.0

_PRINTABLE_OK = set("\r\n\t")


def _fn_body(src: str, name: str) -> str:
    """Brace-matched body of one CommonCLI method, or "" if absent."""
    i = src.find(f"void CommonCLI::{name}")
    if i < 0:
        return ""
    b = src.find("{", i)
    if b < 0:
        return ""
    depth = 0
    for j in range(b, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[b:j + 1]
    # Unbalanced braces. Returning the tail would sweep in every `isKey` call
    # from every function that follows -- inventing keys that are not CLI keys
    # at all. Fail closed: no body, no keys, and the caller's require_keys
    # check turns that into a loud error rather than an empty sweep.
    return ""


def enumerate_keys(source: str, require_keys: bool = False):
    """(get_keys, set_keys) read from the dispatch chains, sorted and unique.

    A `set` matcher usually carries a trailing space (`"freq "`), because the
    value follows the key. That is stripped -- the key is `freq`.
    """
    gets = {k.strip() for k in _KEY.findall(_fn_body(source, "handleGetCmd"))}
    sets = {k.strip() for k in _KEY.findall(_fn_body(source, "handleSetCmd"))}
    # The terminal `else` formats "??: %s"; it is a fallback, not a key.
    drop = lambda s: {k for k in s if k and "?" not in k and "%" not in k}
    g, st = sorted(drop(gets)), sorted(drop(sets))
    if require_keys and not g:
        # A renamed method or a moved dispatch chain yields nothing here. Writing
        # an empty command list would report a clean sweep having tested nothing
        # -- the silent no-op this whole effort exists to eliminate.
        raise NoKeysFound(
            "no `get` keys found -- the dispatch chain moved, was renamed, or "
            "the matchers changed. Refusing to emit an empty sweep."
        )
    return g, st


def build_commands(keys, verb: str = "get"):
    return [f"{verb} {k}" for k in keys]


def build_probes(keys, verb: str = "get"):
    """One junk-suffix probe per prefix ROOT, not per key.

    A probe exists to prove an arm is not prefix-matching. `radio`,
    `radio.rxgain` and `radio.fem.rxgain` share a root, so probing all three
    exercises the same arm three times -- and on a 47-key surface that doubles
    the run for no extra coverage.
    """
    roots = []
    for k in keys:
        r = k.split(".")[0]
        if r not in roots:
            roots.append(r)
    return [f"{verb} {r}X" for r in roots]


def analyse(rows, slow_s: float = DEFAULT_SLOW_S):
    """Findings from a `send-batch --json-out` result set.

    Each row is judged against what its command SHOULD do. A row marked
    `probe: true` is a junk-suffix key that is expected to reach the `??:`
    fallback -- for those, the expectation is inverted.
    """
    out = []

    def add(r, kind, detail):
        out.append({"command": r.get("command"), "kind": kind, "detail": detail})

    for r in rows:
        cmd = r.get("command", "")
        reply = (r.get("reply") or "").strip()
        probe = bool(r.get("probe"))

        if r.get("skipped"):
            continue
        if "error" in r:
            add(r, "transport-error", r["error"])
            continue

        # "never answered" and "answered with nothing" are DIFFERENT failures.
        # The first is the latency/loss symptom; the second is #764's shape.
        if not r.get("answered"):
            add(r, "no-reply", f"no bytes within the read window "
                               f"({r.get('elapsed_s')}s)")
            continue
        if r.get("empty_reply") or not reply:
            add(r, "empty-reply", "matched the key and wrote nothing")
            continue

        is_fallback = reply.startswith("??:")
        if probe:
            # A junk suffix MUST fall through. A real value back means the arm
            # is still prefix-matching -- #764's `cad` / `extra.sf` half.
            if not is_fallback:
                add(r, "prefix-match",
                    f"junk suffix answered with a real value: {reply[:40]!r}")
        elif is_fallback:
            # Checked BEFORE the redaction branch below, so a fallback that
            # somehow carried a redaction marker is still reported rather than
            # reading as a healthy answer.
            # A key read from the source came back unknown: the dispatch does
            # not do what the source says. That is #657's shape.
            add(r, "unknown-key", f"enumerated from source but dispatch says {reply[:40]!r}")

        # Redaction markers are a healthy answer, not garbage.
        if "<redacted:" not in reply:
            bad = [c for c in reply if not c.isprintable() and c not in _PRINTABLE_OK]
            if bad:
                add(r, "non-printable",
                    f"{len(bad)} non-printable byte(s) in the reply")

        el = r.get("elapsed_s")
        if el is not None and el > slow_s:
            add(r, "slow", f"{el}s (threshold {slow_s}s)")

    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    e = sub.add_parser("emit", help="write the command list, read from source")
    e.add_argument("--source", default=DEFAULT_SOURCE)
    e.add_argument("--out", required=True)
    e.add_argument("--probes", action="store_true",
                   help="append junk-suffix probes (get radioX, ...) that MUST "
                        "reach the ??: fallback")

    a = sub.add_parser("analyse", help="assert on a send-batch --json-out result")
    a.add_argument("--json", required=True)
    a.add_argument("--slow-s", type=float, default=DEFAULT_SLOW_S)

    args = ap.parse_args(argv)
    root = Path(__file__).resolve().parent.parent

    if args.cmd == "emit":
        src = (root / args.source).read_text(encoding="utf-8", errors="replace")
        gets, sets = enumerate_keys(src, require_keys=True)
        cmds = build_commands(gets)
        if args.probes:
            cmds.extend(build_probes(gets))
        Path(args.out).write_text("\n".join(cmds) + "\n", encoding="utf-8")
        print(f"{len(gets)} get key(s), {len(sets)} set key(s) read from {args.source}")
        print(f"wrote {len(cmds)} command(s) to {args.out}")
        print("set keys are NOT swept: they mutate config, and batch is read-only "
              "by construction. Interrogate them individually with `send`.")
        return 0

    rows = json.loads(Path(args.json).read_text(encoding="utf-8"))
    findings = analyse(rows, slow_s=args.slow_s)
    if not findings:
        print(f"cli_sweep: {len(rows)} command(s), no findings")
        return 0
    for f in findings:
        print(f"{f['command']}: {f['kind']}: {f['detail']}")
    kinds = {}
    for f in findings:
        kinds[f["kind"]] = kinds.get(f["kind"], 0) + 1
    print("\n" + ", ".join(f"{v} {k}" for k, v in sorted(kinds.items())))
    print(f"{len(findings)} finding(s) across {len(rows)} command(s). See #852.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
