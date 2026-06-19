#!/usr/bin/env python3
"""Back up a device's MeshCore identity (private key) over the serial CLI.

Sends `get prv.key` to the device's CommonCLI serial console (local-serial-only
command) and saves the exported private key to
device_backups/<name>-prv.key.txt (gitignored).

SECURITY: the key is NEVER printed to stdout/stderr, and NEVER written anywhere
except the gitignored key file -- only a confirmation + a sha256 head so you can
verify integrity later. The output dir is verified gitignored at runtime.

Restore on the device with:  set prv.key <hex>   (then reboot)

Usage:
  python scripts/backup-device-key.py --port COM20 --name wismesh1w-repeater
  python scripts/backup-device-key.py --port COM20 --name x --force   # overwrite
"""
import argparse
import hashlib
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import serial  # pyserial

REPO = Path(__file__).resolve().parent.parent
OUTDIR = REPO / "device_backups"

# MeshCore identity private key is 64 bytes = 128 hex chars. Accept >=64 hex
# (a real key, not a short EUI/pubkey fragment) and warn if not exactly 128.
MIN_KEY_HEX = 64
EXPECTED_KEY_HEX = 128

# A COMPLETE reply line: the CLI prints "> <hex>" terminated by CR/LF. Requiring
# the trailing line-end means a key still mid-transmission is never accepted
# (guards against a fixed-timer read truncating the key).
KEY_LINE_RE = re.compile(rb">\s*([0-9A-Fa-f]{64,})\s*[\r\n]")


def output_is_gitignored(out_path):
    """True if the key file would be gitignored, False if not, None if git is unavailable.

    Checks the *file* path, not the bare dir: a `device_backups/` (trailing-slash)
    pattern matches files inside the dir, but `git check-ignore device_backups`
    (no slash, dir absent) returns "not ignored" -- a false negative.
    """
    try:
        target = out_path.relative_to(REPO)
    except ValueError:
        target = out_path
    try:
        r = subprocess.run(
            ["git", "-C", str(REPO), "check-ignore", "-q", str(target)],
            capture_output=True,
        )
        return r.returncode == 0  # 0 = ignored, 1 = not ignored
    except (FileNotFoundError, OSError):
        return None


def read_until_key(ser, deadline):
    """Read until a COMPLETE '> <hex>\\n' reply is seen, or the deadline passes."""
    buf = b""
    while time.time() < deadline:
        chunk = ser.read(256)
        if chunk:
            buf += chunk
            if KEY_LINE_RE.search(buf):  # full key line received -> stop reading
                break
    return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--name", required=True, help="device label used in the filename")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--read-secs", type=float, default=6.0)
    ap.add_argument("--force", action="store_true", help="overwrite an existing backup file")
    args = ap.parse_args()

    out = OUTDIR / f"{args.name}-prv.key.txt"

    # Verify the key file would be gitignored BEFORE we ever read/write a key.
    ignored = output_is_gitignored(out)
    if ignored is False:
        print("ERROR: device_backups/ is NOT gitignored -- refusing to write a private key.")
        print("  Add 'device_backups/' to .gitignore first, then re-run.")
        return 4
    if ignored is None:
        print("WARN: could not verify device_backups/ is gitignored (git unavailable).")
        print("      Ensure 'device_backups/' is in .gitignore before committing anything.")

    OUTDIR.mkdir(exist_ok=True)
    if out.exists() and not args.force:
        print(f"ERROR: backup already exists: device_backups/{out.name}")
        print("  Re-run with --force to overwrite.")
        return 3

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.3)
    except Exception as e:  # noqa: BLE001
        print(f"ERROR: could not open {args.port}: {e}")
        return 2

    try:
        time.sleep(1.5)            # settle any port-open reset
        ser.reset_input_buffer()
        ser.write(b"\r\n")         # nudge a fresh prompt
        time.sleep(0.3)
        ser.reset_input_buffer()   # discard echo/prompt so the buffer is just our reply
        ser.write(b"get prv.key\r\n")
        buf = read_until_key(ser, time.time() + args.read_secs)
    finally:
        ser.close()

    m = KEY_LINE_RE.search(buf)
    if not m:
        # SECURITY: do NOT write the raw buffer anywhere -- it may contain the key.
        # Report only the byte count so the protocol can be debugged without leaking.
        print("NO COMPLETE KEY FOUND in the serial response.")
        print(f"  Received {len(buf)} bytes; no '> <hex>' line terminated by a newline.")
        print("  Check: right port? device running the CLI firmware? Re-run, or raise --read-secs.")
        return 1

    key_hex = m.group(1).decode("ascii").lower()
    if len(key_hex) % 2 != 0:
        print(f"ERROR: captured an odd-length hex string ({len(key_hex)} chars) -- not a valid key.")
        return 1
    try:
        key_bytes = bytes.fromhex(key_hex)
    except ValueError:
        # Controlled message -- never let an uncaught traceback echo key_hex.
        print(f"ERROR: captured text is not valid hex ({len(key_hex)} chars).")
        return 1
    if len(key_hex) != EXPECTED_KEY_HEX:
        print(f"WARN: key is {len(key_hex)} hex chars (expected {EXPECTED_KEY_HEX}). Saving; verify it.")

    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    out.write_text(
        f"# {args.name} private key via `get prv.key` -- {stamp}\n"
        f"# restore: set prv.key <hex>   then reboot\n"
        f"{key_hex}\n",
        encoding="utf-8",
    )
    digest = hashlib.sha256(key_bytes).hexdigest()
    print(f"OK: saved {len(key_hex)}-hex-char private key for '{args.name}'")
    print(f"    -> device_backups/{out.name}  (gitignored)")
    print(f"    sha256(key) head = {digest[:16]}   (key itself NOT printed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
