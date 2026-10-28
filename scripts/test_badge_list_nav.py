"""Call-site guard for the keyboardless badge's list navigation (#1884).

keynav::listTap() is the rule and test_keynav covers it. What no unit test reaches is
whether the three main list screens -- Messages, Contacts, Nearby -- CALL it, with the
real keyboard-fitted state. Drop the call from one screen and a pyramid can again open
only the top row of that list, with every keynav test still green.

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
    end = src.find("\n}\n", m.end())
    return src[m.end():end if end >= 0 else len(src)]


def problems(src):
    src = strip_comments(src)
    out = []
    for screen, args in LIST_SCREENS.items():
        body = handler_body(src, screen)
        if body is None:
            out.append("%s::handleInput not found" % screen)
            continue
        if not re.search(r"keynav::listTap\s*\(\s*key\s*,\s*_task->hasKeyboard\(\)\s*,\s*%s\s*\)"
                         % args, body):
            out.append("%s: a button tap no longer goes through keynav::listTap with the "
                       "fitted-keyboard state and this screen's own selection and row count "
                       "-- a keyboardless badge is stuck on the top row" % screen)
        # Each outcome must do its own job: a swapped direction or an unused row is a bug
        # every keynav test would miss.
        wiring = (
            (r"ListTap::Select\)\s*select\([^;]*t\.sel\)", "Select must highlight t.sel"),
            (r"ListTap::CycleNext\)\s*_task->cycle\(1\)", "CycleNext must cycle(1)"),
            (r"ListTap::CyclePrev\)\s*_task->cycle\(-1\)", "CyclePrev must cycle(-1)"),
        )
        for pattern, what in wiring:
            if not re.search(pattern, body):
                out.append("%s: %s" % (screen, what))
    return out


def cycle_problems(src):
    """cycle() must start a keyboardless arrival at the right end of each list."""
    src = strip_comments(src)
    m = re.search(r"void\s+UITask::cycle\s*\(", src)
    if not m:
        return ["UITask::cycle not found"]
    body = src[m.end():src.find("\n}\n", m.end())]
    out = []
    if not re.search(r"keynav::listEntry\s*\(\s*step\s*,\s*hasKeyboard\(\)\s*\)", body):
        out.append("cycle() no longer asks keynav::listEntry where a list starts")
    for screen, ptr in (("ContactsScreen", "contacts"), ("NearbyScreen", "nearby"),
                        ("InboxScreen", "home")):
        if not re.search(r"\(\(%s\*\)%s\)->enterList\(at_bottom\)" % (screen, ptr), body):
            out.append("cycle() no longer places the highlight on arrival at %s" % screen)
    return out


def test_the_three_list_screens_walk_their_lists():
    assert problems(SCREENS.read_text(encoding="utf-8")) == []


def test_cycle_places_the_highlight_on_arrival():
    assert cycle_problems(UITASK.read_text(encoding="utf-8")) == []


def test_negative_swapped_cycle_directions_are_caught():
    src = SCREENS.read_text(encoding="utf-8")
    body = handler_body(src, "ContactsScreen")
    swapped = (body.replace("CycleNext) _task->cycle(1)", "CycleNext) _task->cycle(@)")
                   .replace("CyclePrev) _task->cycle(-1)", "CyclePrev) _task->cycle(1)")
                   .replace("cycle(@)", "cycle(-1)"))
    assert swapped != body, "fixture did not apply"
    found = problems(src.replace(body, swapped))
    assert any("CycleNext must cycle(1)" in p for p in found), found
    assert any("CyclePrev must cycle(-1)" in p for p in found), found


def test_negative_wrong_arguments_are_caught():
    src = SCREENS.read_text(encoding="utf-8")
    bad = src.replace("keynav::listTap(key, _task->hasKeyboard(), _sel, count)",
                      "keynav::listTap(key, _task->hasKeyboard(), 0, 0)")
    assert bad != src, "fixture did not apply"
    assert any("InboxScreen" in p for p in problems(bad))


def test_negative_a_select_that_ignores_the_new_row_is_caught():
    src = SCREENS.read_text(encoding="utf-8")
    body = handler_body(src, "NearbyScreen")
    bad = body.replace("select(t.sel)", "select(_sel)")
    assert bad != body, "fixture did not apply"
    assert any("NearbyScreen: Select" in p for p in problems(src.replace(body, bad)))


def test_negative_cycle_forgetting_where_a_list_starts_is_caught():
    src = UITASK.read_text(encoding="utf-8")
    bad = src.replace("if (enter) ((NearbyScreen*)nearby)->enterList(at_bottom);", "")
    assert bad != src, "fixture did not apply"
    assert any("NearbyScreen" in p for p in cycle_problems(bad))
    bad = src.replace("keynav::listEntry(step, hasKeyboard())", "keynav::ListEntry::Keep")
    assert any("listEntry" in p for p in cycle_problems(bad))


def test_negative_a_screen_back_on_plain_cycling_is_caught():
    src = SCREENS.read_text(encoding="utf-8")
    bad = src.replace("keynav::listTap(key, _task->hasKeyboard(), _sel, _count)",
                      "keynav::listTap(key, true, _sel, _count)")
    assert bad != src, "fixture did not apply"
    assert any("ContactsScreen" in p for p in problems(bad))


def test_negative_a_dropped_select_branch_is_caught():
    src = SCREENS.read_text(encoding="utf-8")
    body = handler_body(src, "NearbyScreen")
    bad = src.replace(body, body.replace("keynav::ListTap::Select", "keynav::ListTap::None"))
    assert any("NearbyScreen: Select" in p for p in problems(bad))


def test_negative_a_commented_out_call_does_not_count():
    src = SCREENS.read_text(encoding="utf-8")
    body = handler_body(src, "InboxScreen")
    gutted = re.sub(r"const keynav::ListTapResult t = [^;]*;",
                    "// const keynav::ListTapResult t = keynav::listTap(key, "
                    "_task->hasKeyboard(), _sel, count);", body)
    assert gutted != body, "fixture did not apply"
    assert any("InboxScreen" in p for p in problems(src.replace(body, gutted)))


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
