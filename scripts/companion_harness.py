#!/usr/bin/env python3
"""Companion-protocol test harness (#412 / #413 phase 1).

Speaks the MeshCore/Offband companion frame protocol over a transport so
automated tests can drive a connected device WITHOUT the client app. Framing
(ArduinoSerialInterface): host->device frames start '<', device->host '>',
each followed by a uint16 little-endian length then the payload.

Phase 1: pure frame core + serial transport + CMD_APP_START -> SELF_INFO.
Later phases add caplog/FEM-LNA/config round-trips (#412) and a BLE transport.
"""
import os
import re
import struct
import subprocess
import sys
import time

# #667: shared with _cap_serial.py so the two retrieval paths for the same log
# cannot disagree about what counts as sensitive.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from log_redact import redact_bytes  # noqa: E402

# --- companion protocol constants (examples/companion_radio/MyMesh.cpp) ---
CMD_APP_START = 1
RESP_CODE_SELF_INFO = 5
RESP_CODE_ERR = 1

# serial-capture (#396/#417): CMD_OFFBAND_CAPLOG 0xC4, request sub-code in byte[1]
CMD_OFFBAND_CAPLOG = 0xC4
CAPLOG_REQ_DOWNLOAD = 0x01
CAPLOG_REQ_ENABLE = 0x02
CAPLOG_REQ_DISABLE = 0x03
CAPLOG_REQ_ERASE = 0x04
CAPLOG_REQ_STATUS = 0x05
CAPLOG_SUB_START = 0x01   # reply byte[1] (download stream)
CAPLOG_SUB_CHUNK = 0x02
CAPLOG_SUB_END = 0x03
CAPLOG_RESP_ACK = 0x10    # reply byte[1] (control)
CAPLOG_RESP_STATUS = 0x11
MLOG_BOOT, MLOG_ERROR, MLOG_DEBUG, MLOG_PACKET = 0, 1, 2, 3

# FEM/LNA control (#298): CMD_OFFBAND_FEM_LNA 0xC3 — used as a collision guard.
CMD_OFFBAND_FEM_LNA = 0xC3
OFFBAND_FEM_LNA_GET = 0x02

_FRAME_HDR_DEVICE = ord(">")   # device -> host
_FRAME_HDR_HOST = b"<"         # host -> device
PUB_KEY_SIZE = 32
# Firmware MAX_FRAME_SIZE is 176; anything much larger means we latched onto a
# stray '>' in unframed noise, so resync rather than buffer a bogus length.
_MAX_FRAME_PAYLOAD = 256


def encode_frame(payload: bytes) -> bytes:
    """Wrap a payload as a host->device frame: '<' + uint16le(len) + payload."""
    return _FRAME_HDR_HOST + struct.pack("<H", len(payload)) + payload


class FrameDecoder:
    """Incrementally parse device->host frames from a raw byte stream, skipping
    any non-'>' noise (e.g. raw [GPS] debug text on the line).

    Limitation: treats the first '>' as a frame start, so a literal '>' inside
    unframed noise would desync until the next real frame boundary. Device debug
    lines ([GPS], [GPSPARSE]) contain no '>', so this is fine in practice.
    """

    def __init__(self):
        self._buf = bytearray()

    def feed(self, data: bytes):
        self._buf += data
        out = []
        while True:
            idx = self._buf.find(_FRAME_HDR_DEVICE)
            if idx < 0:                      # no frame start -> all noise
                self._buf.clear()
                break
            if idx > 0:                      # drop leading noise before '>'
                del self._buf[:idx]
            if len(self._buf) < 3:           # need '>' + uint16 length
                break
            length = self._buf[1] | (self._buf[2] << 8)
            if length > _MAX_FRAME_PAYLOAD:  # bogus -> this '>' was noise; resync
                del self._buf[:1]
                continue
            if len(self._buf) < 3 + length:  # payload not fully arrived yet
                break
            out.append(bytes(self._buf[3:3 + length]))
            del self._buf[:3 + length]
        return out


def build_app_start(app_name: bytes) -> bytes:
    """CMD_APP_START payload: [1][7 reserved][app_name...] (len >= 8)."""
    return bytes([CMD_APP_START]) + b"\x00" * 7 + app_name


def parse_self_info(body: bytes) -> dict:
    """Parse a RESP_CODE_SELF_INFO reply:
    [5][adv_type][tx_power][max_tx_power][pubkey*32]..."""
    if not body or body[0] != RESP_CODE_SELF_INFO:
        raise ValueError(f"not a SELF_INFO frame: {body[:1].hex()}")
    return {
        "adv_type": body[1],
        "tx_power": body[2],
        "max_tx_power": body[3],
        "pubkey": bytes(body[4:4 + PUB_KEY_SIZE]),
    }


def _registry_clone_root():
    """Locate the clone that holds the gitignored hardware-devices.yaml. It lives
    only in the primary clone (main worktree), so resolve it via the shared git
    common dir -- this makes the harness work whether it runs from the primary
    clone or any worktree (where the registry file is absent)."""
    here = os.path.dirname(os.path.abspath(__file__))
    try:
        common = subprocess.run(
            ["git", "-C", here, "rev-parse", "--path-format=absolute", "--git-common-dir"],
            capture_output=True, text=True, timeout=10,
        ).stdout.strip()
        if common:
            root = os.path.dirname(common)  # <primary>/.git -> <primary>
            if os.path.exists(os.path.join(root, "hardware-devices.yaml")):
                return root
    except Exception:
        pass
    return os.path.dirname(here)  # fallback: this script's own clone


def parse_caplog_status(body: bytes) -> dict:
    """[0xC4, 0x11(STATUS), enabled, level, used(4B LE), cap(4B LE)]."""
    if len(body) < 12 or body[0] != CMD_OFFBAND_CAPLOG or body[1] != CAPLOG_RESP_STATUS:
        raise ValueError(f"not a caplog STATUS frame: {body[:2].hex()}")
    used = int.from_bytes(body[4:8], "little")
    cap = int.from_bytes(body[8:12], "little")
    return {"enabled": body[2], "level": body[3], "used": used, "cap": cap}


def parse_caplog_start(body: bytes) -> int:
    """[0xC4, 0x01(START), total_len(4B LE)] -> total_len."""
    if len(body) < 6 or body[0] != CMD_OFFBAND_CAPLOG or body[1] != CAPLOG_SUB_START:
        raise ValueError(f"not a caplog START frame: {body[:2].hex()}")
    return int.from_bytes(body[2:6], "little")


def resolve_device_port(name: str):
    """Resolve a registered device name to its current COM port by MAC, via
    `pio-flash list` (which matches by usb_serial/MAC, not COM). None if absent."""
    root = _registry_clone_root()
    try:
        res = subprocess.run(
            ["python", os.path.join(root, "scripts", "pio-flash.py"), "list"],
            capture_output=True, text=True, timeout=30,
        )
    except (subprocess.TimeoutExpired, OSError):
        return None
    for line in res.stdout.splitlines():
        if f"device:{name}" in line:
            m = re.match(r"\s*(COM\d+)\b", line)
            if m:
                return m.group(1)
    return None


class CompanionSession:
    """Serial transport + framed request/response against a companion device."""

    def __init__(self, port, baud=115200, timeout=6.0):
        import serial  # pyserial; imported lazily so pure tests need no hardware deps
        self._ser = serial.Serial(port, baud, timeout=0.2)
        self._dec = FrameDecoder()
        self._pending = []   # decoded-but-not-yet-returned frames (a read() can yield several)
        self._timeout = timeout

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()
        return False

    def close(self):
        try:
            self._ser.close()
        except Exception:
            pass

    def flush_pending(self):
        """Drop any buffered/unread frames + serial input. Call between distinct
        logical operations so a stray or timed-out frame (or raw [GPS] noise on
        the shared USB-CDC line, #411) can't cross-talk into the next step."""
        self._pending.clear()
        self._dec = FrameDecoder()
        try:
            self._ser.reset_input_buffer()
        except Exception:
            pass

    def send_frame(self, payload: bytes):
        self._ser.write(encode_frame(payload))
        self._ser.flush()

    def read_frame(self, match_code=None, timeout=None):
        """Return the oldest buffered frame whose byte[0] == match_code (or the
        oldest frame if match_code is None), reading more serial as needed. A
        single serial read() can decode SEVERAL frames (START+CHUNK+END arrive
        together), so decoded frames are buffered in FIFO order and served one
        per call -- never dropped. Returns None on timeout."""
        deadline = time.time() + (timeout if timeout is not None else self._timeout)
        while True:
            for i, payload in enumerate(self._pending):
                if match_code is None or (payload and payload[0] == match_code):
                    return self._pending.pop(i)
            if time.time() >= deadline:
                return None
            chunk = self._ser.read(256)
            if chunk:
                self._pending.extend(self._dec.feed(chunk))

    def app_start(self, app_name=b"harness") -> dict:
        self.send_frame(build_app_start(app_name))
        body = self.read_frame(match_code=RESP_CODE_SELF_INFO)
        if body is None:
            raise TimeoutError("no SELF_INFO reply to CMD_APP_START")
        return parse_self_info(body)

    # --- serial-capture (#396/#417) ------------------------------------------
    def _caplog_ack(self, req_op) -> bool:
        """Send a control op and expect [0xC4, 0x10(ACK), req_op, ok]."""
        body = self.read_frame(match_code=CMD_OFFBAND_CAPLOG)
        return bool(body and len(body) >= 4 and body[1] == CAPLOG_RESP_ACK
                    and body[2] == req_op and body[3] == 1)

    def caplog_enable(self, level=MLOG_DEBUG) -> bool:
        self.send_frame(bytes([CMD_OFFBAND_CAPLOG, CAPLOG_REQ_ENABLE, level]))
        return self._caplog_ack(CAPLOG_REQ_ENABLE)

    def caplog_disable(self) -> bool:
        self.send_frame(bytes([CMD_OFFBAND_CAPLOG, CAPLOG_REQ_DISABLE]))
        return self._caplog_ack(CAPLOG_REQ_DISABLE)

    def caplog_erase(self) -> bool:
        self.send_frame(bytes([CMD_OFFBAND_CAPLOG, CAPLOG_REQ_ERASE]))
        return self._caplog_ack(CAPLOG_REQ_ERASE)

    def caplog_status(self) -> dict:
        self.send_frame(bytes([CMD_OFFBAND_CAPLOG, CAPLOG_REQ_STATUS]))
        body = self.read_frame(match_code=CMD_OFFBAND_CAPLOG)
        if body is None:
            raise TimeoutError("no caplog STATUS reply")
        return parse_caplog_status(body)

    def caplog_download(self, raw: bool = False) -> bytes:
        """Request a download, reassemble START -> CHUNK* -> END into the log bytes.

        The busy rejection is a bare [RESP_CODE_ERR] frame (byte[0]=1, not 0xC4),
        so read ANY frame first and dispatch on byte[0] -- do NOT match on 0xC4
        (and never test byte[1]==1 for 'error', since START's sub-code is also 1).

        #667: the returned bytes are REDACTED by default -- credentials scrubbed
        and GPS position classified (see scripts/log_redact.py). This path used to
        return the ring verbatim while the serial-capture path scrubbed, and a
        caplog pulled off a device carried real coordinates into a file. Safe is
        the default precisely because the leak happened by omission, not by an
        explicit choice to keep the raw bytes.

        Pass raw=True only when the caller genuinely needs the unmodified ring
        (byte-exact ring diagnostics) and is not going to ship the result.

        The returned `total` is the ON-DEVICE size from the START frame, which is
        what the integrity check is made against; after redaction len(data) will
        differ from it, and that is expected."""
        self.send_frame(bytes([CMD_OFFBAND_CAPLOG, CAPLOG_REQ_DOWNLOAD]))
        start = self.read_frame()
        if start is None:
            raise TimeoutError("no caplog download reply")
        if start[0] == RESP_CODE_ERR:
            raise RuntimeError("caplog download rejected (busy)")
        if start[0] != CMD_OFFBAND_CAPLOG or start[1] != CAPLOG_SUB_START:
            raise RuntimeError(f"unexpected download reply: {start[:2].hex()}")
        total = parse_caplog_start(start)
        out = bytearray()
        while True:
            frame = self.read_frame(match_code=CMD_OFFBAND_CAPLOG)
            if frame is None:
                raise TimeoutError("caplog download truncated (no END)")
            if frame[1] == CAPLOG_SUB_CHUNK:
                out += frame[2:]
            elif frame[1] == CAPLOG_SUB_END:
                break
        # Integrity check against the on-device size FIRST -- redaction changes
        # the length, so checking afterwards would compare the wrong things.
        if len(out) != total:
            raise RuntimeError(f"caplog download size mismatch: got {len(out)}, START said {total}")
        data = bytes(out)
        return (data if raw else redact_bytes(data)), total

    def fem_lna_get(self):
        """Send CMD_OFFBAND_FEM_LNA (0xC3) GET. Returns the reply body (byte[0]
        must be 0xC3 — the #408 collision guard: 0xC3 must NOT hit caplog)."""
        self.send_frame(bytes([CMD_OFFBAND_FEM_LNA, OFFBAND_FEM_LNA_GET]))
        return self.read_frame()  # any frame; caller checks byte[0]


if __name__ == "__main__":
    import sys
    dev = resolve_device_port(sys.argv[1] if len(sys.argv) > 1 else "hv4-bench-1")
    if dev is None:
        print("device not attached")
        sys.exit(1)
    with CompanionSession(dev) as s:
        info = s.app_start()
        print(f"connected: pubkey={info['pubkey'].hex()} tx={info['tx_power']} "
              f"max_tx={info['max_tx_power']} adv_type={info['adv_type']}")
