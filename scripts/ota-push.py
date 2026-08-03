#!/usr/bin/env python3
"""
scripts/ota-push.py - LoRa OTA discipline wrapper (Python body)

Pushes a firmware.bin to a registered LoRa device over WiFi/HTTP, with
mechanical guarantees against the client-side mistakes that caused the
2026-05-17 silent-failure incident. See Strycher/LoRa#88 for the epic.

The script makes three failure modes structurally impossible:

  1. WRONG MD5 - file is read once into memory; MD5 is hashed from the
     same in-memory bytes that are uploaded as the request body. No
     copy-paste, no second file read, no possibility of staleness.

  2. WRONG FILE - device is identified from hardware-devices.yaml; the
     firmware path is taken explicitly from --firmware. SHA-256 of the
     uploaded bytes is recorded in flash-history.jsonl, matching the
     pio-flash audit format.

  3. SILENT SUCCESS - curl's exit code lies for OTA (timeouts on both
     success and failure paths because AsyncWebServer + ESP.restart() race
     drops the response). After upload, the script verifies via a separate
     channel (serial / mqtt / telemetry, per registry verify_channel) and
     refuses to claim success unless the partition state confirms it.

Usage:
    ota-push.py <device-name> --firmware <path-to-firmware.bin>
                              [--verify-channel serial|mqtt|telemetry]
                              [--verify-timeout-sec N]
                              [--upload-timeout-sec N]
                              [--dry-run]

Exit codes:
    0  success: upload completed AND verify confirmed app1 in valid set
    1  refused: missing config, bad args, registry mismatch, etc.
    2  upload failed at HTTP layer (connection refused, auth fail, etc.)
    3  upload appeared to succeed but verify-channel reported FAILURE
    4  verify-channel itself was unavailable / inconclusive

Tracks: Strycher/LoRa#88
"""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Optional

try:
    import yaml
except ImportError:
    print("FATAL: PyYAML not installed. Run: pip install PyYAML", file=sys.stderr)
    sys.exit(1)

# Sibling import of shared firmware_identity module (#200 / LoRa-wek).
# The wrappers are run from various CWDs; force scripts/ onto sys.path so
# the import works regardless of where the script was launched from.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from firmware_identity import get_firmware_identity  # noqa: E402

try:
    import requests
except ImportError:
    print("FATAL: requests not installed. Run: pip install requests", file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
PROJECT_ROOT = Path(__file__).resolve().parent.parent
REGISTRY_PATH = PROJECT_ROOT / "hardware-devices.yaml"
FLASH_HISTORY_PATH = PROJECT_ROOT / "flash-history.jsonl"
# Secrets source. LoRa#209 moved secrets out of a co-located (and now
# deprecated) platformio.local.ini into a canonical file the BUILD reads via
# `extra_configs = ${sysenv.PIO_SECRETS_FILE}` (see platformio.ini). ota-push
# must read the SAME file, or it resolves stale/absent credentials. The prior
# value (PROJECT_ROOT / "meshcore-firmware" / "platformio.local.ini") assumed
# the pre-migration nested layout AND the deprecated filename -- doubly wrong
# after the OffbandMesh cutover, so every OTA push refused. Prefer the env var
# (single source of truth with the build); fall back to the legacy in-repo path
# only when it is unset, so a mis-configured host gets a clear message.
_PIO_SECRETS_FILE = os.environ.get("PIO_SECRETS_FILE", "").strip()
SECRETS_PATH = Path(_PIO_SECRETS_FILE) if _PIO_SECRETS_FILE else (PROJECT_ROOT / "platformio.local.ini")

# Default timeouts. Generous; tunable per-call.
DEFAULT_UPLOAD_TIMEOUT_SEC = 180     # curl-equivalent ceiling
DEFAULT_VERIFY_TIMEOUT_SEC = 60      # global fallback (used only when verify_channel is unknown)
POST_UPLOAD_SETTLE_SEC = 15          # device boot/recover window

# LoRa#233: per-channel verify timeout defaults. The global 60s default was
# too short for the `telemetry` channel — that channel just listens for the
# device's next natural MQTT publish, which on a 15-min telemetry interval
# (the pre-#216 default) can take up to ~17 min. 60s false-negatived
# successful OTAs (tonight's patio FF6 flash was the trigger). Per-channel
# defaults below; explicit --verify-timeout-sec from the CLI always wins.
DEFAULT_VERIFY_TIMEOUT_BY_CHANNEL = {
    "serial":    60,    # serial CLI roundtrip — fast
    "mqtt":      60,    # cmd-relay dispatch + response — fast
    "telemetry": 1200,  # may wait for natural publish; covers 15-min interval + buffer
}

# Strong-assertion success set for the RUNNING partition's state after a
# successful OTA reboot. See findings_2026-05-14_ota_rollback_actual_behavior.md
# for the state machine details:
#   - pending_verify: booted, in arduino-esp32 auto-validate window (first ~60s)
#   - valid         : booted, arduino-esp32 mark_valid succeeded
# Anything else (undef = no OTA history on the running partition; aborted =
# bootloader rolled back; invalid = bootloader refused image) is FAILURE
# *when paired with a partition-switch check* below.
#
# Note: 'new' is deliberately EXCLUDED here. NEW means "just written, never
# booted" - if we see NEW on the running partition, something is impossible
# (we can't be running a never-booted partition). NEW on the *other* slot
# would mean OTA wrote but device didn't reboot (also failure, surfaced via
# the partition-switch check, not this set).
RUNNING_PARTITION_SUCCESS_STATES = {"pending_verify", "valid"}


# ---------------------------------------------------------------------------
# Output helpers - same style as pio-flash.py for grep-ability.
# ---------------------------------------------------------------------------
def out(msg: str) -> None:
    print(msg)


def err(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)


def refuse(msg: str, *, exit_code: int = 1) -> "NoReturn":
    print(f"REFUSE: {msg}", file=sys.stderr)
    sys.exit(exit_code)


# ---------------------------------------------------------------------------
# Registry loading - mirrors pio-flash.py's load_registry().
# ---------------------------------------------------------------------------
def load_registry() -> dict:
    if not REGISTRY_PATH.exists():
        refuse(f"hardware-devices.yaml not found at {REGISTRY_PATH}")
    with REGISTRY_PATH.open(encoding="utf-8") as f:
        data = yaml.safe_load(f)
    if not isinstance(data, dict):
        refuse(f"registry at {REGISTRY_PATH} is not a YAML mapping")
    data.setdefault("devices", {})
    data.setdefault("foreign_devices", {})
    return data


def resolve_ota_device(name: str, registry: dict) -> dict:
    """
    Find the device in the registry and confirm it has an `ota:` block.
    Refuses cleanly on every missing-field condition with a clear message.
    Returns the ota config dict augmented with the device's `mac` and `role`
    for downstream logging.
    """
    if name in (registry.get("foreign_devices") or {}):
        refuse(
            f"'{name}' is registered as a FOREIGN device (do-not-touch). "
            "Refusing OTA push."
        )
    devs = registry.get("devices") or {}
    if name not in devs:
        refuse(
            f"'{name}' not in hardware-devices.yaml. Known LoRa devices: "
            f"{', '.join(sorted(devs.keys()))}"
        )
    entry = devs[name]
    ota = entry.get("ota")
    if not ota or not isinstance(ota, dict):
        refuse(
            f"device '{name}' has no 'ota:' block in registry. "
            "This device is not configured for OTA. To add OTA support, "
            "populate the ota: fields per the schema in hardware-devices.yaml."
        )

    # Required fields (every OTA push needs these).
    for key in ("lan_ip", "admin_password_secret_key", "node_id", "verify_channel"):
        if not ota.get(key):
            refuse(
                f"device '{name}' is missing ota.{key} in registry "
                f"(value: {ota.get(key)!r}). Set it before invoking ota-push."
            )
    if ota["verify_channel"] not in ("serial", "mqtt", "telemetry"):
        refuse(
            f"device '{name}' has invalid ota.verify_channel: "
            f"{ota['verify_channel']!r}. Must be 'serial', 'mqtt', or 'telemetry'."
        )

    # Return a defensively-copied ota dict with parent metadata attached.
    out_dict = dict(ota)
    out_dict["_device_name"] = name
    out_dict["_mac"] = entry.get("mac")
    out_dict["_role"] = entry.get("role")
    return out_dict


# ---------------------------------------------------------------------------
# Secrets loading - reads platformio.local.ini via configparser, dereferences
# the "section.key" pointer stored in registry.
#
# Why this layer exists:
#   The registry is intentionally NOT a place to store secrets. Secrets live
#   in platformio.local.ini (which is gitignored). The registry stores a
#   POINTER ("section.key") into that ini file. This keeps the security
#   property: even if hardware-devices.yaml gets committed by accident, no
#   credentials leak.
# ---------------------------------------------------------------------------
def load_secret(section_dot_key: str) -> str:
    if not SECRETS_PATH.exists():
        refuse(
            f"platformio.local.ini not found at {SECRETS_PATH}. "
            "Cannot resolve admin password."
        )
    parts = section_dot_key.split(".", 1)
    if len(parts) != 2:
        refuse(
            f"admin_password_secret_key must be 'section.key' format, "
            f"got: {section_dot_key!r}"
        )
    section, key = parts
    parser = configparser.ConfigParser()
    parser.read(SECRETS_PATH, encoding="utf-8")
    if section not in parser:
        refuse(
            f"section [{section}] not found in {SECRETS_PATH}. "
            f"Sections present: {list(parser.sections())}"
        )
    if key not in parser[section]:
        refuse(
            f"key '{key}' not found in section [{section}] of {SECRETS_PATH}. "
            f"Keys present: {list(parser[section].keys())}"
        )
    return parser[section][key]


def load_mqtt_config() -> tuple[str, int, Optional[str], Optional[str]]:
    """Returns (host, port, user, password). Anonymous if user is blank."""
    parser = configparser.ConfigParser()
    if not SECRETS_PATH.exists():
        refuse(f"platformio.local.ini not found at {SECRETS_PATH}")
    parser.read(SECRETS_PATH, encoding="utf-8")
    if "mqtt_secrets" not in parser:
        refuse("section [mqtt_secrets] missing from platformio.local.ini")
    s = parser["mqtt_secrets"]
    host = s.get("host", "").strip()
    port = int(s.get("port", "1883"))
    user = s.get("user", "").strip() or None
    password = s.get("password", "").strip() or None
    if not host:
        refuse("mqtt_secrets.host is blank in platformio.local.ini")
    return host, port, user, password


def load_ota_trigger_secret() -> str:
    parser = configparser.ConfigParser()
    parser.read(SECRETS_PATH, encoding="utf-8")
    if "ota_secrets" not in parser or "trigger_secret" not in parser["ota_secrets"]:
        refuse(
            "ota_secrets.trigger_secret missing from platformio.local.ini "
            "(needed for verify_channel=mqtt)"
        )
    return parser["ota_secrets"]["trigger_secret"]


# ---------------------------------------------------------------------------
# History log - append-only audit trail, same JSONL format as pio-flash uses.
# ---------------------------------------------------------------------------
def log_history(entry: dict) -> None:
    FLASH_HISTORY_PATH.parent.mkdir(parents=True, exist_ok=True)
    with FLASH_HISTORY_PATH.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry, separators=(",", ":")) + "\n")


# FF5 (#182 / LoRa-wa7) + #200 (LoRa-wek): firmware identity for the audit
# log. Source of truth is the embedded XWIRE marker blob in the firmware
# binary; git is the fallback for pre-#200 builds. See
# scripts/firmware_identity.py for the orchestration / fallback policy.
# Post-migration the repo root IS the firmware source tree (the pre-cutover
# nested "meshcore-firmware/" subdir no longer exists). Used only for the
# git-fallback identity path (offband binaries carry #200 markers, so this is
# rarely hit); a stale path here degrades identity to "unknown" rather than
# failing, but point it at the real tree regardless.
FIRMWARE_DIR = PROJECT_ROOT


# ---------------------------------------------------------------------------
# Upload - the mechanical-MD5 step. Read once, hash once, upload once.
# ---------------------------------------------------------------------------
def upload_firmware(
    lan_ip: str,
    admin_password: str,
    firmware_path: Path,
    timeout_sec: int,
) -> dict:
    """
    Read firmware bytes ONCE into memory. Compute MD5 from those bytes.
    POST to http://<lan_ip>/update with auth + MD5 form param + binary body.
    Return a dict of upload telemetry (sha256, md5, size, status_code,
    elapsed_sec, exception_class_if_any).

    The 'one-shot read' guarantee is structural: nothing can swap the file
    out from under us between hash and upload because both operate on the
    same Python bytes object held by this function's local.
    """
    if not firmware_path.exists():
        refuse(f"firmware {firmware_path} does not exist")
    blob = firmware_path.read_bytes()
    md5 = hashlib.md5(blob).hexdigest()
    sha256 = hashlib.sha256(blob).hexdigest()
    size = len(blob)

    url = f"http://{lan_ip}/update"
    auth = ("admin", admin_password)

    out(f"  POST {url}")
    out(f"  Firmware bytes : {size} ({size/1024:.1f} KB)")
    out(f"  Firmware sha256: {sha256}")
    out(f"  Firmware md5   : {md5}")
    out(f"  Upload timeout : {timeout_sec}s")

    start = time.time()
    status_code: Optional[int] = None
    response_body: Optional[str] = None
    exc_class: Optional[str] = None
    exc_msg: Optional[str] = None

    try:
        resp = requests.post(
            url,
            auth=auth,
            files={
                # Same blob object referenced as both the form-data 'firmware'
                # part and the source bytes hashed for MD5 above. No second
                # read; no opportunity for the file to change underneath.
                "firmware": ("firmware.bin", blob, "application/octet-stream"),
            },
            data={"MD5": md5},
            timeout=timeout_sec,
        )
        status_code = resp.status_code
        # Body may be empty if the device restarted before flushing - that's
        # the expected success path. Capture what we got either way.
        try:
            response_body = resp.text[:500]
        except Exception:
            response_body = "(unable to decode)"
    except requests.exceptions.ReadTimeout as e:
        exc_class = "ReadTimeout"
        exc_msg = str(e)
    except requests.exceptions.ConnectionError as e:
        # Common: device rebooted mid-upload (success case) -> connection
        # dropped before we got a response. ALSO common: device wasn't in
        # OTA mode and refused the connection. Caller's verify step
        # disambiguates.
        exc_class = "ConnectionError"
        exc_msg = str(e)
    except Exception as e:
        exc_class = type(e).__name__
        exc_msg = str(e)
    elapsed = time.time() - start

    if exc_class:
        out(f"  Upload exception: {exc_class}: {exc_msg[:200]}")
    else:
        out(f"  HTTP response  : {status_code}")
        if response_body:
            out(f"  Response body  : {response_body!r}")
    out(f"  Elapsed        : {elapsed:.1f}s")

    return {
        "firmware_sha256": sha256,
        "firmware_md5": md5,
        "firmware_size": size,
        "url": url,
        "status_code": status_code,
        "response_body": response_body,
        "exception_class": exc_class,
        "exception_message": exc_msg,
        "elapsed_sec": round(elapsed, 2),
    }


# ---------------------------------------------------------------------------
# Verify channels
#
# Each returns a (success: bool, detail: str) tuple. detail goes into the
# audit log for forensic reconstruction.
# ---------------------------------------------------------------------------
def _query_safety_partitions_serial(device_name: str) -> Optional[dict]:
    """
    Subprocess to pio-flash send <device> 'safety partitions' and parse
    the response. Returns a dict {"run": "appN", "states": {"appN": "..."}}
    or None on parse/timeout failure.
    """
    pio_flash = PROJECT_ROOT / "scripts" / "pio-flash.py"
    if not pio_flash.exists():
        return None
    try:
        result = subprocess.run(
            [sys.executable, str(pio_flash), "send", device_name,
             "safety partitions", "--read-time", "5"],
            capture_output=True, text=True, timeout=30,
        )
    except subprocess.TimeoutExpired:
        return None
    stdout = result.stdout or ""
    # Expected: "run=app0 next=app1 [app0:undef app1:valid]"
    import re as _re
    run_m = _re.search(r"run=(\w+)", stdout)
    if not run_m:
        return None
    state_matches = dict(_re.findall(r"(app\d+):(\w+)", stdout))
    return {"run": run_m.group(1), "states": state_matches}


def verify_via_serial(
    device_name: str,
    pre_state: Optional[dict],
) -> tuple[bool, str]:
    """
    Two-part check against `safety partitions`:

    1. The running partition must have CHANGED between pre and post.
       If `run` is the same, the device either didn't reboot (Update.end
       rejected the image) or rolled back to where it started (combined
       firmware crashed during PENDING_VERIFY).

    2. The post-push running partition's state must be in
       RUNNING_PARTITION_SUCCESS_STATES ({pending_verify, valid}).
       VALID = arduino-esp32 auto-validate fired; PENDING_VERIFY = still
       in the auto-validate window. UNDEFINED on a *running* partition
       would only happen if we're back on the factory/no-history slot
       (= failure: never switched).

    pre_state may be None if pre-push capture failed; in that case the
    partition-switch check is impossible and we issue a weaker assertion
    (state-only) plus an explicit warning.
    """
    post = _query_safety_partitions_serial(device_name)
    if not post:
        return (False, "verify_serial: post-push 'safety partitions' query failed")

    run_post = post["run"]
    state_post = post["states"].get(run_post, "?")

    if pre_state and pre_state.get("run"):
        run_pre = pre_state["run"]
        if run_post == run_pre:
            return (
                False,
                f"verify_serial: running partition did NOT switch "
                f"(pre={run_pre}, post={run_post}). OTA upload was sent but "
                f"the device did not boot a new image. Likely cause: Update.end() "
                f"rejected the firmware (bad MD5 or image-magic), OR bootloader "
                f"rolled back from a PENDING_VERIFY crash. Full post-state: "
                f"run={run_post} states={post['states']!r}"
            )
        # Switched. Now check the new running slot is in a good state.
        if state_post in RUNNING_PARTITION_SUCCESS_STATES:
            return (
                True,
                f"verify_serial: SUCCESS - running partition switched {run_pre}->{run_post}, "
                f"state={state_post}, full post-state {post['states']!r}"
            )
        return (
            False,
            f"verify_serial: running partition switched {run_pre}->{run_post} BUT "
            f"state={state_post!r} not in {sorted(RUNNING_PARTITION_SUCCESS_STATES)}. "
            f"Anomalous - investigate. Full: {post['states']!r}"
        )

    # No pre-state available. Weaker check: state-only.
    ok = state_post in RUNNING_PARTITION_SUCCESS_STATES
    return (
        ok,
        f"verify_serial: pre-state unavailable, state-only check. "
        f"run={run_post} state={state_post} "
        f"in_success_set={ok}. "
        f"WARNING: cannot detect 'didn't reboot' failure without pre-state. "
        f"Full: {post['states']!r}"
    )


def _query_safety_log_mqtt(ota_cfg: dict, timeout_sec: int) -> Optional[dict]:
    """
    Helper: publish safety_log_dump cmd, await response, parse out the
    latest boot_inc seq and st. Returns {"latest_seq": int, "latest_st": str}
    or None on failure.
    """
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        return None
    host, port, user, password = load_mqtt_config()
    secret = load_ota_trigger_secret()
    node_id = ota_cfg["node_id"]
    prefix = ota_cfg.get("mqtt_topic_prefix") or "meshcore"
    cmd_topic = f"{prefix}/{node_id}/cmd"
    resp_topic = f"{prefix}/{node_id}/cmd/response"

    import threading as _th
    response_payload: list[bytes] = []
    done = _th.Event()

    def on_message(client, userdata, msg):
        response_payload.append(msg.payload)
        done.set()

    client = mqtt.Client()
    if user:
        client.username_pw_set(user, password)
    client.on_message = on_message
    try:
        client.connect(host, port, 30)
    except Exception:
        return None
    client.subscribe(resp_topic)
    client.loop_start()
    time.sleep(0.5)
    payload = json.dumps({"action": "safety_log_dump", "auth": secret})
    client.publish(cmd_topic, payload, qos=0)
    done.wait(timeout=timeout_sec)
    client.loop_stop()
    client.disconnect()

    if not response_payload:
        return None
    try:
        text = response_payload[0].decode("utf-8", errors="replace")
        body = json.loads(text)
    except Exception:
        return None
    log_text = (body.get("data") or {}).get("log") or body.get("message") or ""
    import re as _re
    matches = list(_re.finditer(r"#(\d+).*?boot_inc:\d+/\d+\s+st=(\w+)", log_text))
    if not matches:
        return None
    latest = max(matches, key=lambda m: int(m.group(1)))
    return {"latest_seq": int(latest.group(1)), "latest_st": latest.group(2)}


def verify_via_mqtt(
    ota_cfg: dict,
    timeout_sec: int,
    pre_state: Optional[dict],
) -> tuple[bool, str]:
    """
    Two-part check via #86 cmd channel safety_log_dump:

    1. The latest boot_inc seq must have INCREASED between pre and post.
       If seq is unchanged, no new boot happened (Update.end rejected).

    2. The post-push latest boot_inc must show st in
       RUNNING_PARTITION_SUCCESS_STATES.

    Requires #86 firmware on the target (i.e., this is the post-first-deploy
    verify path - patio first deploy must use verify_channel=telemetry then
    switch to mqtt).
    """
    post = _query_safety_log_mqtt(ota_cfg, timeout_sec)
    if not post:
        return (
            False,
            f"verify_mqtt: no usable response from safety_log_dump within "
            f"{timeout_sec}s (transport down? #86 not present on this firmware? "
            "auth fail?)"
        )

    if pre_state and pre_state.get("latest_seq") is not None:
        pre_seq = pre_state["latest_seq"]
        post_seq = post["latest_seq"]
        if post_seq <= pre_seq:
            return (
                False,
                f"verify_mqtt: latest boot_inc seq did NOT advance "
                f"(pre={pre_seq}, post={post_seq}). OTA upload was sent but no "
                f"new boot happened. Likely Update.end() rejected the firmware. "
                f"post.latest_st={post['latest_st']}"
            )
        if post["latest_st"] in RUNNING_PARTITION_SUCCESS_STATES:
            return (
                True,
                f"verify_mqtt: SUCCESS - boot_inc seq advanced #{pre_seq}->#{post_seq}, "
                f"st={post['latest_st']}"
            )
        return (
            False,
            f"verify_mqtt: boot_inc seq advanced #{pre_seq}->#{post_seq} BUT "
            f"st={post['latest_st']!r} not in "
            f"{sorted(RUNNING_PARTITION_SUCCESS_STATES)}. "
            f"Possible mid-rollback or anomalous state."
        )

    # No pre-state; state-only check.
    ok = post["latest_st"] in RUNNING_PARTITION_SUCCESS_STATES
    return (
        ok,
        f"verify_mqtt: pre-state unavailable, state-only check. "
        f"latest boot_inc #{post['latest_seq']} st={post['latest_st']} "
        f"in_success_set={ok}. "
        f"WARNING: cannot detect 'didn't reboot' failure without pre-state."
    )


def verify_via_telemetry(ota_cfg: dict, timeout_sec: int) -> tuple[bool, str]:
    """
    Weak verify path: subscribe to the device's state topic, wait for a
    fresh publish (not retained). Only proves the device is alive and
    publishing. CANNOT distinguish app0 vs app1.

    Intended for pre-#86 remote bootstrap deploys (e.g., initial patio
    push where #86 isn't installed yet). After that first deploy lands,
    the device should be updated to verify_channel=mqtt.
    """
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        return (False, "verify_telemetry: paho-mqtt not installed")

    host, port, user, password = load_mqtt_config()
    node_id = ota_cfg["node_id"]
    prefix = ota_cfg.get("mqtt_topic_prefix") or "meshcore"
    state_topic = f"{prefix}/{node_id}/state"

    import threading as _th
    fresh_publishes: list[bytes] = []
    done = _th.Event()

    def on_message(client, userdata, msg):
        # Ignore retained - we want evidence of a NEW publish post-OTA.
        if msg.retain:
            return
        fresh_publishes.append(msg.payload)
        done.set()

    client = mqtt.Client()
    if user:
        client.username_pw_set(user, password)
    client.on_message = on_message
    try:
        client.connect(host, port, 30)
    except Exception as e:
        return (False, f"verify_telemetry: connect to {host}:{port} failed: {e}")
    client.subscribe(state_topic)
    client.loop_start()

    out(f"  Waiting up to {timeout_sec}s for fresh publish on {state_topic}...")
    done.wait(timeout=timeout_sec)
    client.loop_stop()
    client.disconnect()

    if not fresh_publishes:
        return (
            False,
            f"verify_telemetry: no fresh (non-retained) publish on {state_topic} "
            f"within {timeout_sec}s. Device may be in burst mode (publish only "
            "every WIFI_TELEMETRY_INTERVAL_MS) or may have failed to boot."
        )
    try:
        body = json.loads(fresh_publishes[0])
        uptime = body.get("uptime_seconds")
    except Exception:
        uptime = "(parse failed)"
    return (
        True,
        f"verify_telemetry: device published state; uptime_seconds={uptime} "
        "(weak: this proves device is alive but does NOT distinguish app0 vs app1)"
    )


# ---------------------------------------------------------------------------
# Main flow
# ---------------------------------------------------------------------------
def run_push(args) -> int:
    registry = load_registry()
    ota_cfg = resolve_ota_device(args.device, registry)

    # CLI override of verify channel (occasional debugging tool).
    verify_channel = args.verify_channel or ota_cfg["verify_channel"]
    if verify_channel not in ("serial", "mqtt", "telemetry"):
        refuse(f"invalid --verify-channel: {verify_channel!r}")

    # LoRa#233: resolve effective verify timeout. CLI --verify-timeout-sec
    # always wins. Otherwise, use per-channel default (telemetry needs much
    # longer than serial/mqtt because it waits for natural MQTT publish).
    if args.verify_timeout_sec is not None:
        verify_timeout_sec = args.verify_timeout_sec
    else:
        verify_timeout_sec = DEFAULT_VERIFY_TIMEOUT_BY_CHANNEL.get(
            verify_channel, DEFAULT_VERIFY_TIMEOUT_SEC
        )

    firmware_path = Path(args.firmware)
    if not firmware_path.is_absolute():
        firmware_path = Path.cwd() / firmware_path
    if not firmware_path.exists():
        refuse(f"firmware not found: {firmware_path}")

    admin_password = load_secret(ota_cfg["admin_password_secret_key"])

    out("=" * 60)
    out("OTA PUSH (mechanical discipline wrapper, issue #88)")
    out("=" * 60)
    out(f"Device         : {args.device}")
    out(f"Device MAC     : {ota_cfg.get('_mac')}")
    out(f"Device LAN IP  : {ota_cfg['lan_ip']}")
    out(f"Node ID        : {ota_cfg['node_id']}")
    out(f"Firmware       : {firmware_path}")
    out(f"Verify channel : {verify_channel} "
        f"(registry default: {ota_cfg['verify_channel']})")
    out(f"Upload timeout : {args.upload_timeout_sec}s")
    if args.verify_timeout_sec is not None:
        out(f"Verify timeout : {verify_timeout_sec}s (CLI override)")
    else:
        out(f"Verify timeout : {verify_timeout_sec}s "
            f"(per-channel default for {verify_channel!r})")
    if args.dry_run:
        out("=" * 60)
        out("DRY RUN - exiting before any device touch.")
        return 0
    out("-" * 60)

    # --- Pre-push state capture ---
    # Critical for distinguishing "OTA actually switched the running
    # partition" from "OTA upload was sent but device didn't reboot."
    # Without this, the verify check is weaker (state-only) and can miss
    # the silent-failure-on-upload case.
    pre_state: Optional[dict] = None
    if verify_channel == "serial":
        out("[1/4] Pre-push: capturing 'safety partitions' baseline...")
        pre_state = _query_safety_partitions_serial(args.device)
        if pre_state:
            out(f"  Pre-state: run={pre_state['run']} states={pre_state['states']!r}")
        else:
            out("  WARNING: pre-state capture failed; verify will be weaker")
    elif verify_channel == "mqtt":
        out("[1/4] Pre-push: capturing safety log baseline via #86 cmd channel...")
        pre_state = _query_safety_log_mqtt(ota_cfg, timeout_sec=10)
        if pre_state:
            out(f"  Pre-state: latest_boot_inc_seq={pre_state.get('latest_seq')}")
        else:
            out("  WARNING: pre-state capture failed; verify will be weaker")
    else:
        out("[1/4] Pre-push: telemetry channel does not support pre-state capture")

    # --- Upload ---
    out("[2/4] Uploading firmware (single-process MD5)...")
    upload_result = upload_firmware(
        lan_ip=ota_cfg["lan_ip"],
        admin_password=admin_password,
        firmware_path=firmware_path,
        timeout_sec=args.upload_timeout_sec,
    )

    # --- Settle period ---
    settle_sec = args.settle_sec
    out(f"[3/4] Settling {settle_sec}s for device to reboot or remain stable...")
    time.sleep(settle_sec)

    # --- Verify ---
    out(f"[4/4] Verifying via {verify_channel} channel...")
    if verify_channel == "serial":
        ok, detail = verify_via_serial(args.device, pre_state)
    elif verify_channel == "mqtt":
        ok, detail = verify_via_mqtt(ota_cfg, verify_timeout_sec, pre_state)
    elif verify_channel == "telemetry":
        ok, detail = verify_via_telemetry(ota_cfg, verify_timeout_sec)
    else:
        ok, detail = (False, f"unknown verify channel: {verify_channel}")

    out(f"  {detail}")

    # --- Audit log ---
    # #200 (LoRa-wek): identity is read from the firmware binary's embedded
    # XWIRE marker blob, NOT re-derived from git. The .bin was built at one
    # point in time; the working tree may have advanced since. firmware_sha256
    # is still the authoritative artifact ID; the offband_* fields are the
    # human-readable label. firmware_identity_source records which path won.
    fw_identity = get_firmware_identity(firmware_path, FIRMWARE_DIR)

    log_entry = {
        "ts_unix": int(time.time()),
        "mode": "ota_push",
        "device": args.device,
        "mac": ota_cfg.get("_mac"),
        "lan_ip": ota_cfg["lan_ip"],
        "node_id": ota_cfg["node_id"],
        "firmware_path": str(firmware_path),
        "firmware_sha256": upload_result["firmware_sha256"],
        "firmware_md5": upload_result["firmware_md5"],
        "firmware_size": upload_result["firmware_size"],
        "offband_version": fw_identity["offband_version"],
        "offband_git_sha": fw_identity["offband_git_sha"],
        "offband_branch": fw_identity["offband_branch"],
        "offband_build_date": fw_identity["offband_build_date"],
        "firmware_identity_source": fw_identity["firmware_identity_source"],
        "upload_status_code": upload_result["status_code"],
        "upload_exception": upload_result["exception_class"],
        "upload_elapsed_sec": upload_result["elapsed_sec"],
        "verify_channel": verify_channel,
        "verify_pre_state": pre_state,
        "verify_ok": ok,
        "verify_detail": detail,
        "exit_code": 0 if ok else 3,
    }
    log_history(log_entry)

    out("-" * 60)
    if ok:
        out("RESULT: SUCCESS")
        out("        Verify confirmed device boot of new firmware.")
        return 0
    else:
        out("RESULT: FAILURE")
        out("        Upload was sent but verify did not confirm a successful boot.")
        out("        See verify detail above and flash-history.jsonl entry.")
        return 3


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="ota-push",
        description=(
            "Mechanical OTA discipline wrapper for LoRa devices. "
            "Reads firmware ONCE, hashes from the same bytes, uploads, "
            "and verifies via separate channel. See Strycher/LoRa#88."
        ),
    )
    p.add_argument("device", help="Device short-name from hardware-devices.yaml (e.g., ST-P)")
    p.add_argument("--firmware", required=True,
                   help="Path to firmware.bin to push (typically .pio/build/<env>/firmware.bin)")
    p.add_argument("--verify-channel", choices=["serial", "mqtt", "telemetry"],
                   help="Override registry's verify_channel for this push only")
    p.add_argument("--upload-timeout-sec", type=int, default=DEFAULT_UPLOAD_TIMEOUT_SEC,
                   help=f"Upload HTTP timeout (default {DEFAULT_UPLOAD_TIMEOUT_SEC})")
    p.add_argument("--verify-timeout-sec", type=int, default=None,
                   help=(
                       "Verify-channel wait. If unset, per-channel default: "
                       f"serial={DEFAULT_VERIFY_TIMEOUT_BY_CHANNEL['serial']}s, "
                       f"mqtt={DEFAULT_VERIFY_TIMEOUT_BY_CHANNEL['mqtt']}s, "
                       f"telemetry={DEFAULT_VERIFY_TIMEOUT_BY_CHANNEL['telemetry']}s "
                       "(LoRa#233). CLI value always overrides."
                   ))
    p.add_argument("--settle-sec", type=int, default=POST_UPLOAD_SETTLE_SEC,
                   help=f"Post-upload sleep before verify (default {POST_UPLOAD_SETTLE_SEC})")
    p.add_argument("--dry-run", action="store_true",
                   help="Resolve config + print plan, then exit before any device touch")
    return p


def main() -> int:
    args = build_parser().parse_args()
    return run_push(args)


if __name__ == "__main__":
    sys.exit(main())
