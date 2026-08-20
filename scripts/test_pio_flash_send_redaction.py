#!/usr/bin/env python3
"""Tests for secret redaction on the `pio-flash send` output path (#849).

Pure. No serial port, no device, no flashing.

Why this exists: `cmd_send` streamed raw serial bytes straight to stdout. Three
CLI keys return secrets -- `get prv.key` (the node private key, and serial-only
by design, which is exactly the path this tool uses), `get guest.password`, and
`get bridge.secret`. Anything reading that stdout -- an agent, a log, a session
transcript -- received the value, and scrubbing afterwards is too late.

The two hard constraints pulled in opposite directions and both are pinned here:

  1. Secret-bearing replies MUST NOT reach stdout in the clear.
  2. Ordinary replies MUST pass through byte-for-byte. `get radio` has to keep
     printing `> 910.525,62.5,7,5` or every existing use of this tool regresses
     and the CLI sweep it was built for cannot assert on anything.

A blanket redact satisfies (1) and destroys (2). Hence command-aware redaction.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import importlib.util
_spec = importlib.util.spec_from_file_location(
    "pio_flash", os.path.join(os.path.dirname(os.path.abspath(__file__)), "pio-flash.py"))
pio_flash = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pio_flash)


# ------------------------------------------------- which commands are secret ---

def test_known_secret_keys_are_recognised():
    for cmd in ("get prv.key", "get guest.password", "get bridge.secret"):
        assert pio_flash._is_secret_command(cmd), cmd


def test_ordinary_keys_are_not_secret():
    for cmd in ("get radio", "get name", "get cad", "get freq", "advert"):
        assert not pio_flash._is_secret_command(cmd), cmd


def test_secret_detection_tolerates_whitespace_and_case():
    assert pio_flash._is_secret_command("  GET PRV.KEY  ")
    assert pio_flash._is_secret_command("get   prv.key")


def test_set_password_is_secret_too():
    """`set password hunter2` puts the secret in the COMMAND, and CommonCLI
    echoes it back in the reply. Both directions have to be covered."""
    assert pio_flash._is_secret_command("set password hunter2")
    assert pio_flash._is_secret_command("set guest.password hunter2")


def test_a_key_that_merely_starts_with_a_secret_name_is_not_secret():
    """Prefix-matching is the exact defect class this repo just spent a session
    removing from the firmware. Do not reintroduce it in the tooling."""
    assert not pio_flash._is_secret_command("get prv.keyfoo")


# ----------------------------------------------------- streaming redaction ---

def _drain(redactor, chunks):
    return "".join(redactor.feed(c) for c in chunks) + redactor.flush()


def test_ordinary_reply_passes_through_unchanged():
    r = pio_flash._ReplyRedactor(secret=False)
    assert _drain(r, [b"get radio\r\n  > 910.525,62.5,7,5\r\n"]) == \
        "get radio\r\n  > 910.525,62.5,7,5\r\n"


def test_secret_reply_is_redacted():
    r = pio_flash._ReplyRedactor(secret=True)
    out = _drain(r, [b"get guest.password\r\n  > hunter2\r\n"])
    assert "hunter2" not in out, out
    assert "redacted" in out, out


def test_secret_split_across_chunks_is_still_redacted():
    """The real failure mode: serial arrives in arbitrary 1024-byte reads, so a
    secret can straddle two of them. Redacting per-chunk misses it entirely."""
    r = pio_flash._ReplyRedactor(secret=True)
    out = _drain(r, [b"  > hun", b"ter2\r\n"])
    assert "hunter2" not in out, out
    assert "redacted" in out, out


def test_length_is_preserved_so_shape_assertions_still_work():
    """The sweep (#852) asserts a secret key ANSWERED, without recording what it
    said. That needs the length to survive redaction."""
    r = pio_flash._ReplyRedactor(secret=True)
    out = _drain(r, [b"  > abcdefghij\r\n"])
    assert "abcdefghij" not in out, out
    assert "10" in out, f"redacted marker should carry the length: {out}"


def test_trailing_partial_line_is_flushed_through_redaction():
    """A reply with no trailing newline must not escape unredacted at flush."""
    r = pio_flash._ReplyRedactor(secret=True)
    out = _drain(r, [b"  > hunter2"])
    assert "hunter2" not in out, out


def test_nothing_is_lost_for_ordinary_output_without_trailing_newline():
    r = pio_flash._ReplyRedactor(secret=False)
    assert _drain(r, [b"  > 910.525"]) == "  > 910.525"


def test_undecodable_bytes_do_not_crash_or_vanish():
    """A garbled reply is itself diagnostic -- exactly the #765 symptom. It must
    survive rather than be swallowed."""
    r = pio_flash._ReplyRedactor(secret=False)
    out = _drain(r, [b"  > \xff\xfe junk\r\n"])
    assert "junk" in out, out



# =========================================================================
# Adversarial-review findings (#892 Gemini gate)
# =========================================================================

def test_a_SHORT_secret_survives_nothing_even_with_the_marker_destroyed():
    """CRITICAL, found by review. Every locating rule needs something to match
    on -- a `>` marker, or a value long enough to look opaque. A log splice can
    destroy the marker and leave a SHORT secret behind:

        [D][main.cpp:123] Loop hunter2

    No `>`, 7 characters, no keyword. Nothing matched and it was emitted
    verbatim. On a command already classified secret we do not need to FIND the
    value -- we refuse to emit the line."""
    r = pio_flash._ReplyRedactor(secret=True)
    out = _drain(r, [b"[D][main.cpp:123] Loop hunter2\r\n"])
    assert "hunter2" not in out, out
    assert "redacted" in out, out


def test_the_unknown_key_fallback_stays_visible_on_a_secret_command():
    """`??:` echoes the KEY, never a value. The sweep needs it to report a
    broken key, so it must survive the fail-closed path."""
    r = pio_flash._ReplyRedactor(secret=True)
    out = _drain(r, [b"??: bridge.secret\r\n"])
    assert "bridge.secret" in out, out


def test_a_clean_secret_reply_still_reports_its_TRUE_length():
    """Fail-closed must not clobber the good path: the sweep asserts a secret
    key ANSWERED using the length in the marker."""
    r = pio_flash._ReplyRedactor(secret=True)
    out = _drain(r, [b"  > abcdefghij\r\n"])
    assert "abcdefghij" not in out and "10" in out, out


def test_a_non_secret_command_is_never_blanket_redacted():
    """The fail-closed rule must be scoped to secret commands only, or every
    ordinary reply becomes unreadable and the sweep can assert nothing."""
    r = pio_flash._ReplyRedactor(secret=False)
    out = _drain(r, [b"  > 910.525,62.5,7,5\r\n"])
    assert "910.525" in out, out



def test_device_emitted_redaction_marker_cannot_suppress_fail_closed():
    """CRITICAL, second-pass review. The fail-closed branch used to skip when
    the literal "<redacted:" appeared in the output -- but that string can come
    FROM THE DEVICE. Whether we redacted is OUR fact; read it from our own
    output, never from device text."""
    r = pio_flash._ReplyRedactor(secret=True)
    out = _drain(r, [b"hunter2 <redacted:benign>\r\n"])
    assert "hunter2" not in out, out


def test_a_fallback_must_BE_the_reply_not_merely_appear_in_it():
    """HIGH, second-pass review. `??:` anywhere on the line used to bypass the
    guard, so `... ??: prv.key > <key>` leaked."""
    r = pio_flash._ReplyRedactor(secret=True)
    out = _drain(r, [b"unknown ??: prv.key hunter2\r\n"])
    assert "hunter2" not in out, out
    clean = pio_flash._ReplyRedactor(secret=True)
    assert "bridge.secret" in _drain(clean, [b"??: bridge.secret\r\n"])


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
