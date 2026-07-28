#!/usr/bin/env python3
"""Tests for companion_harness (#413).

Pure frame-core tests run with no hardware. The hardware-in-the-loop test
(APP_START -> SELF_INFO against an attached device) SKIPS cleanly when no
device is present, so this is safe in cloud CI.

Run: python scripts/test_companion_harness.py   (self-contained)
  or python -m pytest scripts/test_companion_harness.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import companion_harness as ch


# --------------------------------------------------------------------------
# Pure frame core (no hardware)
# --------------------------------------------------------------------------
# Wire framing (ArduinoSerialInterface): host->device frames start '<', the
# device->host frames start '>', both followed by a 16-bit little-endian length
# then the payload.

def test_encode_frame_host_to_device_uses_lt_and_len16le():
    assert ch.encode_frame(b"\x01hello") == b"<" + struct.pack("<H", 6) + b"\x01hello"


def test_encode_empty_frame():
    assert ch.encode_frame(b"") == b"<\x00\x00"


def test_decoder_extracts_single_frame():
    d = ch.FrameDecoder()
    out = list(d.feed(b">" + struct.pack("<H", 3) + b"abc"))
    assert out == [b"abc"]


def test_decoder_skips_leading_noise():
    # Raw debug text (e.g. the [GPS] lines) on the line must be ignored.
    d = ch.FrameDecoder()
    junk = b"[GPS] detected=0 active=0\r\n"
    out = list(d.feed(junk + b">" + struct.pack("<H", 3) + b"abc"))
    assert out == [b"abc"]


def test_decoder_handles_split_across_feeds():
    d = ch.FrameDecoder()
    frame = b">" + struct.pack("<H", 4) + b"data"
    assert list(d.feed(frame[:3])) == []       # partial
    assert list(d.feed(frame[3:])) == [b"data"]


def test_decoder_handles_multiple_frames_one_feed():
    d = ch.FrameDecoder()
    f1 = b">" + struct.pack("<H", 2) + b"hi"
    f2 = b">" + struct.pack("<H", 3) + b"bye"
    assert list(d.feed(f1 + f2)) == [b"hi", b"bye"]


def test_decoder_zero_length_frame():
    d = ch.FrameDecoder()
    assert list(d.feed(b">\x00\x00")) == [b""]


def test_decoder_resyncs_past_spurious_start_with_bogus_length():
    # A stray '>' in noise followed by a huge bogus length must not stall the
    # decoder or buffer unboundedly -- it resyncs and still finds the next real
    # frame. (No legit device frame exceeds the firmware MAX_FRAME_SIZE=176.)
    d = ch.FrameDecoder()
    bogus = b">" + struct.pack("<H", 60000) + b"garbage"
    real = b">" + struct.pack("<H", 3) + b"abc"
    out = list(d.feed(bogus + real))
    assert b"abc" in out


def test_app_start_frame_shape():
    # [CMD_APP_START=1][7 reserved][app_name...], len >= 8, then framed for the wire.
    payload = ch.build_app_start(b"harness")
    assert payload[0] == ch.CMD_APP_START
    assert len(payload) >= 8
    assert payload[8:] == b"harness"


def test_parse_self_info_extracts_pubkey():
    # [RESP_CODE_SELF_INFO=5][adv_type][tx][max_tx][pubkey*32]...
    body = bytes([ch.RESP_CODE_SELF_INFO, 1, 22, 22]) + bytes(range(32)) + b"\x00" * 20
    info = ch.parse_self_info(body)
    assert info["pubkey"] == bytes(range(32))
    assert info["tx_power"] == 22


# --------------------------------------------------------------------------
# Hardware-in-the-loop (skips without a device)
# --------------------------------------------------------------------------
def test_hardware_app_start_handshake():
    dev = ch.resolve_device_port("hv4-bench-1")
    if dev is None:
        print("SKIP: hv4-bench-1 not attached")
        return
    with ch.CompanionSession(dev) as s:
        info = s.app_start()
        assert len(info["pubkey"]) == 32
        print(f"  SELF_INFO ok: pubkey={info['pubkey'].hex()[:12]}… tx={info['tx_power']}")


def _run_all():
    fns = [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]
    failed = 0
    for fn in fns:
        try:
            fn()
            print(f"PASS {fn.__name__}")
        except AssertionError as e:
            failed += 1
            print(f"FAIL {fn.__name__}: {e}")
    print(f"\n{len(fns)-failed}/{len(fns)} passed")
    return failed


if __name__ == "__main__":
    sys.exit(1 if _run_all() else 0)
