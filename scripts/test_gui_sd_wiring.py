"""The SD archive's call sites (Epic I #1433, I8 #1704).

The archive's rules are host-tested (test/test_sd_archive). What those tests can't
see is whether the firmware USES them: a stored message whose delivery state or echo
count changes must also reach the card, or the card's copy keeps the old state
forever, with every test still green. So:

    every _gui_store.setDelivery / addEcho in MyMesh.cpp, and every store.setDelivery
    in ui-gui/UITask.cpp, is followed within a few lines by guiStoreChanged(...)
    MyMesh's loop drives GuiSd (_gui_sd.loop), and its begin() tags it (_gui_sd.begin)
    both are inside #ifdef PIN_SD_CS, so boards without a card compile none of it

Source assertions, not behavior: the bench (#1679) is what shows a card catching up.

Run: python scripts/test_gui_sd_wiring.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MYMESH = ROOT / "examples" / "companion_radio" / "MyMesh.cpp"
UITASK = ROOT / "examples" / "companion_radio" / "ui-gui" / "UITask.cpp"
WINDOW = 4   # lines after a change in which the hook must appear

CHANGE = re.compile(r"\b(?:_gui_store|store)\.(?:setDelivery|addEcho)\s*\(")


def code_lines(text):
    """Lines with // comments removed, so a hook named in prose isn't a call."""
    return [l.split("//", 1)[0] for l in text.splitlines()]


def unhooked(text):
    """1-based line numbers of store changes with no guiStoreChanged after them."""
    lines = code_lines(text)
    bad = []
    for i, line in enumerate(lines):
        if CHANGE.search(line):
            window = "\n".join(lines[i:i + WINDOW + 1])
            if "guiStoreChanged(" not in window:
                bad.append(i + 1)
    return bad


def failures(mymesh, uitask):
    out = []
    for name, text in (("MyMesh.cpp", mymesh), ("UITask.cpp", uitask)):
        bad = unhooked(text)
        if bad:
            out.append("%s changes a stored message at line(s) %s without guiStoreChanged(): "
                       "the card's copy keeps the old state" % (name, bad))
    if not sum(1 for l in code_lines(mymesh) if CHANGE.search(l)):
        out.append("no store changes found in MyMesh.cpp: this check can't see what it protects")
    code = "\n".join(code_lines(mymesh))
    for call, why in (("_gui_sd.loop(", "nothing would ever reach the card"),
                      ("_gui_sd.begin(", "the archive would have no owner and never open")):
        m = code.find(call)
        if m < 0:
            out.append("MyMesh.cpp never calls %s): %s" % (call, why))
            continue
        before = code[:m]
        if before.rfind("#ifdef PIN_SD_CS") <= before.rfind("#endif"):
            out.append("%s) must sit inside #ifdef PIN_SD_CS, so cardless boards compile none of it"
                       % call)
    return out


def _real():
    return MYMESH.read_text(encoding="utf-8"), UITASK.read_text(encoding="utf-8")


def _fails_with(fragment, mymesh=None, uitask=None):
    m, u = _real()
    got = failures(mymesh if mymesh is not None else m, uitask if uitask is not None else u)
    assert any(fragment in f for f in got), "expected %r, got %s" % (fragment, got)


def test_the_real_tree_passes():
    got = failures(*_real())
    assert not got, "\n".join(got)


def test_negative_a_delivery_change_without_the_hook_is_caught():
    m, _ = _real()
    _fails_with("without guiStoreChanged", mymesh=m.replace("guiStoreChanged(_gui_ack_seq[i]);", "", 1))


def test_negative_an_echo_without_the_hook_is_caught():
    m, _ = _real()
    _fails_with("without guiStoreChanged", mymesh=m.replace("guiStoreChanged(seq);", "", 1))


def test_negative_a_failed_send_without_the_hook_is_caught():
    _, u = _real()
    _fails_with("UITask.cpp changes", uitask=u.replace("the_mesh.guiStoreChanged(seq);", ""))


def test_negative_a_new_unhooked_change_is_caught():
    _, u = _real()
    _fails_with("UITask.cpp changes", uitask=u + "\nvoid x() { store.setDelivery(1, Delivery::Failed); }\n")


def test_negative_dropping_the_loop_call_is_caught():
    m, _ = _real()
    _fails_with("never calls _gui_sd.loop(", mymesh=re.sub(r"_gui_sd\.loop\([^;]*;", "", m))


def test_negative_the_loop_call_outside_its_guard_is_caught():
    m, _ = _real()
    moved = m.replace("  #ifdef PIN_SD_CS\n  _gui_sd.loop(", "  _gui_sd.loop(", 1)
    assert moved != m, "sanity: the guarded loop call must be found"
    _fails_with("inside #ifdef PIN_SD_CS", mymesh=moved)


def test_negative_a_hook_named_in_a_comment_does_not_count():
    assert unhooked("store.setDelivery(1, x);\n// then guiStoreChanged(1)\n") == [1]


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
