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
import time

# --- companion protocol constants (examples/companion_radio/MyMesh.cpp) ---
CMD_APP_START = 1
RESP_CODE_SELF_INFO = 5

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

    def send_frame(self, payload: bytes):
        self._ser.write(encode_frame(payload))
        self._ser.flush()

    def read_frame(self, match_code=None, timeout=None):
        """Read framed payloads until one whose byte[0] == match_code (or any
        frame if match_code is None). Returns the payload or None on timeout."""
        deadline = time.time() + (timeout if timeout is not None else self._timeout)
        while time.time() < deadline:
            chunk = self._ser.read(256)
            if chunk:
                for payload in self._dec.feed(chunk):
                    if match_code is None or (payload and payload[0] == match_code):
                        return payload
        return None

    def app_start(self, app_name=b"harness") -> dict:
        self.send_frame(build_app_start(app_name))
        body = self.read_frame(match_code=RESP_CODE_SELF_INFO)
        if body is None:
            raise TimeoutError("no SELF_INFO reply to CMD_APP_START")
        return parse_self_info(body)


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
