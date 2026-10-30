"""The repeater runs the CoreScope /neighbors cycle and its commands (#1907).

NeighborsSchedule and NeighborsCli are native-tested on their own; this checks the
repeater-side wiring the native tests cannot reach:

- MyMesh::loop() steps the cycle, under OFFBAND_NEIGHBORS_REPORT;
- MyMesh::handleCommand answers discover.scopes itself, under the flag, before the
  common CLI (which would reach NeighborsCli's walk wording and its "no neighbors yet"
  refusal; a repeater with no neighbors still has its own scopes to report);
- main.cpp registers the NeighborsCli hooks and applies the saved settings at boot;
- ObserverCli dispatches handleNeighborsCli and prints the nbr: line under the flag,
  not under OFFBAND_OBSERVER, so the repeater's set/get mqtt.neighbors work.

Run: python scripts/test_repeater_neighbors_wiring.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MYMESH = ROOT / "examples" / "simple_repeater" / "MyMesh.cpp"
MAIN = ROOT / "examples" / "simple_repeater" / "main.cpp"
CLI = ROOT / "src" / "helpers" / "wifi_observer" / "ObserverCli.cpp"
V4_INI = ROOT / "variants" / "heltec_v4" / "platformio.ini"
FLAG = "OFFBAND_NEIGHBORS_REPORT"


def strip_comments(text):
    """Comments out, string and char literals kept intact."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def open_conditions(text, pos):
    """The preprocessor conditions open at pos. A branch after #else/#elif is marked
    ELSE: and an #ifndef NDEF:, so code there does not count as under the condition."""
    stack = []
    for m in re.finditer(r"^[ \t]*#[ \t]*(ifdef|ifndef|if|elif|else|endif)\b([^\n]*)", text[:pos], re.M):
        kind = m.group(1)
        if kind == "endif":
            if stack:
                stack.pop()
        elif kind in ("else", "elif"):
            if stack:
                stack[-1] = "ELSE:" + stack[-1]
        else:
            stack.append(("NDEF:" if kind == "ifndef" else "") + m.group(2))
    return stack


def gated_on(conds, name):
    """Whether one of the open conditions requires name to be defined."""
    for c in conds:
        flat = c.replace(" ", "")
        if c.startswith(("ELSE:", "NDEF:")) or ("!defined(%s)" % name) in flat:
            continue
        if name in c:
            return True
    return False


def function_body(code, signature):
    start = code.find(signature)
    if start < 0:
        return None, -1
    open_at = code.find("{", start)
    depth, i = 0, open_at
    while i < len(code):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return code[open_at:i + 1], open_at
        i += 1
    return None, -1


def under_flag(text, at):
    return at >= 0 and gated_on(open_conditions(text, at), FLAG)


def mymesh_failures(text):
    code = strip_comments(text)
    got = []
    loop, base = function_body(code, "void MyMesh::loop()")
    tick = -1 if loop is None else loop.find("neighborsCycleTick();")
    if tick < 0:
        got.append("MyMesh::loop() no longer steps the /neighbors cycle")
    elif not under_flag(code, base + tick):
        got.append("MyMesh::loop() steps the cycle outside %s" % FLAG)
    cmd, base = function_body(code, "void MyMesh::handleCommand(")
    if cmd is None:
        return got + ["MyMesh::handleCommand is gone"]
    scopes = cmd.find('"discover.scopes"')
    common = cmd.find("_cli.handleCommand(")
    if scopes < 0:
        got.append("handleCommand no longer answers discover.scopes")
    else:
        if common >= 0 and scopes > common:
            got.append("handleCommand answers discover.scopes after the common CLI")
        if not under_flag(code, base + scopes):
            got.append("handleCommand answers discover.scopes outside %s" % FLAG)
        if "startNeighborsReportNow()" not in cmd[scopes:common if common > scopes else None]:
            got.append("discover.scopes no longer starts the report")
    return got


def main_failures(text):
    code = strip_comments(text)
    got = []
    reg = code.find("offband::setNeighborsCliHooks(&kNeighborsCliHooks);")
    if reg < 0:
        got.append("main.cpp no longer registers the NeighborsCli hooks")
    elif not under_flag(code, reg):
        got.append("main.cpp registers the hooks outside %s" % FLAG)
    if not re.search(r"the_mesh\.configureNeighborsSchedule\(\s*offband::readNeighborsEnabled\(\)\s*,"
                     r"\s*offband::readNeighborsIntervalHours\(\)\s*\)\s*;", code):
        got.append("main.cpp no longer applies the saved mqtt.neighbors settings at boot")
    save, _ = function_body(code, "static bool nbrSaveSettings(")
    if save is None or not re.search(r"the_mesh\.configureNeighborsSchedule\(\s*s\.enabled\s*,\s*s\.interval_hours\s*\)", save):
        got.append("nbrSaveSettings no longer applies a changed setting live")
    return got


def env_failures(ini):
    start = ini.find("[env:heltec_v4_repeater_telemetry]")
    if start < 0:
        return ["heltec_v4_repeater_telemetry is gone"]
    nxt = ini.find("\n[", start + 1)
    block = ini[start:] if nxt < 0 else ini[start:nxt]
    live = "\n".join(line.split(";", 1)[0] for line in block.splitlines())
    if "+<helpers/wifi_observer/NeighborsCli.cpp>" not in live:
        return ["heltec_v4_repeater_telemetry does not build NeighborsCli.cpp"]
    return []


def cli_failures(text):
    code = strip_comments(text)
    got = []
    for anchor, what in (("if (handleNeighborsCli(cmd, reply, reply_size)) return true;", "dispatch"),
                         ("formatNeighborsStatusLine(", "nbr: status line")):
        at = code.find(anchor)
        if at < 0:
            got.append("ObserverCli no longer has the neighbors %s" % what)
            continue
        conds = open_conditions(code, at)
        if not gated_on(conds, FLAG):
            got.append("ObserverCli's neighbors %s is not under %s" % (what, FLAG))
        if gated_on(conds, "OFFBAND_OBSERVER"):
            got.append("ObserverCli's neighbors %s is observer-only" % what)
    return got


def read(p):
    return p.read_text(encoding="utf-8")


def test_the_real_tree_passes():
    got = (mymesh_failures(read(MYMESH)) + main_failures(read(MAIN)) + cli_failures(read(CLI))
           + env_failures(read(V4_INI)))
    assert not got, "\n".join(got)


def test_negative_a_tick_in_the_else_of_the_flag_is_caught():
    text = read(MYMESH).replace("#if defined(%s)\n  neighborsCycleTick();" % FLAG,
                                "#if defined(%s)\n#else\n  neighborsCycleTick();" % FLAG, 1)
    assert text != read(MYMESH)
    assert any("outside" in f for f in mymesh_failures(text))


def test_negative_a_save_that_does_not_apply_live_is_caught():
    text = read(MAIN).replace("  the_mesh.configureNeighborsSchedule(s.enabled, s.interval_hours);\n", "", 1)
    assert text != read(MAIN)
    assert any("applies a changed setting live" in f for f in main_failures(text))


def test_negative_an_env_without_the_cli_source_is_caught():
    ini = read(V4_INI).replace("+<helpers/wifi_observer/NeighborsCli.cpp>", "", 1)
    assert env_failures(ini)


def test_an_ifndef_observer_guard_is_not_mistaken_for_observer_only():
    assert not gated_on(["!defined(OFFBAND_OBSERVER) && defined(%s)" % FLAG], "OFFBAND_OBSERVER")
    assert gated_on(["!defined(OFFBAND_OBSERVER) && defined(%s)" % FLAG], FLAG)


def test_negative_no_tick_is_caught():
    text = read(MYMESH).replace("  neighborsCycleTick();   // #1907", "  // neighborsCycleTick();", 1)
    assert any("no longer steps" in f for f in mymesh_failures(text))


def test_negative_an_unguarded_tick_is_caught():
    text = read(MYMESH).replace("#if defined(%s)\n  neighborsCycleTick();" % FLAG, "#if 1\n  neighborsCycleTick();", 1)
    assert text != read(MYMESH)
    assert any("outside" in f for f in mymesh_failures(text))


def test_negative_discover_scopes_left_to_the_common_cli_is_caught():
    text = read(MYMESH).replace('memcmp(command, "discover.scopes", 15)', 'memcmp(command, "discover.scopez", 15)', 1)
    assert any("no longer answers discover.scopes" in f for f in mymesh_failures(text))


def test_negative_discover_scopes_that_starts_nothing_is_caught():
    text = read(MYMESH).replace("      startNeighborsReportNow();\n", "", 1)
    assert any("no longer starts the report" in f for f in mymesh_failures(text))


def test_negative_unregistered_hooks_are_caught():
    text = read(MAIN).replace("offband::setNeighborsCliHooks(&kNeighborsCliHooks);", "", 1)
    assert any("no longer registers" in f for f in main_failures(text))


def test_negative_settings_not_applied_at_boot_are_caught():
    text = read(MAIN).replace("offband::readNeighborsEnabled(),\n", "true,\n", 1)
    assert any("saved mqtt.neighbors settings" in f for f in main_failures(text))


def test_negative_an_observer_only_dispatch_is_caught():
    text = read(CLI).replace("#ifdef %s\n    // #1398: discover.*" % FLAG, "#ifdef OFFBAND_OBSERVER\n    // #1398: discover.*", 1)
    assert text != read(CLI)
    assert any("dispatch is not under" in f for f in cli_failures(text))


def test_negative_an_observer_only_status_line_is_caught():
    text = read(CLI).replace("#ifdef %s\n    // #1399: the periodic" % FLAG, "#ifdef OFFBAND_OBSERVER\n    // #1399: the periodic", 1)
    assert text != read(CLI)
    assert any("status line is not under" in f for f in cli_failures(text))


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
