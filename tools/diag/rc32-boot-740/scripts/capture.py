#!/usr/bin/env python3
"""#740 -- retention capture for the RC32 boot investigation.

Holds ONE connection to the Feather sniffer open and appends every line it
emits, timestamped, to a durable file. That file is the artifact; nothing is
piped, filtered or tailed on the way in.

WHY A DEDICATED SCRIPT
----------------------
Every prior capture on this board came from `pio device monitor` with the
terminal scrollback as the only record. That loses data on reconnect, has no
timestamps, and cannot tell you how long a gap between two boots was. It also
made it easy to conflate "nothing was emitted" with "nobody was listening".

WHAT IT DOES NOT DO
-------------------
It never touches the RC32. The RC32's own USB console power-cycles the board on
attach, which is why every log ever captured that way came from a boot that
SUCCEEDED (#702). This script talks only to the Feather, which is a separate
device on a separate USB port, so opening it has no effect on the RC32.

DTR/RTS are forced low before open so the Feather is not reset on attach. That
matters less than it does for the RC32 (the Feather holds no state worth
keeping) but a reset mid-capture costs bytes, so we avoid it.

USAGE
-----
    python capture.py --port COM16 --out ../evidence/rst-session.log
    python capture.py --list
    python capture.py --port COM16 --send RST        # SNIFFER-v3 only

The --send path writes one command line and returns; the long-running capture
should already be attached in another process to record what it triggers.
"""

import argparse
import datetime
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is required:  python -m pip install pyserial")


def ts() -> str:
    """UTC, millisecond resolution. UTC deliberately: sessions here span days
    and cross a DST boundary twice a year, and a boot log that jumps backwards
    an hour is worse than useless."""
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


def do_list() -> int:
    for p in sorted(list_ports.comports(), key=lambda x: x.device):
        print(f"{p.device:8s}  {p.vid:04X}:{p.pid:04X}  {p.description}"
              if p.vid is not None else f"{p.device:8s}  ----:----  {p.description}")
    return 0


def open_port(port: str, baud: int) -> "serial.Serial":
    """Open without asserting DTR/RTS, so attaching does not reset the Feather."""
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 1
    s.dtr = False
    s.rts = False
    s.open()
    return s


def do_send(port: str, baud: int, cmd: str) -> int:
    with open_port(port, baud) as s:
        time.sleep(0.2)                      # let the port settle
        s.write((cmd.strip() + "\n").encode())
        s.flush()
    print(f"sent: {cmd.strip()}")
    return 0


def do_capture(port: str, baud: int, out_path: str) -> int:
    print(f"[{ts()}] capture -> {out_path}  (port {port} @ {baud})", flush=True)
    # Line-buffered append. Append, never truncate: an accidental re-run must
    # not destroy an overnight capture.
    with open(out_path, "a", buffering=1, encoding="utf-8", errors="replace") as f:
        f.write(f"\n===== capture started {ts()} port={port} baud={baud} =====\n")
        s = None
        while True:
            try:
                if s is None:
                    s = open_port(port, baud)
                    f.write(f"[{ts()}] <port opened>\n")
                raw = s.readline()
                if not raw:
                    continue                  # 1 s timeout tick, not a disconnect
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                f.write(f"[{ts()}] {line}\n")
            except KeyboardInterrupt:
                f.write(f"[{ts()}] <capture stopped by operator>\n")
                return 0
            except Exception as e:
                # Loud, never silent (SAFELANE 6). A dropped port is recorded in
                # the artifact itself so a gap in the log is always explained.
                f.write(f"[{ts()}] <port error: {e!r}; retrying in 2s>\n")
                try:
                    if s:
                        s.close()
                except Exception:
                    pass
                s = None
                time.sleep(2)


def main() -> int:
    ap = argparse.ArgumentParser(description="#740 RC32 boot capture (Feather sniffer)")
    ap.add_argument("--port", help="Feather serial port, e.g. COM16")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", help="capture file (appended)")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--send", help="send one command line to SNIFFER-v3 and exit")
    a = ap.parse_args()

    if a.list:
        return do_list()
    if not a.port:
        ap.error("--port is required (use --list to find it)")
    if a.send:
        return do_send(a.port, a.baud, a.send)
    if not a.out:
        ap.error("--out is required for capture")
    return do_capture(a.port, a.baud, a.out)


if __name__ == "__main__":
    sys.exit(main())
