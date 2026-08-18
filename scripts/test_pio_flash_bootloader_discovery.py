"""Bootloader-port discovery tests for pio-flash (#807).

The defect these pin: `_discover_bootloader_port()` built its candidate set by
subtracting the pre-trigger port list -- `new = [p for p in now if p["com"] not
in before_coms]` -- and then applied every match rule to `new` only. That
requires the device to CHANGE COM ports entering download mode.

That assumption holds for the hardware it was written against:

  * bridge-attached boards (CP2102/CH340) -- the bridge keeps its own port while
    the SoC behind it reboots, so a distinct download port appears; and
  * nRF52 -- the app CDC port is replaced by a separate DFU port.

It does NOT hold when the USB device IS the SoC. On ESP32-C3/C6/S3 in
USB-Serial/JTAG mode the download-mode peripheral is in the same silicon: the
port does not drop, the PID stays 303A:1001, and the serial is unchanged.
Enumeration before and after the trigger is byte-identical, so `new` is empty by
construction, no candidate can ever be evaluated, and the function spins for the
full timeout before refusing a device that is sitting right there.

Observed on `rcc6-bench-1` (ESP32-C6): `bootstrap` and `backup` both connected
fine -- because those let esptool do its own reset-and-connect -- while `confirm`
refused with "no bootloader port appeared within 20s". Reported on `rc32-bench-1`
(ESP32-S3) as the same wall.

Why these are unit tests: reproducing this on hardware means owning a native-USB
part AND a bridge part AND an nRF52 and power-cycling each through download mode
per run. The behaviour is entirely a function of two port lists, so it is pinned
here instead. Hardware verification is complementary, not replaced.

WHAT MUST NOT REGRESS. The fallback is gated on SERIAL EQUALITY ONLY. A class
match (vendor + PID) must never be able to claim the flash target, and ambiguity
must still refuse rather than pick -- that is the #503 guarantee, and weakening
it here would let the tool write firmware to the wrong board.

Run: python -m pytest scripts/test_pio_flash_bootloader_discovery.py -q
"""

import importlib.util
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "pio_flash", Path(__file__).resolve().parent / "pio-flash.py"
)
pio_flash = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(pio_flash)

_discover = pio_flash._discover_bootloader_port

# Real values from rcc6-bench-1, so a reader can tie these back to the incident.
C6_SERIAL = "58:8C:81:2F:91:F8"
C6_PID = "303A:1001"
ESP_VENDOR = "303A"


def port(com, vid_pid, serial="", hash_="8&3B623357"):
    return {
        "com": com,
        "vid_pid": vid_pid,
        "usb_serial": serial,
        "instance_hash": hash_,
    }


@pytest.fixture(autouse=True)
def _fast_and_isolated(monkeypatch):
    """Neutralise sleep so the timeout path costs nothing, and make refuse()
    raise instead of exiting so failures are assertable."""
    monkeypatch.setattr(pio_flash.time, "sleep", lambda *_: None)

    def _raise(msg):
        raise AssertionError(f"REFUSED: {msg}")

    monkeypatch.setattr(pio_flash, "refuse", _raise)
    monkeypatch.setattr(pio_flash, "err", lambda *_a, **_k: None)
    monkeypatch.setattr(pio_flash, "out", lambda *_a, **_k: None)


def _enumerates(monkeypatch, sequence):
    """Feed enumerate_ports() a fixed list (or a sequence of lists)."""
    if sequence and isinstance(sequence[0], dict):
        monkeypatch.setattr(pio_flash, "enumerate_ports", lambda: list(sequence))
    else:
        it = iter(sequence)
        last = {}

        def _next():
            nonlocal last
            try:
                last = list(next(it))
            except StopIteration:
                pass
            return list(last)

        monkeypatch.setattr(pio_flash, "enumerate_ports", _next)


# --------------------------------------------------------------------------
# The #807 defect itself
# --------------------------------------------------------------------------

def test_same_port_device_is_found_by_serial(monkeypatch):
    """ESP32-C6: port, PID and serial all survive download entry.

    Enumeration is identical before and after the trigger. The pre-#807 code
    refused here; the device must now be found on its unchanged port.
    """
    ports = [port("COM45", C6_PID, C6_SERIAL)]
    _enumerates(monkeypatch, ports)

    found = _discover(before=ports, vendor=ESP_VENDOR, running_pid="1001",
                      timeout=0, running_serial=C6_SERIAL)

    assert found["com"] == "COM45"


def test_same_port_fallback_requires_serial(monkeypatch):
    """No serial -> no fallback. Identity must never be inferred from position.

    A native-USB part with no readable serial is genuinely unidentifiable after
    the trigger; refusing is correct. This is the guard that stops the fallback
    degrading into "flash whatever is on that port".
    """
    ports = [port("COM45", C6_PID, serial="")]
    _enumerates(monkeypatch, ports)

    with pytest.raises(AssertionError, match="no bootloader port appeared"):
        _discover(before=ports, vendor=ESP_VENDOR, running_pid="1001",
                  timeout=0, running_serial="")


def test_same_port_fallback_refuses_on_duplicate_serial(monkeypatch):
    """Two present ports carrying one serial is inconsistent -- refuse, do not pick."""
    ports = [
        port("COM45", C6_PID, C6_SERIAL),
        port("COM46", C6_PID, C6_SERIAL, hash_="8&OTHER"),
    ]
    _enumerates(monkeypatch, ports)

    with pytest.raises(AssertionError, match="multiple present ports carry"):
        _discover(before=ports, vendor=ESP_VENDOR, running_pid="1001",
                  timeout=0, running_serial=C6_SERIAL)


def test_same_port_fallback_never_matches_by_class(monkeypatch):
    """A same-class port with a DIFFERENT serial must not be claimed (#503).

    This is the wrong-device-flash guard. Another 303A:1001 board sitting on the
    bus shares vendor and PID; only the serial separates them.
    """
    ports = [port("COM45", C6_PID, "AA:BB:CC:DD:EE:FF")]
    _enumerates(monkeypatch, ports)

    with pytest.raises(AssertionError, match="no bootloader port appeared"):
        _discover(before=ports, vendor=ESP_VENDOR, running_pid="1001",
                  timeout=0, running_serial=C6_SERIAL)


# --------------------------------------------------------------------------
# Pre-existing behaviour that must survive the change
# --------------------------------------------------------------------------

def test_transitioning_device_still_matched_by_serial(monkeypatch):
    """Bridge/nRF52 shape: a NEW port carrying the same serial still wins.

    The fast path must be preferred -- the fallback only runs after the loop
    finds nothing, so a genuine transition must never reach it.
    """
    before = [port("COM10", "239A:0029", C6_SERIAL)]
    after = before + [port("COM11", "239A:1234", C6_SERIAL, hash_="8&NEW")]
    _enumerates(monkeypatch, after)

    found = _discover(before=before, vendor="239A", running_pid="0029",
                      timeout=1, running_serial=C6_SERIAL)

    assert found["com"] == "COM11"


def test_new_serialless_port_with_changed_pid_still_matched(monkeypatch):
    """The serial-less class fallback for modes that expose no serial is intact."""
    before = [port("COM10", "239A:0029", serial="")]
    after = before + [port("COM11", "239A:0x50", serial="", hash_="8&NEW")]
    _enumerates(monkeypatch, after)

    found = _discover(before=before, vendor="239A", running_pid="0029",
                      timeout=1, running_serial="")

    assert found["com"] == "COM11"


def test_slow_transition_wins_over_same_port_fallback(monkeypatch):
    """Race guard: a device that transitions JUST as the loop expires.

    Raised by adversarial review. While the loop is running only the old
    app-mode port is visible; the real bootloader port appears on the final
    post-deadline enumeration. The same-port fallback must NOT claim the stale
    app port -- the transitioned port has to win, or slow devices fail
    spuriously with an esptool sync error.
    """
    old = port("COM45", C6_PID, C6_SERIAL)
    late = port("COM46", "303A:0002", C6_SERIAL, hash_="8&LATE")

    # Deterministic clock so the timing is pinned rather than raced:
    #   t=0.0  deadline computed as 0.0 + timeout
    #   t=0.0  loop check passes -> ONE iteration, which sees only the old port
    #   t=99.0 loop check fails  -> exit, then the post-deadline enumeration runs
    ticks = iter([0.0, 0.0, 99.0])
    monkeypatch.setattr(pio_flash.time, "time", lambda: next(ticks, 99.0))

    # First enumeration (inside the loop) shows only the old port; the second
    # (after the deadline) is where the transition finally appears.
    _enumerates(monkeypatch, [[old], [old, late]])

    found = _discover(before=[old], vendor=ESP_VENDOR, running_pid="1001",
                      timeout=1, running_serial=C6_SERIAL)

    assert found["com"] == "COM46", "stale app-mode port claimed over the real bootloader port"


def test_nothing_present_still_refuses(monkeypatch):
    """Device genuinely gone -> refuse. The fallback must not invent a target."""
    _enumerates(monkeypatch, [])

    with pytest.raises(AssertionError, match="no bootloader port appeared"):
        _discover(before=[], vendor=ESP_VENDOR, running_pid="1001",
                  timeout=0, running_serial=C6_SERIAL)
