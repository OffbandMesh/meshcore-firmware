"""Call-site guard for the keyboardless badge's list navigation (#1884, #1989).

keynav::listTap() is the rule and test_keynav covers it. What no unit test reaches is
whether the three main list screens -- Messages, Contacts, Nearby -- CALL it, with the
real keyboard-fitted state and the real list focus. Drop the call from one screen and a
pyramid can again open only the top row of that list, with every keynav test still green.

Since #1989 the button picks a screen first, then walks that screen's list: a tap with
the focus on the screens either cycles or enters the list, and a tap past the top of the
list leaves it. A screen is always reached with the focus on the screens.

A text tripwire, like test_qcc_call_sites.py: it checks the call is present and fed the
right arguments, not that the screen behaves. A refactor that keeps the text passes.

Run: python scripts/test_badge_list_nav.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCREENS = ROOT / "examples" / "companion_radio" / "ui-new" / "BadgeScreens.cpp"

UITASK = ROOT / "examples" / "companion_radio" / "ui-new" / "UITask.cpp"

# Each screen's own selection and row count, exactly as listTap must be fed them.
LIST_SCREENS = {
    "InboxScreen": r"_sel\s*,\s*count",
    "ContactsScreen": r"_sel\s*,\s*_count",
    "NearbyScreen": r"_sel\s*,\s*\(int\)_list\.count\(\)",
}


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def handler_body(src, screen):
    m = re.search(r"bool\s+%s::handleInput\s*\(" % screen, src)
    if not m:
        return None
    return body_after(src, m)


def body_after(src, m):
    # A function ends at the first brace in column 0, whatever follows it on the line, so a
    # trailing comment cannot stretch one screen's body into the next screen's.
    end = re.search(r"\n\}", src[m.end():])
    return src[m.end():m.end() + end.start() if end else len(src)]


def call_flagged(found, screen):
    """True when the listTap call check itself fired for this screen, not some other check."""
    return any(p.startswith("%s: a button tap no longer goes through keynav::listTap" % screen)
               for p in found)


def problems(src):
    src = strip_comments(src)
    out = []
    for screen, args in LIST_SCREENS.items():
        body = handler_body(src, screen)
        if body is None:
            out.append("%s::handleInput not found" % screen)
            continue
        if not re.search(r"keynav::listTap\s*\(\s*key\s*,\s*_task->hasKeyboard\(\)\s*,\s*"
                         r"_task->listFocused\(\)\s*,\s*%s\s*\)" % args, body):
            out.append("%s: a button tap no longer goes through keynav::listTap with the "
                       "fitted-keyboard state, the list focus and this screen's own selection "
                       "and row count -- a keyboardless badge cannot walk this list" % screen)
        # Each outcome must do its own job: a swapped direction, an unused row or a focus
        # change that never happens is a bug every keynav test would miss.
        wiring = (
            (r"ListTap::EnterList:\s*_task->enterList\(\);\s*select\([^;]*t\.sel\)",
             "EnterList must enterList() and highlight t.sel"),
            (r"ListTap::Select:\s*select\([^;]*t\.sel\)", "Select must highlight t.sel"),
            (r"ListTap::LeaveList:\s*_task->leaveList\(\)", "LeaveList must leaveList()"),
            (r"ListTap::CycleNext:\s*_task->cycle\(1\)", "CycleNext must cycle(1)"),
            (r"ListTap::CyclePrev:\s*_task->cycle\(-1\)", "CyclePrev must cycle(-1)"),
        )
        for pattern, what in wiring:
            if not re.search(pattern, body):
                out.append("%s: %s" % (screen, what))
    return out


def cycle_problems(src):
    """cycle() must reach every screen with the focus on the screens (#1989)."""
    src = strip_comments(src)
    m = re.search(r"void\s+UITask::cycle\s*\(", src)
    if not m:
        return ["UITask::cycle not found"]
    body = body_after(src, m)
    if not re.search(r"\bleaveList\(\)\s*;", body):
        return ["cycle() no longer puts the focus on the screens when it changes screen"]
    return []


def test_the_three_list_screens_walk_their_lists():
    assert problems(SCREENS.read_text(encoding="utf-8")) == []


def test_cycle_arrives_with_the_focus_on_the_screens():
    assert cycle_problems(UITASK.read_text(encoding="utf-8")) == []


def test_negative_swapped_cycle_directions_are_caught():
    src = SCREENS.read_text(encoding="utf-8")
    body = handler_body(src, "ContactsScreen")
    swapped = (body.replace("CycleNext: _task->cycle(1)", "CycleNext: _task->cycle(@)")
                   .replace("CyclePrev: _task->cycle(-1)", "CyclePrev: _task->cycle(1)")
                   .replace("cycle(@)", "cycle(-1)"))
    assert swapped != body, "fixture did not apply"
    found = problems(src.replace(body, swapped))
    assert any("CycleNext must cycle(1)" in p for p in found), found
    assert any("CyclePrev must cycle(-1)" in p for p in found), found


def test_negative_wrong_arguments_are_caught():
    src = SCREENS.read_text(encoding="utf-8")
    bad = src.replace("keynav::listTap(key, _task->hasKeyboard(), _task->listFocused(), _sel, count)",
                      "keynav::listTap(key, _task->hasKeyboard(), _task->listFocused(), 0, 0)")
    assert bad != src, "fixture did not apply"
    assert call_flagged(problems(bad), "InboxScreen")


def test_negative_a_fixed_focus_is_caught():
    src = SCREENS.read_text(encoding="utf-8")
    bad = src.replace("keynav::listTap(key, _task->hasKeyboard(), _task->listFocused(), _sel, _count)",
                      "keynav::listTap(key, _task->hasKeyboard(), true, _sel, _count)")
    assert bad != src, "fixture did not apply"
    assert call_flagged(problems(bad), "ContactsScreen")


def test_negative_a_select_that_ignores_the_new_row_is_caught():
    src = SCREENS.read_text(encoding="utf-8")
    body = handler_body(src, "NearbyScreen")
    bad = body.replace("Select:    select(t.sel)", "Select:    select(_sel)")
    assert bad != body, "fixture did not apply"
    assert any("NearbyScreen: Select" in p for p in problems(src.replace(body, bad)))


def test_negative_an_enter_that_never_takes_the_focus_is_caught():
    src = SCREENS.read_text(encoding="utf-8")
    body = handler_body(src, "NearbyScreen")
    bad = body.replace("EnterList: _task->enterList(); ", "EnterList: ")
    assert bad != body, "fixture did not apply"
    assert any("NearbyScreen: EnterList" in p for p in problems(src.replace(body, bad)))


def test_negative_a_leave_that_keeps_the_focus_is_caught():
    src = SCREENS.read_text(encoding="utf-8")
    body = handler_body(src, "InboxScreen")
    bad = body.replace("LeaveList: _task->leaveList();", "LeaveList:")
    assert bad != body, "fixture did not apply"
    assert any("InboxScreen: LeaveList" in p for p in problems(src.replace(body, bad)))


def test_negative_a_screen_back_on_plain_cycling_is_caught():
    src = SCREENS.read_text(encoding="utf-8")
    bad = src.replace("keynav::listTap(key, _task->hasKeyboard(), _task->listFocused(), _sel, _count)",
                      "keynav::listTap(key, true, _task->listFocused(), _sel, _count)")
    assert bad != src, "fixture did not apply"
    assert call_flagged(problems(bad), "ContactsScreen")


def test_negative_cycle_keeping_the_list_focus_is_caught():
    src = UITASK.read_text(encoding="utf-8")
    m = re.search(r"void\s+UITask::cycle\s*\(", src)
    body = body_after(src, m)
    bad = body.replace("leaveList();", "", 1)
    assert bad != body, "fixture did not apply"
    assert cycle_problems(src.replace(body, bad))


def test_negative_a_dropped_select_branch_is_caught():
    src = SCREENS.read_text(encoding="utf-8")
    body = handler_body(src, "NearbyScreen")
    bad = src.replace(body, body.replace("keynav::ListTap::Select:", "keynav::ListTap::None:"))
    assert any("NearbyScreen: Select" in p for p in problems(bad))


def test_negative_a_commented_out_call_does_not_count():
    src = SCREENS.read_text(encoding="utf-8")
    body = handler_body(src, "InboxScreen")
    gutted = re.sub(r"const keynav::ListTapResult t\s*=\s*[^;]*;",
                    "// const keynav::ListTapResult t = keynav::listTap(key, "
                    "_task->hasKeyboard(), _task->listFocused(), _sel, count);", body)
    assert gutted != body, "fixture did not apply"
    assert call_flagged(problems(src.replace(body, gutted)), "InboxScreen")


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("PASS %s" % name)
            except AssertionError as e:
                failures += 1
                print("FAIL %s: %s" % (name, e))
    print("\n%d failure(s)" % failures)
    sys.exit(1 if failures else 0)
