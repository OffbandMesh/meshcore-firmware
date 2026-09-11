#!/usr/bin/env python3
"""Tests for the `wdt hang` / `wdt status` console verbs (#1159).

Pure, no hardware, no compiler. Run: python scripts/test_wdt_hang_verb.py
  or python -m pytest scripts/test_wdt_hang_verb.py

CommonCLI.cpp is not built natively, so these are source-level checks in the
same family as check_cli_dispatch / check_wdt_guard. What they pin:

  * `wdt hang` exists exactly once, is console-only (sender_timestamp == 0), and
    sits inside a WDT_TEST_HANG conditional -- so it can never be in a release
    image or reachable over the mesh.
  * The hang arm really hangs: a `for (;;)` with no feedWatchdog(), delay() or
    yield() in its CODE (comments are stripped first -- the arm's own comment
    explains why delay() is forbidden, which is not the same as calling it).
  * The console line it prints is the documented copy (user-facing text) and is
    flushed before the spin.
  * `wdt` / `wdt status` exist OUTSIDE the flag and ask the board
    (isWatchdogArmed()), not the build flag.

Each check is a function over source text so the regression shapes can be
pinned on synthetic input as well as on the real file. Multi-line arm
conditions are joined, because the status arm's key sits on its second line.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "src", "helpers", "CommonCLI.cpp")

HANG_KEY = '"wdt hang"'
STATUS_KEY = '"wdt status"'
HANG_COPY = '"wdt: hanging the main loop -- expect a TASK_WDT reset in %u s\\n"'
FLAG = "WDT_TEST_HANG"

DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")
LINE_COMMENT = re.compile(r"//.*$", re.M)


def code_only(text):
    return LINE_COMMENT.sub("", text)


def arms(src):
    """Yield (start_line_no, joined_condition_text, inside_flag) per dispatch arm.

    An arm starts on a line containing `else if` + `memcmp(command`; its
    condition may continue on following lines until one ends with `{`.
    """
    stack = []
    lines = src.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        m = DIRECTIVE.match(line)
        if m:
            kind, rest = m.group(1), m.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                stack.append(rest)
            elif kind == "elif":
                if stack:
                    stack.pop()
                stack.append(rest)
            elif kind == "endif":
                if stack:
                    stack.pop()
            i += 1
            continue
        if "else if" in line and "memcmp(command" in line:
            start = i + 1
            text = line.strip()
            while not text.endswith("{") and i + 1 < len(lines):
                i += 1
                text += " " + lines[i].strip()
            yield start, text, any(FLAG in g for g in stack)
        i += 1


def arm_body(src, start_line_no):
    """Lines after the arm's condition up to the next arm or a directive."""
    lines = src.splitlines()
    # skip the (possibly multi-line) condition
    i = start_line_no - 1
    while not lines[i].strip().endswith("{"):
        i += 1
    body = []
    for line in lines[i + 1:]:
        if "} else if" in line or (DIRECTIVE.match(line) and DIRECTIVE.match(line).group(1) == "endif"):
            break
        body.append(line)
    return "\n".join(body)


def hang_arms(src):
    return [a for a in arms(src) if HANG_KEY in a[1]]


def status_arms(src):
    return [a for a in arms(src) if STATUS_KEY in a[1]]


# ------------------------------------------------------------- real file ---

def real():
    with open(CLI, encoding="utf-8") as f:
        return f.read()


def test_hang_verb_exists_once():
    assert len(hang_arms(real())) == 1


def test_hang_verb_is_inside_the_diag_flag():
    (_, _, inside), = hang_arms(real())
    assert inside, "wdt hang must be compiled only under WDT_TEST_HANG"


def test_hang_verb_is_console_only():
    (_, cond, _), = hang_arms(real())
    assert "sender_timestamp == 0" in cond


def test_hang_verb_really_hangs():
    (n, _, _), = hang_arms(real())
    body = code_only(arm_body(real(), n))
    assert "for (;;)" in body
    for forbidden in ("feedWatchdog(", "delay(", "yield(", "vTaskDelay("):
        assert forbidden not in body, forbidden


def test_hang_verb_prints_the_documented_copy_and_flushes_before_hanging():
    (n, _, _), = hang_arms(real())
    body = arm_body(real(), n)
    assert HANG_COPY in body
    assert body.index(HANG_COPY) < body.index("for (;;)")
    assert "Serial.flush()" in body and body.index("Serial.flush()") < body.index("for (;;)")


def test_status_verb_exists_outside_the_flag_and_asks_the_board():
    found = status_arms(real())
    assert len(found) == 1, found
    n, cond, inside = found[0]
    assert not inside, "wdt status must exist in every build"
    assert 'memcmp(command, "wdt", 3) == 0 && command[3] == 0' in cond, "bare `wdt` alias"
    body = code_only(arm_body(real(), n))
    assert "isWatchdogArmed()" in body
    assert FLAG not in body


# --------------------------------------------------------- synthetic shapes ---

UNGUARDED = '''
    } else if (sender_timestamp == 0 && memcmp(command, "wdt hang", 8) == 0) {
      for (;;) { }
    } else if (memcmp(command, "x", 1) == 0) {
'''


def test_synthetic_unguarded_hang_is_detected():
    (_, _, inside), = hang_arms(UNGUARDED)
    assert not inside


GUARDED_BUT_YIELDS = '''
#if defined(WDT_TEST_HANG)
    } else if (sender_timestamp == 0 && memcmp(command, "wdt hang", 8) == 0) {
      for (;;) { delay(1); }
#endif
'''


def test_synthetic_hang_that_yields_is_detected():
    (n, _, inside), = hang_arms(GUARDED_BUT_YIELDS)
    assert inside
    assert "delay(" in code_only(arm_body(GUARDED_BUT_YIELDS, n))


COMMENT_ONLY_DELAY = '''
#if defined(WDT_TEST_HANG)
    } else if (sender_timestamp == 0 && memcmp(command, "wdt hang", 8) == 0) {
      // no delay() here -- delay() yields
      for (;;) { }
#endif
'''


def test_synthetic_comment_mentioning_delay_is_not_a_call():
    (n, _, _), = hang_arms(COMMENT_ONLY_DELAY)
    assert "delay(" not in code_only(arm_body(COMMENT_ONLY_DELAY, n))


MULTILINE_STATUS = '''
    } else if ((memcmp(command, "wdt", 3) == 0 && command[3] == 0)
               || (memcmp(command, "wdt status", 10) == 0 && (command[10] == 0 || command[10] == ' '))) {
      if (_board->isWatchdogArmed()) { }
    } else if (memcmp(command, "x", 1) == 0) {
'''


def test_synthetic_multiline_condition_is_joined():
    found = status_arms(MULTILINE_STATUS)
    assert len(found) == 1
    assert "isWatchdogArmed()" in arm_body(MULTILINE_STATUS, found[0][0])


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("ok   %s" % name)
            except AssertionError as e:
                failures += 1
                print("FAIL %s: %s" % (name, e))
    print("%d failure(s)" % failures)
    sys.exit(1 if failures else 0)
