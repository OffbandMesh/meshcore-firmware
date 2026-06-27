#!/usr/bin/env python3
# D2 diagnostic (Epic #216): send UBX-MON-VER to a GPS modem on a USB-UART adapter
# and capture the reply, to settle "is the M100 Mini a genuine u-blox (accepts UBX)
# or a u-blox-compatible clone (NMEA/PUBX only)?".
#
# READ-ONLY w.r.t. the device's config: MON-VER is a poll, it changes nothing.
# This is a HOST-side tool on a USB-UART adapter wired to the M100's TX/RX/GND
# (NOT the radio's USB serial) -- so it cannot wedge the radio. GPS UART != USB CDC.
#
# Usage:  python scripts/_probe_ublox.py <port> [baud] [secs]
#   e.g.  python scripts/_probe_ublox.py COM7 115200 4
#
# Interpretation:
#   reply contains 0xB5 0x62 0x0A 0x04 (UBX-MON-VER) + ASCII version strings
#                                  -> GENUINE u-blox (UBX-configurable in the active phase)
#   only "$G..."/"$P..." NMEA, no UBX frame
#                                  -> u-blox-compatible CLONE (drive via NMEA/PUBX)
#   nothing at all                 -> wrong baud / wiring -> retry other baud, check TX/RX swap

import sys, time

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed (pip install pyserial)"); sys.exit(2)

port = sys.argv[1] if len(sys.argv) > 1 else "COM7"
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 4.0

# UBX-MON-VER poll: B5 62 | class 0A id 04 | len 0000 | ck 0E 34  (Fletcher over 0A 04 00 00)
MONVER = bytes([0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34])

s = None
try:
    s = serial.Serial(port=port, baudrate=baud, timeout=0.3)
    time.sleep(0.2)
    s.reset_input_buffer()
    s.write(MONVER)
    s.flush()
    print("[sent UBX-MON-VER @ %d baud on %s]  bytes=%s" % (baud, port, MONVER.hex(" ")))

    raw = b""
    t0 = time.time()
    while time.time() - t0 < secs:
        chunk = s.read(4096)
        if chunk:
            raw += chunk

    ubx = (b"\xb5\x62\x0a\x04" in raw)
    nmea = (b"$G" in raw) or (b"$P" in raw)
    print("[got %d bytes in %.1fs]" % (len(raw), secs))
    # ASCII view (printable only) so version strings are readable
    ascii_view = "".join(chr(b) if 32 <= b <= 126 else "." for b in raw)
    print("--- ASCII ---"); print(ascii_view[:1200])
    print("--- HEX (first 160 bytes) ---"); print(raw[:160].hex(" "))
    print("=== VERDICT ===")
    if ubx:
        print("GENUINE u-blox: UBX-MON-VER frame present -> accepts UBX commands.")
    elif nmea:
        print("CLONE (NMEA/PUBX only): NMEA seen, no UBX-MON-VER frame -> NOT UBX-configurable.")
    else:
        print("INCONCLUSIVE: no UBX and no NMEA -> wrong baud/wiring; retry other baud + check TX/RX.")
except Exception as e:                       # no silent failure
    print("ERROR: %s" % e); sys.exit(1)
finally:
    if s is not None:
        try: s.close()
        except Exception: pass
