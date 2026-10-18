#!/usr/bin/env python
"""Offband #1619: tests for check_frame_cap.py.

Run:  python scripts/test_check_frame_cap.py
Exit: 0 on pass, nonzero with a clear failure message otherwise.
No external deps -- stdlib only, no build, matching the other scripts/test_*.py.

These are deliberately NEGATIVE tests. A checker exercised only against a clean tree
proves nothing about what it CATCHES -- only that it stays quiet. Every case below is
either a defect the tool must fail on, or a legitimate pattern it must NOT flag.

The false-positive cases carry as much weight as the true-positive ones, and more than
usual here: MAX_FRAME_SIZE is named in some thirty comments in this tree, including the
comment that states the very rule being checked, and `KISS_MAX_FRAME_SIZE` is a different
constant for a different protocol with its own buffer. A guard that flagged those would
be switched off within a day, and then it would guard nothing.

Cases run the REAL check_tree() over fixture trees written to a temp dir, with the two
exemption tables passed in. Nothing is re-implemented here: a test that encoded the rules
itself would keep passing while the checker was broken.
"""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_frame_cap as chk  # noqa: E402

FAILURES = []


def expect(condition, label):
    if condition:
        print("  pass  %s" % label)
    else:
        print("  FAIL  %s" % label)
        FAILURES.append(label)


# ---------------------------------------------------------------------------
# Fixture trees. A fixture is a real directory tree, because the checker walks
# one -- and because the coverage floor asserts on the anchor file, so a fixture
# that did not carry one would fail for the wrong reason and hide the case.
# ---------------------------------------------------------------------------

ANCHOR_SRC = """\
#pragma once
#define MAX_FRAME_SIZE  176

class BaseSerialInterface {
public:
  virtual size_t maxFrameSize() const { return MAX_FRAME_SIZE; }
  size_t maxFramePayload(size_t header_bytes) const {
    size_t m = maxFrameSize();
    return m > header_bytes ? m - header_bytes : 0;
  }
};
"""

# A builder that does the right thing. Present in every fixture so the "not one call to
# maxFramePayload()" floor does not fire except where that is the case under test.
GOOD_BUILDER_SRC = """\
#include "helpers/BaseSerialInterface.h"

void buildChunk(BaseSerialInterface* serial, uint8_t* out) {
  const size_t cap = serial->maxFramePayload(2);
  fill(&out[2], cap);
}
"""


class Fixture(object):
    """A throwaway source tree. Use as a context manager."""

    def __init__(self, files, with_anchor=True, with_good_builder=True):
        self.root = tempfile.mkdtemp(prefix="framecap-")
        if with_anchor:
            self.write(chk.ANCHOR_FILE, ANCHOR_SRC)
        if with_good_builder:
            self.write("examples/good/Good.cpp", GOOD_BUILDER_SRC)
        for rel, text in files.items():
            self.write(rel, text)

    def write(self, rel, text):
        path = os.path.join(self.root, rel.replace("/", os.sep))
        parent = os.path.dirname(path)
        if parent and not os.path.isdir(parent):
            os.makedirs(parent)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        shutil.rmtree(self.root, ignore_errors=True)
        return False


def transport_table(extra=None, with_anchor=True):
    """The fixture's TRANSPORT_OWNED.

    The anchor is in it by default because it is in the real one: BaseSerialInterface.h
    both DEFINES the constant and returns it from maxFrameSize()'s default body, which is
    the ceiling itself and not a builder sizing a payload. A fixture that left it out
    would see violations in the anchor in every single case -- which is exactly what the
    first run of this suite did.
    """
    table = {}
    if with_anchor:
        table[chk.ANCHOR_FILE] = "defines the constant and maxFrameSize()'s default body"
    if extra:
        table.update(extra)
    return table


def problems_for(files, transport_owned=None, exempt_lines=None,
                 with_anchor=True, with_good_builder=True):
    """Run the real checker over a fixture tree. Returns the problem list."""
    with Fixture(files, with_anchor, with_good_builder) as fx:
        probs, _stats = chk.check_tree(
            root=fx.root,
            transport_owned=transport_table(transport_owned, with_anchor),
            exempt_lines=() if exempt_lines is None else exempt_lines)
    return probs


def builder(body):
    """Wrap a snippet in a plausible builder file."""
    return "#include \"helpers/BaseSerialInterface.h\"\n\nvoid build() {\n%s\n}\n" % body


def mentions(problems, needle):
    return any(needle in p for p in problems)


# ---------------------------------------------------------------------------
# TRUE POSITIVES -- the tool must fail on each of these. This is the #450/#711
# defect class: a builder deriving a payload size from the buffer constant.
# ---------------------------------------------------------------------------

def test_catches_raw_arithmetic():
    print("true positives: raw MAX_FRAME_SIZE arithmetic in a builder")

    cases = (
        ("  size_t cap = MAX_FRAME_SIZE - 3;", "subtraction with spaces"),
        ("  size_t cap = MAX_FRAME_SIZE-header;", "subtraction without spaces"),
        ("  size_t cap = MAX_FRAME_SIZE - header_bytes;", "subtraction of a variable"),
        ("  if (i + tlen > MAX_FRAME_SIZE) { tlen = MAX_FRAME_SIZE - i; }",
         "the #450 clamp shape"),
        ("  if (len + 3 <= MAX_FRAME_SIZE) { write(buf, len); }",
         "header-additive comparison before a write"),
        ("  size_t avail = (size_t)MAX_FRAME_SIZE - (size_t)i;", "cast arithmetic"),
        # A bare assignment is also caught, but with the alias-specific message --
        # see test_catches_aliasing_with_its_own_message().
        ("  memcpy(dst, src, MAX_FRAME_SIZE - 2);", "arithmetic in an argument"),
        ("  for (int k = 0; k < MAX_FRAME_SIZE - 1; k++) {}", "arithmetic in a loop bound"),
    )
    for body, label in cases:
        probs = problems_for({"examples/app/Builder.cpp": builder(body)})
        expect(len(probs) == 1 and mentions(probs, "raw MAX_FRAME_SIZE in a frame builder"),
               "catches %s" % label)

    # The message has to tell the author what to do instead, or the guard just blocks.
    probs = problems_for({"examples/app/Builder.cpp": builder("  size_t c = MAX_FRAME_SIZE - 3;")})
    expect(mentions(probs, "maxFramePayload(header_bytes)"),
           "failure message names maxFramePayload(header_bytes) as the fix")
    expect(mentions(probs, "#450/#711"),
           "failure message cites the regression it exists to prevent")
    expect(mentions(probs, "examples/app/Builder.cpp:4"),
           "failure message gives file:line (got %r)" % (probs[0] if probs else None))


def test_catches_define_derived_cap():
    print("true positives: a compile-time cap derived by subtraction")

    probs = problems_for({
        "examples/app/Proto.h": "#define MAX_CHANNEL_DATA_LENGTH (MAX_FRAME_SIZE - 9)\n"})
    expect(len(probs) == 1 and mentions(probs, "raw MAX_FRAME_SIZE in a frame builder"),
           "catches a #define payload cap built by subtraction")


def test_catches_unrecognised_bracket_shape():
    print("true positives: rule 3, a bracketed use the grammar does not recognise")

    # An INDEX expression, not a declaration. A grammar loose enough to wave this
    # through would also wave through a declaration-shaped payload cap.
    probs = problems_for({
        "examples/app/Builder.cpp": builder("  out_frame[MAX_FRAME_SIZE - 1] = 0;")})
    expect(len(probs) == 1 and mentions(probs, "does not "
                                        "recognise"),
           "catches an index expression as an unrecognised bracket shape")
    expect(mentions(probs, "Teach it or fix it"),
           "unrecognised-shape message says what the reader must do")

    # An element type the declaration grammar does not know. This is the stale-grammar
    # case: it must FAIL asking to be taught, never be silently skipped.
    probs = problems_for({
        "examples/app/Buf.h": "struct S { uint16_t buf[MAX_FRAME_SIZE]; };\n"})
    expect(len(probs) == 1 and mentions(probs, "BUFFER_ELEM_TYPES"),
           "an unknown buffer element type fails loudly, naming the stale table")


# ---------------------------------------------------------------------------
# FALSE-POSITIVE AVOIDANCE -- every legitimate use, by the category the
# docstring names. A noisy guard gets switched off.
# ---------------------------------------------------------------------------

def test_allows_buffer_declarations():
    print("false positives: a frame buffer declared at the protocol constant")

    cases = (
        ("struct F { uint8_t buf[MAX_FRAME_SIZE]; };\n", "uint8_t buf[MAX_FRAME_SIZE]"),
        ("struct F { uint8_t out_frame[MAX_FRAME_SIZE + 1]; };\n", "declaration with + 1"),
        ("struct F { uint8_t b[MAX_FRAME_SIZE+1]; };\n", "declaration with +1, no spaces"),
        ("struct F { char txt[MAX_FRAME_SIZE]; };\n", "char element type"),
        ("struct F { byte b[MAX_FRAME_SIZE]; };\n", "byte element type"),
        ("struct F { unsigned char b[MAX_FRAME_SIZE]; };\n", "unsigned char element type"),
        ("static uint8_t w_buf[MAX_FRAME_SIZE];\n", "static file-scope declaration"),
        ("  uint8_t *p; uint8_t local[MAX_FRAME_SIZE];\n", "local declaration"),
        # Gemini review finding 6.2: both were in the grammar but unpinned. int8_t is a
        # declared element type, and the regex's `\*?` admits a pointer array. Neither is
        # a payload calculation, so neither should be flagged -- pinned so a future
        # tightening of the grammar cannot silently start flagging them.
        ("struct F { int8_t b[MAX_FRAME_SIZE]; };\n", "int8_t element type"),
        ("static uint8_t *slots[MAX_FRAME_SIZE];\n", "array of pointers"),
    )
    for text, label in cases:
        probs = problems_for({"examples/app/Buf.h": text})
        expect(probs == [], "allows %s" % label)


def test_allows_comments_and_literals():
    print("false positives: comments and string literals naming the constant")

    # This is the sentence the rule itself is written in. If the checker flags its own
    # documentation, it is unusable.
    probs = problems_for({"examples/app/Builder.cpp": (
        "// Builders MUST cap payload to MAX_FRAME_SIZE - header so nothing is clipped.\n"
        "// A hardcoded 170 only fit MTU 176; MAX_FRAME_SIZE - 3 would still clip.\n"
        "void build() {}\n")})
    expect(probs == [], "allows // comments doing MAX_FRAME_SIZE arithmetic")

    probs = problems_for({"examples/app/Builder.cpp": (
        "/* MAX_FRAME_SIZE - 3 is the BLE deliverable frame at MTU 176.\n"
        " * See #450. MAX_FRAME_SIZE is a buffer size, not a link size.\n"
        " */\nvoid build() {}\n")})
    expect(probs == [], "allows /* */ block comments doing the same arithmetic")

    probs = problems_for({"examples/app/Builder.cpp": builder(
        '  LOG("frame too big: len=%d max=MAX_FRAME_SIZE-3", len);')})
    expect(probs == [], "allows the constant's name inside a string literal")

    # A real line from SerialWifiInterface.cpp: the name appears in the format string
    # AND as a bare argument. The bare one is the only one that counts.
    probs = problems_for({"examples/app/Builder.cpp": builder(
        '  LOG("larger than MAX_FRAME_SIZE=%d", MAX_FRAME_SIZE - 1);')})
    expect(len(probs) == 1,
           "a bare occurrence beside a string mention is still counted exactly once")


def test_allows_different_constant():
    print("false positives: KISS_MAX_FRAME_SIZE is a different protocol's constant")

    # The obvious trap. KISS_MAX_FRAME_SIZE is 512, with its own buffer, in
    # examples/kiss_modem. A substring match would flag all of it.
    probs = problems_for({"examples/kiss_modem/KissModem.h": (
        "#define KISS_MAX_FRAME_SIZE  512\n"
        "#define KISS_HW_MAX_PAYLOAD_SIZE (KISS_MAX_FRAME_SIZE + KISS_HW_SUBCMD_BYTES)\n"
        "static uint8_t _rx_buf[KISS_MAX_FRAME_SIZE];\n"
        "void f() { if (len > KISS_MAX_FRAME_SIZE - 2) return; }\n")})
    expect(probs == [], "KISS_MAX_FRAME_SIZE arithmetic is not flagged")

    # ... and the suffix direction too.
    probs = problems_for({"examples/app/Other.h": (
        "#define MAX_FRAME_SIZE_BYTES 176\n"
        "void f() { size_t c = MAX_FRAME_SIZE_BYTES - 3; }\n")})
    expect(probs == [], "MAX_FRAME_SIZE_BYTES is not flagged either")


def test_allows_transport_owned_files():
    print("false positives: a transport owning its own frame buffer")

    owned = {"src/helpers/esp32/SerialBLEInterface.cpp": "owns the queue frame buffer"}
    probs = problems_for(
        {"src/helpers/esp32/SerialBLEInterface.cpp": (
            "size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {\n"
            "  if (len > MAX_FRAME_SIZE) { return 0; }\n"
            "  return ble_frame::deliverableFrame(_att_mtu, MAX_FRAME_SIZE);\n"
            "}\n")},
        transport_owned=owned)
    expect(probs == [],
           "a transport's own buffer-bound rejection and deliverableFrame() bound pass")

    # The same text in a builder file is the defect. The rule is ownership, not text.
    probs = problems_for({"examples/app/Builder.cpp": (
        "void build() {\n  if (len > MAX_FRAME_SIZE) { return; }\n}\n")})
    expect(len(probs) == 1,
           "the identical expression in a NON-owning file is still a violation")


def test_allows_arithmetic_on_the_api_result():
    print("false positives: arithmetic on maxFramePayload()'s result is legitimate")

    probs = problems_for({"examples/app/Builder.cpp": builder(
        "  const size_t cap = serial->maxFramePayload(2);\n"
        "  size_t chunk = cap > 8 ? cap - 8 : 0;")})
    expect(probs == [],
           "clamping/chunking the value the transport returned is not flagged")


# ---------------------------------------------------------------------------
# STALE TABLES -- the pin-guard rule applied to this checker's own tables.
# A table that silently stops matching the tree is how a guard keeps printing
# OK over a shrinking slice of it.
# ---------------------------------------------------------------------------

def test_catches_stale_transport_exemption():
    print("stale tables: TRANSPORT_OWNED")

    probs = problems_for(
        {"src/helpers/Refactored.cpp": "void f() { /* no constant here any more */ }\n"},
        transport_owned={"src/helpers/Refactored.cpp": "used to declare the frame buffer"})
    expect(len(probs) == 1 and mentions(probs, "STALE"),
           "a listed file that no longer mentions the constant fails as stale")
    expect(mentions(probs, "used to declare the frame buffer"),
           "the stale-exemption message quotes the reason that was recorded")

    probs = problems_for(
        {}, transport_owned={"src/helpers/Deleted.cpp": "owned the buffer once"})
    expect(len(probs) == 1 and mentions(probs, "does not exist"),
           "a listed file that no longer exists fails as stale")


def test_catches_stale_exempt_line():
    print("stale tables: EXEMPT_LINES")

    entry = ("examples/app/Builder.cpp", "size_t cap = MAX_FRAME_SIZE - 3;",
             1, "baseline", "#1621: tracked, must shrink")

    # Converted and the table not shrunk -- the case that matters, because the person
    # doing the conversion is the person who must remember.
    probs = problems_for(
        {"examples/app/Builder.cpp": builder("  size_t cap = serial->maxFramePayload(3);")},
        exempt_lines=(entry,))
    expect(len(probs) == 1 and mentions(probs, "STALE"),
           "an entry whose line is gone fails, rather than passing quietly")
    expect(mentions(probs, "delete the entry"),
           "the message says to delete the entry")

    # A SECOND occurrence must not inherit the first one's exemption. Modelled on the
    # real tree, where `if (i + tlen > MAX_FRAME_SIZE) {` really does appear in two
    # builders in one file -- which is why the table counts lines instead of just
    # recording that a file has an exemption.
    dup_line = "if (i + tlen > MAX_FRAME_SIZE) { tlen = 0; }"
    probs = problems_for(
        {"examples/app/Two.cpp":
            "void a() {\n  %s\n}\n\nvoid b() {\n  %s\n}\n" % (dup_line, dup_line)},
        exempt_lines=(("examples/app/Two.cpp", dup_line, 1, "baseline", "#1621"),))
    expect(len(probs) == 1 and mentions(probs, "count mismatch"),
           "a third occurrence of an exempted line fails on the count")
    expect(mentions(probs, "cannot inherit an existing exemption"),
           "the count-grew message explains why that is not allowed")

    # Two declared, one left: the table must shrink.
    probs = problems_for(
        {"examples/app/Builder.cpp": builder("  size_t cap = MAX_FRAME_SIZE - 3;")},
        exempt_lines=(("examples/app/Builder.cpp", "size_t cap = MAX_FRAME_SIZE - 3;",
                       2, "baseline", "#1621"),))
    expect(len(probs) == 1 and mentions(probs, "Shrink the table to 1"),
           "a count that dropped fails and names the new number")

    # Reindenting a file must NOT fail the check: keys are whitespace-normalised.
    probs = problems_for(
        {"examples/app/Builder.cpp": builder("      size_t    cap  =  MAX_FRAME_SIZE - 3;")},
        exempt_lines=(entry,))
    expect(probs == [], "an exemption survives reindentation and respacing")

    # An `allowed` entry is honoured the same way, and counted separately.
    with Fixture({"examples/app/Harness.cpp": builder(
            "  if (len > MAX_FRAME_SIZE) len = MAX_FRAME_SIZE;")}) as fx:
        probs, stats = chk.check_tree(
            root=fx.root, transport_owned=transport_table(),
            exempt_lines=(("examples/app/Harness.cpp",
                           "if (len > MAX_FRAME_SIZE) len = MAX_FRAME_SIZE;",
                           1, "allowed", "diag harness; the size is the test variable"),))
    expect(probs == [], "an `allowed` entry passes")
    expect(stats["allowed_lines"] == 1 and stats["baseline_lines"] == 0,
           "an `allowed` line is counted as allowed, not as baseline")


# ---------------------------------------------------------------------------
# COVERAGE FLOOR -- an OK that classified nothing must not look like an OK
# that classified everything. Each floor is asserted by removing what it
# protects and requiring a failure.
# ---------------------------------------------------------------------------

def test_coverage_floor():
    print("coverage floor")

    probs = problems_for({}, with_anchor=False, with_good_builder=False)
    expect(mentions(probs, "scanned 0 files"), "an empty tree fails the file floor")

    probs = problems_for({"examples/app/Plain.cpp": "void f() {}\n"},
                         with_anchor=False, with_good_builder=False)
    expect(mentions(probs, "found 0 MAX_FRAME_SIZE occurrences"),
           "a tree with no occurrences fails rather than reporting OK")

    probs = problems_for({"examples/app/Builder.cpp": builder(
        "  const size_t cap = serial->maxFramePayload(2);")}, with_anchor=False)
    expect(mentions(probs, "invariant's own anchor is gone"),
           "a missing/renamed anchor file fails the floor")

    # The anchor present but the API gone from it -- a rename. The rule would then be
    # asserting something nothing follows.
    probs = problems_for(
        {chk.ANCHOR_FILE: "#define MAX_FRAME_SIZE 176\nstruct B { size_t maxFrameSize(); };\n"},
        with_anchor=False, with_good_builder=False)
    expect(mentions(probs, "invariant's own anchor is gone"),
           "an anchor that no longer declares the API fails the floor")
    expect(mentions(probs, "not one call to maxFramePayload()"),
           "a tree with zero calls to the sanctioned API fails the floor")


def test_line_numbers_survive_stripping():
    print("line-number accuracy after comment stripping")

    # The stripper replaces comment bytes with spaces and keeps newlines. If it ever
    # collapsed a block comment instead, every reported line number after one would be
    # wrong, and the check would send readers to the wrong place.
    text = ("/* one\n * two\n * three\n */\n"
            "// four\n"
            "void build() {\n"
            "  size_t cap = MAX_FRAME_SIZE - 3;\n"
            "}\n")
    probs = problems_for({"examples/app/Builder.cpp": text})
    expect(len(probs) == 1 and mentions(probs, "Builder.cpp:7"),
           "the violation is reported on line 7, after a 4-line block comment "
           "(got %r)" % (probs[0].split(":")[1] if probs else None))

    stripped = chk.strip_comments_and_literals(text)
    expect(len(stripped.splitlines()) == len(text.splitlines()),
           "stripping preserves the line count exactly")


# ---------------------------------------------------------------------------
# THE REAL TREE -- the positive case, and the only one that proves the tables
# describe THIS repo rather than a fixture.
# ---------------------------------------------------------------------------

def test_real_tree_is_clean_and_not_empty():
    print("the real tree")

    probs, stats = chk.check_tree()
    expect(probs == [],
           "the real tree passes (%d problem(s))%s"
           % (len(probs), "" if not probs else ": " + probs[0]))

    # The floor, asserted here too and with real numbers: the checker's own floor would
    # catch a total collapse, but not a scan that quietly dropped most of the tree.
    expect(stats["files_scanned"] >= 300,
           "at least 300 source files are scanned (got %d)" % stats["files_scanned"])
    expect(stats["occurrences"] >= 40,
           "at least 40 MAX_FRAME_SIZE occurrences are classified (got %d)"
           % stats["occurrences"])
    expect(stats["transport_files_seen"] >= 10,
           "at least 10 transport-owned files are recognised (got %d)"
           % stats["transport_files_seen"])
    expect(stats["buffer_decls"] >= 4,
           "at least 4 frame-buffer declarations are recognised (got %d)"
           % stats["buffer_decls"])
    expect(stats["api_calls"] >= 6,
           "at least 6 calls to maxFramePayload() are found (got %d)"
           % stats["api_calls"])
    expect(stats["anchor_seen"], "the anchor file is found and carries both the "
                                 "constant and the API")

    # Every table entry must describe the real tree. check_tree() already fails on a
    # stale one; this says so in its own voice, because the tables are the part a
    # reader is most likely to doubt.
    expect(len(chk.TRANSPORT_OWNED) == stats["transport_files_seen"],
           "every TRANSPORT_OWNED entry matched a real file that mentions the constant")
    baseline = [e for e in chk.EXEMPT_LINES if e[3] == "baseline"]
    allowed = [e for e in chk.EXEMPT_LINES if e[3] == "allowed"]
    expect(sum(e[2] for e in baseline) == stats["baseline_lines"],
           "the declared baseline line count matches what the tree has")
    expect(sum(e[2] for e in allowed) == stats["allowed_lines"],
           "the declared allowed line count matches what the tree has")
    expect(all("#" in e[4] for e in baseline),
           "every baseline entry cites the issue tracking its conversion")


def test_allows_sanctioned_sizing_calls():
    print("false positives: the sanctioned sizing helpers (#1621 fix shapes)")

    # These are the EXACT line shapes from the #1621 fix on fix/1621-frame-cap-deliverable,
    # taken from that session rather than paraphrased, so this pins the real thing.
    cases = (
        ("#define MAX_CHANNEL_DATA_LENGTH (ble_frame::bleSafeFrame(MAX_FRAME_SIZE) - 9)\n",
         "the #define cap derived through bleSafeFrame"),
        ("void f() {\n  const int frame_cap = (int)ble_frame::bleSafeFrame(MAX_FRAME_SIZE);\n}\n",
         "a cast, namespace-qualified bleSafeFrame assigned to a local"),
        ("void f() {\n  size_t c = bleSafeFrame(MAX_FRAME_SIZE);\n}\n",
         "bleSafeFrame unqualified"),
        ("void f() {\n  size_t c = ble_frame :: bleSafeFrame ( MAX_FRAME_SIZE ) - 9;\n}\n",
         "arithmetic on the helper's result, with loose spacing"),
        ("void f() {\n  return ble_frame::deliverableFrame(_att_mtu, MAX_FRAME_SIZE);\n}\n",
         "deliverableFrame in a non-transport file"),
    )
    for text, label in cases:
        probs = problems_for({"examples/app/Builder.cpp": text})
        expect(probs == [], "allows %s" % label)

    # ... and the thing that keeps this from being a blanket exemption for any call.
    probs = problems_for({"examples/app/Builder.cpp": builder(
        "  size_t c = myOwnCap(MAX_FRAME_SIZE);")})
    expect(len(probs) == 1,
           "an UNLISTED helper taking the constant is still a violation")

    probs = problems_for({"examples/app/Builder.cpp": builder(
        "  size_t c = ble_frame::bleSafeFrameish(MAX_FRAME_SIZE);")})
    expect(len(probs) == 1,
           "a helper whose name merely starts with a sanctioned one is still a violation")

    # The counter is wired, so coverage still reports what it classified.
    with Fixture({"examples/app/Builder.cpp": builder(
            "  size_t c = ble_frame::bleSafeFrame(MAX_FRAME_SIZE) - 9;")}) as fx:
        probs, stats = chk.check_tree(
            root=fx.root, transport_owned=transport_table(), exempt_lines=())
    expect(probs == [] and stats["sanctioned_calls"] == 1,
           "a sanctioned call is counted as such (got %d)" % stats["sanctioned_calls"])


def test_recognised_use_does_not_exempt_the_whole_line():
    print("true positives: a recognised use must not cover raw arithmetic beside it")

    # The hole this closes: accepting the LINE because something on it was recognised.
    cases = (
        ("  uint8_t b[MAX_FRAME_SIZE]; size_t c = MAX_FRAME_SIZE - 3;",
         "a buffer declaration sharing a line with raw arithmetic"),
        ("  size_t c = ble_frame::bleSafeFrame(MAX_FRAME_SIZE) - MAX_FRAME_SIZE;",
         "a sanctioned call sharing a line with a raw use"),
        ("  size_t c = bleSafeFrame(MAX_FRAME_SIZE), d = MAX_FRAME_SIZE - 1;",
         "a sanctioned call and a raw use in one declaration"),
    )
    for body, label in cases:
        probs = problems_for({"examples/app/Builder.cpp": builder(body)})
        expect(len(probs) == 1, "catches %s" % label)


def test_stripper_fails_closed():
    print("stripper fails CLOSED, never open (Gemini review of #1619, finding 3.1)")

    # THE defect class for a stripper inside a guard. The first version scanned forward
    # from a quote for its match without stopping at the newline, so one stray apostrophe
    # blanked out the rest of the file -- and any violation in that span vanished while
    # the checker printed OK. Every case here is a real violation that must still be seen
    # THROUGH a quote that does not open a literal.
    cases = (
        ("const uint32_t BIG = 1'000;\n", "a C++14 digit separator with an odd quote count"),
        ("const uint32_t BIG = 1'000'000;\n", "a digit separator with an even quote count"),
        ('const char* s = "unterminated;\n', "an unterminated string literal"),
        ("const char c = 'unterminated;\n", "an unterminated char literal"),
    )
    for prefix, label in cases:
        probs = problems_for({"examples/app/Builder.cpp":
                              prefix + builder("  size_t cap = MAX_FRAME_SIZE - 3;")})
        expect(any("raw MAX_FRAME_SIZE" in p for p in probs),
               "a violation after %s is still caught" % label)

    # And the stripper must still do its actual job on well-formed literals.
    stripped = chk.strip_comments_and_literals(
        'void f() { log("MAX_FRAME_SIZE - 3"); }\n')
    expect("MAX_FRAME_SIZE" not in stripped,
           "a properly terminated string literal is still stripped")

    stripped = chk.strip_comments_and_literals("char q = '\\'';\nsize_t c = 1;\n")
    expect(len(stripped.splitlines()) == 2,
           "an escaped quote inside a char literal does not eat the next line")


def test_stripper_honours_comment_continuation():
    print("stripper honours a // comment continued with a backslash (finding 3.2)")

    # The compiler treats the continued line as more comment. Scanning it anyway would
    # flag a line that is not code -- a divergence between what the checker sees and what
    # the compiler sees, which is the kind of thing that gets a guard distrusted.
    text = ("// a long comment split for readability \\\n"
            "size_t cap = MAX_FRAME_SIZE - 3;\n"
            "void build() {}\n")
    probs = problems_for({"examples/app/Builder.cpp": text})
    expect(probs == [],
           "a line continued from a // comment is not scanned as code")

    stripped = chk.strip_comments_and_literals(text)
    expect(len(stripped.splitlines()) == len(text.splitlines()),
           "comment continuation preserves the line count")

    # Without the backslash the very same line IS code, and IS a violation.
    probs = problems_for({"examples/app/Builder.cpp": text.replace(" \\\n", "\n", 1)})
    expect(len(probs) == 1,
           "the same line without the continuation is still a violation")


def test_allows_static_assert():
    print("false positives: a compile-time assertion about the constant (finding 2.1)")

    # static_assert is an established idiom in this tree (57 of them). An assertion
    # cannot produce a runtime payload size, so exempting it opens no hole.
    probs = problems_for({"examples/app/Proto.h": (
        "#define SOME_OTHER_LIMIT 128\n"
        "static_assert(MAX_FRAME_SIZE > SOME_OTHER_LIMIT, \"frame must be larger\");\n")})
    expect(probs == [], "static_assert comparing against the constant is not flagged")

    probs = problems_for({"examples/app/Proto.h":
                          "static_assert(MAX_FRAME_SIZE - 9 >= 160, \"headroom\");\n"})
    expect(probs == [], "static_assert doing arithmetic on the constant is not flagged")


def test_catches_aliasing_with_its_own_message():
    print("true positives: aliasing the constant to a local (findings 1.1 and 1.2)")

    # Gemini's claim was that aliasing LANDS GREEN. It does not: the aliasing line itself
    # is a violation, so neither multi-line arithmetic nor a macro alias can reach CI
    # green. These cases pin that, because the claim is the kind that gets repeated.
    probs = problems_for({"examples/app/Builder.cpp": builder(
        "  const size_t buf_size = MAX_FRAME_SIZE;\n"
        "  const size_t payload_cap = buf_size - 3;")})
    expect(len(probs) == 1 and mentions(probs, "ALIASED"),
           "aliasing to a const local is caught, with the alias-specific message")
    expect(mentions(probs, "Do NOT exempt this line"),
           "the alias message warns against exempting it, which would hide the defect")

    probs = problems_for({"examples/app/Builder.cpp": builder(
        "  size_t payload_cap = MAX_FRAME_SIZE;\n  payload_cap -= 3;")})
    expect(len(probs) == 1 and mentions(probs, "ALIASED"),
           "multi-line arithmetic is caught at the line that introduces the alias")

    probs = problems_for({"examples/app/Builder.cpp":
                          "#define BUF_SIZE MAX_FRAME_SIZE\n" + builder(
                              "  const size_t cap = BUF_SIZE - 3;")})
    expect(len(probs) == 1,
           "a macro alias is caught at the #define, so the hidden arithmetic cannot land")

    # Parenthesised and cast forms of the same alias.
    for body, label in (
            ("  size_t c = (MAX_FRAME_SIZE);", "parenthesised alias"),
            ("  size_t c = MAX_FRAME_SIZE ;", "alias with stray spacing")):
        probs = problems_for({"examples/app/Builder.cpp": builder(body)})
        expect(len(probs) == 1 and mentions(probs, "ALIASED"),
               "%s gets the alias message" % label)


def test_ci_wiring():
    print("CI wiring")

    # "In CI" means a job ci-green depends on. A checker nothing calls is not a guard,
    # and a blocking step quietly turned advisory is the same thing wearing a badge.
    # Parsed as text, not YAML: pyyaml is not guaranteed on a developer machine, and
    # this suite is stdlib-only by contract.
    ci = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      ".github", "workflows", "ci.yml")
    with open(ci, "r", encoding="utf-8") as fh:
        lines = fh.read().splitlines()

    for script in ("scripts/test_check_frame_cap.py", "scripts/check_frame_cap.py"):
        idx = [i for i, ln in enumerate(lines)
               if ln.strip().startswith("run:") and ln.strip().endswith(script)]
        expect(len(idx) == 1, "%s is invoked exactly once in ci.yml" % script)
        if len(idx) != 1:
            continue
        # Walk back to the step's `- name:` and check nothing in between made it
        # advisory. continue-on-error sits inside the step, above or below `run:`.
        start = idx[0]
        while start > 0 and not lines[start].strip().startswith("- name:"):
            start -= 1
        end = idx[0] + 1
        while end < len(lines) and not lines[end].strip().startswith("- name:") \
                and not (lines[end].strip() and not lines[end].startswith("       ")):
            end += 1
        block = "\n".join(lines[start:end])
        expect("continue-on-error" not in block,
               "%s is BLOCKING, not advisory" % script)

    needs = [ln for ln in lines if "needs:" in ln and "config-lint" in ln]
    expect(bool(needs),
           "ci-green still aggregates config-lint, so these steps gate merges")


def main():
    test_catches_raw_arithmetic()
    test_catches_define_derived_cap()
    test_catches_unrecognised_bracket_shape()
    test_catches_aliasing_with_its_own_message()
    test_recognised_use_does_not_exempt_the_whole_line()
    test_allows_buffer_declarations()
    test_allows_sanctioned_sizing_calls()
    test_allows_static_assert()
    test_allows_comments_and_literals()
    test_stripper_fails_closed()
    test_stripper_honours_comment_continuation()
    test_allows_different_constant()
    test_allows_transport_owned_files()
    test_allows_arithmetic_on_the_api_result()
    test_catches_stale_transport_exemption()
    test_catches_stale_exempt_line()
    test_coverage_floor()
    test_line_numbers_survive_stripping()
    test_ci_wiring()
    test_real_tree_is_clean_and_not_empty()

    print()
    if FAILURES:
        print("FAIL: %d case(s) failed:" % len(FAILURES), file=sys.stderr)
        for f in FAILURES:
            print("  - %s" % f, file=sys.stderr)
        return 1
    print("OK: all check_frame_cap cases pass (negative cases included).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
