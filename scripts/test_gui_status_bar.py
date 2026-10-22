"""The status bar never shows a blank or placeholder strip on a screen change (#1730).

A tester's video on the T-Deck: every tap on the rail blanked the clock and icons for
a frame and showed "Text", LVGL's placeholder for a label never given any. The bar is
rebuilt with the screen, and its readings only came back at the next quarter-second
update. So:

    StatusBar::build() ends by re-applying the last readings (update(_shown, ...))
    StatusBar's label() helper gives every label empty text, never the placeholder

Source assertions, not behavior: the bench (#1730) is what shows the strip steady.

Run: python scripts/test_gui_status_bar.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BAR = ROOT / "examples" / "companion_radio" / "ui-gui" / "StatusBar.cpp"


def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def body_of(code, signature):
    m = re.search(signature, code)
    if not m:
        return None
    i = code.index("{", m.end())
    depth = 0
    for j in range(i, len(code)):
        if code[j] == "{":
            depth += 1
        elif code[j] == "}":
            depth -= 1
            if depth == 0:
                return code[i:j + 1]
    return None


def failures(bar):
    out = []
    code = strip_comments(bar)
    build = body_of(code, r"void\s+StatusBar::build\s*\(")
    if build is None:
        return ["StatusBar::build() not found: this check cannot see what it protects"]
    if not re.search(r"if\s*\(\s*_ever_shown\s*\)\s*update\(\s*_shown\s*,", build):
        out.append("StatusBar::build() must re-apply the last readings, or a screen change blanks the strip")
    lab = body_of(code, r"lv_obj_t\*\s+label\s*\(\s*lv_obj_t\*\s*parent\s*,\s*FontRole")
    if lab is None or not re.search(r'lv_label_set_text\(\s*l\s*,\s*""\s*\)', lab):
        out.append("StatusBar's label() must start every label empty, not with LVGL's placeholder \"Text\"")
    return out


def test_the_real_tree_passes():
    got = failures(BAR.read_text(encoding="utf-8"))
    assert not got, "\n".join(got)


def test_negative_a_rebuild_without_the_last_readings_is_caught():
    real = BAR.read_text(encoding="utf-8")
    broken = real.replace("if (_ever_shown) update(_shown, lv_tick_get());", "")
    assert broken != real
    assert any("blanks the strip" in f for f in failures(broken))


def test_negative_a_placeholder_label_is_caught():
    real = BAR.read_text(encoding="utf-8")
    broken = real.replace('lv_label_set_text(l, "");', "")
    assert broken != real
    assert any("placeholder" in f for f in failures(broken))


if __name__ == "__main__":
    n_fail = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("PASS %s" % name)
            except AssertionError as e:
                n_fail += 1
                print("FAIL %s: %s" % (name, e))
    print("\n%d failure(s)" % n_fail)
    sys.exit(1 if n_fail else 0)
