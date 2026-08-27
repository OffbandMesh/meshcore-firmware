#!/usr/bin/env python3
"""
.claude/hooks/block-raw-curl-ota.py - PreToolUse hook for Claude Code

Intercepts the Bash tool's command input and refuses raw `curl` invocations
against the /update endpoint of any known LoRa device, redirecting the
agent to scripts/ota-push.py.

Tracks: Strycher/LoRa#88

Why this exists
---------------
The 2026-05-17 silent-failure incident: I uploaded firmware to ST-P with a
wrong MD5 form parameter, the device's Update.end() rejected it silently
(curl timeout indistinguishable from success), and I spent the next hour
chasing a "combined firmware crashes on OTA boot" hypothesis that didn't
exist. The MD5 mistake was preventable; the diagnostic time was not.

scripts/ota-push.py makes the MD5 calculation structurally correct
(single-process, single-read, no copy-paste) AND verifies outcome via a
separate channel (serial / mqtt / telemetry). It must be used for OTA
pushes; this hook is the mechanical fence that ensures it is.

Behavior
--------
- Reads PreToolUse JSON event from stdin.
- Only acts on tool_name == "Bash".
- If the command contains 'ota-push' as a token, passes through (the
  wrapper is invoking curl internally on its own behalf, which is fine).
  We DO let it through; ota-push.py is allowed to use requests, but if a
  user pipes via the wrapper for some reason, that's also fine.
- Loads hardware-devices.yaml and extracts ota.lan_ip values for all
  registered devices.
- If the command is `curl ... <known-LoRa-IP>/update`, refuses with exit 2.
- Fail-closed on JSON parse error.
- Fail-OPEN on registry-load error: a malformed YAML shouldn't block
  unrelated curl invocations to non-LoRa hosts. We log to stderr but
  pass through.

Exit codes
----------
0  pass through to the underlying Bash tool
2  block AND surface stderr to the agent
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent

# #1012: the registry is per-host and canonical, NOT checkout-local.
#
# Resolution lives in scripts/offband_state.py and is NOT duplicated here: a
# second copy of the path logic drifts, and a security hook reading a
# different registry than the tool it guards is worse than no hook.
#
# sys.path.append (NOT insert(0)) so a file dropped in scripts/ cannot shadow
# a stdlib module for this hook.
sys.path.append(str(PROJECT_ROOT / "scripts"))
try:
    from offband_state import registry_path
except ImportError as _e:  # no silent second implementation -- see below
    print(
        "WARN: block-raw-curl-ota: cannot import scripts/offband_state.py "
        f"({_e}); OTA IP guard is OPEN. Fix the checkout.",
        file=sys.stderr,
    )
    registry_path = None

REGISTRY_PATH = registry_path() if registry_path else None

WRAPPER_PATH = "scripts/ota-push.py"


def load_known_lora_ips() -> list[str]:
    """
    Return the list of lan_ip values from hardware-devices.yaml devices that
    have an 'ota' block. Returns empty list on any registry load failure
    (intentional fail-open: bad registry shouldn't block all curl usage).
    """
    if REGISTRY_PATH is None or not REGISTRY_PATH.exists():
        return []
    try:
        import yaml
    except ImportError:
        # No PyYAML in env -> can't enforce. Pass through with a stderr note.
        print(
            "WARN: block-raw-curl-ota: PyYAML not installed, hook is open. "
            "Install with: pip install PyYAML",
            file=sys.stderr,
        )
        return []
    try:
        with REGISTRY_PATH.open(encoding="utf-8") as f:
            data = yaml.safe_load(f)
    except Exception as e:
        print(f"WARN: block-raw-curl-ota: registry parse failed: {e}", file=sys.stderr)
        return []
    if not isinstance(data, dict):
        return []
    out: list[str] = []
    for entry in (data.get("devices") or {}).values():
        if not isinstance(entry, dict):
            continue
        ota = entry.get("ota")
        if not isinstance(ota, dict):
            continue
        ip = ota.get("lan_ip")
        if isinstance(ip, str) and ip.strip():
            out.append(ip.strip())
    return out


# CMD_BOUNDARY: same convention as block-raw-flash.py - matches the START
# of a shell command, not arbitrary whitespace. Prevents false-positives
# when "curl" appears inside a quoted string of an unrelated command.
CMD_BOUNDARY = r"(?:^|[;&|`]\s*|(?:&&|\|\|)\s*|\$\(\s*)"

# Authorized wrapper detection.
WRAPPER_TOKEN = re.compile(r"\bota-push(?:\.py)?\b")


def parse_event() -> dict:
    raw = sys.stdin.read()
    try:
        return json.loads(raw)
    except json.JSONDecodeError as e:
        print(
            f"BLOCK: block-raw-curl-ota could not parse PreToolUse JSON: {e}\n"
            f"Raw input (first 200 chars): {raw[:200]!r}",
            file=sys.stderr,
        )
        sys.exit(2)


def build_block_pattern(ips: list[str]) -> re.Pattern:
    """
    Build a single regex matching `curl` (with optional flags) followed by
    a URL whose host matches one of the registered LoRa IPs and whose path
    is /update (with optional trailing slash or query string).

    Examples that should match:
      curl http://10.0.0.42/update
      curl -X POST -F MD5=... http://10.0.0.42/update
      curl --user admin:foo http://10.0.0.42/update?x=1
      curl https://10.0.0.42/update    (https unlikely but covered)

    Examples that should NOT match:
      curl http://10.0.0.43/something    (LAN broker, not /update)
      curl http://1.2.3.4/update             (not a registered IP)
      git curl ...                           (not a curl invocation)
    """
    if not ips:
        # No registered IPs -> never matches anything.
        return re.compile(r"$^")  # impossible-match regex
    # Escape dots in IPs for regex literal use.
    ip_alt = "|".join(re.escape(ip) for ip in ips)
    pattern = (
        CMD_BOUNDARY
        + r"curl\b"            # the curl command at a command boundary
        + r"[^\n;&|]*?"        # any args (no command separators)
        + r"https?://"         # http or https
        + r"(?:" + ip_alt + r")"
        + r"(?::\d+)?"         # optional :port
        + r"/update\b"         # the /update endpoint
    )
    return re.compile(pattern)


def main() -> int:
    event = parse_event()
    if event.get("tool_name") != "Bash":
        return 0
    cmd = (event.get("tool_input") or {}).get("command", "")
    if not cmd:
        return 0

    # Wrapper passes through unconditionally.
    if WRAPPER_TOKEN.search(cmd):
        return 0

    ips = load_known_lora_ips()
    pattern = build_block_pattern(ips)
    if pattern.search(cmd):
        # Identify which IP(s) matched for clarity in the refusal.
        matched_ips = [ip for ip in ips if ip in cmd]
        print(
            "BLOCK (raw curl OTA): direct upload to known LoRa device /update endpoint\n"
            "\n"
            f"Matched IP(s): {matched_ips}\n"
            "\n"
            "Refusing raw curl per project OTA discipline (Strycher/LoRa#88).\n"
            "Raw curl produces silent failure modes:\n"
            "  - Wrong MD5 (stale file, sha256/md5 mixup, hex transposition)\n"
            "  - 'Successful' upload that didn't take (Update.end rejected)\n"
            "  - Curl timeout indistinguishable between success and failure\n"
            "\n"
            "Use the wrapper instead:\n"
            f"  python {WRAPPER_PATH} <device-name> --firmware <path-to-firmware.bin>\n"
            "\n"
            "The wrapper enforces:\n"
            "  - Single-process MD5 (read once, hash once, upload once)\n"
            "  - Cross-channel verify (serial/mqtt/telemetry per registry)\n"
            "  - Audit trail in flash-history.jsonl (sha256, md5, verify result)\n"
            "\n"
            "See: C:\\Dev\\LoRa\\CLAUDE.md OTA Discipline section,\n"
            "     Strycher/LoRa#88\n"
            "\n"
            "Original command was:\n"
            "  " + cmd,
            file=sys.stderr,
        )
        return 2

    return 0


if __name__ == "__main__":
    sys.exit(main())
