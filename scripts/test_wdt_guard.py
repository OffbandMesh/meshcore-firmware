#!/usr/bin/env python3
"""Tests for check_wdt_guard (#1083).

Pure, no hardware, no compiler. Run: python scripts/test_wdt_guard.py
  or python -m pytest scripts/test_wdt_guard.py

The regression case is the repeater's setup()/loop() as they stood on
firmware-base before #1083, verbatim in shape. The false-positive cases pin the
things a naive "any #if around the call" check would wrongly flag: feature
guards, the nRF52 heartbeat that legitimately stays platform-specific, and a
call that has left the guard while its sibling stayed inside.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_wdt_guard as g


def lines(src):
    return sorted((f["line"], f["call"]) for f in g.analyze_source(src))


# ------------------------------------------------------------ the real bug ---

REGRESSION = '''
  board.onBootComplete();

#if defined(NRF52_PLATFORM)
  // #266: start the hardware watchdog after boot/init ...
  board.startWatchdog(30);
  board.startHeartbeat();
#endif
}

void loop() {
  offband::crashLogStandardTick(millis());
#if defined(NRF52_PLATFORM)
  board.feedWatchdog();  // #266: feed from the MAIN LOOP only
  board.heartbeatTick();
#endif
'''


def test_catches_the_shape_that_left_esp32_unarmed():
    assert lines(REGRESSION) == [(6, "startWatchdog"), (14, "feedWatchdog")]


def test_names_the_guard():
    guards = {f["call"]: f["guard"] for f in g.analyze_source(REGRESSION)}
    assert guards["startWatchdog"] == "#if defined(NRF52_PLATFORM)"
    assert guards["feedWatchdog"] == "#if defined(NRF52_PLATFORM)"


# ---------------------------------------------------------- repaired form ---

REPAIRED = '''
  board.onBootComplete();

  board.startWatchdog(WDT_TIMEOUT_SECS);
#if defined(NRF52_PLATFORM)
  board.startHeartbeat();
#endif
}

void loop() {
  offband::crashLogStandardTick(millis());
  board.feedWatchdog();  // #266/#1083: feed from the MAIN LOOP only
#if defined(NRF52_PLATFORM)
  board.heartbeatTick();
#endif
'''


def test_repaired_form_is_clean():
    assert lines(REPAIRED) == []


# ---------------------------------------------------- not platform guards ---

def test_feature_guard_is_not_a_platform_guard():
    src = '''
#ifdef DISPLAY_CLASS
  board.feedWatchdog();
#endif
#if ENV_INCLUDE_GPS == 1
  board.startWatchdog(WDT_TIMEOUT_SECS);
#endif
'''
    assert lines(src) == []


def test_nested_feature_inside_platform_is_still_flagged():
    src = '''
#if defined(NRF52_PLATFORM)
  #ifdef DISPLAY_CLASS
  board.feedWatchdog();
  #endif
#endif
'''
    assert lines(src) == [(4, "feedWatchdog")]


def test_else_branch_of_a_platform_guard_is_flagged():
    """`#else` of a platform test is still platform-conditional."""
    src = '''
#if defined(ESP32)
  something();
#else
  board.feedWatchdog();
#endif
'''
    assert lines(src) == [(5, "feedWatchdog")]
    assert g.analyze_source(src)[0]["guard"] == "#else of #if defined(ESP32)"


def test_elif_replaces_the_condition():
    src = '''
#if defined(DISPLAY_CLASS)
  x();
#elif defined(NRF52_PLATFORM)
  board.feedWatchdog();
#endif
'''
    assert lines(src) == [(5, "feedWatchdog")]


def test_endif_pops_back_out():
    src = '''
#if defined(NRF52_PLATFORM)
  board.startHeartbeat();
#endif
  board.feedWatchdog();
'''
    assert lines(src) == []


def test_arch_macros_count_as_platform():
    src = '''
#ifdef ARDUINO_ARCH_ESP32
  board.feedWatchdog();
#endif
'''
    assert lines(src) == [(3, "feedWatchdog")]


def test_escape_hatch():
    src = '''
#if defined(NRF52_PLATFORM)
  board.feedWatchdog();   // wdt-guard: allow-platform sleep-entry feed, nRF52 only
#endif
'''
    assert lines(src) == []


def test_declaration_in_a_class_is_not_a_call_site_we_care_about_but_is_harmless():
    # The base-class declarations live in MeshCore.h, not in a role main.cpp;
    # if a main.cpp ever declared its own, unguarded is the only clean shape.
    src = "virtual void startWatchdog(uint32_t timeout_secs) { (void)timeout_secs; }"
    assert lines(src) == []


# ----------------------------------------- what review found it could not see ---
#
# #1268: an adversarial review of the first version defeated it two ways, both
# reproduced before fixing. These pin the fix AND the line numbers, because a
# blocking check that points at the wrong line wastes the time it was meant to
# save. Raw strings: in an ordinary literal a trailing backslash would be eaten
# by Python and the scanner would never see it.

def test_line_continued_if_is_one_directive_and_line_numbers_hold():
    src = r'''
#if \
    defined(NRF52_PLATFORM)
  board.startWatchdog(30);
#endif
  x();
#if defined(ESP32)
  board.feedWatchdog();
#endif
'''
    # 4 and 8 are the PHYSICAL lines of the calls: the splice at 2-3 must not
    # shift anything after it.
    assert lines(src) == [(4, "startWatchdog"), (8, "feedWatchdog")]


def test_continuation_in_the_middle_of_the_expression():
    src = r'''
#if defined( \
    NRF52_PLATFORM)
  board.feedWatchdog();
#endif
'''
    assert lines(src) == [(4, "feedWatchdog")]


def test_platform_test_behind_a_same_file_macro():
    src = '''
#define ON_NRF defined(NRF52_PLATFORM)
void setup() {
#if ON_NRF
  board.startWatchdog(30);
#endif
}
'''
    assert lines(src) == [(5, "startWatchdog")]
    assert g.analyze_source(src)[0]["guard"] == "#if ON_NRF"


def test_alias_of_an_alias():
    src = '''
#define IS_NORDIC defined(NRF52_PLATFORM)
#define SLEEPY_BOARD IS_NORDIC
#if SLEEPY_BOARD
  board.feedWatchdog();
#endif
'''
    assert lines(src) == [(5, "feedWatchdog")]


def test_function_like_alias():
    src = '''
#define ON_NRF() defined(NRF52_PLATFORM)
#if ON_NRF()
  board.feedWatchdog();
#endif
'''
    assert lines(src) == [(4, "feedWatchdog")]


def test_a_feature_macro_is_still_not_a_platform_guard():
    """The new alias rule must not turn every macro-tested block into a finding."""
    src = '''
#define HAS_GPS (ENV_INCLUDE_GPS == 1)
#if HAS_GPS
  board.feedWatchdog();
#endif
'''
    assert lines(src) == []


def test_alias_matches_whole_names_only():
    src = '''
#define ON_NRF defined(NRF52_PLATFORM)
#if ON_NRF_DISPLAY
  board.feedWatchdog();
#endif
'''
    assert lines(src) == []


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
