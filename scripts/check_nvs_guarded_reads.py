#!/usr/bin/env python3
r"""Refuse an unguarded `Preferences::getString()` on an optional pref (#899).

WHY THIS EXISTS

Arduino's `Preferences::getString(key, default)` takes a default parameter
precisely so that a MISSING key is a normal outcome -- and then logs at ERROR
when it uses that default:

    // framework-arduinoespressif32/libraries/Preferences/src/Preferences.cpp:483
    esp_err_t err = nvs_get_str(_handle, key, value, &len);
    if(err){
        log_e("nvs_get_str len fail: %s %s", key, nvs_error(err));
        return String(defaultValue);
    }

Any caller using that API as designed emits ERROR spam by construction. Our
broker config makes it continuous: `writeBrokerConfig` deliberately REMOVES
empty fields rather than storing blanks (#182), so absent keys are the designed
steady state, and the MQTT pool worker re-drives every slot on a 500 ms cycle.

Measured on ST-P (`heltec_v4_repeater_telemetry`, brokers unconfigured):

    10,135 ERROR lines in 346 s  =  29.3/sec, median gap 9 ms
    884,912 of 911,485 serial bytes  =  97.1% of ALL output

That is not merely noise. The log path and the CLI reply path write the same
UART with no mutual exclusion and interleave MID-WRITE, so replies came back
spliced inside a word (`not _supportesd`, `??: lonXil`) -- mojibake on a CLI
reply, and the same corruption defeated secret redaction in `pio-flash`.

WHAT THIS CHECKS

Only `getString` is guarded, and that is deliberate rather than an oversight:
the numeric getters log at `log_v` (VERBOSE), not `log_e` --

    // Preferences.cpp, getUChar
    log_v("nvs_get_u8 fail: %s %s", key, nvs_error(err));

-- which is why exactly the string keys appeared in the capture while
`enabled`, `port` and `transport` did not. Widening to `getUChar` would flag
code that emits nothing. (If a future core release promotes those to `log_e`,
this check must widen with it -- noted because it is an assumption about
third-party code, not a fact about ours.)

The rule is a bare-call ban rather than "an isKey must appear nearby":
proximity checks need data-flow analysis to be sound, and a regex pretending to
do one is how a guard develops false positives and gets switched off. One
helper, one place to audit, mechanically checkable.

    BAD:   String u = p.getString(kKeyBrokerUsername, "");
    GOOD:  String u = prefStr(p, kKeyBrokerUsername);

The helper is allowed exactly one `getString`, inside its own type check.

SCOPE

Repo-wide. An earlier version scoped this to `src/helpers/wifi_observer/` on the
assumption that a wider net would flag too much existing code and get switched
off. That assumption was never measured, and the measurement contradicted it:
after the fix the whole tree contains THREE bare call sites. A narrow guard
bought nothing and guaranteed the defect would recur elsewhere.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

DEFAULT_TARGETS = ["src", "examples"]

# `.` AND `->`: a pointer receiver (`pp->getString(k)`) is the same defect, and
# matching only the dot operator meant one keystroke defeated the guard.
#
# Requires a NON-EMPTY argument list. That is what separates a Preferences read
# (always passes a key) from `HTTPClient::getString()`, which takes none -- a
# real call in examples/simple_repeater/main.cpp that a repo-wide guard would
# otherwise flag forever. DOTALL so a call split across lines still matches.
_GET_STRING = re.compile(
    r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\.|->)\s*getString\s*\(\s*(?!\))",
    re.DOTALL,
)

# A macro that hides the call. It cannot be resolved without a preprocessor, so
# it is REPORTED rather than silently missed.
_MACRO_HIDING = re.compile(r"^[ \t]*#[ \t]*define\b[^\n]*getString[ \t]*\(", re.MULTILINE)

HELPER_NAME = "prefStr"
_HELPER_DEF = re.compile(r"\b" + HELPER_NAME + r"\s*\(")

# Skip directories that are not ours to police.
_SKIP_PARTS = {".pio", "build", "node_modules", ".git"}


def _strip_comments(text: str) -> str:
    """Blank `//` and `/* */` comments, preserving length and newlines.

    Length preservation matters: findings are located by counting newlines up to
    a match offset, so deleting characters would shift every reported line.

    Blanking also closes two holes at once -- a commented-out call can no longer
    read as a violation, and a comment containing the guard keyword can no
    longer forge the helper exemption.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        # String and char literals are skipped WHOLE. Without this, the `//` in
        #     const char* url = "http://example.com"; auto v = p.getString(k);
        # starts a comment and blanks the rest of the line -- silently swallowing
        # a real violation. A guard that misses quietly is worse than no guard.
        if c in ('"', "'"):
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and nxt == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                if i + 1 < n:
                    out[i + 1] = " "
            i += 2
        else:
            i += 1
    return "".join(out)


def _find_helper_span(lines):
    """(start, end) 1-based line range of the helper definition, or None.

    Requires the TYPE CHECK to be present inside the definition. A helper that
    lost its guard is NOT a valid exemption -- otherwise renaming any function
    to `prefStr` would silently disable this check for every caller.

    Comments are already blanked by the time this runs, so the keyword cannot be
    forged in a comment.
    """
    for i, line in enumerate(lines):
        if not _HELPER_DEF.search(line):
            continue
        if ";" in line and "{" not in line:
            continue  # forward declaration
        window = lines[i:i + 14]
        body = "\n".join(window)
        if "{" in body and ("PT_STR" in body or "isKey" in body):
            return (i + 1, i + len(window))
    return None


def analyze_source(text: str, rel: str):
    """Findings for one translation unit."""
    findings = []
    text = _strip_comments(text)
    lines = text.split("\n")

    # Now that the scan is repo-wide, `getString(args)` is no longer proof of a
    # Preferences read: an unrelated class can define its own, e.g.
    #     class MyJsonParser { String getString(const char* key); };
    # and flagging that would be wrong forever -- which is how a guard earns a
    # reputation for false positives and gets switched off.
    #
    # A translation unit that never NAMES Preferences cannot hold a Preferences
    # read (barring an exotic typedef), so it is skipped. This is a deliberate
    # trade: it can miss a read reached through an aliased type, in exchange for
    # not crying wolf on the ~250 files that have nothing to do with NVS.
    if "Preferences" not in text:
        return findings

    for m in _MACRO_HIDING.finditer(text):
        findings.append({
            "file": rel,
            "line": text[:m.start()].count("\n") + 1,
            "kind": "macro-hides-getString",
            "detail": ("a macro wrapping getString cannot be checked statically; "
                       f"call {HELPER_NAME}() directly instead"),
            "text": m.group(0).strip()[:120],
        })

    helper_span = _find_helper_span(lines)

    # Scanned over the WHOLE text, not line by line: a call split as
    #     String v = p.
    #         getString(k);
    # is invisible to a per-line search.
    for m in _GET_STRING.finditer(text):
        line_no = text[:m.start()].count("\n") + 1
        if helper_span and helper_span[0] <= line_no <= helper_span[1]:
            continue
        findings.append({
            "file": rel,
            "line": line_no,
            "kind": "unguarded-getString",
            "detail": (f"`{m.group(1)}.getString(...)` logs at ERROR when the key "
                       f"is absent; route it through {HELPER_NAME}()"),
            "text": lines[line_no - 1].strip()[:120] if line_no <= len(lines) else "",
        })
    return findings


def iter_sources(target: Path):
    if target.is_file():
        yield target
        return
    # Headers too, not just .cpp. The exempt helper lives in a header, and if
    # this only scanned .cpp then the one place `getString` is legitimately
    # called bare would be the one place never checked -- so the helper could
    # silently lose its guard while this still reported clean.
    for p in sorted(list(target.rglob("*.cpp")) + list(target.rglob("*.h"))):
        if _SKIP_PARTS.intersection(p.parts):
            continue
        yield p


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("targets", nargs="*",
                    help="files or directories; defaults to src/ and examples/")
    ap.add_argument("--json-out", help="write findings as JSON for CI to archive")
    ap.add_argument("--advisory", action="store_true",
                    help="report findings but exit 0 (matches check_cli_dispatch)")
    args = ap.parse_args(argv)

    root = Path(__file__).resolve().parent.parent
    targets = [Path(t) for t in args.targets] if args.targets else \
              [root / t for t in DEFAULT_TARGETS]

    findings, scanned = [], 0
    for t in targets:
        if not t.exists():
            print(f"check_nvs_guarded_reads: missing {t}", file=sys.stderr)
            return 2
        for src in iter_sources(t):
            rel = src.relative_to(root).as_posix() if root in src.parents else src.as_posix()
            findings.extend(analyze_source(
                src.read_text(encoding="utf-8", errors="replace"), rel))
            scanned += 1

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(findings, indent=2), encoding="utf-8")

    if not findings:
        print(f"check_nvs_guarded_reads: {scanned} file(s) clean")
        return 0

    for f in sorted(findings, key=lambda x: (x["file"], x["line"])):
        print(f"{f['file']}:{f['line']}: {f['kind']}: {f['detail']}")
        print(f"    {f['text']}")
    print(f"\n{len(findings)} finding(s) across {scanned} file(s). See #899.")
    return 0 if args.advisory else 1


if __name__ == "__main__":
    sys.exit(main())
