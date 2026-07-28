#!/usr/bin/env python3
"""Serial capture for MeshCore/Offband devices, with on-the-fly credential redaction.

Two roles:
  * Internal quick-capture (unchanged spirit of the original 31-line tool):
        python _cap_serial.py --port COM10 --secs 8
  * Tester-facing capture to hand a remote user (default): auto-detect the port,
    stream redacted lines to the console, AND write them to a timestamped file the
    tester sends back. Redaction runs BEFORE anything is written, so a WiFi SSID /
    PSK / token never lands in the file.

Firmware-agnostic: a serial capture is a byte-dump, so this works on both the
gessaman and Offband observer boxes (we can only *interpret* Offband output, but
we can *capture* either). Redaction patterns are grounded in the actual Offband
serial strings (WifiBootstrap.cpp / ObserverCli.cpp / CommonCLI.cpp / JwtHelper)
plus conservative generic catch-alls for the unknown gessaman source.

Redaction is IN-LINE (the secret value is replaced, the rest of the line is kept)
so diagnostic context that shares a line with a secret is preserved.

Known limitation: label-less interactive CLI replies. The serial console prints
`get wifi.ssid` / `get guest_password` / `get bridge_secret` replies as an
indented "  > <value>" with no field name. The single-line form IS now caught
(#382). A hypothetical value-on-the-NEXT-line reply ("> \n<value>") would not be —
line-based redaction can't see across lines, and our firmware doesn't emit that
form (CommonCLI `> %s` is single-line). Testers are told below not to run any
secret-returning `get` command during capture.

Context: OffbandMesh/meshcore-firmware#379 (OKI-Mesh/CoreScope#72) — capture the
device side of an observer that stopped publishing to MQTT and did not reconnect.
"""

import argparse
import re
import sys
import time

# ---------------------------------------------------------------------------
# Redaction — patterns ordered specific → generic. Each entry is (compiled
# regex, replacement). Group 1 (kept prefix) is preserved; the secret is
# swapped for a labelled placeholder so the reader knows what was removed.
# ---------------------------------------------------------------------------

_REDACTIONS = [
    # -- WiFi SSID (network identifier; Ben's explicit target) --------------
    # Boot: "[WifiBootstrap] Saved WiFi SSID=MyNet; attempting STA."  value → ';'
    (re.compile(r"(SSID\s*=\s*)([^;\r\n]+)", re.IGNORECASE), r"\1<redacted:ssid>"),
    # CLI: "wifi.ssid = MyNet"  value → end-of-line (SSIDs may contain spaces)
    (re.compile(r"(wifi\.ssid\s*[:=]\s*)([^\r\n]+)", re.IGNORECASE), r"\1<redacted:ssid>"),

    # -- JWT bearer tokens (three base64url segments) ----------------------
    # Distinctive enough to redact anywhere; version strings like 1.16.0 don't
    # match (segments require >=8 base64url chars each).
    (re.compile(r"\b[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\b"),
     "<redacted:jwt>"),

    # -- Email / jwt_email (PII) -------------------------------------------
    (re.compile(r"(jwt_email\s*[:=]\s*)([^\r\n]+)", re.IGNORECASE), r"\1<redacted:email>"),
    (re.compile(r"[\w.+-]+@[\w-]+\.[\w.-]+"), "<redacted:email>"),

    # -- Generic secret key=value (gessaman + defensive Offband) -----------
    # Word-boundaried keys so "password"/"passed" don't trip the bare "pass"
    # rule and vice-versa. Bare "auth" is deliberately EXCLUDED (auth_type /
    # auth_fail / jwt_audience are diagnostics, not secrets). Value is the run
    # of non-space chars after the separator.
    # A `[\w.-]*` prefix lets underscore/dot/dash-joined keys match too
    # (mqtt_password, wifi.pass, reconnect-token). Longer alternatives precede
    # "pass" so "password" isn't shadowed. Group 1 keeps the key+separator; the
    # value is captured to end-of-line ([^\r\n]+) so a secret containing spaces
    # (WiFi PSKs commonly do) or quotes can never partially survive.
    (re.compile(r"(\b[\w.-]*(?:password|passwd|pwd|psk|secret|token|apikey|api_key|bearer|pass)\b\s*[:=]\s*)([^\r\n]+)",
                re.IGNORECASE), r"\1<redacted:secret>"),

    # Last-resort net for label-less interactive CLI replies. The serial console
    # prints `get wifi.ssid` / `get guest_password` / `get bridge_secret` replies
    # as "> <value>" with NO field label (CommonCLI `> %s`), so nothing above can
    # catch them -- the grounded `wifi.ssid =` rule only matches the _sys-channel
    # format. #382: the live reply is INDENTED ("  > tsunami"), so the anchor must
    # allow leading whitespace (`^\s*>`); the original `^>` missed it and leaked a
    # real SSID on the bench. Normal runtime logs use "[Tag]" prefixes, not a
    # leading ">", so over-redaction risk stays low.
    (re.compile(r"(^\s*>\s*)([^\r\n]+)"), r"\1<redacted:cli-reply>"),
]


def redact_line(line: str) -> str:
    """Return the line with any recognised secret value replaced in-line."""
    for pattern, repl in _REDACTIONS:
        line = pattern.sub(repl, line)
    return line


# ---------------------------------------------------------------------------
# Capture
# ---------------------------------------------------------------------------

def autodetect_port():
    """Best-effort single-port auto-detect. Returns a device name or None."""
    try:
        from serial.tools import list_ports
    except Exception:
        return None
    ports = list(list_ports.comports())
    if not ports:
        return None
    # Prefer a USB CDC / JTAG device if the description hints at one.
    for p in ports:
        desc = (p.description or "").lower()
        if any(h in desc for h in ("usb", "jtag", "cdc", "serial", "cp210", "ch340", "silicon")):
            return p.device
    return ports[0].device


def _now_stamp():
    return time.strftime("%Y%m%d-%H%M%S", time.localtime())


TESTER_INSTRUCTIONS = """\
============================================================================
 Offband / MeshCore serial log capture
============================================================================
 1. Leave the device plugged into this computer over USB for the whole capture.
 2. Let it run through the problem window (e.g. force the WiFi drop/return you
    are debugging). Capture keeps going until you press Ctrl-C (or --secs).
 3. WiFi SSID, tokens and known secrets are redacted automatically before the
    file is written -- but as a precaution DO NOT run any secret-returning `get`
    command (e.g. `get guest_password`, `get bridge_secret`) in the device CLI
    while capturing.
 4. When done, send us the file printed at the end.
============================================================================
"""


def main(argv=None):
    ap = argparse.ArgumentParser(description="Serial capture with credential redaction.")
    ap.add_argument("--port", default=None,
                    help="Serial port (e.g. COM10 / /dev/ttyUSB0). Auto-detected if omitted.")
    ap.add_argument("--secs", type=float, default=None,
                    help="Capture duration in seconds. Omit to run until Ctrl-C (tester default).")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200).")
    ap.add_argument("--out", default=None,
                    help="Output file. Defaults to observer-serial-<timestamp>.log; use '-' for none.")
    ap.add_argument("--no-redact", action="store_true",
                    help="INTERNAL/LOCAL ONLY: disable redaction. Never use when shipping to a tester.")
    ap.add_argument("--dtr", action="store_true",
                    help="Assert DTR/RTS (needed on ESP32-S3 internal JTAG-CDC to ungate TX).")
    # Back-compat: allow the old positional form `_cap_serial.py PORT SECS DTR`.
    ap.add_argument("pos", nargs="*", help=argparse.SUPPRESS)
    args = ap.parse_args(argv)

    if args.pos:
        if args.port is None and len(args.pos) >= 1:
            args.port = args.pos[0]
        if args.secs is None and len(args.pos) >= 2:
            args.secs = float(args.pos[1])
        if len(args.pos) >= 3 and args.pos[2] == "1":
            args.dtr = True

    try:
        import serial
    except ImportError:
        sys.exit("ERROR: pyserial not installed. Run: pip install pyserial")

    port = args.port or autodetect_port()
    if not port:
        sys.exit("ERROR: no serial port given and none auto-detected. Pass --port COMx.")

    redact = (lambda s: s) if args.no_redact else redact_line

    out_path = None
    out_fh = None
    if args.out != "-":
        out_path = args.out or ("observer-serial-%s.log" % _now_stamp())
        out_fh = open(out_path, "w", encoding="utf-8", newline="\n")

    if args.secs is None:
        print(TESTER_INSTRUCTIONS)

    s = serial.Serial()
    s.port = port
    s.baudrate = args.baud
    s.timeout = 0.3
    # DTR/RTS are not wired to EN/BOOT on the internal JTAG-CDC, so asserting
    # them does not reset the device; it only ungates TX on PID 0002.
    s.dtr = args.dtr
    s.rts = args.dtr
    s.open()

    print("[[capturing on %s @ %d baud%s%s]]" % (
        port, args.baud,
        "" if args.secs is None else " for %.0fs" % args.secs,
        "" if not args.no_redact else " -- REDACTION OFF"))

    def emit(line):
        line = redact(line)
        print(line)
        sys.stdout.flush()
        if out_fh:
            out_fh.write(line + "\n")
            out_fh.flush()

    t0 = time.time()
    buf = b""
    try:
        while args.secs is None or (time.time() - t0) < args.secs:
            chunk = s.read(4096)
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    emit(raw.decode("utf-8", "replace").rstrip("\r"))
    except KeyboardInterrupt:
        pass
    except serial.SerialException as e:
        # Device unplugged / port vanished mid-capture. Fail loud but graceful
        # so a tester sees a plain message, not a Python traceback.
        print("\n[[device disconnected or port lost: %s -- stopping capture]]" % e)
    finally:
        if buf:
            emit(buf.decode("utf-8", "replace").rstrip())
        s.close()
        if out_fh:
            out_fh.close()

    dur = time.time() - t0
    print("[[capture done %0.0fs on %s]]" % (dur, port))
    if out_path:
        print("[[log written to %s -- send us this file]]" % out_path)


if __name__ == "__main__":
    main()
