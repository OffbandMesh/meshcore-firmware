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
    python capture.py --port COM16 --send  RST   # standalone only, see below
    python capture.py --port COM16 --out ../evidence/rst-session.log --queue RST

ISSUING COMMANDS WHILE CAPTURING
--------------------------------
SNIFFER-v3 accepts RST / BOOT / BOOTRST / PING / HELP on its USB serial. There
are two ways to send one, and only one of them works during a capture.

`--send` opens the port itself. It CANNOT be used while a capture is running:
Windows holds a serial port exclusively, so the second open fails with access
denied. This docstring previously claimed the opposite -- that --send could run
alongside a capture in another process -- which was wrong, and made the command
surface useless exactly when it mattered, since a reset is only worth issuing if
something is recording what it triggers.

`--queue` is the one to use during a capture. It writes the command to a small
queue file (`<out>.cmd`) which the RUNNING capture picks up within ~1 s and
forwards over the port it already owns. The injection is logged as
`<<< INJECT <cmd>` immediately above whatever it causes, so a captured session
documents its own stimulus.
"""

import argparse
import os
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


def open_port(port: str, baud: int, assert_dtr: bool = True) -> "serial.Serial":
    """Open the Feather's port.

    DTR IS ASSERTED BY DEFAULT, and that is deliberate.

    The obvious-looking choice is dtr=False, to avoid resetting the sniffer on
    attach. That was tried and it is wrong for this device: the Feather is a
    native-USB ESP32-S3 whose CDC stack gates transmission on DTR, so opening
    without it produced a port that connected cleanly and then emitted NOTHING
    -- no heartbeat, no relayed data. The instrument looked dead.

    Silence is only evidence if the instrument is provably alive, which is the
    whole reason SNIFFER-v2 grew a heartbeat. Asserting DTR costs one Feather
    reset, which costs nothing -- it holds no state. `pio device monitor`, which
    demonstrably worked for the earlier captures, asserts DTR too.

    Note this resets the FEATHER only. The RC32 is a separate device on a
    separate port and is not touched.
    """
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 1
    s.dtr = assert_dtr
    s.rts = False          # RTS stays low: on ESP boards it is the reset line
    s.open()
    return s


def do_send(port: str, baud: int, cmd: str) -> int:
    with open_port(port, baud) as s:
        time.sleep(0.2)                      # let the port settle
        s.write((cmd.strip() + "\n").encode())
        s.flush()
    print(f"sent: {cmd.strip()}")
    return 0


def drain_cmd_file(cmd_path: str, s, f) -> None:
    """Forward one queued command from cmd_path to the sniffer, then remove it.

    WHY THIS EXISTS. The --send path opens the port itself, which cannot work
    while a capture is running: on Windows a serial port is held EXCLUSIVELY, so
    the second open fails with access denied. The module docstring used to claim
    the two could run side by side ("the long-running capture should already be
    attached in another process"). That was wrong, and it made the command
    surface and the capture mutually exclusive at exactly the moment you want
    both -- issuing a reset is only useful if something is recording what it
    triggers.

    So the process that already owns the port does the writing. A command is
    queued by dropping a line into a file; this forwards it and deletes it.

    The injection is recorded in the log itself, immediately above whatever it
    causes. Combined with the sketch's own ">>>" stamps that makes a capture
    self-documenting: the stimulus always appears directly above the resulting
    ROM banner, which is the "what caused this boot?" ambiguity these markers
    were added to kill (#740).

    Failure is loud and the file is still removed -- a command that cannot be
    delivered must not silently retry forever on every loop iteration.
    """
    try:
        if not os.path.exists(cmd_path):
            return
        with open(cmd_path, "r", encoding="utf-8", errors="replace") as cf:
            cmd = cf.readline().strip()
        os.remove(cmd_path)
        if not cmd:
            return
        f.write(f"[{ts()}] <<< INJECT {cmd}\n")
        s.write((cmd + "\n").encode())
        s.flush()
    except Exception as e:
        f.write(f"[{ts()}] <<< INJECT FAILED: {e!r}\n")
        try:
            if os.path.exists(cmd_path):
                os.remove(cmd_path)
        except Exception:
            pass


def do_capture(port: str, baud: int, out_path: str, cmd_path: str = None) -> int:
    if cmd_path is None:
        cmd_path = out_path + ".cmd"
    print(f"[{ts()}] capture -> {out_path}  (port {port} @ {baud})", flush=True)
    print(f"[{ts()}] command queue: {cmd_path}", flush=True)
    # Line-buffered append. Append, never truncate: an accidental re-run must
    # not destroy an overnight capture.
    with open(out_path, "a", buffering=1, encoding="utf-8", errors="replace") as f:
        f.write(f"\n===== capture started {ts()} port={port} baud={baud} =====\n")
        f.write(f"===== command queue: {cmd_path} =====\n")
        s = None
        while True:
            try:
                if s is None:
                    s = open_port(port, baud)
                    f.write(f"[{ts()}] <port opened>\n")
                # Checked every loop iteration. readline() returns on its 1 s
                # timeout even when the wire is silent, so a queued command is
                # picked up within ~1 s regardless of traffic.
                drain_cmd_file(cmd_path, s, f)
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
    ap.add_argument("--send", help="send one command line to SNIFFER-v3 and exit "
                                   "(standalone only -- fails if a capture holds the port; "
                                   "use --queue instead while capturing)")
    ap.add_argument("--queue", help="queue a command for a RUNNING capture to forward "
                                    "(writes <out>.cmd; needs --out to locate the queue)")
    ap.add_argument("--cmd-file", default=None,
                    help="override the command-queue path (default: <out>.cmd)")
    a = ap.parse_args()

    if a.list:
        return do_list()
    if not a.port:
        ap.error("--port is required (use --list to find it)")
    if a.queue:
        if not a.out:
            print("--queue needs --out so the queue file can be located", file=sys.stderr)
            return 2
        qp = a.cmd_file or (a.out + ".cmd")
        with open(qp, "w", encoding="utf-8") as qf:
            qf.write(a.queue.strip() + "\n")
        print(f"queued: {a.queue.strip()} -> {qp}")
        return 0

    if a.send:
        return do_send(a.port, a.baud, a.send)
    if not a.out:
        ap.error("--out is required for capture")
    return do_capture(a.port, a.baud, a.out, a.cmd_file)


if __name__ == "__main__":
    sys.exit(main())
