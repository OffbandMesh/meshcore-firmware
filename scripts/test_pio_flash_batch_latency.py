#!/usr/bin/env python3
"""Tests for per-command latency capture in batch runs (#851).

Pure. No serial port, no device. Timing is driven by an injected clock so the
assertions are exact rather than flaky.

WHY THIS EXISTS, in the owner's words: CLI responses over the client path are
slow to return, sometimes never return, and handling is spotty. Client-side work
has been done and the owner is not convinced it is client-side only.

Nobody can currently answer that, because nothing measures it. A serial baseline
settles it: if the firmware answers every key promptly over the console while the
over-the-air path is slow or lossy, the fault is in the transport or the client.
If serial is also slow on particular keys, it is ours.
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


# ------------------------------------------------------------- basic timing ---

def test_every_command_records_elapsed_time():
    clock = Clock()
    def transport(cmd):
        clock.advance(0.4 if cmd == "get slow" else 0.05)
        return b"> ok\r\n"
    rows = pio_flash._run_batch(["get fast", "get slow"], transport,
                                read_time=3, now_fn=clock)
    assert rows[0]["elapsed_s"] == 0.05, rows[0]
    assert rows[1]["elapsed_s"] == 0.4, rows[1]


def test_a_slow_key_is_distinguishable_from_a_fast_one():
    """The entire point: which key is slow, not just that something is."""
    clock = Clock()
    def transport(cmd):
        clock.advance({"get a": 0.02, "get b": 1.5, "get c": 0.03}[cmd])
        return b"> ok\r\n"
    rows = pio_flash._run_batch(["get a", "get b", "get c"], transport,
                                read_time=3, now_fn=clock)
    slowest = max(rows, key=lambda r: r["elapsed_s"])
    assert slowest["command"] == "get b", rows


# ------------------------------------------- timeout vs answered-but-empty ---
# These are DIFFERENT failures and conflating them destroys the diagnosis.
# "answered with nothing" is #764's shape. "never answered" is the owner's
# symptom. Both must be visible, and both must carry timing.

def test_a_command_that_never_answers_still_records_its_elapsed_time():
    clock = Clock()
    def transport(cmd):
        clock.advance(3.0)          # burned the full cap
        return b""
    rows = pio_flash._run_batch(["get gone"], transport, read_time=3, now_fn=clock)
    assert rows[0]["answered"] is False, rows[0]
    assert rows[0]["elapsed_s"] == 3.0, rows[0]


def test_timeout_and_empty_reply_are_separately_visible():
    clock = Clock()
    def transport(cmd):
        clock.advance(0.05)
        return b"\r\n" if cmd == "get quiet" else b""
    rows = pio_flash._run_batch(["get quiet", "get gone"], transport,
                                read_time=3, now_fn=clock)
    quiet, gone = rows
    assert (quiet["answered"], quiet["empty_reply"]) == (True, True), quiet
    assert (gone["answered"], gone["empty_reply"]) == (False, False), gone


# ------------------------------------------------------------- first byte ---

def test_first_byte_latency_is_recorded_when_the_transport_reports_it():
    """Updated after review: the transport reports timing via the explicit
    TransportReply type, not a bare tuple. See
    test_a_bare_tuple_is_not_mistaken_for_timing_data for why."""
    """Time-to-first-byte separates 'the device was slow to start answering'
    from 'the device answered at length'. A transport may return
    (data, first_byte_s); one that returns plain bytes still works."""
    def transport(cmd):
        return pio_flash.TransportReply(data=b"> ok\r\n", first_byte_s=0.12)
    rows = pio_flash._run_batch(["get x"], transport, read_time=3)
    assert rows[0]["first_byte_s"] == 0.12, rows[0]
    assert rows[0]["answered"] is True


def test_a_plain_bytes_transport_still_works_and_reports_no_first_byte():
    rows = pio_flash._run_batch(["get x"], lambda c: b"> ok\r\n", read_time=3)
    assert rows[0]["answered"] is True
    assert rows[0]["first_byte_s"] is None, rows[0]


# ---------------------------------------------------- timing survives redaction ---

def test_a_redacted_secret_reply_still_reports_true_timing_and_length():
    """Redaction must not cost the measurement. #852 asserts a secret key
    ANSWERED and how long it took, without recording what it said."""
    clock = Clock()
    def transport(cmd):
        clock.advance(0.33)
        return b"> deadbeefcafe\r\n"
    rows = pio_flash._run_batch(["get prv.key"], transport, read_time=3, now_fn=clock)
    r = rows[0]
    assert "deadbeef" not in r["reply"], r
    assert r["elapsed_s"] == 0.33, r
    assert r["answered"] is True
    assert r["reply_bytes"] == 16, r


# ------------------------------------------------------------------ skipped ---

def test_skipped_rows_carry_no_timing():
    """A command that was never attempted must not report a duration -- a zero
    there would read as 'answered instantly' in any summary."""
    def dead(cmd):
        raise OSError("gone")
    rows = pio_flash._run_batch([f"get k{i}" for i in range(8)], dead, read_time=3)
    skipped = [r for r in rows if r.get("skipped")]
    assert skipped, "expected an abort"
    assert all(r.get("elapsed_s") is None for r in skipped), skipped[0]


# ===========================================================================
# Gemini review follow-ups (#851). Two were real measurement defects.
# ===========================================================================

def test_transport_reply_is_an_explicit_type_not_a_bare_tuple():
    """`isinstance(result, tuple)` was a weak contract: any 2-tuple returned for
    an unrelated reason would be silently unpacked as timing data. An explicit
    type cannot be hit by accident."""
    r = pio_flash.TransportReply(data=b"> ok\r\n", first_byte_s=0.07)
    rows = pio_flash._run_batch(["get x"], lambda c: r, read_time=3)
    assert rows[0]["first_byte_s"] == 0.07, rows[0]
    assert rows[0]["answered"] is True


def test_a_bare_tuple_is_not_mistaken_for_timing_data():
    """A transport returning (data, status) for some other purpose must not have
    its second element silently recorded as a latency."""
    rows = pio_flash._run_batch(["get x"], lambda c: (b"> ok\r\n", "some-status"),
                                read_time=3)
    assert rows[0]["first_byte_s"] is None, rows[0]


def test_first_byte_is_measured_from_the_same_origin_as_elapsed():
    """Both timers must start when the command starts. Measuring first-byte from
    AFTER the write/flush leaves the two figures uncorrelatable -- you cannot say
    where the time went, which is the only reason to collect them."""
    clock = Clock()
    state = {}

    def timed(cmd):
        state["t0"] = clock()
        clock.advance(0.10)                       # write + flush
        first = round(clock() - state["t0"], 4)   # relative to command start
        clock.advance(0.40)                       # reply streams in
        return pio_flash.TransportReply(data=b"> ok\r\n", first_byte_s=first)

    rows = pio_flash._run_batch(["get x"], timed, read_time=3, now_fn=clock)
    r = rows[0]
    assert r["elapsed_s"] == 0.5, r
    assert r["first_byte_s"] == 0.10, r
    assert r["first_byte_s"] <= r["elapsed_s"], "first byte cannot follow completion"


def test_read_until_idle_anchors_first_byte_to_a_supplied_origin():
    """So the caller can anchor it to the moment the command was written, rather
    than to whenever the read loop happened to start."""
    clock = Clock()
    seq = [b"", b"", b"> hi\r\n", b"", b"", b"", b"", b""]

    def read_fn():
        clock.advance(0.01)
        return seq.pop(0) if seq else b""

    st = {}
    origin = clock() - 0.20          # pretend the write happened 0.20s ago
    pio_flash._read_until_idle(read_fn, idle_s=0.05, max_s=5.0,
                               now_fn=clock, sleep_fn=lambda d: None,
                               stats=st, origin=origin)
    assert abs(st["first_byte_s"] - 0.23) < 0.001, st


def test_first_byte_granularity_is_declared():
    """The read loop can only notice data when a blocking read returns, so
    first_byte_s carries an error of up to one read timeout. Consumers must be
    able to discover that rather than over-trusting the number."""
    assert isinstance(pio_flash.FIRST_BYTE_GRANULARITY_NOTE, str)
    assert "granularity" in pio_flash.FIRST_BYTE_GRANULARITY_NOTE.lower()


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
