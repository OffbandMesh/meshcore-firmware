#!/usr/bin/env python3
"""Tests for `pio-flash send-batch` (#850).

Pure. No serial port, no device, no flashing. The batch core takes an injected
transport so the sequencing, classification and fail-soft behaviour can be
tested without hardware.

Why batch exists: `send` is one command per run, and the flash discipline is one
human approval per run of the tool. Sweeping the CLI surface is 47 `get` keys
(#852), so at one approval each it costs 47 approvals and therefore never gets
run. The tool's granularity is what makes the work impossible, not the work.
"""
import os
import sys
import importlib.util

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_file_location(
    "pio_flash", os.path.join(os.path.dirname(os.path.abspath(__file__)), "pio-flash.py"))
pio_flash = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pio_flash)


def fake(mapping, missing=b""):
    """Transport stub: command -> raw bytes the device would emit."""
    return lambda cmd: mapping.get(cmd, missing)


# ------------------------------------------------------- read-only gating ---
# The approval model exists because a wrong-device flash happened. Batch must
# not become the way a config mutation gets scripted past a single approval.

def test_get_commands_are_allowed():
    for cmd in ("get radio", "get name", "get prv.key"):
        assert pio_flash._is_read_only_command(cmd), cmd


def test_set_commands_are_refused():
    for cmd in ("set name foo", "set password hunter2", "set freq 910.5"):
        assert not pio_flash._is_read_only_command(cmd), cmd


def test_side_effecting_commands_are_refused():
    """These change device state or transmit. A sweep has no business issuing
    them, and one approval must not cover forty of them."""
    for cmd in ("reboot", "advert", "erase", "start ota", "factory reset"):
        assert not pio_flash._is_read_only_command(cmd), cmd


def test_batch_refuses_the_whole_run_if_any_command_mutates():
    """Refuse up front, not halfway through. A partially-executed batch is the
    worst outcome: some state changed, and the operator approved none of it."""
    try:
        pio_flash._run_batch(["get radio", "set name foo"], fake({}), read_time=0)
    except pio_flash.BatchRefused as e:
        assert "set name foo" in str(e), str(e)
    else:
        assert False, "expected the batch to be refused before sending anything"


def test_nothing_is_sent_when_the_batch_is_refused():
    sent = []
    def spy(cmd):
        sent.append(cmd)
        return b"> ok\r\n"
    try:
        pio_flash._run_batch(["get radio", "reboot"], spy, read_time=0)
    except pio_flash.BatchRefused:
        pass
    assert sent == [], f"commands were sent despite refusal: {sent}"


# ------------------------------------------------------------- the results ---

def test_every_command_gets_a_result_in_order():
    rows = pio_flash._run_batch(
        ["get radio", "get name"],
        fake({"get radio": b"> 910.525,62.5,7,5\r\n", "get name": b"> Offband-T096-R\r\n"}),
        read_time=0)
    assert [r["command"] for r in rows] == ["get radio", "get name"]
    assert rows[0]["reply"].strip() == "> 910.525,62.5,7,5"


def test_ordinary_replies_are_not_redacted():
    rows = pio_flash._run_batch(
        ["get radio"], fake({"get radio": b"> 910.525,62.5,7,5\r\n"}), read_time=0)
    assert "910.525,62.5,7,5" in rows[0]["reply"], rows[0]


def test_secret_reply_is_redacted_but_still_recorded_as_answered():
    """#852 needs to assert a secret key ANSWERED without recording what it
    said. Redacted, non-empty, length preserved."""
    rows = pio_flash._run_batch(
        ["get prv.key"], fake({"get prv.key": b"> deadbeefcafe\r\n"}), read_time=0)
    r = rows[0]
    assert "deadbeef" not in r["reply"], r
    assert "redacted" in r["reply"], r
    assert r["answered"] is True, r


def test_mixed_batch_redacts_only_the_secret_one():
    rows = pio_flash._run_batch(
        ["get prv.key", "get radio"],
        fake({"get prv.key": b"> deadbeefcafe\r\n", "get radio": b"> 910.525,62.5,7,5\r\n"}),
        read_time=0)
    assert "deadbeef" not in rows[0]["reply"]
    assert "910.525,62.5,7,5" in rows[1]["reply"]


# --------------------------------------------------------------- fail-soft ---

def test_a_silent_command_is_recorded_and_the_run_continues():
    """An empty reply is a FINDING -- it is the #764 shape. It must be recorded
    as answered=False, not dropped, and must not abort the remaining commands."""
    rows = pio_flash._run_batch(
        ["get radio", "get broken", "get name"],
        fake({"get radio": b"> 910.525\r\n", "get name": b"> Offband\r\n"}, missing=b""),
        read_time=0)
    assert len(rows) == 3
    assert rows[1]["answered"] is False
    assert rows[2]["answered"] is True, "run must continue past a silent command"


def test_a_transport_exception_does_not_discard_earlier_results():
    """A device that stops responding mid-run must yield a partial result with
    the failure recorded -- not an exception that throws away 40 good replies."""
    def flaky(cmd):
        if cmd == "get boom":
            raise OSError("device went away")
        return b"> ok\r\n"
    rows = pio_flash._run_batch(["get a", "get boom", "get b"], flaky, read_time=0)
    assert len(rows) == 3
    assert rows[0]["answered"] is True
    assert rows[1]["answered"] is False and "error" in rows[1], rows[1]
    assert rows[2]["answered"] is True


def test_unknown_key_fallback_is_recorded_as_answered():
    """`??: cadfoo` IS a reply. Distinguishing it from silence is the whole
    point -- one is correct behaviour, the other is a defect."""
    rows = pio_flash._run_batch(
        ["get cadfoo"], fake({"get cadfoo": b"??: cadfoo\r\n"}), read_time=0)
    assert rows[0]["answered"] is True
    assert "??:" in rows[0]["reply"]


# ===========================================================================
# Gemini review follow-ups (#850). Three of five findings were real.
# ===========================================================================

# --- read until idle, not for a fixed window -------------------------------
# A fixed read window truncates a slow reply (falsely "unanswered") and burns
# the full window on a fast one -- 47 keys x 2s = 94s of deliberate waiting.
# It also makes #851 impossible: every command would report the same duration.

class FakeClock:
    def __init__(self): self.t = 0.0
    def __call__(self): return self.t
    def sleep(self, d): self.t += d


def test_read_stops_after_idle_gap_rather_than_burning_the_window():
    clock = FakeClock()
    chunks = [b"> 910.525", b",62.5,7,5\r\n", b"", b"", b""]
    def read_fn():
        clock.sleep(0.01)
        return chunks.pop(0) if chunks else b""
    data = pio_flash._read_until_idle(read_fn, idle_s=0.05, max_s=10.0,
                                      now_fn=clock, sleep_fn=clock.sleep)
    assert data == b"> 910.525,62.5,7,5\r\n", data
    assert clock.t < 1.0, f"should return on idle, not burn the window (t={clock.t})"


def test_read_waits_for_a_slow_first_byte():
    """A reply that starts late must not be reported as absent."""
    clock = FakeClock()
    seq = [b"", b"", b"", b"> late\r\n", b"", b"", b""]
    def read_fn():
        clock.sleep(0.02)
        return seq.pop(0) if seq else b""
    data = pio_flash._read_until_idle(read_fn, idle_s=0.05, max_s=10.0,
                                      now_fn=clock, sleep_fn=clock.sleep)
    assert b"late" in data, data


def test_read_is_capped_by_max_so_a_chattering_device_cannot_hang_the_run():
    clock = FakeClock()
    def read_fn():
        clock.sleep(0.01)
        return b"noise "
    data = pio_flash._read_until_idle(read_fn, idle_s=0.05, max_s=0.5,
                                      now_fn=clock, sleep_fn=clock.sleep)
    assert clock.t <= 1.0, f"max_s must bound the read (t={clock.t})"
    assert data, "partial data should still be returned"


# --- persistent transport failure aborts -----------------------------------

def test_persistent_transport_failure_aborts_the_rest():
    """A disconnected device should not produce 47 identical exceptions after a
    long wait. Abort, and say the remainder was skipped."""
    calls = []
    def dead(cmd):
        calls.append(cmd)
        raise OSError("device went away")
    cmds = [f"get k{i}" for i in range(10)]
    rows = pio_flash._run_batch(cmds, dead, read_time=0)
    assert len(rows) == 10, "every command still needs a row"
    assert len(calls) <= 4, f"should stop retrying a dead port, tried {len(calls)}"
    assert any(r.get("skipped") for r in rows), "remainder must be marked skipped"


def test_an_isolated_failure_does_not_abort():
    """One bad command is not a dead device -- the run must continue."""
    def flaky(cmd):
        if cmd == "get boom":
            raise OSError("transient")
        return b"> ok\r\n"
    rows = pio_flash._run_batch(["get a", "get boom", "get b", "get c"], flaky, read_time=0)
    assert rows[3]["answered"] is True, "run aborted on a single transient failure"
    assert not any(r.get("skipped") for r in rows)


# --- answered means the transport yielded bytes ----------------------------

def test_whitespace_only_reply_counts_as_answered():
    """`answered` is about whether the device responded AT ALL. Content is a
    separate question -- conflating them turns a valid quiet reply into a
    phantom failure and sends someone debugging nothing."""
    rows = pio_flash._run_batch(["get quiet"], fake({"get quiet": b"\r\n"}), read_time=0)
    assert rows[0]["answered"] is True, rows[0]
    assert rows[0]["reply"].strip() == "", rows[0]


def test_empty_content_is_still_visible_as_a_finding():
    """#764's shape: matched the key, wrote nothing. Must remain detectable."""
    rows = pio_flash._run_batch(["get quiet"], fake({"get quiet": b"\r\n"}), read_time=0)
    assert rows[0]["empty_reply"] is True, rows[0]



# =========================================================================
# Port-open settle (found on ST-P, an ESP32-S3)
#
# Opening a serial port asserts DTR/RTS and REBOOTS an ESP32. The batch path
# allowed 0.2s, which was only ever adequate because the first board tested
# was an nRF52 that does not reboot on open. On ESP32 the first commands land
# mid-boot and answer empty or garbled -- the #764 signature -- so the sweep
# would have reported false defects indistinguishable from real ones.
# =========================================================================

class _FakeClock:
    """Controllable time. Real sleeps would make these tests take 12 seconds."""

    def __init__(self):
        self.t = 0.0

    def now(self):
        return self.t

    def sleep(self, d):
        self.t += d


def test_settle_waits_out_a_boot_banner_before_returning():
    """A device that chatters on open is booting. Do not send into that."""
    clk = _FakeClock()
    chunks = [b"ESP-ROM:esp32s3", b"boot: loading app", b"MeshCore v1.16", b"", b"", b""]

    def read():
        clk.t += 0.1
        return chunks.pop(0) if chunks else b""

    got = pio_flash._await_device_ready(read, quiet_s=0.6, grace_s=1.0,
                                        max_s=12.0, now_fn=clk.now,
                                        sleep_fn=clk.sleep)
    assert b"ESP-ROM" in got and b"MeshCore" in got, got
    assert clk.t >= 0.6, "returned before the device went quiet"


def test_settle_returns_fast_when_the_board_is_already_up():
    """A silent board must not cost the full 12s window every run. This is why
    it is not _read_until_idle, which waits for a first byte for the whole
    window and would burn max_s on every quiet device."""
    clk = _FakeClock()

    def read():
        clk.t += 0.01
        return b""

    got = pio_flash._await_device_ready(read, quiet_s=0.6, grace_s=1.0,
                                        max_s=12.0, now_fn=clk.now,
                                        sleep_fn=clk.sleep)
    assert got == b"", got
    assert clk.t < 2.0, f"silent board burned {clk.t}s; grace is 1.0s"


def test_settle_is_bounded_when_the_device_never_stops_chattering():
    """A board in a boot loop would otherwise hang the run forever."""
    clk = _FakeClock()

    def read():
        clk.t += 0.1
        return b"rst:0x3 PANIC "

    pio_flash._await_device_ready(read, quiet_s=0.6, grace_s=1.0, max_s=5.0,
                                  now_fn=clk.now, sleep_fn=clk.sleep)
    assert clk.t <= 5.5, f"ran {clk.t}s past a 5.0s bound"


def test_settle_returns_the_banner_rather_than_discarding_it():
    """The drained bytes are evidence that a reset happened. Dropping them
    silently would hide the very behaviour that broke the ESP32 run."""
    clk = _FakeClock()
    chunks = [b"rst:0x1 (POWERON)", b"", b"", b""]

    def read():
        clk.t += 0.2
        return chunks.pop(0) if chunks else b""

    got = pio_flash._await_device_ready(read, now_fn=clk.now, sleep_fn=clk.sleep)
    assert got == b"rst:0x1 (POWERON)", got



def test_settle_grace_covers_a_silent_boot_stage():
    """Adversarial review, HIGH: silence is AMBIGUOUS -- an already-booted board
    and a board in a silent boot stage look identical. At a 1.0s grace an ESP32
    whose banner had not started yet was declared ready, and command 1 collided
    with the banner, returning empty -- indistinguishable from #764."""
    assert pio_flash.SETTLE_GRACE_S >= 3.0, pio_flash.SETTLE_GRACE_S
    clk = _FakeClock()
    chunks = [b""] * 25 + [b"ESP-ROM:esp32s3", b"", b"", b""]

    def read():
        clk.t += 0.1
        return chunks.pop(0) if chunks else b""

    got = pio_flash._await_device_ready(read, quiet_s=0.6,
                                        grace_s=pio_flash.SETTLE_GRACE_S,
                                        max_s=12.0, now_fn=clk.now,
                                        sleep_fn=clk.sleep)
    assert b"ESP-ROM" in got, "banner after 2.5s of silence was missed"


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as e:
                failures += 1
                print(f"FAIL {name}: {e}")
            except Exception as e:
                failures += 1
                print(f"ERROR {name}: {type(e).__name__}: {e}")
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
