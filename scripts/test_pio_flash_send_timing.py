#!/usr/bin/env python3
"""Tests for round-trip timing on a single `pio-flash send` (#896).

Pure. No serial port, no device.

Why single `send` needs its own timing when batch already has it: batch is
`get`-only by construction and stays that way -- volatile commands must not ride
in on one approval. That means batch refuses every command #893 asks about:

    wifi          REFUSED        get radio     ACCEPTED
    wifi on 5     REFUSED        get prv.key   ACCEPTED

So the instrument built in #851 cannot take the measurement #893 needs. Single
`send` is one command under one approval with a human watching, which is the
right shape for a state-touching command -- and it is the only shape that can
time one.
"""
import os
import sys
import importlib.util

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_file_location(
    "pio_flash", os.path.join(os.path.dirname(os.path.abspath(__file__)), "pio-flash.py"))
pio_flash = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pio_flash)


class Clock:
    def __init__(self): self.t = 0.0
    def __call__(self): return self.t
    def advance(self, d): self.t += d


# ------------------------------------------------------------ the measurement ---

def test_timing_is_reported_for_a_single_command():
    clock = Clock()

    def transport(cmd):
        clock.advance(0.30)
        return pio_flash.TransportReply(data=b"> ok\r\n", first_byte_s=0.05)

    row = pio_flash._timed_send("wifi", transport, now_fn=clock)
    assert row["elapsed_s"] == 0.30, row
    assert row["first_byte_s"] == 0.05, row
    assert row["answered"] is True, row


def test_volatile_commands_are_accepted_here():
    """The whole point. Batch refuses these; single send must not, because a
    human approved this one run and is watching it."""
    clock = Clock()
    for cmd in ("wifi", "wifi on 5", "wifi off", "wifi reset", "advert"):
        row = pio_flash._timed_send(cmd, lambda c: b"> ok\r\n", now_fn=clock)
        assert row["command"] == cmd
        assert row["answered"] is True, cmd


def test_batch_still_refuses_what_single_send_allows():
    """Guards the boundary: adding timing here must not have relaxed the batch
    gate. If this ever fails, volatile commands can ride in on one approval."""
    for cmd in ("wifi", "wifi on 5", "wifi off", "wifi reset"):
        assert not pio_flash._is_read_only_command(cmd), cmd


# ------------------------------------------------------- no second implementation ---

def test_it_reuses_the_shared_timing_helpers():
    """Two timing implementations drift, and the copy that drifts is the one
    that lies. `_timed_send` must lean on the same helpers batch uses."""
    assert hasattr(pio_flash, "_read_until_idle")
    assert hasattr(pio_flash, "TransportReply")
    import inspect
    src = inspect.getsource(pio_flash._timed_send)
    assert "TransportReply" in src, "should consume the shared reply type"


def test_a_plain_bytes_transport_still_works():
    row = pio_flash._timed_send("get radio", lambda c: b"> 910.525\r\n")
    assert row["answered"] is True
    assert row["first_byte_s"] is None, row
    assert "910.525" in row["reply"], row


# --------------------------------------------------------------- the failure modes ---

def test_a_silent_command_is_reported_as_unanswered_with_its_elapsed_time():
    """"Never answered" is the #893 symptom and must carry a duration -- knowing
    it waited the full window is the finding."""
    clock = Clock()

    def silent(cmd):
        clock.advance(3.0)
        return b""

    row = pio_flash._timed_send("wifi", silent, now_fn=clock)
    assert row["answered"] is False, row
    assert row["elapsed_s"] == 3.0, row


def test_answered_but_empty_stays_distinct_from_silence():
    """#764's shape. Same distinction batch makes; it must not be lost here."""
    row = pio_flash._timed_send("get quiet", lambda c: b"\r\n")
    assert row["answered"] is True and row["empty_reply"] is True, row


def test_a_transport_error_is_recorded_not_raised():
    clock = Clock()

    def boom(cmd):
        clock.advance(0.2)
        raise OSError("port vanished")

    row = pio_flash._timed_send("wifi", boom, now_fn=clock)
    assert row["answered"] is False
    assert "error" in row, row
    assert row["elapsed_s"] == 0.2, row


# ------------------------------------------------------------------- redaction ---

def test_a_secret_reply_is_still_redacted_and_still_timed():
    """Timing must not cost the redaction, and redaction must not cost the
    timing. #849 exists because output on this path reached a transcript."""
    clock = Clock()

    def transport(cmd):
        clock.advance(0.4)
        return b"> deadbeefcafe\r\n"

    row = pio_flash._timed_send("get prv.key", transport, now_fn=clock)
    assert "deadbeef" not in row["reply"], row
    assert row["elapsed_s"] == 0.4, row
    assert row["reply_bytes"] == 16, row

CRLF = b"\r\n"



# ===========================================================================
# Gemini review follow-ups (#896)
# ===========================================================================

def test_read_until_idle_streams_chunks_as_they_arrive():
    """Restores what the refactor cost: `send` used to print output as it
    landed. Buffering everything until idle means a slow multi-line reply shows
    nothing for seconds and then dumps -- the user loses any signal that the
    command is still working."""
    clock = Clock()
    seen = []
    chunks = [b"line one" + b"\r\n", b"line two" + b"\r\n", b"", b"", b"", b"", b""]

    def read_fn():
        clock.advance(0.01)
        return chunks.pop(0) if chunks else b""

    pio_flash._read_until_idle(read_fn, idle_s=0.05, max_s=5.0,
                               now_fn=clock, sleep_fn=lambda d: None,
                               on_chunk=seen.append)
    assert seen, "no chunks were streamed"
    assert b"".join(seen) == b"line one" + b"\r\n" + b"line two" + b"\r\n", seen


def test_streaming_is_optional_and_absent_by_default():
    clock = Clock()
    chunks = [b"x" + b"\r\n", b"", b"", b"", b"", b""]
    def read_fn():
        clock.advance(0.01)
        return chunks.pop(0) if chunks else b""
    data = pio_flash._read_until_idle(read_fn, idle_s=0.05, max_s=5.0,
                                      now_fn=clock, sleep_fn=lambda d: None)
    assert data == b"x" + b"\r\n"


def test_send_idle_default_is_looser_than_batch():
    """A multi-stage reply that pauses between stages would be TRUNCATED by an
    aggressive idle gap. Batch only issues `get` keys, which answer in one shot;
    single `send` reaches commands that may print in stages, so its default must
    be more forgiving."""
    assert pio_flash.SEND_IDLE_DEFAULT > pio_flash.BATCH_IDLE_DEFAULT, (
        pio_flash.SEND_IDLE_DEFAULT, pio_flash.BATCH_IDLE_DEFAULT)


def test_a_transport_returning_a_string_is_refused_not_crashed():
    """The shared path is now used by two callers, so a bad transport crashes
    both. A str would have reached the redactor and raised TypeError."""
    row = pio_flash._timed_send("get x", lambda c: "not bytes")
    assert row["answered"] is False, row
    assert "error" in row, row


# ===========================================================================
# Found on HARDWARE (T096, COM42), not by any fake clock.
# ===========================================================================

def test_round_trip_excludes_the_idle_wait():
    """First real run: elapsed_s 1.2188, first_byte_s 0.2136 -- a difference of
    1.005, which is exactly SEND_IDLE_DEFAULT. elapsed_s was round-trip PLUS the
    idle timeout, so every measurement carried a constant inflation, and one
    that changes with --idle-time. Two runs at different settings were not
    comparable, and #893's 20-25s would have been compared against an offset
    this tool invented.

    No fake-clock test could catch this: the clock only advanced by what the
    stub chose, so the idle tail never existed to be measured.
    """
    clock = Clock()
    seq = [b"> ok" + b"\r\n"] + [b""] * 40

    def read_fn():
        clock.advance(0.01)
        return seq.pop(0) if seq else b""

    st = {}
    pio_flash._read_until_idle(read_fn, idle_s=0.5, max_s=10.0,
                               now_fn=clock, sleep_fn=lambda d: None,
                               stats=st, origin=0.0)
    assert st["first_byte_s"] == 0.01, st
    assert "last_byte_s" in st, "the true round trip must be recorded"
    assert st["last_byte_s"] == 0.01, st
    assert clock() > 0.5, "the read did wait out the idle gap"


def test_timed_send_reports_round_trip_separately_from_wall_time():
    clock = Clock()

    def transport(cmd):
        clock.advance(2.0)      # includes an idle tail the caller must not be charged
        return pio_flash.TransportReply(data=b"> ok" + b"\r\n",
                                        first_byte_s=0.2, last_byte_s=0.3)

    row = pio_flash._timed_send("get radio", transport, now_fn=clock)
    assert row["elapsed_s"] == 2.0, row
    assert row["round_trip_s"] == 0.3, row
    assert row["round_trip_s"] < row["elapsed_s"], row


def test_round_trip_is_none_when_nothing_answered():
    row = pio_flash._timed_send("get gone", lambda c: b"")
    assert row["answered"] is False
    assert row["round_trip_s"] is None, row


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
