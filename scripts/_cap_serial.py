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
import os
import sys
import time

# ---------------------------------------------------------------------------
# Redaction
#
# #667: the patterns live in scripts/log_redact.py and are SHARED with
# companion_harness.py's caplog download. They used to live here only, so the
# two retrieval paths for the same device log disagreed -- the serial path
# scrubbed credentials while the caplog download returned the ring verbatim,
# and a caplog carried real GPS coordinates into a file in plaintext.
# One list, both tools: a second copy is a copy that drifts, and the copy that
# drifts is the one that leaks.
# ---------------------------------------------------------------------------

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from log_redact import redact_line  # noqa: E402


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


# USB VIDs of UART bridges that (a) don't gate TX on DTR and (b) can auto-reset
# if DTR toggles: SiLabs CP210x (0x10C4), WCH CH34x (0x1A86), FTDI (0x0403).
# We never auto-assert DTR on these -- a silent bridge means an idle device, not
# a TX gate, so asserting would only risk a reset. (#386, Gemini review.)
_UART_BRIDGE_VIDS = {0x10C4, 0x1A86, 0x0403}


def _port_vid(port):
    """USB VID of the given port, or None if unknown."""
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            if p.device == port:
                return p.vid
    except Exception:
        pass
    return None


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
                    help="Force DTR/RTS asserted from the start (ESP32-S3 JTAG-CDC / some nRF52 CDCs gate TX on it).")
    ap.add_argument("--no-auto-dtr", action="store_true",
                    help="Disable the auto-assert-DTR-if-silent fallback (fallback is on by default).")
    ap.add_argument("--send", action="append", metavar="CMD", default=None,
                    help="Send a CLI command over the SAME held connection, then keep capturing. "
                         "Repeatable -- commands are issued in order. This exists so a series of "
                         "commands can be run WITHOUT reconnecting: on a board using the ESP32-S3 "
                         "hardware USB Serial/JTAG (ARDUINO_USB_MODE=1) every fresh port open "
                         "reboots the target, so one-command-per-connect tooling measures a device "
                         "it keeps restarting (#661).")
    ap.add_argument("--send-delay", type=float, default=3.0, metavar="SECS",
                    help="Seconds between queued --send commands (default 3).")
    ap.add_argument("--send-wait", type=float, default=3.0, metavar="SECS",
                    help="Seconds to wait after opening before the first --send, so boot output "
                         "and the auto-DTR fallback settle first (default 3).")
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
    # DTR/RTS start deasserted unless forced. The ESP32-S3 internal JTAG-CDC and
    # some nRF52 CDCs gate TX until DTR asserts ("terminal present"), so the line
    # reads silent without it -- the empty-file footgun a tester hits (#386). But
    # CP210x/CH340 UART bridges don't need DTR, and on CP210x auto-reset boards
    # forcing it can reset the device -- so we do NOT assert up front. Instead, if
    # nothing arrives within AUTO_DTR_SILENCE_S, assume a TX-gated CDC/JTAG and
    # assert then. Setting DTR==RTS does not drive the classic auto-reset circuit,
    # and a live UART bridge is never silent, so the fallback never fires on one.
    s.dtr = args.dtr
    s.rts = args.dtr
    s.open()

    AUTO_DTR_SILENCE_S = 3.0
    dtr_on = args.dtr
    auto_dtr = not args.no_auto_dtr and not args.dtr   # only when not already forced
    if auto_dtr and _port_vid(port) in _UART_BRIDGE_VIDS:
        auto_dtr = False
        print("[[%s is a UART bridge -- auto-DTR disabled (pass --dtr only if TX is gated)]]" % port)
    got_any = False

    print("[[capturing on %s @ %d baud%s%s%s]]" % (
        port, args.baud,
        "" if args.secs is None else " for %.0fs" % args.secs,
        "" if not args.no_redact else " -- REDACTION OFF",
        " -- DTR asserted" if dtr_on else (" -- auto-DTR armed" if auto_dtr else "")))

    def emit(line):
        line = redact(line)
        print(line)
        sys.stdout.flush()
        if out_fh:
            out_fh.write(line + "\n")
            out_fh.flush()

    # Queued commands, issued on the SAME open connection (see --send). Kept in
    # this single read loop rather than a writer thread: the loop already owns the
    # port, and a second thread would need locking for no benefit at these rates.
    pending = list(args.send or [])
    next_send = time.time() + args.send_wait if pending else None
    if pending:
        print("[[%d command(s) queued; first at +%.0fs, then every %.0fs]]"
              % (len(pending), args.send_wait, args.send_delay))

    t0 = time.time()
    buf = b""
    try:
        while args.secs is None or (time.time() - t0) < args.secs:
            if pending and next_send is not None and time.time() >= next_send:
                cmd = pending.pop(0)
                emit("[[>> %s]]" % cmd)      # echo into the log so the capture is self-describing
                s.write((cmd + "\r").encode("utf-8"))
                s.flush()
                next_send = time.time() + args.send_delay if pending else None
            chunk = s.read(4096)
            if chunk:
                got_any = True
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    emit(raw.decode("utf-8", "replace").rstrip("\r"))
            elif auto_dtr and not dtr_on and not got_any \
                    and (time.time() - t0) >= AUTO_DTR_SILENCE_S:
                # Silent so far -> likely a TX-gated CDC/JTAG. Assert DTR and keep
                # going. Harmless on a genuinely idle UART (DTR==RTS doesn't hit
                # the reset circuit); only fires when nothing has ever arrived.
                s.dtr = True
                s.rts = True
                dtr_on = True
                print("[[no data in %.0fs -- asserting DTR to ungate TX (CDC/JTAG)]]"
                      % AUTO_DTR_SILENCE_S)
                sys.stdout.flush()
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
