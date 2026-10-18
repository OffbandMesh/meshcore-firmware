#!/usr/bin/env python
"""Offband #1619: frame sizing goes through maxFramePayload(), not MAX_FRAME_SIZE arithmetic.

Why this exists
---------------
`MAX_FRAME_SIZE` is 176: a protocol constant the client shares, and the size of the frame
BUFFER. It is NOT what a link delivers. A BLE notification carries the negotiated ATT MTU
minus 3 bytes, and the BLE stack SILENTLY CLIPS a larger frame -- no error, no short
return from writeFrame(), no failing test. So a builder that sizes its payload against
176 is correct on serial and TCP and wrong on BLE, which is exactly why it survives
review.

That is #450. #711 was its regression, two weeks in the field, and `ci.yml` already says
why in its own words: "the frame-size ceiling had no mechanical guard, only a comment."
The comment is still there, on BaseSerialInterface.h:32 -- "Builders that assemble
variable-length frames MUST cap their payload to maxFrameSize() minus their own header
bytes". This is that sentence, made mechanical.

Why source level and not the artifact
-------------------------------------
`check_symbol_presence.py` (#1602) asserts capabilities are LINKED by reading .symtab out
of the built ELF. It cannot carry this one, and its docstring says so:
`BaseSerialInterface::maxFramePayload` is a non-virtual inline member, so it survives in
some envs and is fully inlined away in others -- BOTH CORRECT. A symbol rule over it would
fail on correct images, which is worse than no rule. The invariant is a property of the
source, so it is checked in the source.

Who owns the buffer -- the distinction the whole rule turns on
--------------------------------------------------------------
All 43 non-comment occurrences in src/ and examples/ were read before this was written.
They are not one population, and a rule that treated them as one would be either noise or
nothing. They are four:

  1. THE DEFINITION. BaseSerialInterface.h: the `#define`, and `maxFrameSize()`'s default
     body returning it. The single source of the ceiling.

  2. TRANSPORT-OWNED BUFFER BOUNDS. Every Serial*Interface under src/helpers/ declares
     `uint8_t buf[MAX_FRAME_SIZE]` and rejects `len > MAX_FRAME_SIZE` inside its own
     writeFrame()/onWrite()/receive loop. That is the buffer bound, not a transport cap,
     and a transport cannot ask maxFramePayload() to validate its own buffer -- that is
     circular. These files are exempt BY NAME in TRANSPORT_OWNED, each with its reason.

  3. THE SANCTIONED COMPUTATION. `ble_frame::deliverableFrame(mtu, MAX_FRAME_SIZE)` takes
     the constant AS the buffer bound by design (BleFrameSizing.h:49), and
     MultiSerialInterface's `size_t smallest = MAX_FRAME_SIZE` is the identity of a
     min-fold inside its own maxFrameSize() override. This is how the cap gets COMPUTED;
     flagging it would flag the fix.

  4. APPLICATION FRAME BUILDERS. The defect class. Anything here must go through
     maxFramePayload(header_bytes).

So: in a builder file, a non-comment MAX_FRAME_SIZE must be a recognised frame-buffer
declaration, or a named entry in EXEMPT_LINES. Anything else fails. There is no
heuristic deciding whether an expression "looks like" sizing arithmetic, because that
judgement is what gets a guard wrong in both directions.

EXEMPT_LINES has two kinds, and the difference matters
------------------------------------------------------
  `allowed`   Permanent and correct. The emitw diag harness in simple_repeater clamps a
              synthetic frame to the buffer before writing to a dedicated
              ArduinoSerialInterface: there the size IS the test variable, and
              maxFramePayload() would change what the harness emits.

  `baseline`  Known-unconverted, tracked in #1621, MUST SHRINK. Five live builder
              expressions in companion_radio/MyMesh.cpp predate this check. They are
              recorded rather than ignored so the guard can block every NEW occurrence
              from today without landing red, and rather than fixed here because changing
              a payload cap alters what goes on the wire and wants a bench check -- a
              guard commit should not carry a behavior change.

A baseline is a liability, so it is counted in the output and it cannot rot: an entry
whose line count no longer matches FAILS, in both directions. Converting a site means
shrinking this table, and forgetting to is a CI failure, not a silent pass.

What it does NOT check, deliberately
------------------------------------
- Comments and string literals. Thirty-odd comments in the tree discuss MAX_FRAME_SIZE by
  name, including the one stating this very rule. Stripping them is not an optimisation,
  it is the difference between a check and noise.
- `KISS_MAX_FRAME_SIZE` (512, examples/kiss_modem). A different constant for a different
  protocol, with its own buffer. The `\b` in the pattern already excludes it -- the
  preceding `_` is a word character -- and there is a negative test pinning that, because
  it is the obvious false-positive trap.
- test/ and variants/. The native suites legitimately construct frames at fixed sizes to
  assert the arithmetic; asserting over them would fight the tests that prove the cap.
- Whether a given header byte count is CORRECT. That a builder passes 3 when it writes 4
  header bytes is a bug this cannot see. It checks that the transport is asked, not that
  it is asked the right question.
- Runtime values. A builder that computes a cap from maxFramePayload() and then adds to it
  is not caught. Arithmetic on the virtual's RESULT is legitimate (clamping, chunking), so
  there is no sound textual rule against it.

Run:  python scripts/check_frame_cap.py [--verbose]
Exit: 0 clean, 1 on any violation, stale table or coverage-floor failure. Paired negative
      tests in scripts/test_check_frame_cap.py.
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Only these trees. test/ and variants/ are excluded on purpose -- see the docstring.
SCAN_DIRS = ("src", "examples")
SCAN_EXT = (".h", ".hpp", ".cpp", ".cc", ".c", ".ino")

CONST = "MAX_FRAME_SIZE"
API = "maxFramePayload"

# The invariant's own anchor. If the scan cannot find the constant defined here AND the
# API declared here, it is misrooted or the API was renamed -- either way it is not
# checking what it claims to check, so it fails rather than reporting OK.
ANCHOR_FILE = "src/helpers/BaseSerialInterface.h"

# Files that OWN a frame buffer, so MAX_FRAME_SIZE is their buffer's size and not a
# transport cap. Exempt as a whole file, each with the reason it is here. A file listed
# that no longer mentions the constant FAILS as a stale exemption: that is how a list
# like this quietly stops covering the tree it was written for.
TRANSPORT_OWNED = {
    "src/helpers/BaseSerialInterface.h":
        "defines MAX_FRAME_SIZE and maxFrameSize()'s default body -- the ceiling itself",
    "src/helpers/MultiSerialInterface.h":
        "`size_t smallest = MAX_FRAME_SIZE` is the identity of the min-fold inside its "
        "own maxFrameSize() override (#711): the fan-out frame must fit the most "
        "constrained enabled interface",
    "src/helpers/ArduinoSerialInterface.h":
        "declares rx_buf[MAX_FRAME_SIZE]",
    "src/helpers/ArduinoSerialInterface.cpp":
        "rejects/truncates to its own rx_buf bound in writeFrame() and the receive loop",
    "src/helpers/esp32/SerialBLEInterface.h":
        "declares the queue frame buffer; maxFrameSize() passes the bound to "
        "ble_frame::deliverableFrame() -- the sanctioned computation",
    "src/helpers/esp32/SerialBLEInterface.cpp":
        "pins the local ATT MTU with setMTU(MAX_FRAME_SIZE) and passes the buffer bound "
        "to deliverableFrame(); rejects over-long frames against its own buffer",
    "src/helpers/esp32/SerialWifiInterface.h":
        "declares the queue frame buffer",
    "src/helpers/esp32/SerialWifiInterface.cpp":
        "rejects frames larger than its own buffer on send and receive",
    "src/helpers/ethernet/SerialEthernetInterface.h":
        "declares _rx_buf and the queue frame buffer",
    "src/helpers/ethernet/SerialEthernetInterface.cpp":
        "rejects/truncates to its own buffer bound on send and receive",
    "src/helpers/nrf52/SerialBLEInterface.h":
        "declares the queue frame buffer",
    "src/helpers/nrf52/SerialBLEInterface.cpp":
        "passes the buffer bound to deliverableFrame(); rejects over-long frames against "
        "its own buffer",
}

# A recognised frame-buffer DECLARATION. Exempt anywhere, because declaring the buffer at
# the protocol constant is the correct thing to do -- it is what the constant is for.
# Built from the element types a frame buffer is actually declared with, so an INDEXING
# expression (`out_frame[MAX_FRAME_SIZE - 1] = x`) does not match and is reported as an
# unrecognised shape instead of being waved through.
BUFFER_ELEM_TYPES = ("uint8_t", "int8_t", "char", "byte", "unsigned char")
_BUFFER_DECL_RE = re.compile(
    r"\b(?:%s)\s+\*?\s*[A-Za-z_]\w*\s*\[\s*%s\s*(?:[+-]\s*\d+\s*)?\]"
    % ("|".join(t.replace(" ", r"\s+") for t in BUFFER_ELEM_TYPES), CONST))

# Any bracketed use at all, recognised or not. Rule 3 keys off the difference.
_BRACKETED_RE = re.compile(r"\[\s*[^\]]*\b%s\b[^\]]*\]" % CONST)

_CONST_RE = re.compile(r"\b%s\b" % CONST)
_API_RE = re.compile(r"\b%s\s*\(" % API)

# A compile-time assertion ABOUT the constant is not a builder sizing a payload, and
# static_assert is an established idiom here (57 of them in the tree). It cannot produce
# a runtime size, so exempting it opens no hole. A static_assert spanning lines with the
# constant on a continuation line is NOT matched -- that produces a false positive, which
# is loud, and the named-exemption path covers it.
_STATIC_ASSERT_RE = re.compile(r"\bstatic_assert\s*\(")

# `x = MAX_FRAME_SIZE;` with no arithmetic: an ALIAS. Still a violation, but it gets its
# own message, because this is the one shape where exempting the flagged line would hide
# a real defect: arithmetic on the alias happens on a LATER line, where a line-based scan
# cannot see it. Found by the Gemini review of #1619.
_ALIAS_RE = re.compile(r"=\s*\(?\s*%s\s*\)?\s*;" % CONST)

# Named exemptions in BUILDER files, keyed on (path, whitespace-normalised source line).
# `count` is the number of LINES in that file matching that text, so two identical
# clamps in two builders are both accounted for and a third one is not.
#
#   kind "allowed"  -- permanent, correct, stays.
#   kind "baseline" -- known-unconverted, tracked, MUST SHRINK.
#
# Keyed on text and not on a line number on purpose: editing the file above a site must
# not fail the check, while editing the EXPRESSION -- which is what converting it does --
# must.
EXEMPT_LINES = (
    (
        "examples/simple_repeater/main.cpp",
        "if (len > MAX_FRAME_SIZE) len = MAX_FRAME_SIZE;",
        1, "allowed",
        "the `emitw` diag harness clamps a synthetic frame to its own w_buf before "
        "writing to a dedicated ArduinoSerialInterface, whose deliverable frame IS "
        "MAX_FRAME_SIZE. The size is the test variable; maxFramePayload() would change "
        "what the harness emits.",
    ),
    (
        "examples/companion_radio/MyMesh.cpp",
        "#define MAX_CHANNEL_DATA_LENGTH       (MAX_FRAME_SIZE - 9)",
        1, "baseline",
        "#1621: compile-time channel-data payload cap by subtraction, gating accept/drop "
        "at :1155 and :2965. A constant cannot express a per-transport cap.",
    ),
    (
        "examples/companion_radio/MyMesh.cpp",
        "if (_serial->isConnected() && len + 3 <= MAX_FRAME_SIZE) {",
        1, "baseline",
        "#1621: logRxRaw() gates on the buffer size and then writeFrame()s directly. The "
        "clearest live instance of the #450 shape -- at MTU 176 the deliverable frame is "
        "173, so a 174-176 byte raw-RX-log frame is clipped.",
    ),
    (
        "examples/companion_radio/MyMesh.cpp",
        "if (i + tlen > MAX_FRAME_SIZE) {",
        2, "baseline",
        "#1621: the contact-msg (:700) and channel-msg (:1013) builders, identical text. "
        "Both reach the wire via addToOfflineQueue() -> getFromOfflineQueue() at :3142.",
    ),
    (
        "examples/companion_radio/MyMesh.cpp",
        "tlen = MAX_FRAME_SIZE - i;",
        2, "baseline",
        "#1621: the clamp bodies of the two builders above (:701, :1014).",
    ),
    (
        "examples/companion_radio/MyMesh.cpp",
        "size_t avail = (size_t)MAX_FRAME_SIZE - (size_t)i;",
        1, "baseline",
        "#1621: the system-channel msg builder (:1140), same queued path.",
    ),
)


def _literal_end(text, start, quote):
    """Index just past the literal opened at `start`, or None if it is not a literal.

    A C or C++ string or character literal does not span a bare newline -- the only way
    to continue one is a backslash line continuation. So if the closing quote is not on
    this logical line, this quote is NOT opening a literal: it is an apostrophe in prose
    ("the caller's buffer", which this tree is full of), a C++14 digit separator
    (`1'000`), or an unterminated literal that will not compile anyway.

    Returning None for those is what makes the stripper fail CLOSED. The earlier version
    scanned forward for a matching quote without stopping at the newline, so a single
    stray apostrophe blanked out everything after it -- and a violation inside that span
    was silently swallowed while the checker printed OK. A guard that fails open is worse
    than no guard; it launders a defect into a green run. That behaviour is pinned by
    test_stripper_fails_closed().
    """
    i = start + 1
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "\\" and i + 1 < n:
            i += 2  # escape, including a backslash-newline continuation
            continue
        if ch == "\n":
            return None  # does not close on this line, so it is not a literal
        if ch == quote:
            return i + 1
        i += 1
    return None


def strip_comments_and_literals(text):
    """Blank out // and /* */ comments and string/char literals, preserving line numbers.

    Every removed character becomes a space and every newline is kept, so a line number
    in the output is its line number in the file. That is not cosmetic: it is what lets a
    violation be reported at a line a reader can open.

    Not handled, and why it is acceptable: raw string literals (R"(...)") -- none exist in
    this tree, and one would leave its contents VISIBLE, which produces a false positive
    (loud) rather than a miss (silent). An unterminated /* block comment does run to end
    of file, but that does not compile, so the build catches it before this does.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        pair = text[i:i + 2]
        if pair == "//":
            # A // comment ends at the newline UNLESS the line is continued with a
            # backslash, which the compiler treats as more comment. Diverging from the
            # compiler here would flag a line that is not code.
            while i < n:
                if text[i] == "\\":
                    nl = i + 1
                    if text[nl:nl + 2] == "\r\n":
                        out.append("  ")
                        out.append("\n")
                        i = nl + 2
                        continue
                    if text[nl:nl + 1] == "\n":
                        out.append(" ")
                        out.append("\n")
                        i = nl + 1
                        continue
                if text[i] == "\n":
                    break
                out.append(" ")
                i += 1
        elif pair == "/*":
            while i < n and text[i:i + 2] != "*/":
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        elif ch in "\"'":
            end = _literal_end(text, i, ch)
            if end is None:
                # Not a literal. Keep the character so the code around it stays visible.
                out.append(ch)
                i += 1
            else:
                for k in range(i, end):
                    out.append("\n" if text[k] == "\n" else " ")
                i = end
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def normalize(line):
    """Collapse whitespace so an exemption key survives reindentation."""
    return " ".join(line.split())


def _exempt_index(entries=None):
    """{(path, normalized line): (count, kind, reason)} -- keys normalized like the code."""
    out = {}
    for path, line, count, kind, reason in (
            EXEMPT_LINES if entries is None else entries):
        out[(path, normalize(line))] = (count, kind, reason)
    return out


def iter_source_files(root=None):
    """Every scannable source file under SCAN_DIRS, as repo-relative POSIX paths."""
    root = REPO_ROOT if root is None else root
    for scan_dir in SCAN_DIRS:
        base = os.path.join(root, scan_dir)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d not in (".git", ".pio")]
            for name in sorted(filenames):
                if name.endswith(SCAN_EXT):
                    full = os.path.join(dirpath, name)
                    rel = os.path.relpath(full, root).replace(os.sep, "/")
                    yield rel, full


def check_tree(root=None, transport_owned=None, exempt_lines=None):
    """Check one tree. Returns (problems, stats) -- problems empty means clean.

    `root` and the two tables are parameters so the negative tests can run the REAL
    logic over fixture trees, rather than monkey-patching module state or re-implementing
    the rules in the test (which would then pass while the checker was broken).
    """
    root = REPO_ROOT if root is None else root
    transport_owned = TRANSPORT_OWNED if transport_owned is None else transport_owned
    exempt = _exempt_index(exempt_lines)

    problems = []
    stats = {
        "files_scanned": 0,
        "files_with_const": 0,
        "transport_files_seen": 0,
        "occurrences": 0,
        "buffer_decls": 0,
        "static_asserts": 0,
        "allowed_lines": 0,
        "baseline_lines": 0,
        "api_calls": 0,
        "anchor_seen": False,
    }
    # (path, normalized line) -> how many lines actually matched, for the stale check.
    seen_exempt = {}
    transport_seen = set()

    for rel, full in iter_source_files(root):
        stats["files_scanned"] += 1
        try:
            with open(full, "r", encoding="utf-8", errors="replace") as fh:
                raw = fh.read()
        except OSError as exc:
            problems.append("%s: cannot read (%s)" % (rel, exc))
            continue

        code = strip_comments_and_literals(raw)
        stats["api_calls"] += len(_API_RE.findall(code))

        if rel == ANCHOR_FILE:
            # The anchor must define the constant and declare the API, in code.
            if re.search(r"#\s*define\s+%s\b" % CONST, code) and _API_RE.search(code):
                stats["anchor_seen"] = True

        if CONST not in code:
            if rel in transport_owned:
                problems.append(
                    "%s: listed in TRANSPORT_OWNED but no longer mentions %s -- the "
                    "exemption is STALE. Remove the entry (reason given was: %s)"
                    % (rel, CONST, transport_owned[rel]))
            continue

        stats["files_with_const"] += 1

        if rel in transport_owned:
            transport_seen.add(rel)
            stats["transport_files_seen"] += 1
            # The transport owns the buffer. Counted, not inspected.
            stats["occurrences"] += len(_CONST_RE.findall(code))
            continue

        # A builder file. Every occurrence must be a recognised buffer declaration or a
        # named exemption.
        for lineno, code_line in enumerate(code.splitlines(), start=1):
            if not _CONST_RE.search(code_line):
                continue
            hits = len(_CONST_RE.findall(code_line))
            stats["occurrences"] += hits
            key = (rel, normalize(code_line))

            if key in exempt:
                seen_exempt[key] = seen_exempt.get(key, 0) + 1
                kind = exempt[key][1]
                stats["baseline_lines" if kind == "baseline" else "allowed_lines"] += 1
                continue

            if _STATIC_ASSERT_RE.search(code_line):
                stats["static_asserts"] += hits
                continue

            if _BUFFER_DECL_RE.search(code_line):
                stats["buffer_decls"] += hits
                continue

            if _BRACKETED_RE.search(code_line):
                # Rule 3, stale shape: it is bracketed, so it is probably a buffer, but
                # not in a shape this recognises. Fail asking to be taught rather than
                # guessing, in either direction.
                problems.append(
                    "%s:%d: %s used inside [] in a shape BUFFER_ELEM_TYPES does not "
                    "recognise -- the declaration grammar is stale, or this is an index "
                    "expression that needs a human. Teach it or fix it: %s"
                    % (rel, lineno, CONST, normalize(code_line)))
                continue

            if _ALIAS_RE.search(code_line):
                problems.append(
                    "%s:%d: %s is ALIASED to a local here -- call %s(header_bytes) "
                    "instead. Do NOT exempt this line: arithmetic on the alias happens "
                    "on a later line, where a line-based check cannot see it, so an "
                    "exemption here would hide the actual defect: %s"
                    % (rel, lineno, CONST, API, normalize(code_line)))
                continue

            problems.append(
                "%s:%d: raw %s in a frame builder -- size the payload with "
                "%s(header_bytes) instead, so the cap follows the transport (BLE "
                "delivers MTU-3 and silently clips the rest; #450/#711): %s"
                % (rel, lineno, CONST, API, normalize(code_line)))

    # Stale EXEMPT_LINES, both directions. A count that grew means a new occurrence
    # hid behind an existing key; one that shrank means a site was converted or moved
    # and the table was not updated.
    for (path, line), (count, kind, reason) in sorted(exempt.items()):
        found = seen_exempt.get((path, line), 0)
        if found == count:
            continue
        if found == 0:
            problems.append(
                "EXEMPT_LINES is STALE: %s no longer contains the %s line `%s`. If it "
                "was converted to %s(), delete the entry. Reason recorded was: %s"
                % (path, kind, line, API, reason))
        else:
            problems.append(
                "EXEMPT_LINES count mismatch in %s for the %s line `%s`: table says %d, "
                "tree has %d. %s"
                % (path, kind, line, count, found,
                   "A new occurrence cannot inherit an existing exemption -- convert it "
                   "to %s() or justify it as its own entry." % API if found > count
                   else "Shrink the table to %d." % found))

    for rel in sorted(transport_owned):
        if rel in transport_seen:
            continue
        if not os.path.isfile(os.path.join(root, rel)):
            problems.append(
                "%s: listed in TRANSPORT_OWNED but does not exist -- the exemption is "
                "STALE. Remove the entry (reason given was: %s)"
                % (rel, transport_owned[rel]))

    # Coverage floor. An OK that inspected nothing must not look like an OK that
    # inspected everything.
    if stats["files_scanned"] == 0:
        problems.append(
            "coverage floor: scanned 0 files under %s -- wrong root, or SCAN_DIRS/"
            "SCAN_EXT is stale" % "/".join(SCAN_DIRS))
    if stats["occurrences"] == 0:
        problems.append(
            "coverage floor: found 0 %s occurrences in %d files. The constant is the "
            "thing being checked; zero means the stripper, the pattern or the tree "
            "changed shape" % (CONST, stats["files_scanned"]))
    if not stats["anchor_seen"]:
        problems.append(
            "coverage floor: %s does not define %s and declare %s() in code -- the "
            "invariant's own anchor is gone, so this check cannot assert what it claims"
            % (ANCHOR_FILE, CONST, API))
    if stats["api_calls"] == 0:
        problems.append(
            "coverage floor: not one call to %s() anywhere in %s -- either every builder "
            "regressed at once, or the API was renamed and this check is now asserting "
            "a rule nothing follows" % (API, "/".join(SCAN_DIRS)))

    return problems, stats


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    verbose = "--verbose" in argv

    problems, stats = check_tree()

    if verbose:
        for path, line, count, kind, reason in EXEMPT_LINES:
            print("  %-8s %-40s x%d  %s" % (kind, os.path.basename(path), count, line))

    if problems:
        print("FAIL: %d frame-cap violation(s):" % len(problems), file=sys.stderr)
        for p in problems:
            print("  - %s" % p, file=sys.stderr)
        return 1

    # Coverage is part of the result, not a footnote: an OK that classified nothing
    # would otherwise look identical to an OK that classified everything.
    print("OK: %d files scanned, %d mention %s, %d occurrences classified "
          "(%d transport-owned file(s), %d buffer declaration(s), %d static_assert(s), "
          "%d allowed, %d baseline); %d call(s) to %s()."
          % (stats["files_scanned"], stats["files_with_const"], CONST,
             stats["occurrences"], stats["transport_files_seen"], stats["buffer_decls"],
             stats["static_asserts"], stats["allowed_lines"], stats["baseline_lines"],
             stats["api_calls"], API))
    if stats["baseline_lines"]:
        print("     %d builder line(s) are a KNOWN-UNCONVERTED baseline (#1621), not a "
              "clean bill of health. Converting one means shrinking EXEMPT_LINES; this "
              "check fails if that is forgotten." % stats["baseline_lines"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
