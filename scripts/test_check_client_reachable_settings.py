#!/usr/bin/env python3
"""Self-test for scripts/check_client_reachable_settings.py (#1792).

The real tree is clean, so a passing run on it proves only that the guard does not
false-positive. These tests plant the failures it exists to catch -- a new setting the
screens write with no client path, a client path deleted by a refactor, a non-NodePrefs
writer nobody mapped -- in a throwaway tree, and prove each one trips it. A guard nobody
has watched trip is one nobody knows is wired.

Run: python scripts/test_check_client_reachable_settings.py
"""
from __future__ import annotations
import contextlib
import io
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_client_reachable_settings as g

EVIDENCE_FILES = {rel for entries in g.REACHABLE.values() for rel, _ in entries}


def make_tree(root: Path, planted: dict[str, str], extra_member: str = "",
              drop_from: tuple[str, str] | None = None) -> Path:
    """The real NodePrefs.h and evidence files, plus planted screen sources.

    `extra_member` appends a NodePrefs member, as a new setting would. `drop_from`
    removes one exact substring from one evidence file, as a refactor might."""
    prefs = (g.ROOT / g.NODEPREFS_REL).read_text(encoding="utf-8", errors="replace")
    if extra_member:
        prefs = prefs.replace("private:", f"  uint8_t {extra_member} = 0;\nprivate:", 1)
    (root / g.NODEPREFS_REL).parent.mkdir(parents=True, exist_ok=True)
    (root / g.NODEPREFS_REL).write_text(prefs, encoding="utf-8")
    for rel in EVIDENCE_FILES:
        text = (g.ROOT / rel).read_text(encoding="utf-8", errors="replace")
        if drop_from and drop_from[0] == rel:
            assert drop_from[1] in text, f"test setup: {drop_from[1]!r} not in {rel}"
            text = text.replace(drop_from[1], "/* removed */")
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(text, encoding="utf-8")
    for name, text in planted.items():
        path = root / "examples" / "companion_radio" / "ui-new" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return root


def run(planted: dict[str, str], **kw) -> tuple[int, list[str]]:
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), planted, **kw)
        problems = g.check(root)
        with contextlib.redirect_stdout(io.StringIO()):
            code = g.main(root)
        return code, problems


# ---- the real tree -------------------------------------------------------------------

def test_real_tree_is_clean():
    assert g.check() == [], g.check()


def test_real_tree_sees_every_setting_epic_1788_found():
    # The three gaps found by hand, plus the screen settings: if the scan stops seeing
    # any of these, it has gone blind, and "clean" would mean nothing.
    found = g.screen_writes()
    for setting in ("gps mode", "channel tone", "custom tune slots", "event tones (ui_tone_*)",
                    "ui_text_size", "ui_msg_text_size", "ui_screen_secs", "ui_tz",
                    "batt_full_mv", "notify_scope", "gps_enabled"):
        assert setting in found, f"{setting} not seen in the screens: {sorted(found)}"


def test_every_reachable_entry_has_evidence_in_the_real_tree():
    for setting in g.REACHABLE:
        assert g.missing_evidence(setting) == [], (setting, g.missing_evidence(setting))


def test_every_exemption_carries_a_reason():
    for setting, reason in g.EXEMPT.items():
        assert len(reason.split()) >= 4, f"{setting}: an exemption needs a reason, got {reason!r}"


# ---- what it must catch --------------------------------------------------------------

def test_a_new_pref_the_screens_write_with_no_client_path_fails():
    code, problems = run({"Planted.cpp": "void f(NodePrefs* prefs) { prefs->ui_new_knob = 3; }\n"},
                         extra_member="ui_new_knob")
    assert code == 1
    assert any(p.startswith("ui_new_knob:") for p in problems), problems


def test_an_in_place_update_counts_as_a_write():
    code, problems = run({"Planted.cpp": "void f(NodePrefs* p) { p->ui_new_knob++; }\n"},
                         extra_member="ui_new_knob")
    assert code == 1 and any("ui_new_knob" in p for p in problems), problems


def test_an_exempted_setting_passes():
    g.EXEMPT["ui_new_knob"] = "a test exemption with a real reason attached"
    try:
        code, problems = run({"Planted.cpp": "void f(NodePrefs* p) { p->ui_new_knob = 1; }\n"},
                             extra_member="ui_new_knob")
    finally:
        del g.EXEMPT["ui_new_knob"]
    assert code == 0 and problems == [], problems


def test_a_deleted_client_path_fails_even_though_the_screen_still_writes():
    code, problems = run({"Planted.cpp": "void f(NodePrefs* p) { p->ui_tz = 2; }\n"},
                         drop_from=(g.MYMESH, "_ui->clientSetTimeZone("))
    assert code == 1
    assert any(p.startswith("ui_tz: client path evidence gone") for p in problems), problems


def test_an_unmapped_writer_call_fails():
    saved = g.REACHABLE.pop("channel tone")
    try:
        code, problems = run({"Planted.cpp": "void f() { the_mesh.setChannelTune(1, 2); }\n"})
    finally:
        g.REACHABLE["channel tone"] = saved
    assert code == 1 and any(p.startswith("channel tone:") for p in problems), problems


# Each of the write shapes below was a blind spot in the first version (review, #1792).

def _knob_caught(src: str, name: str = "Planted.cpp") -> list[str]:
    code, problems = run({name: src}, extra_member="ui_new_knob")
    assert code == 1, f"not caught: {src!r}"
    return problems


def test_a_write_through_a_reference_is_caught():
    _knob_caught("void f(NodePrefs& prefs) { prefs.ui_new_knob = 5; }\n")


def test_a_write_split_across_lines_is_caught():
    _knob_caught("void f(NodePrefs* prefs) {\n  prefs->ui_new_knob\n    = 300;\n}\n")


def test_a_memcpy_into_a_member_is_caught():
    _knob_caught("void f(NodePrefs* p) { uint8_t v = 1; memcpy(&p->ui_new_knob, &v, 1); }\n")


def test_a_compound_shift_is_caught():
    _knob_caught("void f(NodePrefs* p) { p->ui_new_knob <<= 1; }\n")


def test_a_write_in_an_inline_header_body_is_caught():
    _knob_caught("struct S { void f(NodePrefs* p) { p->ui_new_knob = 2; } };\n", "Planted.h")


def test_a_pointer_to_member_write_is_reported_unattributed():
    code, problems = run({"Planted.cpp": "void f(NodePrefs* p, uint8_t NodePrefs::*m) { p->*m = 1; }\n"})
    assert code == 1 and any(p.startswith(g.UNATTRIBUTED) for p in problems), problems


def test_a_write_after_a_string_holding_slashes_is_still_seen():
    # The first comment stripper read `//` inside the string as a comment and ate the line.
    _knob_caught('void f(NodePrefs* p) { log("http://x"); p->ui_new_knob = 1; }\n')


def test_the_line_number_reported_is_the_writes_own():
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"Planted.cpp": "/* one\n two */\n// three\nvoid f(NodePrefs* p) {\n"
                                                  "  p->ui_new_knob = 1;\n}\n"},
                         extra_member="ui_new_knob")
        assert g.screen_writes(root)["ui_new_knob"] == [
            "examples/companion_radio/ui-new/Planted.cpp:5"], g.screen_writes(root)


def test_a_write_after_a_raw_string_holding_slashes_is_still_seen():
    _knob_caught('void f(NodePrefs* p) { log(R"(http://x)"); p->ui_new_knob = 1; }\n')


def test_a_members_address_handed_to_a_helper_counts_as_a_write():
    _knob_caught("void f(NodePrefs* p) { set_value(&p->ui_new_knob, 1); }\n")


def test_a_signed_member_is_parsed_and_its_write_caught():
    code, problems = run({"Planted.cpp": "void f(NodePrefs* p) { p->ui_new_knob = -3; }\n"},
                         extra_member="ui_new_knob")
    assert code == 1
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {})
        prefs = (root / g.NODEPREFS_REL).read_text(encoding="utf-8")
        (root / g.NODEPREFS_REL).write_text(
            prefs.replace("private:", "  int8_t ui_signed_knob = 0;\n  uint8_t ui_camelKnob = 0;\nprivate:", 1),
            encoding="utf-8")
        members = g.nodeprefs_members(root)
        assert "ui_signed_knob" in members and "ui_camelKnob" in members, sorted(members)


def test_a_call_whose_result_is_kept_is_still_a_call():
    with tempfile.TemporaryDirectory() as d:
        found = g.screen_writes(make_tree(Path(d), {
            "Planted.cpp": "void f() { auto res = the_mesh.setChannelTune(1, 2); }\n"}))
        assert any("Planted.cpp" in w for w in found.get("channel tone", [])), found


def test_an_unregistered_writer_the_screens_call_fails():
    code, problems = run({"Planted.cpp": "void f() { the_mesh.setSomethingNew(4); }\n"})
    assert code == 1 and any("unregistered writer: setSomethingNew" in p for p in problems), problems


def test_every_not_a_setting_entry_carries_a_reason():
    for name, reason in g.NOT_A_SETTING.items():
        assert len(reason.split()) >= 4, f"{name}: needs a reason, got {reason!r}"


def test_evidence_left_only_in_a_trailing_comment_fails():
    code, problems = run({"Planted.cpp": "void f(NodePrefs* p) { p->ui_tz = 2; }\n"},
                         drop_from=(g.UI_NEW, "_node_prefs->ui_tz = tz;"))
    assert code == 1, problems
    # And the same line kept alive only as a trailing comment still does not count.
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"Planted.cpp": "void f(NodePrefs* p) { p->ui_tz = 2; }\n"},
                         drop_from=(g.UI_NEW, "_node_prefs->ui_tz = tz;"))
        path = root / g.UI_NEW
        path.write_text(path.read_text(encoding="utf-8") + "\nint x; // _node_prefs->ui_tz = tz;\n",
                        encoding="utf-8")
        assert any(p.startswith("ui_tz: client path evidence gone") for p in g.check(root))


def test_evidence_inside_if_0_does_not_count():
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"Planted.cpp": "void f(NodePrefs* p) { p->ui_tz = 2; }\n"},
                         drop_from=(g.UI_NEW, "_node_prefs->ui_tz = tz;"))
        path = root / g.UI_NEW
        path.write_text(path.read_text(encoding="utf-8") + "\n#if 0\n  _node_prefs->ui_tz = tz;\n#endif\n",
                        encoding="utf-8")
        assert any(p.startswith("ui_tz: client path evidence gone") for p in g.check(root))


def test_emptying_a_client_hook_body_fails():
    # The handler and the hook's signature are still there; the write is not.
    code, problems = run({"Planted.cpp": "void f(NodePrefs* p) { p->ui_tz = 2; }\n"},
                         drop_from=(g.UI_NEW, "_node_prefs->ui_tz = tz;"))
    assert code == 1 and any(p.startswith("ui_tz: client path evidence gone") for p in problems), problems


# ---- what it must NOT catch ----------------------------------------------------------

def test_comparisons_are_not_writes():
    for op in ("==", ">=", "<=", "!="):
        code, problems = run({"Planted.cpp": f"bool f(NodePrefs* p) {{ return p->ui_new_knob {op} 1; }}\n"},
                             extra_member="ui_new_knob")
        assert code == 0 and problems == [], (op, problems)


def test_every_write_in_a_comma_statement_is_seen():
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"Planted.cpp": "void f(NodePrefs* p) { p->ui_new_knob = 1, p->ui_tz = 5; }\n"},
                         extra_member="ui_new_knob")
        found = g.screen_writes(root)
        assert any("Planted.cpp" in w for w in found["ui_new_knob"]), found
        assert any("Planted.cpp" in w for w in found["ui_tz"]), found


def test_a_comment_continued_by_a_line_splice_hides_the_next_line_too():
    code, problems = run({"Planted.cpp": "void f(NodePrefs* p) {\n  // gone \\\n  p->ui_new_knob = 2;\n}\n"},
                         extra_member="ui_new_knob")
    assert code == 0 and problems == [], problems


def test_logical_and_is_not_taking_an_address():
    code, problems = run({"Planted.cpp": "bool f(NodePrefs* p) { return p && p->ui_new_knob; }\n"
                                         "bool g2(NodePrefs* p, bool a) { return a &&p->ui_new_knob; }\n"},
                         extra_member="ui_new_knob")
    assert code == 0 and problems == [], problems


def test_a_write_inside_if_0_is_not_a_write():
    code, problems = run({"Planted.cpp": "#if 0\nvoid f(NodePrefs* p) { p->ui_new_knob = 1; }\n#endif\n"},
                         extra_member="ui_new_knob")
    assert code == 0 and problems == [], problems


def test_a_definition_with_any_return_type_is_not_a_call():
    for decl in ("uint8_t setChannelTune(int a, int b) {}\n",
                 "static void\nsetGpsMode(int m) {}\n",
                 "bool MyMesh::setChannelTune(uint8_t idx, uint8_t stored) {}\n"):
        with tempfile.TemporaryDirectory() as d:
            found = g.screen_writes(make_tree(Path(d), {"Planted.cpp": decl}))
            planted = [w for ws in found.values() for w in ws if "Planted.cpp" in w]
            assert not planted, (decl, planted)


def test_a_call_is_a_call_however_it_is_reached():
    for call in ("the_mesh.setChannelTune(1, 2);", "ok = setChannelTune(1, 2);",
                 "return offband::setChannelTune(1, 2);", "if (x) setChannelTune(1, 2);"):
        with tempfile.TemporaryDirectory() as d:
            found = g.screen_writes(make_tree(Path(d), {"Planted.cpp": f"void f() {{ {call} }}\n"}))
            assert "channel tone" in found, (call, found)

def test_a_commented_out_write_is_not_a_write():
    code, problems = run({"Planted.cpp": "// p->ui_new_knob = 1;\n/* p->ui_new_knob = 2; */\n"},
                         extra_member="ui_new_knob")
    assert code == 0 and problems == [], problems


def test_declaring_a_writer_is_not_calling_it():
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"Planted.h": "inline bool setStoredTone(NodePrefs* p, int e, int id);\n"})
        assert "event tones (ui_tone_*)" not in g.screen_writes(root)


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
