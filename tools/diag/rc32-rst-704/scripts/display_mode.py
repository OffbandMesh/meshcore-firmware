"""#704 Phase 3 minimal test: read the companion's LIVE persisted display mode.

Wire contract (examples/companion_radio/OffbandConfigProtocol.h):
    [0xC5][0x05]  ->  [0xC5][0x05][mode]
    mode: 0 auto (blanks after timeout), 1 always-on, 2 always-off (boots dark)

Hypothesis under test: _disp_mode == 2 would make the companion boot dark and
stay dark regardless of what is compiled in -- while the repeater, which lights
the panel unconditionally in setup(), is unaffected. Read-only; sets nothing.
"""
import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), "scripts"))
from companion_harness import CompanionSession, resolve_device_port

CMD_DEVICE_UI    = 0xC5
UI_DISPLAY_GET   = 0x05
UI_LED_GET       = 0x07
MODES = {0: "auto (on, blanks after timeout)",
         1: "always-on",
         2: "always-off  <-- BOOTS DARK, STAYS DARK"}

dev = resolve_device_port("rc32-bench-1")
with CompanionSession(dev) as s:
    s.app_start()

    s.send_frame(bytes([CMD_DEVICE_UI, UI_DISPLAY_GET]))
    f = s.read_frame(match_code=CMD_DEVICE_UI, timeout=5)
    print(f"raw display reply : {bytes(f).hex(' ') if f else None}")
    if f and len(f) >= 3 and f[1] == UI_DISPLAY_GET:
        mode = f[2]
        print(f"DISPLAY MODE      : {mode} = {MODES.get(mode, 'UNKNOWN VALUE')}")
    elif f and len(f) >= 3 and f[1] == 0x7F:
        print(f"ERROR reply, reason byte = {f[2]}")
    else:
        print("no usable display reply")

    s.send_frame(bytes([CMD_DEVICE_UI, UI_LED_GET]))
    f = s.read_frame(match_code=CMD_DEVICE_UI, timeout=5)
    print(f"raw led reply     : {bytes(f).hex(' ') if f else None}")
    if f and len(f) >= 3 and f[1] == UI_LED_GET:
        print(f"LED STATE         : {f[2]}")
