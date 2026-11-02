"""T1000-E parity flags and call sites: send tone, GPS power save, message LED (#1958-#1960).

Source assertions, like test_ui_orig_tunes.py beside it. They catch a dropped flag or call,
not behavior; the owner's test on a T1000-E is what shows it works. The heartbeat rule
itself is host-tested in test_heartbeat_cadence.

Run: python scripts/test_t1000e_parity.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INI = ROOT / "variants" / "t1000-e" / "platformio.ini"
TARGET_H = ROOT / "variants" / "t1000-e" / "target.h"
MYMESH = ROOT / "examples" / "companion_radio" / "MyMesh.cpp"
UI_CPP = ROOT / "examples" / "companion_radio" / "ui-orig" / "UITask.cpp"
HB_TEST = ROOT / "test" / "test_heartbeat_cadence" / "test_heartbeat_cadence.cpp"
VARIANT_H = ROOT / "variants" / "t1000-e" / "variant.h"
ENVS = ("env:t1000e_companion_radio_usb", "env:t1000e_companion_radio_ble")
FLAGS = {
    "OFFBAND_SENT_TONE_ON_CLIENT_SEND": None,
    "DEFAULT_TONE_SENT": "offband::kTuneTick",
    "OFFBAND_GPS_POWER_SAVE": "1",
    "OFFBAND_HEARTBEAT_MSG_ON_MS": "490",
}


def section(text, name):
    m = re.search(r"^\[%s\]\s*$(.*?)(?=^\[|\Z)" % re.escape(name), text, re.S | re.M)
    return None if m is None else "\n".join(l.split(";", 1)[0] for l in m.group(1).splitlines())


def failures(ini, target_h, mymesh, ui, hb, variant_h):
    bad = []
    for env in ENVS:
        body = section(ini, env)
        if body is None:
            bad.append("no [%s]" % env)
            continue
        for name, value in FLAGS.items():
            m = re.search(r"-D\s+%s(?:=(\S+))?" % name, body)
            if m is None or (value is not None and m.group(1) != value):
                bad.append("[%s] must set -D %s%s" % (env, name, "" if value is None else "=" + value))
    m = re.search(r"kT1000MsgOn\s*=\s*(\d+)", hb)
    shipped = re.search(r"-D\s+OFFBAND_HEARTBEAT_MSG_ON_MS=(\d+)", section(ini, ENVS[1]) or "")
    if m is None or shipped is None or m.group(1) != shipped.group(1):
        bad.append("test_heartbeat_cadence's kT1000MsgOn must match the env's lit time, or its "
                   "boundary check covers a number the board doesn't use (#1960)")
    if "gpsSnapshot() const;" not in target_h or "bool gpsHasFix()" not in target_h:
        bad.append("T1000SensorManager needs gpsHasFix() and gpsSnapshot() for power save (#1959)")
    sends = mymesh.count("_ui->notify(UIEventType::sent)")
    if sends < 2:
        bad.append("both client send paths, DM and channel, must play the send tone (#1958); found %d" % sends)
    if "attempt == 0" not in mymesh:
        bad.append("a client's retries must not replay the send tone (#1958)")
    if ui.count("showMessageWaiting(msgcount);") < 2 or "setHeartbeatMessageWaiting" not in ui:
        bad.append("ui-orig must report message waiting on arrival and on read (#1960)")
    if (re.search(r"#define\s+AREF_VOLTAGE\s+\(3\.0\)", variant_h)
            and not re.search(r"#define\s+VBAT_AR_INTERNAL\s+AR_INTERNAL_3_0", variant_h)):
        bad.append("variant.h divides by a 3.0 V reference without VBAT_AR_INTERNAL, so SafeBoot "
                   "reads Vbat 17% low and a charged board sleeps on every wake (#1805)")
    return bad


def _real():
    return (INI.read_text(), TARGET_H.read_text(), MYMESH.read_text(encoding="utf-8", errors="replace"),
            UI_CPP.read_text(), HB_TEST.read_text(), VARIANT_H.read_text())


def test_invariants_hold():
    bad = failures(*_real())
    assert not bad, "\n".join(bad)


def _fails(phrase, which, old, new=""):
    files = list(_real())
    assert not failures(*files), "sanity"
    assert old in files[which], "mutation target %r is gone" % old
    files[which] = files[which].replace(old, new)
    bad = failures(*files)
    assert any(phrase in b for b in bad), "expected %r; got %s" % (phrase, bad or "NO FAILURES")


def test_negative_no_send_tone_flag():
    _fails("OFFBAND_SENT_TONE_ON_CLIENT_SEND", 0, "-D OFFBAND_SENT_TONE_ON_CLIENT_SEND", "")


def test_negative_a_silent_send_default():
    _fails("DEFAULT_TONE_SENT", 0, "DEFAULT_TONE_SENT=offband::kTuneTick", "DEFAULT_TONE_SENT=offband::kTuneSilent")


def test_negative_no_power_save():
    _fails("OFFBAND_GPS_POWER_SAVE", 0, "-D OFFBAND_GPS_POWER_SAVE=1", "")


def test_negative_a_lit_time_the_test_doesnt_cover():
    _fails("kT1000MsgOn", 0, "OFFBAND_HEARTBEAT_MSG_ON_MS=490", "OFFBAND_HEARTBEAT_MSG_ON_MS=500")


def test_negative_missing_gps_accessors():
    _fails("gpsHasFix", 1, "bool gpsHasFix()", "bool gpsFix()")


def test_negative_one_send_path_silent():
    files = list(_real())
    files[2] = files[2].replace("_ui->notify(UIEventType::sent)", "_ui->notify(UIEventType::none)", 1)
    assert any("both client send paths" in b for b in failures(*files))


def test_negative_retries_replaying_the_tone():
    _fails("retries", 2, "attempt == 0", "attempt >= 0")


def test_negative_safeboot_reading_vbat_low():
    _fails("17% low", 5, "#define VBAT_AR_INTERNAL        AR_INTERNAL_3_0", "")


def test_negative_message_waiting_never_reported():
    _fails("message waiting", 3, "setHeartbeatMessageWaiting", "setHeartbeatSomething")


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
