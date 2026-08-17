#!/usr/bin/env python3
"""Tests for check_cli_dispatch (#764).

Pure, no hardware, no compiler. Run: python scripts/test_cli_dispatch.py
  or python -m pytest scripts/test_cli_dispatch.py

Half of these tests exist because a first, naive version of this guard reported
four findings that were not bugs: a deliberately empty body, two arms separated
by an arity check, and the same key used in genuinely unrelated chains. A guard
that cries wolf gets switched off, so the false-positive cases are pinned here
just as firmly as the true ones.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_cli_dispatch as g


def kinds(src):
    return sorted(f["kind"] for f in g.analyze_source(src))


def keys(src, kind):
    return sorted(k for f in g.analyze_source(src) if f["kind"] == kind for k in f["keys"])


# ------------------------------------------------------------ the real bug ---

REGRESSION = '''
void handleGetCmd(char* config, char* reply) {
  if (isKey(config, "af")) {
    sprintf(reply, "> %s", af);
  } else if (isKey(config, "radio")) {
  } else if (memcmp(config, "radio", 5) == 0) {
    sprintf(reply, "> %s,%s", freq, bw);
  }
}
'''


def test_catches_the_shape_that_broke_get_radio():
    """The #764 regression verbatim: guard hoisted, body left behind."""
    assert kinds(REGRESSION) == ["empty-body", "shadowed-key"]


def test_names_the_offending_key():
    assert keys(REGRESSION, "empty-body") == ["radio"]
    assert keys(REGRESSION, "shadowed-key") == ["radio"]


def test_reports_the_line_of_the_empty_arm():
    empty = [f for f in g.analyze_source(REGRESSION) if f["kind"] == "empty-body"][0]
    assert REGRESSION.splitlines()[empty["line"] - 1].strip().startswith("} else if")


def test_repaired_form_is_clean():
    """What the fix looks like: body restored, shadowed duplicate removed."""
    assert kinds('''
    if (isKey(config, "af")) {
      sprintf(reply, "> %s", af);
    } else if (isKey(config, "radio")) {
      sprintf(reply, "> %s,%s", freq, bw);
    }
    ''') == []


def test_whitespace_only_body_is_still_empty():
    assert "empty-body" in kinds('''
    if (isKey(config, "a")) { x(); } else if (isKey(config, "radio")) {

    }
    ''')


def test_braceless_empty_arm_is_caught():
    assert "empty-body" in kinds('if (isKey(c, "a")) x(); else if (isKey(c, "radio")) ;')


# ------------------------------------------------------ must NOT fire on ... ---

def test_deliberate_empty_body_without_a_key_is_ignored():
    """CommonCLI.cpp:1041 -- the arm calls a function that fills `reply` itself.
    No string key, so it is not a dispatch arm and is none of our business."""
    assert kinds('''
    if (!wifi_is_persistent()) {
      strcpy(reply, "ERR");
    } else if (!_board->startOTAUpdateOverSTA(name, password, reply)) {
    }
    ''') == []


def test_arity_guard_prevents_shadowing():
    """CommonCLI.cpp:1883/1891 -- `home` twice, split by argument count. Both
    arms are live; flagging either would be wrong."""
    assert kinds('''
    if (n >= 3 && strcmp(parts[1], "home") == 0) {
      setHome(parts[2]);
    } else if (n == 2 && strcmp(parts[1], "home") == 0) {
      getHome();
    }
    ''') == []


def test_same_key_in_separate_chains_is_fine():
    """companion MyMesh.cpp -- `ExtraFS/` appears in three unrelated handlers."""
    assert kinds('''
    void a() {
      if (memcmp(path, "UserData/", 9) == 0) { path += 8; }
      else if (memcmp(path, "ExtraFS/", 8) == 0) { path += 7; }
    }
    void b() {
      if (memcmp(path, "UserData/", 9) == 0) { path += 8; }
      else if (memcmp(path, "ExtraFS/", 8) == 0) { path += 7; }
    }
    ''') == []


def test_different_subjects_do_not_collide():
    """`reboot` matched on cmd_frame in one chain and cli_command in another."""
    assert kinds('''
    if (cmd_frame[0] == CMD_REBOOT && memcmp(&cmd_frame[1], "reboot", 6) == 0) {
      board.reboot();
    } else if (strcmp(cli_command, "reboot") == 0) {
      board.reboot();
    }
    ''') == []


def test_allow_empty_pragma_is_honoured():
    assert kinds('''
    if (isKey(config, "a")) { x(); }
    else if (isKey(config, "drain")) {   // cli-dispatch-guard: allow-empty consumed upstream
    }
    ''') == []


def test_distinct_keys_are_not_shadowed():
    assert kinds('''
    if (isKey(config, "radio.rxgain")) { a(); }
    else if (isKey(config, "radio")) { b(); }
    else if (isKey(config, "radio.fem.rxgain")) { c(); }
    ''') == []


def test_extra_conjunct_does_not_claim_the_key():
    """`sender_timestamp == 0 && isKey(config, "prv.key")` -- serial-only arms
    claim part of the key space, so a later general arm stays reachable."""
    assert kinds('''
    if (sender_timestamp == 0 && isKey(config, "prv.key")) { a(); }
    else if (isKey(config, "prv.key")) { b(); }
    ''') == []


# ------------------------------------------------------------- mechanics ----

def test_comments_cannot_fake_a_body():
    """A comment is not code: an arm whose only content is a comment writes
    nothing, and must be reported."""
    assert "empty-body" in kinds('''
    if (isKey(config, "a")) { x(); } else if (isKey(config, "radio")) {
      // TODO: fill this in
    }
    ''')


def test_braces_in_comments_do_not_confuse_the_parser():
    assert kinds('''
    if (isKey(config, "a")) {
      /* } else if (isKey(config, "radio")) { */
      x();
    }
    ''') == []


def test_nested_chain_is_analysed():
    assert "empty-body" in kinds('''
    if (ready) {
      if (isKey(config, "a")) { x(); }
      else if (isKey(config, "radio")) { }
    }
    ''')


def test_tests_only_key_distinguishes_bare_from_guarded():
    assert g.tests_only_key('isKey(config, "radio")', "radio")
    assert g.tests_only_key('memcmp(config, "radio", 5) == 0', "radio")
    assert not g.tests_only_key('n == 2 && strcmp(parts[1], "radio") == 0', "radio")


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
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
