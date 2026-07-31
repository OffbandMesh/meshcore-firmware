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
    pio-flash preview  <device> --artifact <path> [--erase]
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
# #500 OWNER RULING: tokens have NO wall-clock expiry. ONE APPROVAL = ONE
# FLASH; the owner's GO does not rot while he is away. A token is single-use
# (deleted on confirm) and hard-invalidates on real state drift — port,
# DeviceID, or artifact-sha change since preview. Time is not a safety
# property; never reintroduce a TTL on a human approval.

# Directory holding the firmware tree (platformio.ini + variants/) for build +
# upload + monitor invocations. Post-migration the Offband repo root IS the
# firmware tree, so the default is PROJECT_ROOT. Override per-host via the
# PIO_FLASH_FIRMWARE_DIR env var, or per-invocation via --firmware-dir.
# Precedence: --firmware-dir > PIO_FLASH_FIRMWARE_DIR > default. See #27.
FIRMWARE_DIR = Path(os.environ.get(
    "PIO_FLASH_FIRMWARE_DIR",
    str(PROJECT_ROOT),
))

# ESP32 OTA partition offsets. Universal across this repo's ESP32 partition
# tables (default*.csv / min_spiffs.csv / max_app_*.csv place the low region
# identically; only the high-region app/spiffs sizes vary). Verified against
# the partition table embedded in CI -merged.bin images (#29):
# nvs@0x9000, otadata@0xe000, app0(ota_0)@0x10000.
ESP32_APP0_OFFSET = 0x10000      # ota_0 (app) partition start
ESP32_OTADATA_OFFSET = 0xe000    # boot selector
ESP32_OTADATA_SIZE = 0x2000

# Bootloader-discovery (#34): native-USB boards change USB
# identity + COM number entering bootloader (ESP32-S3 303A:0002->303A:1001;
# nRF52/Adafruit 239A:8029->239A:00xx). After triggering bootloader entry on the
# verified running port, the wrapper re-enumerates and discovers the new port by
# vendor VID + a changed PID.
ESP32S3_VENDOR = "303A"
NRF52_VENDOR = "239A"
BOOTLOADER_DISCOVER_TIMEOUT = 20   # seconds to wait for re-enumeration

# USB-UART bridge chips (#273/#468): the USB identity belongs to the BRIDGE,
# not the SoC behind it. Consequences: (a) no bootloader re-enumeration -- the
# bridge keeps its COM/VID:PID while DTR/RTS reset the SoC into download mode,
# so the #34 discovery dance must be skipped; (b) passive identity is
# unavailable -- CH340 has no serial, CP2102 ships with the factory default
# below -- so board identity is the SoC MAC, verified actively at Tier-A time.
BRIDGE_VENDORS = {"10C4": "CP2102", "1A86": "CH340"}
# Factory-default serials shared by every unprogrammed chip of the family.
# NEVER identity: one recorded default would Tier-1-match any sibling chip.
DEFAULT_USB_SERIALS = {"0001"}


def bridge_chip(vid_pid: str) -> Optional[str]:
    """CP2102/CH340 name when vid_pid belongs to a USB-UART bridge, else None."""
    return BRIDGE_VENDORS.get((vid_pid or "").split(":")[0].upper())


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
    # LEGACY, PORT-PATH ONLY (#323). "8&1A77809D" identifies the USB SOCKET, not the
    # board: move a device to another port and this changes; plug another device into
    # that port and it inherits this value. Retained only as a weak fallback for
    # registry entries that predate usb_serial. NEVER treat it as identity.
    $inst = ''
    if ($did -match '\\([0-9A-Fa-f]+&[0-9A-Fa-f]+)(?:&[0-9A-Fa-f]+)*$') {
        $inst = $matches[1]
    }
    # IDENTITY (#323): the device-unique USB serial. A COM port is an interface
    # (…&MI_00\<port-path>); its PARENT is the USB device, whose instance id ends in
    # the serial: USB\VID_303A&PID_0002\441BF662448C. Port paths always contain '&',
    # serials do not -- that is how we tell them apart when a device is not composite
    # and the parent lookup returns another port-path.
    $serial = ''
    try {
        $parent = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Parent' -ErrorAction Stop).Data
        if ($parent -is [array]) { $parent = $parent[0] }
        if ("$parent" -match '\\([^\\]+)$') {
            $tail = $matches[1]
            if ($tail -notmatch '&') { $serial = $tail.ToUpper() }
        }
    } catch { }
    # Non-composite devices (CP2102 and reprogrammed bridges) carry the serial in
    # their OWN InstanceId tail; the parent is just the hub (#468). Same rule:
    # serial-form tails contain no '&'.
    if (-not $serial) {
        if ($did -match '\\([^\\]+)$') {
            $tail = $matches[1]
            if ($tail -notmatch '&') { $serial = $tail.ToUpper() }
        }
    }
    # Emit one JSON line per port. ConvertTo-Json with -Compress is single-line.
    @{
        com = $com
        vid_pid = ($vid + ':' + $prodid)
        deviceid_full = $did
        instance_hash = $inst
        usb_serial = $serial
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
        # #468: neutralize factory-default serials (e.g. CP2102 '0001') -- shared
        # by every unprogrammed chip of the family, so they are class markers,
        # not identity. Keep the raw value for display/transparency only.
        raw = norm_serial(d.get("usb_serial"))
        if raw in DEFAULT_USB_SERIALS:
            d["usb_serial_default"] = d.get("usb_serial")
            d["usb_serial"] = ""
        ports.append(d)
    return ports


# ---------------------------------------------------------------------------
# Registry lookup - given a port's VID:PID + instance hash, find the matching
# registered device (or foreign device). Returns (kind, name, entry) where
# kind is "device" / "foreign" / None.
# ---------------------------------------------------------------------------
def norm_serial(value: Any) -> str:
    """Normalise a USB serial for comparison (#323).

    The SAME board reports its serial differently depending on which USB endpoint
    it enumerates on -- observed on one ESP32-S3: 'E8:F6:0A:CA:4E:54' via one
    interface and 'E8F60ACA4E54' via the other. Comparing raw strings would fail
    precisely when the device changes mode (runtime vs bootloader), which is the
    moment identification matters most. Compare on alphanumerics only, uppercased.
    """
    return re.sub(r"[^0-9A-Za-z]", "", str(value or "")).upper()


def entry_usb_serials(entry: dict) -> list[str]:
    """Device-unique USB serial(s) recorded for a registry entry (#323).

    Accepted at either `usb_serial:` (top level) or
    `discriminators.windows.usb_serial`; a single value or a list. Returned
    normalised via norm_serial().

    NOTE (#468): factory-default serials (DEFAULT_USB_SERIALS, e.g. CP2102
    '0001') are FILTERED OUT here -- an entry recording one behaves as
    serial-less everywhere, including the bridge rival-entry check in
    resolve_device. If a YAML entry visibly carries `usb_serial: "0001"` yet
    acts unmatched/rival, this filter is why.
    """
    d = (entry.get("discriminators") or {}).get("windows") or {}
    vals = [entry.get("usb_serial"), d.get("usb_serial")]
    out = []
    for v in vals:
        if not v:
            continue
        if isinstance(v, (list, tuple)):
            out.extend(norm_serial(x) for x in v if x)
        else:
            out.append(norm_serial(v))
    # #468: a recorded factory-default serial must never act as identity --
    # it would Tier-1-match every unprogrammed chip of that family.
    return [s for s in out if s and s not in DEFAULT_USB_SERIALS]


def find_in_registry(
    registry: dict, vid_pid: str, instance_hash: str, usb_serial: str = ""
) -> tuple[Optional[str], Optional[str], Optional[dict]]:
    # Two-tier match: an exact DeviceID-instance match ALWAYS wins over a weaker
    # VID:PID-class-only match. A class-only candidate (no instance hash recorded,
    # e.g. discriminators.windows == null) is settled for ONLY when it is the
    # *sole* device claiming this VID:PID class. When several registered devices
    # share the VID:PID (e.g. multiple ESP32-S3 303A:0002 boards) and none match
    # by instance hash, a class-only guess would mislabel the port -- this is the
    # recurring "COM<n> shows as LIBT" bug, where LIBT carried a null Windows
    # discriminator and wildcard-grabbed any unmatched 303A:0002 port (the
    # companion sitting on a different bench port than its recorded hash). In that
    # ambiguous case we return UNMATCHED so the caller surfaces an honest
    # "unidentified" port instead of a confidently-wrong device name.
    serial_up = norm_serial(usb_serial)
    legacy_candidates: list[tuple[Optional[str], Optional[str], Optional[dict]]] = []

    for kind_key, kind_label in [("devices", "device"), ("foreign_devices", "foreign")]:
        for name, entry in (registry.get(kind_key) or {}).items():
            entry_vid_pids = entry.get("vid_pid") or []
            if vid_pid not in entry_vid_pids:
                continue

            # TIER 1 -- USB serial. Device-unique and port-independent: this IS identity.
            serials = entry_usb_serials(entry)
            if serials:
                if serial_up and serial_up in serials:
                    return (kind_label, name, entry)
                # This entry has a serial and it is NOT this port's device. Excluding
                # it here is what stops a registered board from impersonating another
                # board that happens to sit in a port it once used.
                continue

            # TIER 2 -- legacy port-path hash, for entries predating usb_serial.
            d = (entry.get("discriminators") or {}).get("windows") or {}
            known_hashes = [
                d.get("runtime_deviceid_instance"),
                d.get("bootloader_deviceid_instance"),
            ]
            known_hashes = [h for h in known_hashes if h]
            if known_hashes and instance_hash in known_hashes:
                legacy_candidates.append((kind_label, name, entry))

            # TIER 3 -- neither serial nor hash (e.g. discriminators.windows == null).
            # DELIBERATELY NOT A CANDIDATE (#323). Such an entry matches its whole
            # VID:PID class, so it silently swallows any unrecognised board of that
            # chip family. That is how a node physically mounted in the garage ceiling
            # was reported as sitting on the bench. Unidentified is the honest answer;
            # a confidently-wrong device name gets firmware written to the wrong board.

    # A legacy hash match is accepted only when it is unambiguous. Note this is
    # weaker than a serial match and follows the socket, not the board -- callers
    # warn the operator to record usb_serial.
    if len(legacy_candidates) == 1:
        return legacy_candidates[0]
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

    # #323: identify by USB serial (device-unique, port-independent) when the entry
    # records one; fall back to the legacy port-path hash only for entries that
    # predate it; refuse outright when neither exists.
    serials = entry_usb_serials(entry)
    matches = []

    if serials:
        # Deliberately NOT filtered by vid_pid: the serial already identifies the
        # board, and a device in bootloader mode legitimately presents a different
        # PID than in runtime. Filtering on VID:PID here is what made a board
        # "disappear" when it enumerated on its other USB endpoint.
        matches = [p for p in ports if norm_serial(p.get("usb_serial")) in serials]
        if not matches:
            present_summary = ", ".join(
                f"{p['com']}={p['vid_pid']} serial={p.get('usb_serial') or '(none)'}"
                for p in ports
            ) or "(no ports)"
            refuse(
                f"no present port carries device '{name}' USB serial "
                f"{sorted(serials)} -- it is not attached to this host. "
                f"Present: {present_summary}"
            )
    else:
        d = (entry.get("discriminators") or {}).get("windows") or {}
        known_hashes = [
            d.get("runtime_deviceid_instance"),
            d.get("bootloader_deviceid_instance"),
        ]
        known_hashes = [h for h in known_hashes if h]
        if not known_hashes:
            # #273/#468: bridge-class entries (CP2102/CH340) can NEVER record a
            # usable passive discriminator -- the bridge hides the SoC and its
            # own serial is absent (CH340) or a shared factory default (CP2102).
            # For these, and ONLY these, allow a PROVISIONAL class match under
            # strict uniqueness: exactly one present port of the class, and this
            # entry is the only registry entry claiming it. The match is marked
            # provisional; Tier-A commands MUST then verify the SoC MAC before
            # touching flash (_verify_bridge_mac). This is not the #323 wildcard:
            # ambiguity in either direction still refuses.
            bridge = all(bridge_chip(vp) for vp in entry_vid_pids)
            if bridge and norm_serial(entry.get("mac")):
                cands = [p for p in ports if p["vid_pid"] in entry_vid_pids]
                rivals = [
                    n for n, e in (registry.get("devices") or {}).items()
                    if n != name
                    and set(e.get("vid_pid") or []) & entry_vid_pids
                    and not entry_usb_serials(e)
                ]
                if len(cands) == 1 and not rivals:
                    port = dict(cands[0])
                    port["bridge_provisional"] = True
                    err(
                        f"NOTE: '{name}' matched PROVISIONALLY as the sole present "
                        f"{bridge_chip(port['vid_pid'])}-bridged candidate. Bridge "
                        "chips expose no board identity; Tier-A operations will "
                        "verify the SoC MAC before touching flash (#468)."
                    )
                    return (port, entry)
                if len(cands) > 1:
                    refuse(
                        f"device '{name}' is bridge-class and {len(cands)} ports of "
                        f"that class are present ({', '.join(p['com'] for p in cands)}). "
                        "Bridges cannot be told apart passively -- disconnect the "
                        "others and retry (#468)."
                    )
                if rivals:
                    refuse(
                        f"device '{name}' is bridge-class but other registry entries "
                        f"({', '.join(rivals)}) claim the same VID:PID class without a "
                        "serial. A provisional match would be a guess. Resolve the "
                        "registry overlap first (#468)."
                    )
                refuse(
                    f"no port of device '{name}' bridge class "
                    f"{sorted(entry_vid_pids)} is present -- it is not attached."
                )
            # Previously this accepted ANY port of the right VID:PID class -- the
            # defect behind #323. A chip-family match is not an identity.
            refuse(
                f"device '{name}' records neither usb_serial nor a DeviceID hash, so it "
                "cannot be identified -- only its VID:PID class, which every board of "
                "that chip family shares. Refusing rather than guessing (#323). "
                "Run 'pio-flash list' to read this board's usb_serial, then record it "
                "on the entry as 'usb_serial: <VALUE>'."
            )
        matches = [
            p for p in ports
            if p["vid_pid"] in entry_vid_pids and p["instance_hash"] in known_hashes
        ]
        if not matches:
            present_summary = ", ".join(
                f"{p['com']}={p['vid_pid']} serial={p.get('usb_serial') or '(none)'}"
                for p in ports
            ) or "(no ports)"
            refuse(
                f"no present port matches device '{name}' by legacy DeviceID port-path "
                f"hash {sorted(known_hashes)}. NOTE: that hash identifies the USB SOCKET, "
                "not the board, so it stops matching whenever the device is moved to a "
                "different port. Record 'usb_serial:' on this entry to make it "
                f"port-independent (#323). Present: {present_summary}"
            )
        err(
            f"WARNING: '{name}' was matched by legacy DeviceID port-path hash, which "
            "identifies the USB socket rather than the board. Record "
            f"'usb_serial: {matches[0].get('usb_serial') or '<unavailable>'}' on this "
            "entry so identification survives a port change (#323)."
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
        kind, _, _ = find_in_registry(
            registry, p["vid_pid"], p["instance_hash"], p.get("usb_serial", "")
        )
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
    # #500: no age check. The owner's approval does not expire — the token is
    # consumed by exactly one confirm, and cmd_confirm re-verifies port,
    # DeviceID, and artifact sha against the previewed state before flashing.
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
    # usb_serial is the identity column (#323); instance is the legacy port-path and
    # is shown only so an operator can see what the old matcher was keying on.
    out(f"{'COM':<8} {'VID:PID':<12} {'usb_serial':<16} {'port-path':<12} {'match':<22} description")
    out("-" * 110)
    for p in ports:
        kind, name, _ = find_in_registry(
            registry, p["vid_pid"], p["instance_hash"], p.get("usb_serial", "")
        )
        if kind == "device":
            tag = f"device:{name}"
        elif kind == "foreign":
            tag = f"FOREIGN:{name}"
        else:
            tag = "unregistered"
            # #468: an unmatched bridge-class port with exactly one bridge-class
            # registry candidate is shown as that candidate -- explicitly marked
            # as unverifiable-until-flash-time, never presented as a match.
            if bridge_chip(p["vid_pid"]):
                cands = [
                    n for n, e in (registry.get("devices") or {}).items()
                    if p["vid_pid"] in (e.get("vid_pid") or [])
                    and not entry_usb_serials(e)
                    and norm_serial(e.get("mac"))
                ]
                if len(cands) == 1:
                    tag = f"bridge-cand:{cands[0]}"
        ser = p.get("usb_serial") or (
            f"{p['usb_serial_default']}(default)" if p.get("usb_serial_default")
            else "(none)"
        )
        out(
            f"{p['com']:<8} {p['vid_pid']:<12} {ser:<16} {p['instance_hash']:<12} "
            f"{tag:<22} {p['description']}"
        )
    if not ports:
        out("(no present serial ports)")
    return 0


# ---------------------------------------------------------------------------
# Artifact-flash (#29): flash a downloaded CI release
# artifact through the SAME resolve_device identity gate as an env flash.
# ESP32 default = NVS-preserving app-slot update (write app0 + reset otadata);
# --erase = full factory write of the -merged.bin at 0x0. nRF52 = serial DFU
# of the .zip (preserves the config filesystem).
# ---------------------------------------------------------------------------
ARTIFACT_RE = re.compile(
    r"-(v\d+\.\d+\.\d+(?:-rc\d+)?)-([0-9a-fA-F]{7,40})(?:-merged)?\.(?:bin|zip|uf2)$"
)


def _artifact_identity(artifact: Path) -> dict:
    """Identity from the CI artifact filename (<env>-<version>-<gitsha>[-merged].ext).

    Deliberately does NOT fall back to the current repo's git state - an
    artifact's identity is whatever the CI stamped into its name, never the
    checkout the wrapper happens to run from.
    """
    m = ARTIFACT_RE.search(artifact.name)
    if m:
        return {
            "offband_version": m.group(1),
            "offband_git_sha": m.group(2),
            "offband_branch": "unknown",
            "offband_build_date": "unknown",
            "firmware_identity_source": "ci-artifact-filename",
        }
    return {
        "offband_version": "unknown",
        "offband_git_sha": "unknown",
        "offband_branch": "unknown",
        "offband_build_date": "unknown",
        "firmware_identity_source": "artifact-filename-unparsed",
    }


def _classify_artifact(path: Path, erase: bool) -> tuple[str, str]:
    """Map an artifact path + --erase to (platform, flash_method). Refuses on
    unsupported combinations rather than guessing."""
    name = path.name.lower()
    suffix = path.suffix.lower()
    if suffix == ".zip":
        if erase:
            refuse(
                "nRF52 --erase is not supported: serial DFU cannot wipe the "
                "config filesystem. Re-run without --erase (DFU preserves it)."
            )
        return ("nrf52", "nrfutil_dfu")
    if suffix == ".uf2":
        refuse(
            "nRF52 .uf2 drive-copy is not supported through the wrapper (it "
            "bypasses the identity gate). Use the .zip DFU package instead."
        )
    if suffix == ".bin":
        is_merged = "merged" in name
        if erase:
            if not is_merged:
                refuse(
                    f"--erase requires the full -merged.bin, but '{path.name}' "
                    "looks like an app-only bin. Pass the *-merged.bin, or drop "
                    "--erase for an NVS-preserving app-slot update."
                )
            return ("esp32", "esptool_merged_full")
        if is_merged:
            refuse(
                f"default (NVS-preserving) mode expects the app-only .bin, not "
                f"'{path.name}'. Pass the non-merged *.bin, or add --erase to "
                "factory-flash the -merged.bin (this WIPES NVS)."
            )
        return ("esp32", "esptool_app_slot")
    refuse(
        f"unrecognized artifact type '{suffix}'. Expected .bin (ESP32) or "
        ".zip (nRF52 DFU package)."
    )


def _preview_artifact(args, port: dict, entry: dict) -> int:
    """Tier A stage 1 for an artifact flash: validate + write token, exit 2.
    resolve_device (the identity gate) has ALREADY run before this is called."""
    artifact = Path(args.artifact).resolve()
    if not artifact.exists():
        refuse(f"artifact {artifact} not found")
    platform, method = _classify_artifact(artifact, args.erase)
    sha = sha256_of(artifact)
    size = artifact.stat().st_size
    ident = _artifact_identity(artifact)

    if method == "esptool_app_slot":
        mode_desc = (
            f"app-slot update: write app0 @ 0x{ESP32_APP0_OFFSET:x}, "
            f"erase otadata @ 0x{ESP32_OTADATA_OFFSET:x} -> PRESERVES NVS"
        )
    elif method == "esptool_merged_full":
        mode_desc = "FULL FACTORY: merged @ 0x0 -> ERASES NVS and all data"
    else:
        mode_desc = "nRF52 serial DFU (.zip) -> PRESERVES config filesystem"

    out("============================================================")
    out("PREVIEW (Tier A artifact-flash) - no device touch yet")
    out("============================================================")
    out(f"Target device : {args.device}")
    out(f"Registered MAC: {entry.get('mac')}")
    out(f"Resolved port : {port['com']}")
    out(f"Port VID:PID  : {port['vid_pid']}")
    out(f"Port DeviceID : {port['deviceid_full']}")
    out(f"Hash match    : {port['instance_hash']}")
    out(f"Artifact      : {artifact}")
    out(f"Artifact sha256: {sha}")
    out(f"Artifact size : {size} bytes")
    out(f"Platform      : {platform}")
    out(f"Flash method  : {method}")
    out(f"Mode          : {mode_desc}")
    out(f"Offband ver : {ident['offband_version']}")
    out(f"Offband SHA : {ident['offband_git_sha']}")
    out(f"Identity src  : {ident['firmware_identity_source']}")
    out("------------------------------------------------------------")
    out("To proceed, get explicit user GO in chat naming the device, then run:")
    out(f"  scripts/pio-flash confirm {args.device} --token {token_path(args.device)}")
    out("Token: single-use, NO expiry (one approval = one flash, #500).")
    out("Invalidates only if port/DeviceID/artifact sha changes before confirm.")
    out("============================================================")

    payload = {
        "device": args.device,
        "mode": "artifact",
        "platform": platform,
        "flash_method": method,
        "erase": bool(args.erase),
        "port": port["com"],
        "deviceid_full": port["deviceid_full"],
        "vid_pid": port["vid_pid"],
        "instance_hash": port["instance_hash"],
        "artifact_path": str(artifact),
        "firmware_sha256": sha,
        "firmware_size": size,
        "offband_version": ident["offband_version"],
        "offband_git_sha": ident["offband_git_sha"],
        "offband_branch": ident["offband_branch"],
        "offband_build_date": ident["offband_build_date"],
        "firmware_identity_source": ident["firmware_identity_source"],
    }
    p = write_token(args.device, payload)
    out(f"Token written: {p}")
    return 2


def env_with_auth() -> dict:
    """os.environ copy with the wrapper-authorization marker the future
    pass-through hook keys on."""
    e = os.environ.copy()
    e["PIO_FLASH_AUTHORIZED"] = "1"
    return e


# esptool / adafruit-nrfutil output markers for output-parsed verification
# (#34). Markers are matched case-insensitively.
_ESPTOOL_WRITE_OK = ["hash of data verified"]
_ESPTOOL_ERASE_OK = ["erased successfully", "erased in"]
_ESPTOOL_FAIL = ["a fatal error", "serial exception", "failed", "traceback (most recent call"]
_NRFUTIL_OK = ["device programmed"]
_NRFUTIL_FAIL = ["failed to upgrade", "traceback (most recent call", "could not open port", "exception"]


def _run_flasher(cmd: list, env_d: dict, success_markers: list,
                 failure_markers: list) -> tuple[int, bool]:
    """Run a flasher subprocess, capture + print its output, and decide success
    by PARSING that output - never by exit code alone.

    Rationale (#34): adafruit-nrfutil exits 0 even on a fatal
    "Failed to upgrade target", which reported false success (the OTA-incident
    class of bug). A flash is "ok" only when exit code 0 AND a positive success
    marker is present AND no failure marker is present. Conservative by design:
    anything ambiguous reads as FAILURE."""
    proc = subprocess.run(cmd, env=env_d, capture_output=True, text=True)
    output = (proc.stdout or "") + (proc.stderr or "")
    if output.strip():
        print(output)
    low = output.lower()
    failed = any(m in low for m in failure_markers)
    succeeded = any(m in low for m in success_markers)
    ok = (proc.returncode == 0) and succeeded and not failed
    if not ok:
        err(f"flasher result NOT verified (rc={proc.returncode}, "
            f"success_marker={'yes' if succeeded else 'NO'}, "
            f"failure_marker={'YES' if failed else 'no'})")
    return proc.returncode, ok


def _touch_1200(com: str) -> None:
    """Pulse a serial port at 1200 baud to trigger reset-into-bootloader (the
    Arduino / Adafruit auto-reset convention). The device re-enumerates as its
    bootloader identity shortly after; the port dropping mid-touch is normal."""
    try:
        import serial
    except ImportError:
        refuse("pyserial not available; pip install pyserial")
    try:
        s = serial.Serial(com, baudrate=1200)
        try:
            s.dtr = False
            time.sleep(0.15)
        finally:
            s.close()
    except Exception as e:
        out(f"  (1200-baud touch on {com} raised, normal on reset: {e})")


def _read_mac_on_port(com: str) -> str:
    """Run esptool read_mac on a port and return the normalised MAC (#468).
    Tier-A side effect: resets the chip into ROM bootloader and back."""
    try:
        result = subprocess.run(
            ["python", "-m", "esptool", "--port", com, "read_mac"],
            env=env_with_auth(), capture_output=True, text=True, timeout=60,
        )
    except subprocess.TimeoutExpired:
        refuse(f"esptool read_mac on {com} timed out (60s)")
    # Gemini review 2026-07-30: reuse parse_base_mac (#290) rather than a local
    # regex -- C6/H2 chips print an 8-byte EUI-64 first, which a naive "MAC:"
    # match would capture, making the identity gate refuse the CORRECT board.
    mac = parse_base_mac(result.stdout or "")
    if not mac:
        refuse(
            f"could not read MAC on {com} (esptool rc={result.returncode}). "
            "The board may not be an ESP32 or did not enter download mode. "
            f"Tail: {(result.stdout or result.stderr or '')[-300:]!r}"
        )
    return norm_serial(mac)


def _verify_bridge_mac(port: dict, entry: dict, device_name: str) -> None:
    """Tier-A identity gate for provisionally-matched bridge boards (#468).
    Reads the SoC MAC through the bridge and hard-refuses on mismatch with the
    entry's recorded mac:. No-op for ports that resolved by real identity.
    The chip resets -- acceptable only where a reset was imminent anyway."""
    if not port.get("bridge_provisional"):
        return
    recorded = norm_serial(entry.get("mac"))
    if not recorded:
        refuse(
            f"'{device_name}' is bridge-class but records no mac: -- cannot "
            "verify identity. Run 'pio-flash bootstrap' guidance in #468."
        )
    out(f"Bridge identity check: reading SoC MAC on {port['com']} "
        "(resets the chip)...")
    live = _read_mac_on_port(port["com"])
    if live != recorded:
        refuse(
            f"MAC MISMATCH on {port['com']}: live {live} != recorded {recorded} "
            f"for '{device_name}'. This is NOT the registered board. Refusing "
            "(#468)."
        )
    out(f"Bridge identity VERIFIED: MAC {entry.get('mac')} matches '{device_name}'.")


def _esp32_trigger_download(com: str) -> None:
    """Reset an ESP32-S3 into ROM download mode via esptool's default reset.
    The connect then fails because the running USB-CDC port vanishes on reset -
    that is expected; the reset is the point. The download port is discovered by
    re-enumeration afterward."""
    out(f"  (esptool default-reset {com} into download; connect failure is expected)")
    try:
        subprocess.run(
            ["python", "-m", "esptool", "--port", com,
             "--before", "default-reset", "--after", "no-reset",
             "--connect-attempts", "1", "chip-id"],
            env=env_with_auth(), capture_output=True, text=True, timeout=30,
        )
    except Exception:
        pass


def _discover_bootloader_port(before: list, vendor: str, running_pid: str,
                              timeout: int) -> dict:
    """After a bootloader-entry trigger, poll enumeration until exactly one NEW
    port appears with the device's vendor VID and a PID different from the
    running PID (i.e. it transitioned to a bootloader identity). Returns that
    port. Refuses on zero (timeout) or more than one (ambiguous) - this is what
    keeps the discovery from ever flashing the wrong device."""
    before_coms = {p["com"] for p in before}
    vendor = vendor.upper()
    running_pid = running_pid.upper()
    deadline = time.time() + timeout
    last_seen: list = []
    while time.time() < deadline:
        now = enumerate_ports()
        new = [p for p in now if p["com"] not in before_coms]
        cands = [
            p for p in new
            if p["vid_pid"].split(":")[0].upper() == vendor
            and p["vid_pid"].split(":")[1].upper() != running_pid
        ]
        last_seen = cands
        if len(cands) == 1:
            return cands[0]
        if len(cands) > 1:
            coms = ", ".join(f"{p['com']}({p['vid_pid']})" for p in cands)
            refuse(
                f"multiple new {vendor} bootloader ports appeared ({coms}); "
                "refusing rather than guessing. Disconnect other devices and retry."
            )
        time.sleep(0.5)
    refuse(
        f"no bootloader port appeared within {timeout}s after the trigger "
        f"(expected a new {vendor} port with PID != {running_pid}). The device "
        "may not have entered bootloader mode. Last candidates: "
        + (", ".join(p["com"] for p in last_seen) or "none")
    )


def _enter_bootloader_and_discover(running_port: dict, platform: str) -> dict:
    """Trigger bootloader entry on the verified running port, then discover the
    device on its new (bootloader) COM. Unified for both chip families - they
    all change identity + COM entering bootloader (#34)."""
    vendor = NRF52_VENDOR if platform == "nrf52" else ESP32S3_VENDOR
    running_pid = running_port["vid_pid"].split(":")[1]
    out(f"Triggering bootloader entry on {running_port['com']} "
        f"({running_port['vid_pid']}, {platform})...")
    before = enumerate_ports()
    if platform == "nrf52":
        _touch_1200(running_port["com"])
    else:
        _esp32_trigger_download(running_port["com"])
    bl = _discover_bootloader_port(before, vendor, running_pid,
                                   BOOTLOADER_DISCOVER_TIMEOUT)
    out(f"Discovered bootloader port: {bl['com']} "
        f"({bl['vid_pid']}, hash {bl['instance_hash']})")
    return bl


def _flash_esp32_app_slot(artifact: Path, bl_com: str, env_d: dict) -> bool:
    """NVS-preserving ESP32 update on the DISCOVERED download port: write the app
    to app0 (--after no-reset, stay in download), then erase otadata so the
    bootloader deterministically boots the written app. NVS (0x9000) untouched.
    Each step verified by output, not exit code."""
    out(f"[1/2] write-flash 0x{ESP32_APP0_OFFSET:x} {artifact.name} (app0; stay in download)")
    _, ok1 = _run_flasher(
        ["python", "-m", "esptool", "--port", bl_com, "--after", "no-reset",
         "write-flash", hex(ESP32_APP0_OFFSET), str(artifact)],
        env_d, _ESPTOOL_WRITE_OK, _ESPTOOL_FAIL,
    )
    if not ok1:
        err("app write-flash did not verify; aborting before otadata erase.")
        return False
    out(f"[2/2] erase-region 0x{ESP32_OTADATA_OFFSET:x} 0x{ESP32_OTADATA_SIZE:x} "
        "(otadata reset -> boot app0; chip resets after)")
    _, ok2 = _run_flasher(
        ["python", "-m", "esptool", "--port", bl_com,
         "erase-region", hex(ESP32_OTADATA_OFFSET), hex(ESP32_OTADATA_SIZE)],
        env_d, _ESPTOOL_ERASE_OK, _ESPTOOL_FAIL,
    )
    return ok2


def _flash_esp32_merged_full(artifact: Path, bl_com: str, env_d: dict) -> bool:
    """Factory ESP32 flash on the discovered download port: write the full merged
    image at 0x0 (spans NVS -> WIPES it; intentional, --erase). Verified by output."""
    out(f"[1/1] write-flash 0x0 {artifact.name} (FULL merged - WIPES NVS)")
    _, ok = _run_flasher(
        ["python", "-m", "esptool", "--port", bl_com,
         "write-flash", "0x0", str(artifact)],
        env_d, _ESPTOOL_WRITE_OK, _ESPTOOL_FAIL,
    )
    return ok


def _flash_nrf52_dfu(artifact: Path, bl_com: str, env_d: dict) -> bool:
    """nRF52 serial DFU of the .zip on the DISCOVERED DFU port (no --touch - the
    device is already in the bootloader). Success is decided by PARSING output:
    adafruit-nrfutil exits 0 even when it fails (#34). A normal
    app DFU preserves the internal config filesystem."""
    out(f"[1/1] adafruit-nrfutil dfu serial {artifact.name} -> {bl_com}")
    _, ok = _run_flasher(
        ["adafruit-nrfutil", "--verbose", "dfu", "serial",
         "--package", str(artifact), "-p", bl_com, "-b", "115200"],
        env_d, _NRFUTIL_OK, _NRFUTIL_FAIL,
    )
    return ok


def _confirm_artifact(args, token: dict, port: dict) -> int:
    """Tier A stage 2 for an artifact flash. The running identity has ALREADY
    been re-verified in cmd_confirm. Here: re-check sha, trigger bootloader entry
    + discover the bootloader port, flash it, verify by output, record."""
    artifact = Path(token["artifact_path"])
    if not artifact.exists():
        refuse(f"artifact {artifact} missing since preview")
    sha_now = sha256_of(artifact)
    if sha_now != token["firmware_sha256"]:
        refuse(
            f"artifact sha256 changed since preview "
            f"({token['firmware_sha256']} -> {sha_now}). Re-run preview."
        )

    method = token.get("flash_method")
    platform = token.get("platform")
    out("============================================================")
    out(f"FLASHING {args.device} (artifact: {method})")
    out(f"Verified running identity: {port['com']} ({port['vid_pid']})")
    out("============================================================")

    # Trigger bootloader entry on the verified running port, then discover the
    # device on its new bootloader COM (it changes identity + port).
    # Exception: boards whose ROM bootloader keeps the same USB identity (e.g.
    # the Heltec V4 TFT stays PID 1001 on the same port) have no transition to
    # discover. With --in-bootloader, flash directly on the already-verified
    # resolved port -- exactly as cmd_factory_reset does -- letting esptool's own
    # reset enter download mode. The port was identity-checked in cmd_confirm.
    if getattr(args, "in_bootloader", False):
        out(f"--in-bootloader: flashing on resolved port {port['com']} "
            f"({port['vid_pid']}) directly; no reset/rediscover dance.")
        bl = port
    elif bridge_chip(port["vid_pid"]):
        # #273: USB-UART bridges (Heltec V3 / CP2102) never re-enumerate -- the
        # bridge keeps its COM while DTR/RTS reset the SoC into download mode.
        # Waiting for a new 303A port here is what made confirm --artifact
        # refuse on the V3. Flash the SAME COM; esptool's default-reset does
        # the classic auto-reset entry itself.
        out(f"bridge-class ({bridge_chip(port['vid_pid'])}): flashing on the same "
            f"port {port['com']} -- bridges do not re-enumerate into a download "
            "port; esptool's DTR/RTS auto-reset enters download mode (#273).")
        bl = port
    else:
        bl = _enter_bootloader_and_discover(port, platform)

    env_d = env_with_auth()
    if method == "esptool_app_slot":
        ok = _flash_esp32_app_slot(artifact, bl["com"], env_d)
    elif method == "esptool_merged_full":
        ok = _flash_esp32_merged_full(artifact, bl["com"], env_d)
    elif method == "nrfutil_dfu":
        ok = _flash_nrf52_dfu(artifact, bl["com"], env_d)
    else:
        refuse(f"unknown flash_method in token: {method!r}")

    log_history({
        "ts_unix": int(time.time()),
        "mode": "artifact-flash",
        "flash_method": method,
        "platform": platform,
        "erase": token.get("erase", False),
        "device": args.device,
        "running_port": port["com"],
        "running_vid_pid": port["vid_pid"],
        "running_instance_hash": port["instance_hash"],
        "bootloader_port": bl["com"],
        "bootloader_vid_pid": bl["vid_pid"],
        "bootloader_instance_hash": bl["instance_hash"],
        "deviceid_full": bl["deviceid_full"],
        "artifact_path": token.get("artifact_path"),
        "firmware_sha256": token.get("firmware_sha256"),
        "firmware_size": token.get("firmware_size"),
        "offband_version": token.get("offband_version", "unknown"),
        "offband_git_sha": token.get("offband_git_sha", "unknown"),
        "firmware_identity_source": token.get("firmware_identity_source", "unknown"),
        "verified_ok": ok,
        "exit_code": 0 if ok else 1,
        "user_confirmation": args.device,
    })

    try:
        Path(args.token).unlink()
    except OSError:
        pass

    if ok:
        out("artifact-flash VERIFIED OK (output-parsed). Device should be "
            "booting the new firmware.")
        return 0
    err("artifact-flash FAILED or NOT VERIFIED. See output above. The device "
        "may still be in bootloader mode; reset it to recover.")
    return 1


def cmd_preview(args, registry):
    """Tier A stage 1: resolve target, validate firmware, write token, exit 2."""
    port, entry = resolve_device(args.device, registry)
    if getattr(args, "artifact", None):
        return _preview_artifact(args, port, entry)
    if getattr(args, "erase", False):
        refuse("--erase only applies to --artifact flashes (the --env path "
               "uses pio upload, which does not erase).")
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
    out("Token: single-use, NO expiry (one approval = one flash, #500).")
    out("Invalidates only if port/DeviceID/firmware sha changes before confirm.")
    out("============================================================")

    # #200 (LoRa-wek): identity is read from firmware_bin's embedded XWIRE
    # marker blob, not re-derived from git. Embedded in the token so
    # cmd_confirm logs the same identity it previewed - no second source of
    # truth between preview and confirm.
    fw_identity = get_firmware_identity(firmware_bin, FIRMWARE_DIR)
    out(f"Offband version : {fw_identity['offband_version']}")
    out(f"Offband SHA     : {fw_identity['offband_git_sha']}")
    out(f"Offband branch  : {fw_identity['offband_branch']}")
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
        "offband_version": fw_identity["offband_version"],
        "offband_git_sha": fw_identity["offband_git_sha"],
        "offband_branch": fw_identity["offband_branch"],
        "offband_build_date": fw_identity["offband_build_date"],
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

    # #468: provisional bridge match -> verify the SoC MAC before ANY flash
    # path runs. The chip was about to be reset by the flash anyway.
    _verify_bridge_mac(port, entry, args.device)

    if token.get("mode") == "artifact":
        return _confirm_artifact(args, token, port)

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
        "offband_version": token.get("offband_version", "unknown"),
        "offband_git_sha": token.get("offband_git_sha", "unknown"),
        "offband_branch": token.get("offband_branch", "unknown"),
        "offband_build_date": token.get("offband_build_date", "unknown"),
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
    if port.get("bridge_provisional"):
        # #468: send WRITES config-mutating CLI commands. A provisional bridge
        # match is a class guess, not identity, and send cannot MAC-verify
        # without resetting the chip (which would also kill the CLI session).
        refuse(
            f"'{args.device}' resolved only provisionally (bridge-class; identity "
            "unverifiable without a chip reset). Refusing to SEND commands to a "
            "possibly-wrong board. Use a flash-time-verified operation, or ensure "
            "this is the only bridge device attached and verify with "
            f"'pio-flash read-mac {args.device}' first (#468)."
        )
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
    if port.get("bridge_provisional"):
        # #468: read-only attach; worst case is watching the wrong console.
        # Loud banner instead of refusal -- monitor must not reset the chip,
        # so MAC verification is impossible here by design.
        err(
            f"UNVERIFIED: '{args.device}' matched provisionally (bridge-class -- "
            "identity not MAC-verified; monitor cannot verify without resetting "
            "the chip). If another bridge board could be attached, treat this "
            "console output with suspicion (#468)."
        )
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
    if port.get("bridge_provisional"):
        err(
            f"UNVERIFIED: '{args.device}' matched provisionally (bridge-class, "
            "not MAC-verified). Read-only query; verify output plausibility (#468)."
        )
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
    if port.get("bridge_provisional"):
        # #468: for a bridge board the read IS the verification -- one reset,
        # compare against the recorded mac:, report, done.
        live = _read_mac_on_port(port["com"])
        recorded = norm_serial(entry.get("mac"))
        verdict = "MATCHES registry" if live == recorded else "MISMATCH vs registry"
        out(f"MAC read from device: {live} ({verdict}: {entry.get('mac')})")
        log_history({
            "ts_unix": int(time.time()),
            "mode": "read-mac",
            "device": args.device,
            "port": port["com"],
            "bridge_provisional": True,
            "mac_live": live,
            "mac_recorded": entry.get("mac"),
            "exit_code": 0 if live == recorded else 1,
        })
        return 0 if live == recorded else 1
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
    _verify_bridge_mac(port, entry, args.device)  # #468: chip resets anyway

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
    ]
    if getattr(args, "no_stub", False):
        # ROM loader reads block-by-block (request->receive->request) instead of
        # streaming continuously, so it can't overflow the ESP32-S3 USB-Serial/JTAG
        # FIFO -- robust against 'serial stream stopped' on long reads. Slower.
        cmd.append("--no-stub")
    cmd += ["read_flash", str(args.offset), str(flash_size), str(output)]
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
    _verify_bridge_mac(port, entry, args.device)  # #468: chip resets anyway

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


# --- MAC parsing (#290) ------------------------------------------------------
# On IEEE 802.15.4 chips (ESP32-C6 / ESP32-H2) `esptool read_mac` prints an
# 8-byte EUI-64 as the FIRST "MAC:" line (e.g. 02:71:bc:ff:fe:12:34:56); its
# first 6 bytes carry the ff:fe EUI-64 fill and are NOT the device base MAC.
# The real 6-byte address is on the "BASE MAC:" line. S3/C3 print only a plain
# 6-byte "MAC:" line (some builds also add "BASE MAC:"). Prefer BASE MAC; fall
# back to a strict 6-byte MAC whose negative lookahead refuses to capture the
# head of an 8-byte EUI-64.
_MAC6 = r"([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})"
# Anchored to line start (re.MULTILINE) so noisy log text like
# "Note: check BASE MAC: .. on sticker" can never be mistaken for the field.
_BASE_MAC_RE = re.compile(r"^BASE\s+MAC:\s*" + _MAC6, re.IGNORECASE | re.MULTILINE)
_PLAIN_MAC_RE = re.compile(
    r"^MAC:\s*" + _MAC6 + r"(?![0-9a-fA-F:])", re.IGNORECASE | re.MULTILINE
)


def parse_base_mac(stdout):
    """Extract the device base MAC (lowercased) from `esptool read_mac` output.

    Returns the 6-byte MAC as "aa:bb:cc:dd:ee:ff", or None if none is found.
    See #290 for the C6/H2 EUI-64 mis-parse this replaces.
    """
    m = _BASE_MAC_RE.search(stdout) or _PLAIN_MAC_RE.search(stdout)
    return m.group(1).lower() if m else None


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
    out(f"usb_serial    : {target.get('usb_serial') or '(UNAVAILABLE)'}   <- identity (#354)")
    out(f"Hash          : {target['instance_hash']}   (port-path, legacy)")
    out(f"Description   : {target['description']}")
    out("")
    # #501: platform-aware identity capture. esptool only speaks to Espressif
    # silicon — running it against an nRF52 (239A Adafruit, 2886 Seeed) can
    # never succeed, which made those boards unregistrable. For non-Espressif
    # ports the usb_serial (chip DEVICEID) already in hand IS the identity the
    # #323 matcher prefers; MAC stays null per the long-standing HARDWARE.md
    # convention (capture from the boot log if ever needed). Espressif-native
    # (303A) and bridge-class ports (10C4/1A86 — the MAC is the identity, #468)
    # keep the esptool read.
    vendor = target["vid_pid"].split(":")[0].upper()
    espressif_read = vendor == ESP32S3_VENDOR or bridge_chip(target["vid_pid"]) is not None
    mac = None
    if espressif_read:
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

        # Parse MAC from esptool output. Prefer the BASE MAC line so 802.15.4 chips
        # (C6/H2) don't record the EUI-64 head instead of the base MAC (#290).
        mac = parse_base_mac(result.stdout)
        if mac is None:
            out(result.stdout)
            refuse("could not parse MAC from esptool output (see stdout above)")
        out(f"MAC read from device: {mac}")
    else:
        if not (target.get("usb_serial") or "").strip():
            refuse(
                f"non-Espressif device ({target['vid_pid']}) exposes no usb_serial — "
                "no identity available to register. Refusing rather than minting a "
                "port-path-only entry (#323/#501)."
            )
        out(f"Non-Espressif device ({target['vid_pid']}): skipping esptool MAC read "
            "(#501). usb_serial is the identity; no device touch performed.")

    # Cross-check: does this identity already belong to a different registered
    # name? MAC for Espressif/bridge paths; usb_serial for the #501 nRF52 path.
    serial_norm = norm_serial(target.get("usb_serial"))
    for kind_key in ("devices", "foreign_devices"):
        for existing_name, existing_entry in (registry.get(kind_key) or {}).items():
            if mac and (existing_entry.get("mac") or "").lower() == mac:
                refuse(
                    f"MAC {mac} is already registered as '{existing_name}' "
                    f"under {kind_key}:. Refusing to register the same MAC under "
                    f"a second name '{name}'. If this is a re-bootstrap, edit "
                    f"the YAML by hand or use a different name."
                )
            if serial_norm and serial_norm in entry_usb_serials(existing_entry):
                refuse(
                    f"usb_serial {target.get('usb_serial')} is already registered "
                    f"as '{existing_name}' under {kind_key}:. Refusing to register "
                    f"the same board under a second name '{name}' (#501)."
                )

    # Build new entry. v1: assumes ESP32-S3 dual-mode; user can edit later.
    #
    # #354: record usb_serial as the PRIMARY identity. It is device-unique and
    # port-independent, so the entry survives being moved between USB ports/hubs.
    # Writing only the port-path hash (as bootstrap did before #354) minted a
    # Tier 2 "legacy" entry on every registration -- exactly the class #323 set
    # out to eliminate -- so a freshly bootstrapped board still followed the
    # socket rather than the board. The value is already in hand from
    # enumerate_ports(); there is no reason to make the operator retype it.
    usb_serial = (target.get("usb_serial") or "").strip()
    new_entry = {
        "mac": mac,
        "role": f"new device registered via bootstrap on {time.strftime('%Y-%m-%d')}",
        "vid_pid": [target["vid_pid"]],
        # Legacy port-path hash retained as a secondary discriminator only. It is
        # harmless once usb_serial is present (find_in_registry prefers Tier 1),
        # and still gives a match for hosts/boards that expose no serial.
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

    # #354: usb_serial is the identity column -- write it when the device exposes
    # one. Placed top-level; entry_usb_serials() accepts it there or under
    # discriminators.windows.
    if usb_serial:
        new_entry["usb_serial"] = usb_serial
        out(f"Recorded usb_serial: {usb_serial} (port-independent identity)")
    elif bridge_chip(target["vid_pid"]):
        # #468: bridge boards can NEVER expose a usable serial (CH340 has none;
        # CP2102 ships the shared factory default). Telling the operator to
        # "record usb_serial" is an impossible instruction. Identity for this
        # class is the SoC MAC just read above, verified actively at Tier-A time.
        out("")
        out(f"!! NOTE: {bridge_chip(target['vid_pid'])} USB-UART bridge -- this "
            "device class has no usable usb_serial.")
        out(f"!!   Identity is the SoC MAC recorded on this entry ({mac}); Tier-A")
        out("!!   operations verify it live before touching flash (#468).")
        out("!!   Passive resolution is provisional: keep only ONE bridge-class")
        out("!!   board attached when addressing this device by name.")
        out("")
    else:
        # No silent legacy write. An entry with only a port-path hash follows the
        # USB socket, not the board -- it will mis-resolve after any port swap.
        out("")
        out("!! WARNING: this device exposed NO usb_serial.")
        out("!!   The entry records only the port-path hash, which identifies the")
        out("!!   USB SOCKET and NOT the board. It will mis-resolve if the device")
        out("!!   is moved to a different port (#323/#354).")
        out("!!   Re-run 'pio-flash list' once the serial enumerates and record it")
        out("!!   on this entry as 'usb_serial: <VALUE>' before trusting it.")
        out("")

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
    ser = (target.get("usb_serial") or "").strip()
    if mac and ser:
        ident_desc = f"usb_serial {ser} (primary identity) + MAC {mac}"
    elif mac:
        ident_desc = f"MAC {mac}"
    else:
        ident_desc = f"usb_serial {ser} (primary identity)"
    out(f"Registered '{name}' with {ident_desc} in {REGISTRY_PATH}")
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
    _verify_bridge_mac(port, entry, args.device)  # #468: chip resets anyway
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
    p.add_argument(
        "--firmware-dir",
        default=None,
        help="override the firmware tree (platformio.ini location) for this "
             "invocation; must precede the subcommand. Precedence: "
             "--firmware-dir > PIO_FLASH_FIRMWARE_DIR env > default "
             "(Offband repo root). See #27.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list", help="enumerate present ports vs registry (Tier 0)")

    s = sub.add_parser("preview", help="stage a Tier A flash (writes token, no flash)")
    s.add_argument("device", help="registered device name (e.g. ST-P)")
    g = s.add_mutually_exclusive_group(required=True)
    g.add_argument("--env", help="pio env to build + upload (e.g. heltec_v4_companion_radio_ble)")
    g.add_argument("--artifact", help="path to a downloaded CI firmware artifact to flash "
                   "through the identity gate. ESP32: app '.bin' (default, NVS-preserving) "
                   "or '-merged.bin' with --erase (factory wipe). nRF52: '.zip' DFU package. "
                   "Mutually exclusive with --env.")
    s.add_argument("--erase", action="store_true",
                   help="ESP32 artifact-flash only: write the full -merged.bin at 0x0 "
                        "including NVS (factory wipe). Without it, an app .bin is written "
                        "to app0 and otadata is reset, preserving NVS.")

    s = sub.add_parser("confirm", help="execute the staged Tier A flash")
    s.add_argument("device", help="must match the device in the token")
    s.add_argument("--token", required=True, help="path to token file from preview")
    s.add_argument("--in-bootloader", action="store_true",
                   help="device's ROM bootloader keeps the same USB identity "
                        "(e.g. Heltec V4 TFT bootloader stays PID 1001 on the same "
                        "port). Skip the reset-and-rediscover dance; flash directly "
                        "on the resolved (identity-verified) port -- esptool handles "
                        "the reset. Mirrors cmd_factory_reset.")

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
                        "high baud produces 'serial stream stopped' on long reads -- "
                        "but note USB-Serial/JTAG ignores baud, where --no-stub is the fix)")
    s.add_argument("--no-stub", action="store_true",
                   help="use the ROM loader instead of the stub -- robust against "
                        "'serial stream stopped' on USB-Serial/JTAG sustained reads (slower)")

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
    global FIRMWARE_DIR
    args = build_parser().parse_args()
    if getattr(args, "firmware_dir", None):
        FIRMWARE_DIR = Path(args.firmware_dir).resolve()
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
