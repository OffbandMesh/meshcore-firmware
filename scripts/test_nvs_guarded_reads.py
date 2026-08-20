#!/usr/bin/env python3
"""Tests for the unguarded-getString guard (#899).

Pure. No device, no NVS, no build.

`ConfigSchema.cpp` cannot be unit-tested on the host: `readBrokerConfig` lives
inside `#ifdef ARDUINO` and needs the real `Preferences` class, and no host shim
for it exists in this tree (the "host round-trip test" mentioned in that file's
comments is aspirational -- nothing references it). Extracting a seam is the
#714 testable-surface problem and is not this task.

So the fix is proven two ways instead, and this file is the first:

  1. this static guard, shown RED on the pre-fix tree (13 findings) and green
     after -- per #712, a guard must be demonstrated catching the real defect
  2. hardware re-verification on ST-P: the NVS ERROR rate must go to 0/sec

Every "bypass" case below came from adversarial review of the FIRST version of
this guard, which each of them defeated.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util

_spec = importlib.util.spec_from_file_location(
    "check_nvs", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "check_nvs_guarded_reads.py"))
check_nvs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(check_nvs)


# The guard skips any translation unit that never NAMES Preferences, so every
# fixture has to look like one. Appended at the END so it cannot shift the line
# numbers of the fixture above it, and as real code rather than a comment
# because comments are blanked before matching.
_TU = "\nPreferences _tu;\n"


def kinds(src):
    return [f["kind"] for f in check_nvs.analyze_source(src + _TU, "t.cpp")]


# ------------------------------------------------------- the defect itself ---

def test_a_bare_getString_is_a_finding():
    """The shape that produced 29.3 ERROR lines/sec on ST-P."""
    src = 'void f(){ String u = p.getString(kKeyBrokerUsername, ""); }'
    assert kinds(src) == ["unguarded-getString"], kinds(src)


def test_the_helper_call_is_not_a_finding():
    src = 'void f(){ String u = prefStr(p, kKeyBrokerUsername); }'
    assert kinds(src) == [], kinds(src)


# ------------------------------------------------------------- bypasses ------
# Each of these defeated the first version of this guard.

def test_a_pointer_receiver_is_caught():
    """`->` is the same defect. Matching only the dot operator meant one
    keystroke disabled the guard."""
    src = 'void f(){ String s = pp->getString("bar", ""); }'
    assert "unguarded-getString" in kinds(src), kinds(src)


def test_a_call_split_across_lines_is_caught():
    """A per-line search cannot see this. The scan runs over the whole text."""
    src = 'void f(){ String v = p.\n    getString(kKeyMqttIata, ""); }'
    assert "unguarded-getString" in kinds(src), kinds(src)


def test_a_macro_hiding_the_call_is_reported():
    """A macro cannot be resolved without a preprocessor, so it is REPORTED
    rather than silently missed -- the guard says what it cannot check."""
    src = '#define GET_PREF(p, k) p.getString(k, "")\nvoid f(){ GET_PREF(p, kX); }'
    assert "macro-hides-getString" in kinds(src), kinds(src)


def test_a_fake_helper_cannot_forge_the_exemption_with_a_comment():
    """The exemption requires the TYPE CHECK in the definition. Comments are
    blanked before matching, so mentioning PT_STR in a comment cannot buy it --
    otherwise renaming any function to prefStr would disable the guard for
    every caller."""
    src = ('// this comment mentions PT_STR and isKey to fool the guard\n'
           'inline String prefStr(Preferences& p, const char* key, const char* d) {\n'
           '    return p.getString(key, d);\n}')
    assert "unguarded-getString" in kinds(src), kinds(src)


def test_the_real_helper_IS_exempt():
    """The genuine article, with its type check, is the one allowed bare call."""
    src = ('inline String prefStr(Preferences& p, const char* key, const char* d) {\n'
           '    return p.getType(key) == PT_STR ? p.getString(key, d) : String(d);\n}')
    assert kinds(src) == [], kinds(src)


# --------------------------------------------------------- false positives ---

def test_HTTPClient_getString_is_NOT_flagged():
    """`http.getString()` takes NO arguments and is a real call in
    examples/simple_repeater/main.cpp. A repo-wide guard that flagged it would
    be wrong forever, which is how guards get switched off. The non-empty
    argument list is what separates a Preferences read from this."""
    src = 'void f(){ HTTPClient http; String body = http.getString(); }'
    assert kinds(src) == [], kinds(src)


def test_a_commented_out_call_is_not_a_finding():
    """Comments are blanked before matching, so dead code cannot fail the build."""
    src = 'void f(){ // String u = p.getString(kKey, "");\n }'
    assert kinds(src) == [], kinds(src)


def test_a_block_commented_call_is_not_a_finding():
    src = 'void f(){ /* String u = p.getString(kKey, ""); */ }'
    assert kinds(src) == [], kinds(src)


def test_line_numbers_survive_comment_stripping():
    """Comments are blanked length-preservingly rather than removed, because
    findings are located by counting newlines to a match offset -- deleting
    characters would misreport every line after the first comment."""
    src = ('// leading comment\n'
           '/* block\n   spanning lines */\n'
           'void f(){ String u = p.getString(kKey, ""); }\n')
    f = check_nvs.analyze_source(src + _TU, "t.cpp")
    assert len(f) == 1 and f[0]["line"] == 4, f


def test_a_slash_slash_inside_a_STRING_does_not_blank_the_line():
    """Found by adversarial review, and it was a silent MISS -- the worst kind.

        const char* url = "http://example.com"; auto v = p.getString(k);

    The `//` inside the URL started a comment, blanking the rest of the line
    including a real violation, and the guard reported the file clean."""
    src = 'void f(){ const char* url = "http://x.com"; auto v = p.getString("k"); }'
    assert "unguarded-getString" in kinds(src), kinds(src)


def test_a_char_literal_slash_does_not_blank_the_line():
    src = "void f(){ char c = '/'; auto v = p.getString(\"k\"); }"
    assert "unguarded-getString" in kinds(src), kinds(src)


def test_an_escaped_quote_inside_a_string_does_not_desync_the_scanner():
    """A mishandled escape would leave the scanner believing it is still inside
    a string, swallowing the remainder of the file."""
    src = 'void f(){ const char* s = "a\\"b"; auto v = p.getString("k"); }'
    assert "unguarded-getString" in kinds(src), kinds(src)


def test_an_unrelated_class_with_its_own_getString_is_NOT_flagged():
    """Repo-wide scanning made `getString(args)` stop being proof of an NVS
    read. A file that never names Preferences cannot hold one, and flagging it
    would be wrong forever -- which is how guards get switched off."""
    src = ('class MyJsonParser { public: String getString(const char* key); };\n'
           'void f(){ MyJsonParser parser; String n = parser.getString("user"); }')
    # deliberately NOT passed through kinds(): no Preferences in this unit
    assert check_nvs.analyze_source(src, "t.cpp") == [], \
        check_nvs.analyze_source(src, "t.cpp")


def test_the_same_call_IS_flagged_once_the_unit_uses_Preferences():
    """The skip must be scoped to units with nothing to do with NVS, not act as
    a blanket escape hatch."""
    src = 'void f(){ Preferences p; String n = p.getString("user"); }'
    assert "unguarded-getString" in [f["kind"] for f in
                                     check_nvs.analyze_source(src, "t.cpp")]


# -------------------------------------------------------------- reporting ---

def test_a_finding_names_the_receiver_and_points_at_the_helper():
    f = check_nvs.analyze_source('void f(){ String u = p.getString(k, ""); }' + _TU,
                                 "t.cpp")
    assert "p.getString" in f[0]["detail"], f
    assert "prefStr" in f[0]["detail"], f


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as e:
                failures += 1
                print(f"FAIL {name}: {e}")
            except Exception as e:
                failures += 1
                print(f"ERROR {name}: {type(e).__name__}: {e}")
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
