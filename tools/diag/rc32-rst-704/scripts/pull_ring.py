"""#704: verify OFFBAND_FORCE_CAPLOG applied at RUNTIME, then pull the full boot ring.

The proof the flag took effect is caplog_status reporting enabled=1 WITHOUT this
script enabling it. Last build reported enabled=0 despite being built with the
flag, which is why every conclusion drawn from that ring was worthless.
"""
import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), "scripts"))
from companion_harness import (CompanionSession, resolve_device_port,
    CMD_OFFBAND_CAPLOG, CAPLOG_REQ_DOWNLOAD, CAPLOG_SUB_CHUNK,
    CAPLOG_SUB_END, parse_caplog_start)
from log_redact import redact_bytes

dev = resolve_device_port("rc32-bench-1")
with CompanionSession(dev) as s:
    s.app_start()
    st = s.caplog_status()
    sys.stderr.write(f"[caplog_status BEFORE any enable: {st}]\n")
    if not st.get("enabled"):
        sys.stderr.write("[FAIL: force flag did NOT apply at runtime -- ring is not trustworthy]\n")

    s.send_frame(bytes([CMD_OFFBAND_CAPLOG, CAPLOG_REQ_DOWNLOAD]))
    f = s.read_frame(match_code=CMD_OFFBAND_CAPLOG)
    total = parse_caplog_start(f)
    out = bytearray()
    while True:
        try:
            f = s.read_frame(match_code=CMD_OFFBAND_CAPLOG, timeout=3)
        except Exception:
            break
        if f is None:
            break
        if f[1] == CAPLOG_SUB_CHUNK:
            out += f[2:]
        elif f[1] == CAPLOG_SUB_END:
            break
    sys.stderr.write(f"[got {len(out)} of {total} bytes]\n")
    sys.stdout.buffer.write(redact_bytes(bytes(out)))
