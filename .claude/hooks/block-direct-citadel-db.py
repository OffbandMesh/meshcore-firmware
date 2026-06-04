#!/usr/bin/env python3
"""block-direct-citadel-db.py — PreToolUse hook for Claude Code.

Refuses raw database access to the Citadel Postgres instance. The supported
paths to interact with Citadel are:

    - dw (the CLI)
    - curl / httpx against https://getunfocused.app/citadel (the HTTP API)

Going around those bypasses the audit trail, the hook stack (require-knowledge-
update, branch-binding checks), and Citadel's invariants. Going around once
normalizes going around always. File the gap, then wait for the CLI/API fix
to land.

Tracks: DifferentWire/standards#72, parent epic DifferentWire/standards#69.

Behavior
--------
- Reads PreToolUse JSON event from stdin.
- Only acts on tool_name == "Bash".
- Pattern-matches the command against known direct-DB-access shapes.
- Refuses with exit 2 (Claude Code surfaces stderr to the model) when matched.
- All other commands pass through (exit 0).
- Fail-closed on JSON parse error: if input is unparsable, block.
- Bypass: set DW_SKIP_DB_BLOCK=1 in env (logged to stderr, every use is an
  incident that should be filed).

Blocked patterns
----------------
1. psql / pg_dump / pg_restore targeting any of:
     - 46.224.181.82             (Hetzner IP)
     - getunfocused.app          (public hostname)
     - docker-db-1               (docker container name)
     - citadel-db                (alternative container name)
     - postgres://...:5432/citadel  (connection URI with citadel db name)
     - /citadel  (database-name argument when other signals confirm Citadel)

2. ssh unfocused@46.224.181.82 "...psql..." / "...pg_dump..."  (SSH-wrapped)

3. docker exec docker-db-1 psql ...  /  docker exec citadel-db psql ...
   (docker exec into the DB container followed by psql/dump)

Pass-through (always allowed)
-----------------------------
- dw   (the supported CLI — any subcommand)
- curl (any URL, including https://getunfocused.app/citadel — the supported HTTP API)
- ssh unfocused@46.224.181.82 "..."  WITHOUT psql/pg_dump/pg_restore  (e.g.,
  update.sh, docker ps, log inspection — legitimate ops)
- psql to non-Citadel hosts (localhost, other instances, test DBs)
- Anything that doesn't match the patterns above

Exit codes
----------
0  pass through to the underlying Bash tool
2  block AND surface stderr to the agent
"""
from __future__ import annotations

import json
import os
import re
import sys

# ─── Citadel target signals ──────────────────────────────────────────────
# These strings identify a connection to the Citadel database. ANY of these
# appearing in a psql/pg_dump/pg_restore command qualifies it as "Citadel
# direct access" and blocks.
CITADEL_HOSTS = (
    "46.224.181.82",
    "getunfocused.app",
    "docker-db-1",
    "citadel-db",
)

# Connection URI pattern: postgres://...host.../citadel or postgresql://...
# Matches whether or not creds are present, and whether the db name is the
# last path segment or has a trailing query string.
CONN_URI_CITADEL = re.compile(
    r"postgres(?:ql)?://[^\s'\"`;|&]*?(?:/citadel\b|@(?:" +
    "|".join(re.escape(h) for h in CITADEL_HOSTS) +
    r")[^\s'\"`;|&]*)"
)

# ─── Command boundary anchor ─────────────────────────────────────────────
# Matches the START of a shell command, NOT arbitrary whitespace. Prevents
# false positives when "psql" or "pg_dump" appears inside a quoted argument
# (e.g., a commit message: `git commit -m "added psql query"` should pass).
#
# A real psql invocation appears at one of:
#   - The very start of the command line
#   - After a command separator: ; & | && ||
#   - After a backtick or $(...) command substitution opener
CMD_BOUNDARY = r"(?:^|[;&|`]\s*|(?:&&|\|\|)\s*|\$\(\s*)"

# ─── Direct-tool patterns ────────────────────────────────────────────────
# These detect a local psql/pg_dump/pg_restore invocation. The match itself
# is just the tool token at a real command boundary; whether it's blocked
# depends on whether the rest of the command targets Citadel (see below).
PSQL_TOOL = re.compile(CMD_BOUNDARY + r"(?:psql|pg_dump|pg_restore)\b")

# ─── SSH-wrapped patterns ────────────────────────────────────────────────
# Block: ssh unfocused@46.224.181.82 "<anything that runs psql/pg_dump>"
# Allow: ssh unfocused@46.224.181.82 "<anything else>"
#
# We look for an SSH command targeting the Citadel host, then check if the
# *quoted command body* contains a psql/pg_dump/pg_restore tool word.
SSH_CITADEL = re.compile(
    CMD_BOUNDARY +
    r"ssh\s+[^\s]*@(?:" + "|".join(re.escape(h) for h in CITADEL_HOSTS) + r")\b"
)
SSH_PSQL_INSIDE = re.compile(r"\b(?:psql|pg_dump|pg_restore)\b")

# ─── docker exec patterns ────────────────────────────────────────────────
# Block: docker exec docker-db-1 psql ...    (and citadel-db variant)
DOCKER_EXEC_DB = re.compile(
    CMD_BOUNDARY +
    r"docker\s+exec\s+(?:-[^\s]+\s+)*(?:docker-db-1|citadel-db)\b[^\n]*?\b(?:psql|pg_dump|pg_restore)\b"
)


def is_citadel_target(cmd: str) -> bool:
    """Return True if the local psql/pg_dump/pg_restore invocation targets Citadel.

    Heuristic — checks for any Citadel signal in the command tail:
      - Citadel hostname (including container names)
      - postgres:// URI pointing at Citadel
      - bare 'citadel' database name combined with -h/-d/--host/--dbname/-U flags
        targeting one of the known signals

    Returns False if we can't confirm Citadel — fail-open on uncertainty so
    that legitimate non-Citadel psql work (test DBs, sample data, training)
    isn't blocked.
    """
    for host in CITADEL_HOSTS:
        if host in cmd:
            return True
    if CONN_URI_CITADEL.search(cmd):
        return True
    # Bare `psql citadel` (db name as positional) is ambiguous on its own —
    # other databases might be named `citadel` too. Only trip if it's
    # combined with another Citadel signal we already failed to catch above,
    # which would be unusual. We DON'T trip on bare db name alone.
    return False


def parse_event() -> dict:
    """Read JSON event from stdin. Fail-closed on parse error."""
    raw = sys.stdin.read()
    try:
        return json.loads(raw)
    except json.JSONDecodeError as e:
        print(
            f"BLOCK: block-direct-citadel-db hook could not parse PreToolUse JSON: {e}\n"
            f"Raw input (first 200 chars): {raw[:200]!r}",
            file=sys.stderr,
        )
        sys.exit(2)


BLOCK_MESSAGE = """\
✗ Direct Citadel DB access blocked.

Triggered pattern: {pattern}
Command:
  {cmd}

If `dw` is missing a capability you need, surface the gap — do not work around it.

To file the gap:
  gh issue create --repo DifferentWire/citadel \\
    --title "dw missing: <capability>" --label "type:task,priority:P1"

  dw --project Citadel create "dw missing: <capability>" --issue <N>

Direct DB access bypasses the audit trail, hooks, and consistency guarantees
that make Citadel trustworthy. Going around it once normalizes going around
it always. File the gap, then wait for the CLI/API fix to land.

Emergency bypass (logged): DW_SKIP_DB_BLOCK=1 <command>
"""


def main() -> int:
    event = parse_event()

    if event.get("tool_name") != "Bash":
        return 0

    cmd = (event.get("tool_input") or {}).get("command", "")
    if not cmd:
        return 0

    # Bypass — emergency only, logged to stderr so it's visible in the
    # terminal and shows up in any session-review audit trail.
    if os.environ.get("DW_SKIP_DB_BLOCK") == "1":
        print(
            "⚠ DB BLOCK BYPASSED — DW_SKIP_DB_BLOCK=1\n"
            f"  Command: {cmd}\n"
            "  This bypass is logged. Every use should be a filed incident.",
            file=sys.stderr,
        )
        return 0

    # Pattern 1: docker exec into the Citadel DB container with psql/dump.
    # Check this BEFORE the bare PSQL_TOOL check because the latter would
    # also match.
    if DOCKER_EXEC_DB.search(cmd):
        print(
            BLOCK_MESSAGE.format(
                pattern="docker exec into Citadel DB container (docker-db-1 / citadel-db)",
                cmd=cmd,
            ),
            file=sys.stderr,
        )
        return 2

    # Pattern 2: SSH to Citadel host with embedded psql/pg_dump/pg_restore.
    if SSH_CITADEL.search(cmd) and SSH_PSQL_INSIDE.search(cmd):
        print(
            BLOCK_MESSAGE.format(
                pattern="ssh to Citadel host with embedded psql/pg_dump/pg_restore",
                cmd=cmd,
            ),
            file=sys.stderr,
        )
        return 2

    # Pattern 3: Local psql / pg_dump / pg_restore targeting Citadel.
    # Only block if Citadel signals are present — pass through psql work
    # against other databases (testing, training, local sample data).
    if PSQL_TOOL.search(cmd) and is_citadel_target(cmd):
        print(
            BLOCK_MESSAGE.format(
                pattern="local psql/pg_dump/pg_restore targeting Citadel",
                cmd=cmd,
            ),
            file=sys.stderr,
        )
        return 2

    # All other commands pass through (dw, curl, non-Citadel psql, regular
    # SSH ops, git, edit, etc.).
    return 0


if __name__ == "__main__":
    sys.exit(main())
