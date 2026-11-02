"""The tune engine on ui-orig: opt-in flag and call sites (#1957).

ToneEngine's rules are tested in test_tunes and test_tone_slots. What those can't see is
whether a UI uses it. If notify() goes back to a fixed melody while toneEngine() stays
non-null, a client is offered a control the board ignores, which is what #1788 forbids.

    the T1000-E envs set OFFBAND_UI_ORIG_TUNE_ENGINE     no tunes on the T1000-E otherwise
    no other variant sets it                             those boards haven't been checked
    ui-orig plays events and channels through the engine
    the fixed melodies stay behind the #else             other ui-orig boards keep theirs

These are source assertions. They catch a dropped flag or call, not behavior. The owner's
test on a T1000-E from the client is what shows it works.

Run: python scripts/test_ui_orig_tunes.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
T1000_INI = ROOT / "variants" / "t1000-e" / "platformio.ini"
UI_CPP = ROOT / "examples" / "companion_radio" / "ui-orig" / "UITask.cpp"
UI_H = ROOT / "examples" / "companion_radio" / "ui-orig" / "UITask.h"
FLAG = "OFFBAND_UI_ORIG_TUNE_ENGINE"
ENVS = ("env:t1000e_companion_radio_usb", "env:t1000e_companion_radio_ble")


def section(text, name):
    m = re.search(r"^\[%s\]\s*$(.*?)(?=^\[|\Z)" % re.escape(name), text, re.S | re.M)
    return None if m is None else "\n".join(l.split(";", 1)[0] for l in m.group(1).splitlines())


def other_inis():
    return {p: p.read_text(encoding="utf-8", errors="replace")
            for p in ROOT.glob("variants/*/platformio.ini") if p != T1000_INI}


def failures(ini, cpp, header, others):
    bad = []
    for env in ENVS:
        body = section(ini, env)
        if body is None:
            bad.append("variants/t1000-e/platformio.ini has no [%s]" % env)
        elif "-D %s" % FLAG not in body:
            bad.append("[%s] must set -D %s, or this board has no tunes (#1957)" % (env, FLAG))
    for path, text in others.items():
        if re.search(r"^\s*-D\s+%s\b" % FLAG, text, re.M):
            bad.append("%s sets %s; that board hasn't been checked (#1957)" % (path.parent.name, FLAG))
    if "_tones.playEvent(ev)" not in cpp:
        bad.append("ui-orig's notify() must play through the engine (#1957)")
    if "_tones.playChannel(" not in cpp:
        bad.append("ui-orig's notifyChannel() must use the channel's own tune (#1957)")
    if "_tones.begin(&buzzer" not in cpp:
        bad.append("ui-orig must begin the engine, or every play comes back NotFound (#1957)")
    if "toneEngine() override" not in header:
        bad.append("ui-orig must return its engine from toneEngine(), or a client sees no controls (#1957)")
    if 'buzzer.play("MsgRcv3' not in cpp or "#else\nvoid UITask::notify" not in cpp.replace("\r\n", "\n"):
        bad.append("the fixed melodies must stay behind the flag for other ui-orig boards (#1957)")
    return bad


def _real():
    return (T1000_INI.read_text(), UI_CPP.read_text(), UI_H.read_text(), other_inis())


def test_invariants_hold():
    bad = failures(*_real())
    assert not bad, "\n".join(bad)


def _fails(phrase, which, old, new=""):
    files = list(_real())
    assert not failures(*files), "sanity: the real files must pass"
    assert old in files[which], "mutation target %r is gone" % old
    files[which] = files[which].replace(old, new)
    bad = failures(*files)
    assert any(phrase in b for b in bad), "expected %r; got %s" % (phrase, bad or "NO FAILURES")


def test_negative_a_t1000_env_losing_the_flag():
    _fails("or this board has no tunes", 0, "-D %s" % FLAG, "")


def test_negative_a_fixed_melody_in_notify():
    _fails("notify() must play", 1, "_tones.playEvent(ev)", 'buzzer.play("x")')


def test_negative_channel_tunes_ignored():
    _fails("channel's own tune", 1, "_tones.playChannel(", "(void)(")


def test_negative_the_engine_never_begun():
    _fails("must begin the engine", 1, "_tones.begin(&buzzer", "(void)(&buzzer")


def test_negative_no_engine_for_the_client():
    _fails("sees no controls", 2, "toneEngine() override", "toneEngine()")


def test_negative_other_boards_losing_their_melodies():
    _fails("fixed melodies", 1, 'buzzer.play("MsgRcv3', 'buzzer.play("Other')


def test_negative_the_flag_leaking_to_another_board():
    files = list(_real())
    files[3] = {Path("variants/other/platformio.ini"): "  -D %s\n" % FLAG}
    assert any("hasn't been checked" in b for b in failures(*files))


if __name__ == "__main__":
    count = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            try:
                fn()
                print("PASS", name)
            except AssertionError as e:
                count += 1
                print("FAIL", name, e)
    print("\n%d failure(s)" % count)
    sys.exit(1 if count else 0)
