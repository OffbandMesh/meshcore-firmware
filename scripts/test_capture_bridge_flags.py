"""Bridge-rig flags of the sniffer capture tool (#1199).
Run: python scripts/test_capture_bridge_flags.py
  or python -m pytest scripts/test_capture_bridge_flags.py -q

tools/diag/rc32-boot-740/scripts/capture.py asserts DTR by default, because the
Feather's native-USB CDC stays silent without it. On a CP2102/CH340 DevKit the same
DTR holds GPIO0 low, so any reset boots ROM download mode. --no-dtr opens without
it, and --reset reboots the rig from flash once, after the first open. These tests
drive the tool against a fake port; no serial hardware is touched.
"""
import importlib.util
import os
import sys
import tempfile
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CAPTURE = ROOT / "tools" / "diag" / "rc32-boot-740" / "scripts" / "capture.py"


def load():
    # capture.py exits at import without pyserial. No test opens a real port, so a
    # stub stands in wherever pyserial is not installed.
    try:
        import serial  # noqa: F401
    except ImportError:
        lp = types.ModuleType("serial.tools.list_ports")
        lp.comports = lambda: []
        tools = types.ModuleType("serial.tools")
        tools.list_ports = lp
        stub = types.ModuleType("serial")
        stub.tools, stub.Serial = tools, object
        sys.modules.update({"serial": stub, "serial.tools": tools,
                            "serial.tools.list_ports": lp})
    spec = importlib.util.spec_from_file_location("capture_under_test", CAPTURE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.time = types.SimpleNamespace(sleep=lambda s: None)   # no real waits
    return mod


class FakePort:
    """Records every DTR/RTS write. readline() replays a script, then raises
    KeyboardInterrupt, which is how an operator stops a capture."""

    def __init__(self, script):
        self.events = []
        self._script = list(script)

    def __setattr__(self, name, value):
        if name in ("dtr", "rts"):
            self.events.append((name, value))
        object.__setattr__(self, name, value)

    def readline(self):
        item = self._script.pop(0) if self._script else KeyboardInterrupt()
        if isinstance(item, BaseException):
            raise item
        return item

    def write(self, data):
        self.events.append(("write", data))

    def flush(self):
        pass

    def close(self):
        self.events.append(("close", None))


def run_capture(cap, ports, **kwargs):
    """do_capture against fake ports; returns (rc, log text, DTR at each open)."""
    opened = []

    def fake_open(port, baud, assert_dtr=True):
        opened.append(assert_dtr)
        p = ports.pop(0)
        p.dtr = assert_dtr        # what open_port presets before opening
        return p

    cap.open_port = fake_open
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "cap.log")
        rc = cap.do_capture("COM99", 115200, out, **kwargs)
        with open(out, encoding="utf-8") as f:
            text = f.read()
    return rc, text, opened


def test_pulse_reset_drops_en_with_dtr_off_then_releases():
    cap = load()
    port = FakePort([])
    cap.pulse_reset(port)
    assert port.events == [("dtr", False), ("rts", True), ("rts", False)], port.events


def test_no_dtr_capture_resets_once_and_not_on_reconnect():
    cap = load()
    first = FakePort([b"=== banner\n", OSError("unplugged")])
    second = FakePort([b"[hb] alive\n"])
    rc, text, opened = run_capture(cap, [first, second], assert_dtr=False, reset_first=True)
    assert rc == 0
    assert opened == [False, False], f"every open must leave DTR off, got {opened}"
    assert ("rts", True) in first.events, "the first open must pulse the reset"
    assert ("rts", True) not in second.events, "a reconnect must not reboot the rig again"
    assert text.count("<reset:") == 1, text
    assert "=== banner" in text and "[hb] alive" in text, text
    assert "dtr=off" in text, "the log header must record the DTR mode"


def test_default_capture_still_asserts_dtr_and_never_resets():
    cap = load()
    port = FakePort([b"line\n"])
    rc, text, opened = run_capture(cap, [port])
    assert rc == 0
    assert opened == [True], "the Feather default must keep DTR asserted"
    assert ("rts", True) not in port.events, port.events
    assert "<reset:" not in text and "dtr=on" in text, text


def run_main(cap, argv):
    """main() with argv; returns its exit code. --queue makes it return before
    any port is opened."""
    saved = sys.argv
    sys.argv = ["capture.py"] + argv
    try:
        return cap.main()
    except SystemExit as e:
        return e.code
    finally:
        sys.argv = saved


def test_reset_is_accepted_with_no_dtr_and_refused_without():
    cap = load()
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "cap.log")
        base = ["--port", "COM99", "--out", out, "--queue", "PING"]
        assert run_main(cap, base + ["--no-dtr", "--reset"]) == 0, "--no-dtr --reset was refused"
        assert run_main(cap, base + ["--reset"]) == 2, "--reset without --no-dtr was accepted"


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except Exception as e:   # a missing function must report, not crash the run
                failures += 1
                print(f"FAIL {name}: {type(e).__name__}: {e}")
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
