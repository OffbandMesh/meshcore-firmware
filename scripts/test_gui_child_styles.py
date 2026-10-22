"""No style is set straight onto a GUI object's second or later child (#1730).

The T-Deck rebooted on every Send advert. Settings > Actions coloured the row's note
with lv_obj_set_style_text_color(lv_obj_get_child(kk, 1), ...), but rows in the
landscape layout (class B) are built without that hint line. lv_obj_get_child() returned
null, and LVGL dereferenced it: LoadProhibited at 0x20, caught by the panic recorder.

Child 0, and child -1 (the last), exist on any object that has children. Child 1 and on
exist only if the layout built them, and layouts differ by class and by row kind. So a
style set on one goes through a pointer that is checked first:

    if (lv_obj_t* x = lv_obj_get_child(row, 1)) lv_obj_set_style_...(x, ...);

Run: python scripts/test_gui_child_styles.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GUI = ROOT / "examples" / "companion_radio" / "ui-gui"

# A style setter whose object is a get_child() call with a literal index of 1 or more.
UNCHECKED = re.compile(r"lv_obj_set_style_\w+\(\s*lv_obj_get_child\(\s*[^,()]+,\s*([1-9]\d*)\s*\)")


def failures(files):
    out = []
    for name, text in files.items():
        code = re.sub(r"//[^\n]*", "", text)
        for m in UNCHECKED.finditer(code):
            line = code.count("\n", 0, m.start()) + 1
            out.append("%s:%d styles child %s without checking it exists: a layout that didn't build it "
                       "makes this a null dereference" % (name, line, m.group(1)))
    return out


def _real():
    return {p.name: p.read_text(encoding="utf-8") for p in sorted(GUI.glob("*.cpp"))}


def test_the_real_tree_passes():
    got = failures(_real())
    assert not got, "\n".join(got)


def test_negative_the_send_advert_crash_is_caught():
    files = _real()
    files["SetupScreens.cpp"] += "\nvoid x() { lv_obj_set_style_text_color(lv_obj_get_child(kk, 1), c(_t.sig), 0); }\n"
    assert any("styles child 1" in f for f in failures(files))


def test_children_that_always_exist_are_allowed():
    files = {"a.cpp": "void x() { lv_obj_set_style_text_color(lv_obj_get_child(kk, 0), c(1), 0);"
                      " lv_obj_set_style_text_color(lv_obj_get_child(row, -1), c(1), 0); }"}
    assert not failures(files)


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
