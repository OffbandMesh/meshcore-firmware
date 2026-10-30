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


def pool_failures(pool_h, pool_cpp):
    got = []
    for name, text in (("MqttBrokerPool.h", pool_h), ("MqttBrokerPool.cpp", pool_cpp)):
        if re.search(r"#\s*if(def)?\b[^\n]*\bOFFBAND_OBSERVER\b", text):
            got.append("%s still gates code on OFFBAND_OBSERVER" % name)
    if "publishNeighbors" not in pool_h or FLAG not in pool_h:
        got.append("MqttBrokerPool.h no longer declares publishNeighbors under %s" % FLAG)
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
    if not re.search(r"-D\s*%s\b" % FLAG, live):
        return ["heltec_v4_repeater_telemetry does not set %s" % FLAG]
    return []


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
