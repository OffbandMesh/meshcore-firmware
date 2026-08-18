#!/usr/bin/env python3
"""Static guard for the CLI dispatch chains (#764).

`d4d417ee` converted `handleGetCmd` from prefix `memcmp` to exact-token `isKey`.
For most keys it hoisted the new guard *and* moved the body. For three keys it
hoisted the guard and left the body behind on the old arm:

    } else if (isKey(config, "radio")) {          // matches, writes nothing
    } else if (memcmp(config, "radio", 5) == 0) { // the body, now shadowed
      sprintf(reply, "> %s,%s,%d,%d", ...);

`get radio` then returned an empty reply for ten months. Nothing caught it: it
compiles, it is not dead code by any compiler's reckoning, and the symptom is
invisible on the serial console because that caller zeroes its buffer and
suppresses empty replies. Over the radio the caller did not, so users got
uninitialised stack (#765).

Two checks, both scoped to *contiguous if/else-if chains* -- the same key in two
separate chains is normal and is not flagged:

  A. EMPTY BODY   -- an arm that matches a string key but writes nothing.
  B. SHADOWED KEY -- an arm whose key was already claimed by an earlier arm in
                     the same chain that tests nothing except that key. The
                     later arm can never see its own exact key.

Arity guards are respected. `n >= 3 && strcmp(parts[1], "home")` does not shadow
`n == 2 && strcmp(parts[1], "home")`, because the earlier arm tests more than the
key alone.

Escape hatch, for the rare arm that is deliberately empty:

    } else if (isKey(config, "x")) {   // cli-dispatch-guard: allow-empty <reason>
    }

KNOWN LIMITS (raised in review; none apply to the current tree, all would need a
new coding style to hit):

  * Keys must be string literals. `isKey(config, RADIO_KEY)` with a macro, or a
    ternary picking between two literals, is invisible here -- this reads source,
    not preprocessed output. Every dispatch arm in the tree spells its key out.
  * Bodies are checked as written, so an arm whose only statement sits inside
    `#ifdef` is NOT reported: the directive lines make the body non-empty. The
    companion scan for that case found no instance, and the arms that do use
    conditional compilation (`pwrmgt.*`) all carry an `#else`.

Usage:  python scripts/check_cli_dispatch.py [--json-out FILE] [files...]
Exit 0 clean, 1 on any finding.
"""
import argparse
import json
import re
import sys
from pathlib import Path

# Dispatch chains live in these files. Listed explicitly rather than globbed so
# that a new CLI surface is a deliberate addition here, not a silent omission.
DEFAULT_TARGETS = [
    "src/helpers/CommonCLI.cpp",
    "src/helpers/config/WifiConfigProvider.cpp",
    "src/helpers/wifi_telemetry/RemoteCommand.cpp",
    "examples/companion_radio/MyMesh.cpp",
    "examples/simple_repeater/main.cpp",
    "examples/simple_repeater/MyMesh.cpp",
    "examples/simple_room_server/MyMesh.cpp",
    "examples/simple_secure_chat/main.cpp",
    "examples/simple_sensor/SensorMesh.cpp",
]

ALLOW_EMPTY = "cli-dispatch-guard: allow-empty"

# A key test: func(subject, "literal"). memcmp/strncmp carry a length we ignore --
# the length is exactly what made these arms prefix-match in the first place.
_KEY_CALL = re.compile(
    r'\b(isKey|memcmp|strcmp|strncmp|strcasecmp|strcmp_P)\s*\(\s*'
    r'([A-Za-z_][\w\.\[\]]*(?:->\w+)*)\s*,\s*"((?:[^"\\]|\\.)*)"'
)


def strip_comments(src):
    """Blank comments, preserving every offset so line numbers stay exact.
    String literals are left intact -- they carry the keys we match on."""
    out = list(src)
    i, n = 0, len(src)
    in_str = in_chr = False
    while i < n:
        c, nxt = src[i], (src[i + 1] if i + 1 < n else "")
        if in_str or in_chr:
            if c == "\\":
                i += 2
                continue
            if (in_str and c == '"') or (in_chr and c == "'"):
                in_str = in_chr = False
            i += 1
            continue
        if c == '"':
            in_str = True
        elif c == "'":
            in_chr = True
        elif c == "/" and nxt == "/":
            while i < n and src[i] != "\n":
                out[i] = " "
                i += 1
            continue
        elif c == "/" and nxt == "*":
            while i < n - 1 and not (src[i] == "*" and src[i + 1] == "/"):
                if src[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n - 1:
                out[i] = out[i + 1] = " "
                i += 2
            continue
        i += 1
    return "".join(out)


def _match_delim(s, start, open_c, close_c):
    """`start` indexes `open_c`. Return the index just past its partner."""
    depth = 0
    i, n = start, len(s)
    while i < n:
        if s[i] == open_c:
            depth += 1
        elif s[i] == close_c:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return -1


def _skip_ws(s, i):
    while i < len(s) and s[i].isspace():
        i += 1
    return i


def keys_in(cond):
    """[(func, subject, key)] for every key test in a condition."""
    return [(m.group(1), m.group(2), m.group(3)) for m in _KEY_CALL.finditer(cond)]


def tests_only_key(cond, key):
    """True when the condition tests `key` and nothing else.

    Such an arm claims the key outright, so any later arm naming the same key is
    unreachable for it. An arm carrying an extra conjunct -- an arity check, a
    permission check -- claims only part of the key space and shadows nothing.
    """
    residue = cond
    while True:
        # Re-scan each pass: excising a call shifts every offset after it.
        m = next((m for m in _KEY_CALL.finditer(residue) if m.group(3) == key), None)
        if m is None:
            break
        paren = residue.find("(", m.start())
        end = _match_delim(residue, paren, "(", ")")
        if end == -1:
            break
        # Absorb a trailing comparison so `memcmp(...) == 0` leaves nothing behind.
        rest = re.sub(r'^\s*[!=]=\s*0', "", residue[end:], count=1)
        residue = residue[:m.start()] + rest
    return re.sub(r'[\s()!]', "", residue) == ""


def parse_chains(src, base=0):
    """Every contiguous if / else-if / else chain, including nested ones.

    Returns [[arm, ...]] where an arm is (offset, cond, body, has_braces).
    Chains are the unit of analysis: two arms only interact if they are rungs of
    the same ladder.
    """
    chains = []
    i, n = 0, len(src)
    while i < n:
        m = re.compile(r'\bif\b').search(src, i)
        if not m:
            break
        # An `else if` is a continuation, not the head of a new chain.
        before = src[:m.start()].rstrip()
        if before.endswith("else"):
            i = m.end()
            continue
        j = _skip_ws(src, m.end())
        if j >= n or src[j] != "(":
            i = m.end()
            continue

        arms, pos, ok = [], m.start(), True
        while True:
            k = re.compile(r'\bif\b').search(src, pos)
            if not k:
                ok = False
                break
            p = _skip_ws(src, k.end())
            if p >= n or src[p] != "(":
                ok = False
                break
            cend = _match_delim(src, p, "(", ")")
            if cend == -1:
                ok = False
                break
            cond = src[p + 1:cend - 1]
            b = _skip_ws(src, cend)
            if b < n and src[b] == "{":
                bend = _match_delim(src, b, "{", "}")
                if bend == -1:
                    ok = False
                    break
                body, braced = src[b + 1:bend - 1], True
            else:  # brace-less single statement
                bend = src.find(";", b)
                if bend == -1:
                    ok = False
                    break
                bend += 1
                body, braced = src[b:bend], False
            arms.append((base + k.start(), cond, body, braced))

            after = _skip_ws(src, bend)
            em = re.compile(r'\belse\b').match(src, after)
            if not em:
                break
            nxt = _skip_ws(src, em.end())
            if re.compile(r'\bif\b').match(src, nxt):
                pos = nxt
                continue
            # Terminal `else`: consume it so the outer scan resumes past the chain.
            if nxt < n and src[nxt] == "{":
                e = _match_delim(src, nxt, "{", "}")
                bend = e if e != -1 else bend
            break

        if ok and arms:
            chains.append(arms)
            for off, _c, body, braced in arms:
                if braced:
                    # Recurse so nested chains are analysed in their own right.
                    inner = src.find(body, off - base) if body else -1
                    if body.strip() and inner != -1:
                        chains.extend(parse_chains(body, base + inner))
            i = max(bend, m.end())
        else:
            i = m.end()
    return chains


def analyze_source(text, path="<memory>"):
    """Findings for one translation unit. Pure -- takes text, returns dicts."""
    src = strip_comments(text)
    raw_lines = text.splitlines()

    def line_no(off):
        return src.count("\n", 0, off) + 1

    def line_text(off):
        n = line_no(off)
        return raw_lines[n - 1].strip() if n <= len(raw_lines) else ""

    findings = []
    for arm_list in parse_chains(src):
        claimed = {}  # (subject, key) -> line of the arm that owns it outright
        for off, cond, body, braced in arm_list:
            found = keys_in(cond)
            if not found:
                continue  # not a key-dispatch arm; empty body is its own business
            ln = line_no(off)

            # A lone `;` is the brace-less spelling of "do nothing".
            if not body.strip().strip(";").strip():
                if ALLOW_EMPTY not in line_text(off):
                    findings.append({
                        "file": path, "line": ln, "kind": "empty-body",
                        "keys": sorted({k for _f, _s, k in found}),
                        "text": line_text(off),
                        "detail": ("arm matches {} but writes nothing; the caller "
                                   "sends whatever was already in the reply buffer"
                                   .format(", ".join(sorted({k for _f, _s, k in found})))),
                    })

            for _func, subject, key in found:
                slot = (subject, key)
                if slot in claimed:
                    findings.append({
                        "file": path, "line": ln, "kind": "shadowed-key",
                        "keys": [key], "text": line_text(off),
                        "detail": ('key "{}" is already claimed outright at line {} '
                                   "of the same chain; this arm can never see it"
                                   .format(key, claimed[slot])),
                    })
                elif tests_only_key(cond, key):
                    claimed[slot] = ln
    return findings


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="defaults to the known dispatch files")
    ap.add_argument("--json-out", help="write findings as JSON for CI to archive")
    args = ap.parse_args(argv)

    root = Path(__file__).resolve().parent.parent
    targets = [Path(f) for f in args.files] if args.files else \
              [root / t for t in DEFAULT_TARGETS]

    findings, scanned = [], 0
    for t in targets:
        if not t.exists():
            print(f"check_cli_dispatch: missing {t}", file=sys.stderr)
            return 2
        rel = t.relative_to(root).as_posix() if t.is_absolute() and root in t.parents \
              else t.as_posix()
        findings.extend(analyze_source(t.read_text(encoding="utf-8", errors="replace"), rel))
        scanned += 1

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(findings, indent=2), encoding="utf-8")

    if not findings:
        print(f"check_cli_dispatch: {scanned} file(s) clean")
        return 0

    for f in sorted(findings, key=lambda x: (x["file"], x["line"])):
        print(f"{f['file']}:{f['line']}: {f['kind']}: {f['detail']}")
        print(f"    {f['text']}")
    print(f"\n{len(findings)} finding(s) across {scanned} file(s). See #764.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
