#!/usr/bin/env python3
"""
scripts/pio-flash.py - LoRa flash-discipline wrapper (Python body)

Reads C:\\Dev\\LoRa\\hardware-devices.yaml, enumerates present USB serial
ports via Windows PowerShell, and gates all device-touching operations on:

  Tier 0  (free)         passive enumeration ('list' subcommand)
  Tier B  (identity-gate) port-opening read-only ('monitor', 'info')
  Tier A  (full gate)    state-changing ('upload', 'read-mac', 'bootstrap')
                          requires preview -> token -> confirm two-stage

Tracks: Strycher/LoRa#47 (A2, sub-task of Epic A #44)
Schema: C:\\Dev\\LoRa\\hardware-devices.yaml (A1 #46)
Hook:   .claude/hooks/block-raw-flash.sh (A3 #48, pending)
Proposal: C:\\Dev\\LoRa\\proposal-flash-discipline.md

Usage:
    pio-flash list
    pio-flash preview  <device> --env <pio-env>
    pio-flash confirm  <device> --token <token-file>
    pio-flash monitor  <device> [--env <env>] [--baud 115200]
    pio-flash info     <device>
    pio-flash read-mac <device>
    pio-flash backup   <device> [--output <path>] [--size <bytes>]
    pio-flash bootstrap <name> --port <COMx>

Exit codes:
    0  success
    1  error (registry, args, refusal, etc.)
    2  preview-only success (token written, did NOT flash)
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Optional

try:
    import yaml
except ImportError:
    print("FATAL: PyYAML not installed. Run: pip install PyYAML", file=sys.stderr)
    sys.exit(1)

# Sibling import of shared firmware_identity module (#200 / LoRa-wek).
# Force scripts/ onto sys.path so the import works regardless of CWD.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from firmware_identity import get_firmware_identity  # noqa: E402


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
PROJECT_ROOT = Path(__file__).resolve().parent.parent
REGISTRY_PATH = PROJECT_ROOT / "hardware-devices.yaml"
FLASH_HISTORY_PATH = PROJECT_ROOT / "flash-history.jsonl"
TOKEN_TTL_SEC = 300   # 5 min per Standards design note #1

# Directory holding the firmware repo / pio.ini for upload + monitor invocations.
# Per CLAUDE.md BUILD_COMMAND, the firmware lives at meshcore-firmware/ within
# the LoRa project. Override at runtime if needed via PIO_FLASH_FIRMWARE_DIR.
FIRMWARE_DIR = Path(os.environ.get(
    "PIO_FLASH_FIRMWARE_DIR",
    str(PROJECT_ROOT / "meshcore-firmware"),
))


# ---------------------------------------------------------------------------
# Output helpers - uniform formatting so the agent can parse refusal messages.
# ---------------------------------------------------------------------------
def out(msg: str) -> None:
    print(msg)


def err(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)


def refuse(msg: str, *, exit_code: int = 1) -> "NoReturn":
    print(f"REFUSE: {msg}", file=sys.stderr)
    sys.exit(exit_code)


# ---------------------------------------------------------------------------
# Registry loading
# ---------------------------------------------------------------------------
def load_registry() -> dict:
    if not REGISTRY_PATH.exists():
        refuse(
            f"hardware-devices.yaml not found at {REGISTRY_PATH}. "
            "Run A1 first or fix PROJECT_ROOT in this script."
        )
    with REGISTRY_PATH.open(encoding="utf-8") as f:
        data = yaml.safe_load(f)
    if not isinstance(data, dict):
        refuse(f"registry at {REGISTRY_PATH} is not a YAML mapping")
    data.setdefault("devices", {})
    data.setdefault("foreign_devices", {})
    return data


# ---------------------------------------------------------------------------
# Port enumeration via Windows PowerShell Get-PnpDevice.
# Returns list of dicts: {com, vid_pid, instance_hash, deviceid_full, description}.
# vid_pid is "VVVV:PPPP" uppercase. instance_hash is the trailing "8&XXXXXXXX"
# portion of the DeviceID string.
# ---------------------------------------------------------------------------
PS_ENUMERATE = r"""
$ErrorActionPreference = 'Stop'
Get-PnpDevice -Class Ports -PresentOnly | Where-Object { $_.Status -eq 'OK' } | ForEach-Object {
    $name = $_.FriendlyName
    $did  = $_.DeviceID
    $com  = ''
    if ($name -match '\(COM(\d+)\)') { $com = 'COM' + $matches[1] }
    $vid = ''; $prodid = ''
    if ($did -match 'VID_([0-9A-Fa-f]{4}).*PID_([0-9A-Fa-f]{4})') {
        $vid = $matches[1].ToUpper(); $prodid = $matches[2].ToUpper()
    }
    # Capture just the two-segment device-hash portion (e.g., "8&1A77809D") and
    # discard the trailing port-address segments (e.g., "&0&0000"), which vary
    # with physical USB topology. Standards-canonical hash format per
    # proposal-flash-discipline.md.
    $inst = ''
    if ($did -match '\\([0-9A-Fa-f]+&[0-9A-Fa-f]+)(?:&[0-9A-Fa-f]+)*$') {
        $inst = $matches[1]
    }
    # Emit one JSON line per port. ConvertTo-Json with -Compress is single-line.
    @{
        com = $com
        vid_pid = ($vid + ':' + $prodid)
        deviceid_full = $did
        instance_hash = $inst
        description = $name
    } | ConvertTo-Json -Compress
}
"""


def enumerate_ports() -> list[dict]:
    try:
        result = subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", PS_ENUMERATE],
            capture_output=True, text=True, check=True, timeout=15,
        )
    except subprocess.CalledProcessError as e:
        refuse(f"PowerShell enumeration failed: {e.stderr or e}")
    except FileNotFoundError:
        refuse("powershell.exe not found. This wrapper is Windows-only in v1.")
    except subprocess.TimeoutExpired:
        refuse("PowerShell enumeration timed out (15s)")

    ports = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            d = json.loads(line)
        except json.JSONDecodeError:
            err(f"could not parse PowerShell output line: {line!r}")
            continue
        # Some ports (non-USB) won't have VID/PID. Skip those.
        if d.get("vid_pid") in (None, "", ":"):
            continue
        ports.append(d)
    return ports


# ---------------------------------------------------------------------------
# Registry lookup - given a port's VID:PID + instance hash, find the matching
# registered device (or foreign device). Returns (kind, name, entry) where
# kind is "device" / "foreign" / None.
# ---------------------------------------------------------------------------
def find_in_registry(
    registry: dict, vid_pid: str, instance_hash: str
) -> tuple[Optional[str], Optional[str], Optional[dict]]:
    for kind_key, kind_label in [("devices", "device"), ("foreign_devices", "foreign")]:
        for name, entry in (registry.get(kind_key) or {}).items():
            entry_vid_pids = entry.get("vid_pid") or []
            if vid_pid not in entry_vid_pids:
                continue
            # VID:PID class match. Now check DeviceID instance hash if known.
            d = (entry.get("discriminators") or {}).get("windows") or {}
            known_hashes = [
                d.get("runtime_deviceid_instance"),
                d.get("bootloader_deviceid_instance"),
            ]
            known_hashes = [h for h in known_hashes if h]
            if not known_hashes:
                # Registry has this device class but no per-instance hash yet.
                # Match by VID:PID alone is weaker but acceptable for first
                # registration of a known-class device.
                return (kind_label, name, entry)
            if instance_hash in known_hashes:
                return (kind_label, name, entry)
            # VID:PID matched but instance hash didn't - keep searching, another
            # entry might match. (Possible if user has two devices of the same
            # class on the bus, e.g., two Heltec V4s.)
    return (None, None, None)


# ---------------------------------------------------------------------------
# Resolve a device name to a present port. Used by every Tier A and Tier B mode.
# Returns the (port_info, entry) tuple on success; refuses cleanly on failure.
# ---------------------------------------------------------------------------
def resolve_device(name: str, registry: dict) -> tuple[dict, dict]:
    if name not in (registry.get("devices") or {}):
        if name in (registry.get("foreign_devices") or {}):
            refuse(
                f"'{name}' is registered as a FOREIGN device "
                "(do-not-touch). Refusing all device-touching operations."
            )
        refuse(
            f"'{name}' not registered in hardware-devices.yaml under 'devices:'. "
            "Did you mean to bootstrap it first? See 'pio-flash bootstrap'."
        )

    entry = registry["devices"][name]
    entry_vid_pids = set(entry.get("vid_pid") or [])
    if not entry_vid_pids:
        refuse(f"device '{name}' has no vid_pid in registry; cannot identify port")

    ports = enumerate_ports()

    # First, see if any present port matches THIS device's registered hashes.
    matches = []
    for p in ports:
        if p["vid_pid"] not in entry_vid_pids:
            continue
        d = (entry.get("discriminators") or {}).get("windows") or {}
        known_hashes = [
            d.get("runtime_deviceid_instance"),
            d.get("bootloader_deviceid_instance"),
        ]
        known_hashes = [h for h in known_hashes if h]
        if known_hashes and p["instance_hash"] in known_hashes:
            matches.append(p)
        elif not known_hashes:
            # Class match only - registry has no hash yet. Accept but warn.
            matches.append(p)

    if not matches:
        # No port matches this device. Surface what IS present so the user can
        # see whether they expected this device to be connected.
        present_summary = ", ".join(
            f"{p['com']}={p['vid_pid']}({p['description']})" for p in ports
        ) or "(no ports)"
        refuse(
            f"no present port matches device '{name}' "
            f"(expected VID:PID in {sorted(entry_vid_pids)}, "
            f"known DeviceID hashes from registry). "
            f"Present: {present_summary}"
        )

    if len(matches) > 1:
        ports_list = ", ".join(p["com"] for p in matches)
        refuse(
            f"device '{name}' matches multiple present ports ({ports_list}). "
            "Refusing rather than guessing. Disconnect duplicates first."
        )

    # Now also confirm no UNREGISTERED port is enumerated that could be confusable.
    # If there's a port with an unknown VID:PID + instance, the user/agent needs
    # to know about it (it's a candidate for bootstrap or foreign registration).
    unregistered = []
    for p in ports:
        kind, _, _ = find_in_registry(registry, p["vid_pid"], p["instance_hash"])
        if kind is None:
            unregistered.append(p)
    if unregistered:
        # Not an error, but a notification. Some unregistered ports are normal
        # (the user has dev boards plugged in we don't care about). Surface it.
        for p in unregistered:
            err(
                f"NOTE: unregistered port present: {p['com']} "
                f"VID:PID={p['vid_pid']} hash={p['instance_hash']} "
                f"desc={p['description']!r}. "
                "If this is a new LoRa device, run 'pio-flash bootstrap'."
            )

    return (matches[0], entry)


# ---------------------------------------------------------------------------
# Token handling for the preview -> confirm two-stage flow.
# ---------------------------------------------------------------------------
def token_path(device_name: str) -> Path:
    safe = re.sub(r"[^A-Za-z0-9_.-]", "_", device_name)
    return Path(tempfile.gettempdir()) / f"pio-flash-token-{safe}.json"


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def write_token(device_name: str, payload: dict) -> Path:
    p = token_path(device_name)
    payload["created_unix"] = int(time.time())
    p.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return p


def read_token(path: Path) -> dict:
    if not path.exists():
        refuse(f"token file {path} does not exist (preview first?)")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        refuse(f"token file {path} is not valid JSON: {e}")
    age = int(time.time()) - int(data.get("created_unix", 0))
    if age > TOKEN_TTL_SEC:
        refuse(
            f"token at {path} expired ({age}s old, TTL {TOKEN_TTL_SEC}s). "
            "Re-run 'pio-flash preview'."
        )
    return data


def log_history(entry: dict) -> None:
    FLASH_HISTORY_PATH.parent.mkdir(parents=True, exist_ok=True)
    with FLASH_HISTORY_PATH.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry, separators=(",", ":")) + "\n")


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------
def cmd_list(args, registry):
    """Tier 0: enumerate present ports vs registry. No device touch."""
    ports = enumerate_ports()
    out(f"Present ports ({len(ports)}):")
    out(f"{'COM':<8} {'VID:PID':<12} {'instance':<14} {'match':<22} description")
    out("-" * 100)
    for p in ports:
        kind, name, _ = find_in_registry(registry, p["vid_pid"], p["instance_hash"])
        if kind == "device":
            tag = f"device:{name}"
        elif kind == "foreign":
            tag = f"FOREIGN:{name}"
        else:
            tag = "unregistered"
        out(f"{p['com']:<8} {p['vid_pid']:<12} {p['instance_hash']:<14} {tag:<22} {p['description']}")
    if not ports:
        out("(no present serial ports)")
    return 0


def cmd_preview(args, registry):
    """Tier A stage 1: resolve target, validate firmware, write token, exit 2."""
    port, entry = resolve_device(args.device, registry)
    env = args.env
    firmware_bin = FIRMWARE_DIR / ".pio" / "build" / env / "firmware.bin"
    if not firmware_bin.exists():
        refuse(
            f"firmware {firmware_bin} not found. "
            f"Build first: cd {FIRMWARE_DIR} && pio run -e {env}"
        )

    sha = sha256_of(firmware_bin)
    size = firmware_bin.stat().st_size

    out("============================================================")
    out("PREVIEW (Tier A flash) - no device touch yet")
    out("============================================================")
    out(f"Target device : {args.device}")
    out(f"Registered MAC: {entry.get('mac')}")
    out(f"Resolved port : {port['com']}")
    out(f"Port VID:PID  : {port['vid_pid']}")
    out(f"Port DeviceID : {port['deviceid_full']}")
    out(f"Hash match    : {port['instance_hash']}")
    out(f"Firmware bin  : {firmware_bin}")
    out(f"Firmware sha256: {sha}")
    out(f"Firmware size : {size} bytes")
    out(f"Pio env       : {env}")
    out("------------------------------------------------------------")
    out("To proceed, get explicit user GO in chat naming the device,")
    out("then run:")
    out(f"  scripts/pio-flash confirm {args.device} --token {token_path(args.device)}")
    out(f"Token TTL: {TOKEN_TTL_SEC}s. Token invalidates if port/DeviceID/")
    out("firmware sha changes before confirm.")
    out("============================================================")

    # #200 (LoRa-wek): identity is read from firmware_bin's embedded XWIRE
    # marker blob, not re-derived from git. Embedded in the token so
    # cmd_confirm logs the same identity it previewed - no second source of
    # truth between preview and confirm.
    fw_identity = get_firmware_identity(firmware_bin, FIRMWARE_DIR)
    out(f"Crosswire version : {fw_identity['crosswire_version']}")
    out(f"Crosswire SHA     : {fw_identity['crosswire_git_sha']}")
    out(f"Crosswire branch  : {fw_identity['crosswire_branch']}")
    out(f"Identity source   : {fw_identity['firmware_identity_source']}")
    out("------------------------------------------------------------")

    payload = {
        "device": args.device,
        "port": port["com"],
        "deviceid_full": port["deviceid_full"],
        "vid_pid": port["vid_pid"],
        "instance_hash": port["instance_hash"],
        "firmware_bin": str(firmware_bin),
        "firmware_sha256": sha,
        "firmware_size": size,
        "pio_env": env,
        "firmware_dir": str(FIRMWARE_DIR),
        "crosswire_version": fw_identity["crosswire_version"],
        "crosswire_git_sha": fw_identity["crosswire_git_sha"],
        "crosswire_branch": fw_identity["crosswire_branch"],
        "crosswire_build_date": fw_identity["crosswire_build_date"],
        "firmware_identity_source": fw_identity["firmware_identity_source"],
    }
    p = write_token(args.device, payload)
    out(f"Token written: {p}")
    return 2


def cmd_confirm(args, registry):
    """Tier A stage 2: validate token, re-verify state, perform the flash."""
    token = read_token(Path(args.token))

    if token["device"] != args.device:
        refuse(
            f"token device '{token['device']}' does not match confirm device "
            f"'{args.device}'. Cannot proceed."
        )

    # Re-resolve and confirm nothing changed since preview.
    port, entry = resolve_device(args.device, registry)
    if port["com"] != token["port"]:
        refuse(
            f"port changed since preview ({token['port']} -> {port['com']}). "
            "Re-run preview."
        )
    if port["deviceid_full"] != token["deviceid_full"]:
        refuse(
            "DeviceID changed since preview "
            f"({token['deviceid_full']} -> {port['deviceid_full']}). "
            "Re-run preview."
        )

    firmware_bin = Path(token["firmware_bin"])
    if not firmware_bin.exists():
        refuse(f"firmware {firmware_bin} missing since preview")
    sha_now = sha256_of(firmware_bin)
    if sha_now != token["firmware_sha256"]:
        refuse(
            f"firmware sha256 changed since preview "
            f"({token['firmware_sha256']} -> {sha_now}). Re-run preview."
        )

    out("============================================================")
    out(f"FLASHING {args.device} on {port['com']} (env={token['pio_env']})")
    out("============================================================")

    cmd = [
        "pio", "run",
        "-e", token["pio_env"],
        "-t", "upload",
        "--upload-port", port["com"],
    ]
    env = os.environ.copy()
    # Mark that this pio invocation came from the wrapper so a future hook
    # iteration can grant pass-through. v1 hook simply lets pio-flash through
    # as the outer script; pio is invoked as a subprocess of THIS python,
    # outside the agent's Bash tool, so the hook does not intercept it.
    env["PIO_FLASH_AUTHORIZED"] = "1"
    rc = subprocess.call(cmd, cwd=str(Path(token["firmware_dir"])), env=env)

    # FF5: identity fields propagated from token (captured at preview time).
    # Fields are .get() to gracefully tolerate older pre-FF5 tokens.
    log_history({
        "ts_unix": int(time.time()),
        "mode": "upload",
        "device": args.device,
        "port": port["com"],
        "deviceid_full": port["deviceid_full"],
        "vid_pid": port["vid_pid"],
        "instance_hash": port["instance_hash"],
        "pio_env": token["pio_env"],
        "firmware_sha256": token["firmware_sha256"],
        "firmware_size": token["firmware_size"],
        "crosswire_version": token.get("crosswire_version", "unknown"),
        "crosswire_git_sha": token.get("crosswire_git_sha", "unknown"),
        "crosswire_branch": token.get("crosswire_branch", "unknown"),
        "crosswire_build_date": token.get("crosswire_build_date", "unknown"),
        "firmware_identity_source": token.get("firmware_identity_source", "git-fallback"),
        "exit_code": rc,
        "user_confirmation": args.device,
    })

    # Invalidate the token by deleting it. Single-use.
    try:
        Path(args.token).unlink()
    except OSError:
        pass

    out(f"pio upload exit code: {rc}")
    return rc


def cmd_send(args, registry):
    """
    Tier B: open serial, write command + CR, read response for N seconds.
    Does not reset the chip on ESP32-S3 native USB / USB-Serial-JTAG.

    Targets devices running CLIs that read from the same Serial endpoint
    used for output (e.g., MeshCore simple_repeater main.cpp's loop()).
    Sends `<command>\\r` and prints whatever the device emits during the
    read window.
    """
    port, _ = resolve_device(args.device, registry)
    out(f"Sending to {args.device} on {port['com']}: {args.command!r}")
    out(f"Reading response for {args.read_time}s...")

    try:
        import serial
    except ImportError:
        refuse("pyserial not available; pip install pyserial")

    try:
        with serial.Serial(port["com"], baudrate=args.baud, timeout=0.2) as s:
            # Brief settle and drain
            time.sleep(0.2)
            try:
                s.reset_input_buffer()
            except Exception:
                pass

            s.write((args.command + "\r").encode("utf-8"))
            s.flush()

            deadline = time.time() + args.read_time
            captured = bytearray()
            while time.time() < deadline:
                data = s.read(1024)
                if data:
                    captured.extend(data)
                    try:
                        sys.stdout.buffer.write(data)
                        sys.stdout.buffer.flush()
                    except Exception:
                        # If stdout.buffer not available, fall back to text print
                        sys.stdout.write(data.decode("utf-8", errors="replace"))
                        sys.stdout.flush()

            if captured and not bytes(captured).endswith(b"\n"):
                print()
    except Exception as e:
        refuse(f"serial send failed on {port['com']}: {e}")

    log_history({
        "ts_unix": int(time.time()),
        "mode": "send",
        "device": args.device,
        "port": port["com"],
        "deviceid_full": port["deviceid_full"],
        "vid_pid": port["vid_pid"],
        "instance_hash": port["instance_hash"],
        "command": args.command,
        "exit_code": 0,
    })
    return 0


def cmd_monitor(args, registry):
    """Tier B: open serial monitor.

    On ESP32-S3 native-USB boards (e.g. XIAO_S3 / RAK_4631 nRF native USB),
    opening the port does not reset the chip.

    On CP2102-bridged ESP32 boards (notably Heltec V3), DTR/RTS are physically
    wired to BOOT/RESET. Default PlatformIO monitor behavior toggles DTR/RTS
    on port open, which resets the chip and -- under tight heap budgets --
    can throw it into a crash-cycle. To prevent that, the affected envs in
    meshcore-firmware (see variants/heltec_v3/platformio.ini
    [env:heltec_v3_companion_observer_wifi]) set:

        monitor_rts = 0
        monitor_dtr = 0

    PlatformIO only picks up those env-scoped settings when monitor is
    invoked with `-e <env>`. Pass --env here to forward that through.
    Without --env on a CP2102 board, monitor open WILL reset the chip;
    a stderr warning is emitted so the operator sees this before the device
    cycles.

    Tracking: Strycher/LoRa#302 (this fix). Prior art comment lives in the
    observer env's platformio.ini.
    """
    port, _ = resolve_device(args.device, registry)
    env_suffix = f" (env={args.env})" if args.env else ""
    out(f"Opening monitor on {port['com']} for {args.device} at {args.baud} baud" + env_suffix)
    if not args.env:
        # Loud warning to stderr per Strycher/LoRa#302 acceptance criteria.
        # CP2102-bridged V3 boards reset on port open without env-set
        # monitor_rts=0/monitor_dtr=0. nRF native-USB boards are unaffected;
        # the warning is intentionally always-on rather than VID:PID-gated
        # so operators get one consistent message regardless of target.
        sys.stderr.write(
            "WARNING: monitor invoked without --env. PlatformIO will use\n"
            "  default DTR/RTS handling. On CP2102-bridged ESP32 boards\n"
            "  (Heltec V3), this resets the chip on port open and can\n"
            "  crash-cycle the device under tight heap budgets. To inherit\n"
            "  env-scoped monitor_rts/monitor_dtr settings, pass\n"
            "    --env <env>\n"
            "  (e.g. --env heltec_v3_companion_observer_wifi). See #302.\n"
        )
        sys.stderr.flush()
    cmd = [
        "pio", "device", "monitor",
        "--port", port["com"],
        "--baud", str(args.baud),
    ]
    if args.env:
        cmd.extend(["-e", args.env])
    rc = subprocess.call(cmd, cwd=str(FIRMWARE_DIR))
    log_history({
        "ts_unix": int(time.time()),
        "mode": "monitor",
        "device": args.device,
        "port": port["com"],
        "deviceid_full": port["deviceid_full"],
        "vid_pid": port["vid_pid"],
        "instance_hash": port["instance_hash"],
        "baud": args.baud,
        "env": args.env,
        "exit_code": rc,
    })
    return rc


def cmd_info(args, registry):
    """Tier B: meshtastic --info. Reads device state, does not reset."""
    port, _ = resolve_device(args.device, registry)
    out(f"Running meshtastic --info on {port['com']} for {args.device}")
    cmd = ["meshtastic", "--port", port["com"], "--info"]
    rc = subprocess.call(cmd)
    log_history({
        "ts_unix": int(time.time()),
        "mode": "info",
        "device": args.device,
        "port": port["com"],
        "exit_code": rc,
    })
    return rc


def cmd_read_mac(args, registry):
    """Tier A: esptool read_mac. Resets the chip on every invocation."""
    port, entry = resolve_device(args.device, registry)
    out(f"Running esptool read_mac on {port['com']} for {args.device}")
    out(f"(this WILL reset the chip into ROM bootloader and back)")
    cmd = [
        "python", "-m", "esptool",
        "--port", port["com"],
        "read_mac",
    ]
    env = os.environ.copy()
    env["PIO_FLASH_AUTHORIZED"] = "1"
    rc = subprocess.call(cmd, env=env)
    log_history({
        "ts_unix": int(time.time()),
        "mode": "read-mac",
        "device": args.device,
        "port": port["com"],
        "deviceid_full": port["deviceid_full"],
        "vid_pid": port["vid_pid"],
        "instance_hash": port["instance_hash"],
        "exit_code": rc,
    })
    return rc


def cmd_backup(args, registry):
    """
    Tier A: read full flash to a local file. esptool read_flash uses the
    same DTR/RTS reset sequence as write_flash, so this resets the chip
    on every invocation. Same identity discipline as other Tier A ops,
    but single-stage (no token chaining): the user is the one who said
    'back up first.'

    Default output: C:\\Dev\\LoRa\\flash-backups\\<device>-<YYYYmmdd-HHMMSS>.bin
    Default size:   0x1000000 (16 MB, full ESP32-S3 flash)
    """
    port, entry = resolve_device(args.device, registry)

    backups_dir = PROJECT_ROOT / "flash-backups"
    backups_dir.mkdir(exist_ok=True)

    timestamp = time.strftime("%Y%m%d-%H%M%S")
    safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", args.device)
    if args.output:
        output = Path(args.output)
    else:
        output = backups_dir / f"{safe_name}-{timestamp}.bin"

    flash_size = args.size
    baud = args.baud
    # Throughput estimate: ~10% of baud rate in bytes/sec after protocol overhead.
    est_sec = max(1, flash_size // (baud // 10))

    out("============================================================")
    out(f"BACKUP (Tier A): read {flash_size} bytes from {args.device}")
    out("============================================================")
    out(f"Target device : {args.device}")
    out(f"Registered MAC: {entry.get('mac')}")
    out(f"Resolved port : {port['com']}")
    out(f"Port DeviceID : {port['deviceid_full']}")
    out(f"Read offset   : 0x{args.offset:x} ({args.offset} bytes from start of flash)")
    out(f"Read size     : 0x{flash_size:x} ({flash_size} bytes)")
    out(f"Output        : {output}")
    out(f"Estimated time: ~{est_sec}s at {baud} baud")
    out("------------------------------------------------------------")
    out("Note: esptool read_flash resets the chip on entry and exit.")
    out("After backup completes, the chip will reboot into normal mode.")
    out("------------------------------------------------------------")

    env_dict = os.environ.copy()
    env_dict["PIO_FLASH_AUTHORIZED"] = "1"
    cmd = [
        "python", "-m", "esptool",
        "--port", port["com"],
        "--baud", str(baud),
        "read_flash", str(args.offset), str(flash_size), str(output),
    ]
    rc = subprocess.call(cmd, env=env_dict)

    if rc == 0 and output.exists():
        actual_size = output.stat().st_size
        sha = sha256_of(output)
        out("")
        out("Backup complete.")
        out(f"  File : {output}")
        out(f"  Size : {actual_size} bytes")
        out(f"  SHA256: {sha}")
        log_history({
            "ts_unix": int(time.time()),
            "mode": "backup",
            "device": args.device,
            "port": port["com"],
            "deviceid_full": port["deviceid_full"],
            "vid_pid": port["vid_pid"],
            "instance_hash": port["instance_hash"],
            "read_offset": args.offset,
            "read_size": flash_size,
            "output_path": str(output),
            "output_size": actual_size,
            "output_sha256": sha,
            "exit_code": 0,
        })
    else:
        err(f"esptool read_flash failed (rc={rc}). Backup may be incomplete.")
        log_history({
            "ts_unix": int(time.time()),
            "mode": "backup",
            "device": args.device,
            "port": port["com"],
            "deviceid_full": port["deviceid_full"],
            "read_offset": args.offset,
            "read_size": flash_size,
            "exit_code": rc,
        })

    return rc


def cmd_erase_region(args, registry):
    """
    Tier A: erase a specific flash region. Same identity discipline as
    backup / read-mac / etc. -- the wrapper-blessed alternative to raw
    esptool erase_region (which the block-raw-flash hook would refuse).

    Use case: recover from corrupted NVS state (e.g., a bad bond entry
    that crashes BLE init on every subsequent boot) by erasing just the
    NVS partition without disturbing app / bootloader / partition table.

    Common ESP32-S3 8MB layout regions (default partition table):
      --offset 0x9000  --size 0x6000    NVS partition (24 KB)
      --offset 0xf000  --size 0x2000    otadata partition (8 KB)
      --offset 0x10000 --size 0x1f0000  app0 partition (~2 MB)
      --offset 0x200000 --size 0x1f0000 app1 partition (~2 MB)

    For broader-blast options use the existing `factory-reset` command
    (which is currently V4-only and only erases SPIFFS -- see follow-up).

    esptool erase_region uses the same DTR/RTS reset sequence as
    write_flash, so this resets the chip on entry AND exit. The chip
    will reboot after the operation completes.
    """
    port, entry = resolve_device(args.device, registry)

    if args.offset < 0 or args.size <= 0:
        refuse(f"invalid offset/size: offset={args.offset}, size={args.size}")

    out("============================================================")
    out(f"ERASE REGION (Tier A): {args.size} bytes from {args.device}")
    out("============================================================")
    out(f"Target device : {args.device}")
    out(f"Registered MAC: {entry.get('mac')}")
    out(f"Resolved port : {port['com']}")
    out(f"Port DeviceID : {port['deviceid_full']}")
    out(f"Erase offset  : 0x{args.offset:x} ({args.offset} bytes from start of flash)")
    out(f"Erase size    : 0x{args.size:x} ({args.size} bytes)")
    out("------------------------------------------------------------")
    out("WARNING: this is DESTRUCTIVE for any data in the named region.")
    out("After erase completes, the chip will reboot. Any in-region")
    out("state (NVS keys, OTA select, app code, etc.) will be GONE.")
    out("------------------------------------------------------------")

    env_dict = os.environ.copy()
    env_dict["PIO_FLASH_AUTHORIZED"] = "1"
    cmd = [
        "python", "-m", "esptool",
        "--chip", "esp32s3",
        "--port", port["com"],
        "erase_region", str(args.offset), str(args.size),
    ]
    rc = subprocess.call(cmd, env=env_dict)

    log_history({
        "ts_unix": int(time.time()),
        "mode": "erase_region",
        "device": args.device,
        "port": port["com"],
        "deviceid_full": port["deviceid_full"],
        "vid_pid": port["vid_pid"],
        "instance_hash": port["instance_hash"],
        "erase_offset": args.offset,
        "erase_size": args.size,
        "exit_code": rc,
    })

    if rc == 0:
        out("")
        out("Erase complete. Chip should be rebooting now.")
    else:
        err(f"esptool erase_region failed (rc={rc}).")

    return rc


def cmd_bootstrap(args, registry):
    """
    Register a new device. User-initiated only. Performs ONE authorized
    esptool read_mac, cross-checks for MAC collisions, then appends to
    hardware-devices.yaml.
    """
    name = args.name
    com = args.port

    if name in (registry.get("devices") or {}):
        refuse(
            f"'{name}' is already registered under devices:. Use a different name "
            "or remove the existing entry first."
        )
    if name in (registry.get("foreign_devices") or {}):
        refuse(
            f"'{name}' is already registered under foreign_devices:. Use a different name."
        )

    # Find the port in the enumeration so we have its VID:PID + DeviceID.
    ports = enumerate_ports()
    target = next((p for p in ports if p["com"] == com), None)
    if target is None:
        present = ", ".join(p["com"] for p in ports) or "(none)"
        refuse(f"port {com} not present. Currently enumerated: {present}")

    out("============================================================")
    out(f"BOOTSTRAP: registering new device '{name}' on {com}")
    out("============================================================")
    out(f"Port VID:PID  : {target['vid_pid']}")
    out(f"Port DeviceID : {target['deviceid_full']}")
    out(f"Hash          : {target['instance_hash']}")
    out(f"Description   : {target['description']}")
    out("")
    out("Reading MAC via esptool (this is the ONE authorized device touch)...")
    out("")

    env = os.environ.copy()
    env["PIO_FLASH_AUTHORIZED"] = "1"
    try:
        result = subprocess.run(
            ["python", "-m", "esptool", "--port", com, "read_mac"],
            capture_output=True, text=True, env=env, timeout=30,
        )
    except subprocess.TimeoutExpired:
        refuse(f"esptool read_mac timed out on {com}")
    if result.returncode != 0:
        refuse(
            f"esptool read_mac failed (rc={result.returncode}). "
            f"stderr: {result.stderr.strip()}"
        )

    # Parse MAC from esptool output. Format example:
    #   MAC: <device-mac>
    m = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", result.stdout)
    if not m:
        out(result.stdout)
        refuse("could not parse MAC from esptool output (see stdout above)")
    mac = m.group(1).lower()
    out(f"MAC read from device: {mac}")

    # Cross-check: does this MAC already belong to a different registered name?
    for kind_key in ("devices", "foreign_devices"):
        for existing_name, existing_entry in (registry.get(kind_key) or {}).items():
            if (existing_entry.get("mac") or "").lower() == mac:
                refuse(
                    f"MAC {mac} is already registered as '{existing_name}' "
                    f"under {kind_key}:. Refusing to register the same MAC under "
                    f"a second name '{name}'. If this is a re-bootstrap, edit "
                    f"the YAML by hand or use a different name."
                )

    # Build new entry. v1: assumes ESP32-S3 dual-mode; user can edit later.
    new_entry = {
        "mac": mac,
        "role": f"new device registered via bootstrap on {time.strftime('%Y-%m-%d')}",
        "vid_pid": [target["vid_pid"]],
        "discriminators": {
            "windows": {
                "runtime_deviceid_instance": target["instance_hash"],
            },
        },
        "notes": (
            f"Bootstrapped via pio-flash bootstrap on "
            f"{time.strftime('%Y-%m-%d %H:%M:%S %Z')}. "
            f"Port at bootstrap time: {com}. "
            f"Description: {target['description']}. "
            "Other discriminators (bootloader mode) TBD on next observation."
        ),
    }
    registry["devices"][name] = new_entry

    # Atomic write: dump to temp file, then rename.
    tmp = REGISTRY_PATH.with_suffix(".yaml.tmp")
    with tmp.open("w", encoding="utf-8") as f:
        # Preserve the header comment by reading + rewriting. Simpler: just
        # dump the data structure and re-add a short header. The full header
        # lives in the original file; bootstrap-modified files lose the long
        # documentation header. Document the trade-off here.
        f.write(
            "# hardware-devices.yaml (regenerated via pio-flash bootstrap)\n"
            "# Original schema documentation: see git history or "
            "proposal-flash-discipline.md section 3.\n"
            "# Tracks: Strycher/LoRa#46 (A1) and Strycher/LoRa#47 (A2 bootstrap path)\n\n"
        )
        yaml.safe_dump(registry, f, sort_keys=False, allow_unicode=True)
    tmp.replace(REGISTRY_PATH)

    log_history({
        "ts_unix": int(time.time()),
        "mode": "bootstrap",
        "device": name,
        "port": com,
        "deviceid_full": target["deviceid_full"],
        "vid_pid": target["vid_pid"],
        "instance_hash": target["instance_hash"],
        "mac": mac,
        "exit_code": 0,
    })

    out("")
    out(f"Registered '{name}' with MAC {mac} in {REGISTRY_PATH}")
    out("Review the file and add bootloader discriminator on next bootloader-mode "
        "observation.")
    return 0


def cmd_factory_reset(args, registry):
    """Tier A: erase data partition + reflash app in one bootloader session.

    Use case: device's runtime prefs file (in LittleFS) overrides build-time
    LORA_FREQ / other defaults. To get the build-flag defaults to take effect,
    the data partition must be erased so loadPrefs() finds no file and falls
    back to defaults.

    Requires the chip to be in ROM bootloader BEFORE this command runs
    (manual BOOT-hold + RST-tap + BOOT-release). esptool's first call uses
    --after no_reset so the chip stays in bootloader; the second call (pio
    upload) re-uses that bootloader connection.

    Hardcoded for Heltec V4 ESP32-S3 16MB layout: SPIFFS at 0xc90000, size 0x370000.
    """
    port, entry = resolve_device(args.device, registry)
    firmware_bin = FIRMWARE_DIR / ".pio" / "build" / args.env / "firmware.bin"
    if not firmware_bin.exists():
        refuse(f"firmware {firmware_bin} missing. Build first: pio run -e {args.env}")

    out("============================================================")
    out("FACTORY RESET FLASH (Tier A, single session)")
    out("============================================================")
    out(f"Target device : {args.device}")
    out(f"Resolved port : {port['com']}")
    out(f"Firmware bin  : {firmware_bin}")
    out(f"Pio env       : {args.env}")
    out(f"Will erase    : 0xc90000 + 0x370000 (data partition, 3.4 MB)")
    out("PREREQUISITE  : chip in ROM bootloader (BOOT+RST manually pressed)")
    out("Sequence: esptool erase_region --after no_reset, then pio upload.")
    out("------------------------------------------------------------")

    # Step 1: erase data partition, keep chip in bootloader for step 2
    erase_cmd = [
        "python", "-m", "esptool",
        "--chip", "esp32s3",
        "--port", port["com"],
        "--after", "no_reset",
        "erase_region", "0xc90000", "0x370000",
    ]
    out(f"[1/2] {' '.join(erase_cmd)}")
    rc1 = subprocess.call(erase_cmd)
    if rc1 != 0:
        refuse(f"erase_region failed with exit {rc1}. Chip may not be in ROM bootloader.")
    out("[1/2] erase OK")

    # Step 2: upload app via pio (re-uses bootloader connection)
    upload_cmd = [
        "pio", "run",
        "-e", args.env,
        "-t", "upload",
        "--upload-port", port["com"],
    ]
    out(f"[2/2] {' '.join(upload_cmd)}")
    env_d = os.environ.copy()
    env_d["PIO_FLASH_AUTHORIZED"] = "1"
    rc2 = subprocess.call(upload_cmd, cwd=str(FIRMWARE_DIR), env=env_d)

    log_history({
        "ts_unix": int(time.time()),
        "mode": "factory_reset",
        "device": args.device,
        "port": port["com"],
        "deviceid_full": port["deviceid_full"],
        "vid_pid": port["vid_pid"],
        "instance_hash": port["instance_hash"],
        "pio_env": args.env,
        "erased_offset": "0xc90000",
        "erased_size": "0x370000",
        "erase_exit_code": rc1,
        "upload_exit_code": rc2,
        "exit_code": rc2,
    })

    out(f"factory_reset complete: erase rc={rc1}, upload rc={rc2}")
    if rc2 != 0:
        out("WARNING: upload failed. Chip may now have erased data partition but old app.")
    return rc2


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="pio-flash",
        description="LoRa flash-discipline wrapper (Epic A #44 / A2 #47).",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list", help="enumerate present ports vs registry (Tier 0)")

    s = sub.add_parser("preview", help="stage a Tier A flash (writes token, no flash)")
    s.add_argument("device", help="registered device name (e.g. ST-P)")
    s.add_argument("--env", required=True, help="pio env (e.g. heltec_v4_repeater_telemetry)")

    s = sub.add_parser("confirm", help="execute the staged Tier A flash")
    s.add_argument("device", help="must match the device in the token")
    s.add_argument("--token", required=True, help="path to token file from preview")

    s = sub.add_parser("monitor", help="open serial monitor (Tier B; no reset on properly-configured envs -- see --env)")
    s.add_argument("device")
    s.add_argument("--baud", type=int, default=115200)
    s.add_argument("--env", default=None,
                   help="pio env (e.g. heltec_v3_companion_observer_wifi). "
                        "REQUIRED on V3 CP2102 SKUs to pick up env-set "
                        "monitor_rts=0/monitor_dtr=0; without this, port "
                        "open toggles DTR/RTS and resets the chip. See "
                        "Strycher/LoRa#302.")

    s = sub.add_parser("send", help="send a CLI command to device, read response (Tier B)")
    s.add_argument("device")
    s.add_argument("command", help="text command to send (CR is auto-appended)")
    s.add_argument("--baud", type=int, default=115200)
    s.add_argument("--read-time", type=float, default=5.0,
                   help="seconds to read response after sending (default 5)")

    s = sub.add_parser("info", help="meshtastic --info (Tier B)")
    s.add_argument("device")

    s = sub.add_parser("read-mac", help="esptool read_mac (Tier A, resets chip)")
    s.add_argument("device")

    s = sub.add_parser("backup", help="read flash region to file (Tier A, resets chip)")
    s.add_argument("device")
    s.add_argument("--output", help="output file path (default: flash-backups/<dev>-<timestamp>.bin)")
    s.add_argument("--offset", type=lambda x: int(x, 0), default=0,
                   help="start offset in bytes (default 0 = start of flash; use 0x9000 for default NVS partition on ESP32-S3 8MB layout)")
    s.add_argument("--size", type=lambda x: int(x, 0), default=0x1000000,
                   help="region size in bytes (default 0x1000000 = 16 MB full ESP32-S3 flash; use 0x6000 for default NVS partition)")
    s.add_argument("--baud", type=int, default=460800,
                   help="post-stub baud rate (default 460800; drop to 115200/230400 if "
                        "high baud produces 'serial stream stopped' on long reads)")

    s = sub.add_parser("erase-region", help="erase specific flash region (Tier A, resets chip)")
    s.add_argument("device")
    s.add_argument("--offset", type=lambda x: int(x, 0), required=True,
                   help="start offset in bytes (e.g. 0x9000 for NVS on ESP32-S3 8MB layout)")
    s.add_argument("--size", type=lambda x: int(x, 0), required=True,
                   help="region size in bytes (e.g. 0x6000 for NVS partition)")

    s = sub.add_parser("bootstrap", help="register a new device (user-initiated only)")
    s.add_argument("name", help="short name for the new device")
    s.add_argument("--port", required=True, help="COM port the new device is on (e.g. COM7)")

    s = sub.add_parser("factory-reset", help="erase data partition + reflash app (Tier A; requires BOOT+RST first)")
    s.add_argument("device", help="registered device name (e.g. ST-P)")
    s.add_argument("--env", required=True, help="pio env (e.g. heltec_v4_repeater_telemetry_stp)")

    return p


def main() -> int:
    args = build_parser().parse_args()
    registry = load_registry()
    dispatch = {
        "list": cmd_list,
        "preview": cmd_preview,
        "confirm": cmd_confirm,
        "monitor": cmd_monitor,
        "send": cmd_send,
        "info": cmd_info,
        "read-mac": cmd_read_mac,
        "backup": cmd_backup,
        "erase-region": cmd_erase_region,
        "bootstrap": cmd_bootstrap,
        "factory-reset": cmd_factory_reset,
    }
    fn = dispatch.get(args.cmd)
    if fn is None:
        err(f"unknown subcommand: {args.cmd}")
        return 1
    return fn(args, registry)


if __name__ == "__main__":
    sys.exit(main())
