#!/usr/bin/env python3
"""
.claude/hooks/block-raw-flash.py - PreToolUse hook for Claude Code

Intercepts the Bash tool's command input and refuses class-A (state-
changing) and class-B (port-opening read-only) flash/serial commands,
redirecting the agent to scripts/pio-flash.

Tracks: Strycher/LoRa#48 (A3, sub-task of Epic A #44)
Proposal: proposal-flash-discipline.md section 1 + corrections C1/C2 from
incident_2026-05-13_desk_command_center_unauthorized_touch.md

Behavior
--------
- Reads PreToolUse JSON event from stdin.
- Only acts on tool_name == "Bash".
- If the command contains 'pio-flash' as a token (the authorized wrapper),
  passes through (exit 0).
- If the command matches a Tier A or Tier B pattern, refuses with exit 2
  (Claude Code surfaces stderr to the model) and a redirect message.
- All other commands pass through (exit 0).
- Fail-closed on JSON parse error: if we can't understand the input we
  block, not allow.

Tier A patterns blocked (state-changing, every invocation resets the chip)
- esptool / esptool.py / esptool.exe / python -m esptool ANY subcommand
- pio run -t upload | -t erase | -t uploadfs
- meshtastic --reboot | --configure | --set | --ch-set
- raw esptool write_flash, erase_flash, erase_region (already covered above)

Tier B patterns blocked-with-redirect (port-opening read-only)
- pio device monitor
- meshtastic --info | --get | --export-config

Class 0 commands passed through
- pio device list (enumeration)
- Get-PnpDevice (enumeration)
- python -c "import serial.tools.list_ports..."
- anything else not matching the above patterns

Exit codes
----------
0  pass through to the underlying Bash tool
2  block AND surface stderr to the agent (Claude Code semantics)
"""
from __future__ import annotations

import json
import re
import sys

WRAPPER_PATH = "scripts/pio-flash"

# CMD_BOUNDARY: matches the START of a shell command, NOT arbitrary whitespace.
# This prevents false positives when the trigger word appears inside a quoted
# argument of some unrelated command (e.g., a `gh issue close --comment "...
# blocks raw esptool..."` would otherwise fire on the literal text "esptool"
# inside the comment string).
#
# A real esptool invocation appears at one of:
#   - The very start of the command line
#   - After a command separator: ; & | && ||
#   - After a backtick or $(...) command substitution opener
#
# Whitespace alone is NOT enough - whitespace happens inside string arguments.
CMD_BOUNDARY = r"(?:^|[;&|`]\s*|(?:&&|\|\|)\s*|\$\(\s*)"

# Patterns: (regex, human_name, suggested_subcommand)
# Patterns are matched against the FULL command line.
TIER_A_PATTERNS = [
    (
        re.compile(CMD_BOUNDARY + r"(?:python\s+-m\s+)?esptool(?:\.py|\.exe)?\b"),
        "esptool (any subcommand)",
        f"{WRAPPER_PATH} read-mac <device>  OR  {WRAPPER_PATH} bootstrap <name> --port <COMx>",
    ),
    (
        # NOTE: \b-t\b does NOT match because both the leading space and
        # the leading '-' are non-word chars, so there is no word boundary
        # between them. Use (?:^|\s)-t\s+ to anchor on the leading space.
        re.compile(CMD_BOUNDARY + r"pio\b[^;&|]*?\brun\b[^;&|]*?(?:^|\s)-t\s+(?:upload|uploadfs|erase)\b"),
        "pio run -t upload/uploadfs/erase",
        f"{WRAPPER_PATH} preview <device> --env <env>  then  {WRAPPER_PATH} confirm <device> --token <file>",
    ),
    (
        re.compile(CMD_BOUNDARY + r"meshtastic\b[^;&|]*--(?:reboot|configure|set|ch-set)\b"),
        "meshtastic state-changing flag (--reboot/--configure/--set/--ch-set)",
        f"Use {WRAPPER_PATH} read-mac for identity then meshtastic via the wrapper (subcommand pending)",
    ),
]

TIER_B_PATTERNS = [
    (
        re.compile(CMD_BOUNDARY + r"pio\b[^;&|]*\bdevice\b\s+monitor\b"),
        "pio device monitor",
        f"{WRAPPER_PATH} monitor <device>",
    ),
    (
        re.compile(CMD_BOUNDARY + r"meshtastic\b[^;&|]*--(?:info|get|export-config)\b"),
        "meshtastic --info/--get/--export-config",
        f"{WRAPPER_PATH} info <device>",
    ),
]

# The wrapper itself, when invoked as a token in the command, bypasses the
# hook. This is how the wrapper internally calls pio/esptool without the
# hook re-firing on those calls (the wrapper invokes them via Python
# subprocess outside Claude Code's Bash tool, but if a user manually pipes
# something through the wrapper, the token match still passes).
WRAPPER_TOKEN = re.compile(r"\bpio-flash\b")


def parse_event() -> dict:
    """Read JSON event from stdin. Fail-closed on parse error."""
    raw = sys.stdin.read()
    try:
        return json.loads(raw)
    except json.JSONDecodeError as e:
        print(
            f"BLOCK: block-raw-flash hook could not parse PreToolUse JSON: {e}\n"
            f"Raw input (first 200 chars): {raw[:200]!r}",
            file=sys.stderr,
        )
        sys.exit(2)


def main() -> int:
    event = parse_event()

    if event.get("tool_name") != "Bash":
        return 0

    cmd = (event.get("tool_input") or {}).get("command", "")
    if not cmd:
        return 0

    # Authorized wrapper invocations pass through unconditionally.
    if WRAPPER_TOKEN.search(cmd):
        return 0

    # Tier A: state-changing, blocked outright.
    for rx, name, redirect in TIER_A_PATTERNS:
        if rx.search(cmd):
            print(
                "BLOCK (Tier A - state-changing command): " + name + "\n"
                f"\n"
                f"Refusing raw invocation per project flash discipline.\n"
                f"This command resets the chip on every invocation and can write flash.\n"
                f"\n"
                f"Use the wrapper instead:\n"
                f"  {redirect}\n"
                f"\n"
                f"The wrapper enforces:\n"
                f"  - Tier 0 VID:PID class check against hardware-devices.yaml\n"
                f"  - DeviceID instance hash match against the named device\n"
                f"  - Two-stage preview+confirm with a 5-min token (for flash)\n"
                f"\n"
                f"See: C:\\Dev\\LoRa\\CLAUDE.md Flashing Discipline section,\n"
                f"     C:\\Dev\\LoRa\\proposal-flash-discipline.md\n"
                f"\n"
                f"Original command was:\n"
                f"  " + cmd,
                file=sys.stderr,
            )
            return 2

    # Tier B: port-opening read-only, blocked with softer redirect message.
    for rx, name, redirect in TIER_B_PATTERNS:
        if rx.search(cmd):
            print(
                "BLOCK (Tier B - port-opening command): " + name + "\n"
                f"\n"
                f"Port-opening commands require identity verification first, even if\n"
                f"they do not reset the chip on open. Use the wrapper:\n"
                f"\n"
                f"  {redirect}\n"
                f"\n"
                f"The wrapper enforces the Tier 0 VID:PID class check and DeviceID\n"
                f"instance hash match, then exec's the underlying command on the\n"
                f"verified port. No token chaining required for Tier B.\n"
                f"\n"
                f"Original command was:\n"
                f"  " + cmd,
                file=sys.stderr,
            )
            return 2

    # All other commands (class 0 enumeration, build, edit, git, etc.) pass through.
    return 0


if __name__ == "__main__":
    sys.exit(main())
