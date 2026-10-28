"""The BLE trace is built into the GUI diag envs and nothing else (#1830).

OFFBAND_BLE_TRACE logs every NimBLE GAP event and each change of the advertising
state, to find why a companion stops advertising (#1828). It belongs in the diag twins
of the GUI envs: the same working firmware, with the trace on. A release env carrying
it would ship a log line per BLE event to every user; a GUI diag env losing it would
take the instrument off the bench without anyone noticing.

Run: python scripts/test_ble_trace_envs.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FLAG = "OFFBAND_BLE_TRACE"
REQUIRED = {
    "env:heltec_v4_r8_tft_companion_radio_ble_gui_diag",
    "env:heltec_v4_tft_companion_radio_ble_gui_diag",
}


def ini_sections(text):
    """platformio.ini split into {section_name: body}, textual, not interpolated."""
    out, name, buf = {}, None, []
    for line in text.splitlines():
        m = re.match(r"\s*\[([^\]]+)\]\s*$", line)
        if m:
            if name is not None:
                out[name] = "\n".join(buf)
            name, buf = m.group(1), []
        elif name is not None:
            buf.append(line)
    if name is not None:
        out[name] = "\n".join(buf)
    return out


def sets_flag(body):
    code = re.sub(r";[^\n]*", "", body)
    return re.search(r"-D\s*" + FLAG + r"\b", code) is not None


def parents(body):
    """Sections a body inherits from: `extends = a, b` and `${a.key}` references."""
    code = re.sub(r";[^\n]*", "", body)
    out = set(re.findall(r"\$\{([^.}]+)\.", code))
    m = re.search(r"^\s*extends\s*=\s*(.+)$", code, re.M)
    if m:
        out.update(p.strip() for p in m.group(1).split(",") if p.strip())
    return out


def carriers(sections):
    """Every section that sets the flag itself or inherits it, through any depth."""
    have = {n for n, b in sections.items() if sets_flag(b)}
    grew = True
    while grew:
        grew = False
        for name, body in sections.items():
            if name not in have and parents(body) & have:
                have.add(name)
                grew = True
    return have


def failures(inis):
    out = []
    sections, where = {}, {}
    for path, text in inis.items():
        for name, body in ini_sections(text).items():
            sections[name] = body
            where[name] = path
    setters = carriers(sections)
    for name in sorted(setters):
        if name.startswith("env:") and not name.endswith("_diag"):
            out.append("%s [%s] sets %s, but only diag envs may" % (where[name], name, FLAG))
    for name in sorted(REQUIRED - setters):
        out.append("[%s] no longer sets %s: the BLE trace is off the bench" % (name, FLAG))
    return out


def _real():
    inis = {"platformio.ini": (ROOT / "platformio.ini").read_text(encoding="utf-8")}
    for p in sorted((ROOT / "variants").glob("*/platformio.ini")):
        inis[p.relative_to(ROOT).as_posix()] = p.read_text(encoding="utf-8")
    return inis


def test_the_real_tree_passes():
    got = failures(_real())
    assert not got, "\n".join(got)


def test_negative_a_release_env_with_the_trace_is_caught():
    inis = _real()
    inis["variants/x/platformio.ini"] = "[env:x_companion_radio_ble_gui]\nbuild_flags =\n  -D OFFBAND_BLE_TRACE\n"
    assert any("only diag envs" in f for f in failures(inis))


def test_negative_a_diag_env_losing_the_trace_is_caught():
    inis = _real()
    key = "variants/heltec_v4_r8/platformio.ini"
    inis[key] = inis[key].replace("-D OFFBAND_BLE_TRACE", "")
    assert any("heltec_v4_r8_tft_companion_radio_ble_gui_diag" in f and "no longer" in f for f in failures(inis))


def test_negative_a_release_env_extending_a_diag_env_is_caught():
    inis = _real()
    inis["variants/x/platformio.ini"] = (
        "[env:x_companion_radio_ble_gui]\nextends = env:heltec_v4_r8_tft_companion_radio_ble_gui_diag\n")
    assert any("x_companion_radio_ble_gui]" in f and "only diag envs" in f for f in failures(inis))


def test_negative_a_release_env_inheriting_the_flag_through_a_base_is_caught():
    inis = {"a.ini": "[trace_base]\nbuild_flags =\n  -D OFFBAND_BLE_TRACE\n"
                     "[env:z_companion_radio_ble_gui]\nbuild_flags =\n  ${trace_base.build_flags}\n"}
    assert any("z_companion_radio_ble_gui" in f for f in failures(inis))


def test_a_shared_base_used_only_by_diag_envs_passes():
    inis = {"a.ini": "[trace_base]\nbuild_flags =\n  -D OFFBAND_BLE_TRACE\n"
                     "[env:z_companion_radio_ble_gui_diag]\nextends = trace_base\n"}
    assert not any("only diag envs" in f for f in failures(inis))


def test_a_commented_out_flag_does_not_count():
    inis = {"a.ini": "[env:y_companion_radio_ble_gui]\nbuild_flags =\n  ; -D OFFBAND_BLE_TRACE\n"}
    got = failures(inis)
    assert not any("only diag envs" in f for f in got)


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
