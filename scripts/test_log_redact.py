#!/usr/bin/env python3
"""Tests for log_redact (#667).

Pure, no hardware. Run: python scripts/test_log_redact.py
  or python -m pytest scripts/test_log_redact.py

Coordinates here are SYNTHETIC. Never put a real fix in a repo file -- that is
the class of data this module exists to keep out of shipped artefacts.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import log_redact as lr

LAT = 12345678       # synthetic, 8 digits, positive
LON = -87654321      # synthetic, 8 digits, negative
GPS = (f"[12319] [GPS] detected=1 active=1 fix=1 baud=9600 "
       f"lat={LAT} lon={LON} alt_cm=25150 sats=7 time=1786500000")


# -------------------------------------------------------------- position ---
# The owner's ruling: redaction must NOT destroy the diagnosis. Reading someone
# else's log, you still have to tell a real fix from 0,0 from garbage.

def test_real_fix_is_classified_not_erased():
    out = lr.redact_line(GPS)
    assert "12345678" not in out, out
    assert "87654321" not in out, out
    # sign and digit count survive: a sign flip or a 1e-N scaling bug stays visible
    assert "lat=<redacted:+8d>" in out, out
    assert "lon=<redacted:-8d>" in out, out


def test_zero_is_kept_because_zero_is_the_diagnosis():
    out = lr.redact_line("[GPS] detected=1 active=1 fix=0 lat=0 lon=0 sats=0")
    assert "lat=0" in out, out
    assert "lon=0" in out, out
    assert "redacted" not in out, out


def test_real_fix_and_no_fix_are_distinguishable():
    """The whole point: 'has a fix somewhere' must not look like '0,0'."""
    fix = lr.redact_line(GPS)
    nofix = lr.redact_line("[GPS] lat=0 lon=0")
    assert fix != nofix
    assert "redacted" in fix and "redacted" not in nofix


def test_out_of_range_is_flagged_as_garbage():
    """Swapped lat/lon, or a scaling bug, pushes the value out of range. That is
    'something entirely different' and the reader must be able to see it."""
    out = lr.redact_line("[GPS] lat=999999999 lon=200000000")
    assert "lat=<redacted:+9d,oor>" in out, out
    assert "lon=<redacted:+9d,oor>" in out, out


def test_scaling_bug_stays_visible_via_digit_count():
    """A 1e-4 scale error yields a plausible-looking but wrong magnitude. The
    digit count is what exposes it, so it must differ from a healthy fix."""
    healthy = lr.redact_line(f"lat={LAT}")
    scaled = lr.redact_line("lat=1234")
    assert healthy != scaled
    assert "<redacted:+8d>" in healthy and "<redacted:+4d>" in scaled


def test_altitude_is_not_redacted():
    """Altitude alone does not locate anyone, and an absurd altitude is signal."""
    assert "alt_cm=25150" in lr.redact_line(GPS)


def test_non_position_fields_survive():
    out = lr.redact_line(GPS)
    for field in ("detected=1", "active=1", "fix=1", "baud=9600", "sats=7",
                  "time=1786500000"):
        assert field in out, f"{field} missing from {out}"


# ------------------------------------------------------------ credentials ---

def test_password_now_echo_still_redacted():
    """CommonCLI.cpp echoes `password now: <value>` -- the #379/#380 leak."""
    assert "hunter2" not in lr.redact_line("password now: hunter2")


def test_ssid_and_secret_key_value():
    assert "MyNet" not in lr.redact_line("[WifiBootstrap] Saved WiFi SSID=MyNet; attempting STA.")
    assert "s3kr1t" not in lr.redact_line("mqtt_password = s3kr1t")


def test_cli_reply_net_still_applies():
    assert "tsunami" not in lr.redact_line("  > tsunami")


# ------------------------------------------------------------------ bytes ---

def test_redact_bytes_roundtrip():
    out = lr.redact_bytes(GPS.encode())
    assert b"12345678" not in out
    assert b"sats=7" in out


def test_undecodable_bytes_are_preserved():
    """A corrupt ring is itself diagnostic and must not be silently mangled."""
    raw = b"[GPS] lat=0 \xff\xfe garbage"
    out = lr.redact_bytes(raw)
    assert b"\xff\xfe" in out, out


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
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
