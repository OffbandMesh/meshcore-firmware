#!/usr/bin/env python3
"""Tests for the bench-only self-hang (WDT_TEST_HANG_AFTER_MS).

Pure, no hardware, no compiler. Run: python scripts/test_wdt_self_hang.py
  or python -m pytest scripts/test_wdt_self_hang.py

The companion role has no console verb path, so `wdt hang` (#1159) cannot reach
it; the self-hang is the trigger that works on every role. These checks pin:

  * MeshCore.h defines wdtTestHangDue() and wdtTestHangTick(), and the
    hang-capable bodies sit only under a WDT_TEST_HANG_AFTER_MS conditional;
  * every role main.cpp calls mesh::wdtTestHangTick() exactly once, on the line
    immediately after board.feedWatchdog() -- adjacent on purpose;
  * the tick really spins: `for (;;)` with no feed/delay/yield in its code;
  * the documented console line is printed and flushed before the spin.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MESHCORE = os.path.join(ROOT, "src", "MeshCore.h")
ROLES = [
    "examples/simple_repeater/main.cpp",
    "examples/companion_radio/main.cpp",
    "examples/simple_room_server/main.cpp",
    "examples/simple_sensor/main.cpp",
]
FLAG = "WDT_TEST_HANG_AFTER_MS"
COPY = '"wdt: self-hang at %lu ms (WDT_TEST_HANG_AFTER_MS) -- expect a TASK_WDT reset in %u s\\n"'
CALL = "mesh::wdtTestHangTick();"
FEED = "board.feedWatchdog();"

LINE_COMMENT = re.compile(r"//.*$", re.M)


def code_only(text):
    return LINE_COMMENT.sub("", text)


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


TICK_SIG = "inline void wdtTestHangTick()"
DUE_SIG = "inline bool wdtTestHangDue(uint32_t now_ms)"


def function_body(src, signature):
    """Text of the function whose DEFINITION starts with `signature`, from its
    opening brace to the matching close brace. Anchors on the signature, not the
    bare name, because the name also appears in comments above the definition."""
    start = src.index(signature)
    open_brace = src.index("{", start)
    depth = 0
    for i in range(open_brace, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[open_brace:i + 1]
    raise AssertionError("unbalanced braces in " + name)


# ---------------------------------------------------------------- MeshCore.h ---

def test_helpers_exist_in_meshcore():
    src = read("src/MeshCore.h")
    assert "inline bool wdtTestHangDue(uint32_t now_ms)" in src
    assert "inline void wdtTestHangTick()" in src


def test_predicate_is_false_without_the_flag():
    body = code_only(function_body(read("src/MeshCore.h"), DUE_SIG))
    assert "#if defined(%s)" % FLAG in body
    assert "#else" in body and "return false;" in body.split("#else", 1)[1]


def test_tick_hangs_only_under_the_flag():
    body = code_only(function_body(read("src/MeshCore.h"), TICK_SIG))
    assert "#if defined(%s)" % FLAG in body
    guarded = body.split("#if defined(%s)" % FLAG, 1)[1].split("#endif", 1)[0]
    assert "for (;;)" in guarded
    for forbidden in ("feedWatchdog(", "delay(", "yield(", "vTaskDelay("):
        assert forbidden not in guarded, forbidden
    # nothing hang-capable outside the guard
    outside = body.replace(guarded, "")
    assert "for (;;)" not in outside


def test_tick_prints_the_documented_copy_and_flushes_before_spinning():
    body = function_body(read("src/MeshCore.h"), TICK_SIG)
    assert COPY in body
    assert body.index(COPY) < body.index("for (;;)")
    assert "Serial.flush()" in body and body.index("Serial.flush()") < body.index("for (;;)")


# --------------------------------------------------------------- role loops ---

def test_every_role_calls_the_tick_exactly_once():
    for rel in ROLES:
        assert code_only(read(rel)).count(CALL) == 1, rel


def test_tick_sits_on_the_line_after_the_feed():
    for rel in ROLES:
        lines = [code_only(l).strip() for l in read(rel).splitlines()]
        feeds = [i for i, l in enumerate(lines) if l.startswith(FEED)]
        assert len(feeds) >= 1, rel
        # the loop-top feed is the LAST unguarded feed in the file for the roles that
        # also feed at sleep entry; require that at least one feed is directly followed
        # by the tick, and that the tick is never anywhere else
        adjacent = [i for i in feeds if i + 1 < len(lines) and lines[i + 1].startswith(CALL)]
        assert len(adjacent) == 1, (rel, feeds)


# ---------------------------------------------------------- synthetic shapes ---

def test_synthetic_tick_without_guard_is_detected():
    src = '''
inline void wdtTestHangTick() {
  if (wdtTestHangDue(millis())) { for (;;) { } }
}
'''
    body = code_only(function_body(src, TICK_SIG))
    assert "#if defined(%s)" % FLAG not in body


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
