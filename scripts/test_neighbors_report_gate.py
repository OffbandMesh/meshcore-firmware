"""The pool's CoreScope /neighbors report has one gate for both roles (#1906).

MqttBrokerPool's /neighbors code (publishNeighbors, neighborsReady, the fan-out tick
and its buffer fields) is compiled under OFFBAND_NEIGHBORS_REPORT. Every observer
gets it from WifiObserverConfig.h, which defines it whenever OFFBAND_OBSERVER is set;
a repeater opts in on its env. This checks:

- no part of the pool's /neighbors code is still gated on OFFBAND_OBSERVER alone,
  which would leave the repeater with half of it;
- WifiObserverConfig.h derives the flag from OFFBAND_OBSERVER;
- MqttBrokerPool.h includes WifiObserverConfig.h before it first tests the flag, so
  every file that sees the pool class sees the same fields (a field set that differs
  between files is a silent memory corruption, not a build error);
- heltec_v4_repeater_telemetry sets the flag.

Run: python scripts/test_neighbors_report_gate.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
POOL_H = ROOT / "src" / "helpers" / "wifi_observer" / "MqttBrokerPool.h"
POOL_CPP = ROOT / "src" / "helpers" / "wifi_observer" / "MqttBrokerPool.cpp"
CONFIG_H = ROOT / "src" / "helpers" / "wifi_observer" / "WifiObserverConfig.h"
V4_INI = ROOT / "variants" / "heltec_v4" / "platformio.ini"
FLAG = "OFFBAND_NEIGHBORS_REPORT"

IMPLIES = re.compile(
    r"#if\s+defined\(\s*OFFBAND_OBSERVER\s*\)\s*&&\s*!\s*defined\(\s*OFFBAND_NEIGHBORS_REPORT\s*\)\s*"
    r"#\s*define\s+OFFBAND_NEIGHBORS_REPORT\s+1\s*#endif")


def env_block(ini, env):
    start = ini.find("[env:%s]" % env)
    if start < 0:
        return None
    nxt = ini.find("\n[", start + 1)
    return ini[start:] if nxt < 0 else ini[start:nxt]


def open_conditions(text, pos):
    """The preprocessor conditions open at pos (#if/#ifdef/#ifndef not yet #endif'd)."""
    stack = []
    for m in re.finditer(r"^[ \t]*#[ \t]*(ifdef|ifndef|if|endif)\b([^\n]*)", text[:pos], re.M):
        if m.group(1) == "endif":
            if stack:
                stack.pop()
        else:
            stack.append(m.group(2))
    return stack


# Places in the pool that are /neighbors code and must sit under the flag.
ANCHORS_H = ("uint8_t publishNeighbors(", "NeighborsFanOut nbr_fanout_;")
ANCHORS_CPP = ("uint8_t MqttBrokerPool::publishNeighbors(", "neighborsFanOutTick(now_ms);",
               "free(nbr_buf_);   // a /neighbors report", "#include <esp_heap_caps.h>")


def pool_failures(pool_h, pool_cpp):
    got = []
    for name, text in (("MqttBrokerPool.h", pool_h), ("MqttBrokerPool.cpp", pool_cpp)):
        if re.search(r"#\s*(if|ifdef|ifndef|elif)\b[^\n]*\bOFFBAND_OBSERVER\b", text):
            got.append("%s still gates code on OFFBAND_OBSERVER" % name)
    for name, text, anchors in (("MqttBrokerPool.h", pool_h, ANCHORS_H),
                                ("MqttBrokerPool.cpp", pool_cpp, ANCHORS_CPP)):
        for anchor in anchors:
            at = text.find(anchor)
            if at < 0:
                got.append("%s: %r is gone" % (name, anchor))
            elif not any(FLAG in c for c in open_conditions(text, at)):
                got.append("%s: %r is not under %s" % (name, anchor, FLAG))
    include_at = pool_h.find('#include "WifiObserverConfig.h"')
    first_test = pool_h.find(FLAG)
    if include_at < 0 or (first_test >= 0 and include_at > first_test):
        got.append("MqttBrokerPool.h tests %s before including WifiObserverConfig.h" % FLAG)
    return got


def config_failures(config_h):
    return [] if IMPLIES.search(config_h) else ["WifiObserverConfig.h no longer derives %s from OFFBAND_OBSERVER" % FLAG]


def env_failures(ini):
    block = env_block(ini, "heltec_v4_repeater_telemetry")
    if block is None:
        return ["heltec_v4_repeater_telemetry is gone"]
    live = "\n".join(line.split(";", 1)[0] for line in block.splitlines())
    got = []
    if not re.search(r"-D\s*%s\b" % FLAG, live):
        got.append("heltec_v4_repeater_telemetry does not set %s" % FLAG)
    # publishNeighbors builds the body with NeighborsReport.cpp; without it the first
    # caller fails to link (the env lists its wifi_observer sources one by one).
    if "+<helpers/wifi_observer/NeighborsReport.cpp>" not in live:
        got.append("heltec_v4_repeater_telemetry does not build NeighborsReport.cpp")
    return got


def read(p):
    return p.read_text(encoding="utf-8")


def test_the_real_tree_passes():
    got = (pool_failures(read(POOL_H), read(POOL_CPP)) + config_failures(read(CONFIG_H))
           + env_failures(read(V4_INI)))
    assert not got, "\n".join(got)


def test_negative_an_observer_only_tick_is_caught():
    cpp = read(POOL_CPP).replace("#ifdef %s\n    neighborsFanOutTick" % FLAG,
                                 "#ifdef OFFBAND_OBSERVER\n    neighborsFanOutTick", 1)
    assert cpp != read(POOL_CPP), "the tick's guard moved; update this test"
    assert any("still gates code on OFFBAND_OBSERVER" in f for f in pool_failures(read(POOL_H), cpp))


def test_negative_an_observer_only_field_block_is_caught():
    h = read(POOL_H).replace("#ifdef %s" % FLAG, "#if defined(OFFBAND_OBSERVER)")
    assert any("still gates code on OFFBAND_OBSERVER" in f for f in pool_failures(h, read(POOL_CPP)))


def test_negative_testing_the_flag_before_the_config_is_caught():
    h = read(POOL_H)
    h = h.replace('#include "WifiObserverConfig.h"\n', "", 1) + '\n#include "WifiObserverConfig.h"\n'
    assert any("before including WifiObserverConfig.h" in f for f in pool_failures(h, read(POOL_CPP)))


def test_negative_a_misspelled_cpp_guard_is_caught():
    cpp = read(POOL_CPP).replace("#ifdef %s\n    neighborsFanOutTick" % FLAG,
                                 "#ifdef OFFBAND_NEIGHBOURS_REPORT\n    neighborsFanOutTick", 1)
    assert any("'neighborsFanOutTick(now_ms);' is not under" in f for f in pool_failures(read(POOL_H), cpp))


def test_negative_a_removed_publish_guard_is_caught():
    cpp = read(POOL_CPP)
    at = cpp.find("uint8_t MqttBrokerPool::publishNeighbors(")
    start = cpp.rfind("#ifdef %s" % FLAG, 0, at)
    cpp = cpp[:start] + "#if 1" + cpp[start + len("#ifdef %s" % FLAG):]
    assert any("publishNeighbors(' is not under" in f for f in pool_failures(read(POOL_H), cpp))


def test_negative_an_elif_observer_gate_is_caught():
    cpp = read(POOL_CPP) + "\n#if 0\n#elif defined(OFFBAND_OBSERVER)\n#endif\n"
    assert any("still gates code on OFFBAND_OBSERVER" in f for f in pool_failures(read(POOL_H), cpp))


def test_negative_a_repeater_without_the_report_source_is_caught():
    ini = read(V4_INI).replace("+<helpers/wifi_observer/NeighborsReport.cpp>", "", 1)
    assert any("does not build NeighborsReport.cpp" in f for f in env_failures(ini))


def test_negative_a_dropped_implication_is_caught():
    c = read(CONFIG_H).replace("#define OFFBAND_NEIGHBORS_REPORT 1", "", 1)
    assert config_failures(c)


def test_negative_a_repeater_env_without_the_flag_is_caught():
    ini = read(V4_INI).replace("-D OFFBAND_NEIGHBORS_REPORT=1", "", 1)
    assert any("does not set" in f for f in env_failures(ini))


def test_negative_a_commented_out_flag_is_caught():
    ini = read(V4_INI).replace("-D OFFBAND_NEIGHBORS_REPORT=1", "; -D OFFBAND_NEIGHBORS_REPORT=1", 1)
    assert any("does not set" in f for f in env_failures(ini))


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
