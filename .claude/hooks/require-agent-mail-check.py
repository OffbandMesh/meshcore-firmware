#!/usr/bin/env python3
"""
.claude/hooks/require-agent-mail-check.py - PreToolUse hook for Claude Code

Intercepts the Bash tool's command input and refuses state-mutating
git/gh/dw operations when the Agent Mail acknowledgment file
(~/.claude/agent_mail_last_check) is missing or older than
AGENT_MAIL_CHECK_TTL seconds (default 600 = 10 min).

Tracks: Strycher/LoRa#249

Why this exists
---------------
In the 2026-05-25 session, the user called out three separate times that I
was not using Agent Mail to coordinate with other concurrent sessions.
Each behavioral apology + commitment produced no change because the
discipline had no mechanical trigger - it relied on me remembering to
check inbox at every milestone, and the work-flow momentum reliably
overrode that intent.

block-raw-flash.py and block-raw-curl-ota.py are the precedent: SAFELANE
"mechanical block does not degrade under momentum." This hook applies the
same principle to the Agent Mail coordination requirement.

Behavior
--------
- Reads PreToolUse JSON event from stdin.
- Only acts on tool_name == "Bash".
- Pattern-matches the command string for state-mutating git/gh/dw
  operations (see STATE_MUTATING / READ_ONLY tables below).
- If a state-mutating pattern matches and the ack file is stale (or
  missing), refuses with exit 2 and an instructive stderr message.
- Honors DW_SKIP_AGENT_MAIL_CHECK=1 env var for genuine emergencies
  (matches DW_SKIP_CITADEL_CHECK convention from CLAUDE-BASE). Bypass is
  logged to stderr so the user sees it in real-time.
- Fail-OPEN on JSON parse errors or unexpected exceptions (don't break
  legitimate work because of a hook bug - matches existing hook
  convention).

The leading anchor (CMD_BOUNDARY) requires a real shell command
separator (`;`, `&`, `|`, backtick, `$(`) before the tool word, so
trigger words appearing inside quoted argument strings of unrelated
commands (commit messages, issue close reasons, etc.) pass through
cleanly.

How the agent refreshes the ack
-------------------------------
After running Agent Mail tools (fetch_inbox / list_window_identities /
similar), the agent runs (in a Bash tool call):

    echo $(date +%s) > ~/.claude/agent_mail_last_check

That single command is in the READ_ONLY_PATTERNS table below so it
doesn't itself trigger a recursive check.

Exit codes
----------
0  pass through to the underlying Bash tool
2  block AND surface stderr to the agent (REFUSAL)
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
from pathlib import Path

ACK_FILE = Path.home() / ".claude" / "agent_mail_last_check"
DEFAULT_TTL_SECONDS = 600  # 10 minutes
BYPASS_ENV_VAR = "DW_SKIP_AGENT_MAIL_CHECK"

# Shell-command-boundary anchor - matches start of line OR after a real
# shell command separator. Adopted from block-raw-flash.py.
#
# Whitespace alone is NOT enough - whitespace happens inside string args.
CMD_BOUNDARY = r"(?:^|[;&|`]\s*|(?:&&|\|\|)\s*|\$\(\s*)"

# Inline env var assignments before the tool word, e.g. `FOO=bar BAZ=qux dw close`.
# Matches zero or more "KEY=value " tokens immediately after CMD_BOUNDARY but
# before the tool name. Without this, `DW_PROJECT=LoRa dw close ...` slips past
# the hook even though it IS a state-mutating dw operation.
ENV_PREFIX = r"(?:[A-Za-z_][A-Za-z0-9_]*=\S*\s+)*"

# State-mutating commands. Order matters only for documentation - the
# hook checks all of these and blocks on first match.
STATE_MUTATING_PATTERNS = [
    # git
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+commit\b"), "git commit"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+push\b"), "git push"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+tag\s+-a\b"), "git tag -a (annotated tag create)"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+rebase\b"), "git rebase"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+worktree\s+add\b"), "git worktree add"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+merge\b"), "git merge"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+reset\s+--hard\b"), "git reset --hard"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+cherry-pick\b"), "git cherry-pick"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+revert\b"), "git revert"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+branch\s+(?:-D|--delete\s+--force|--force)\b"),
     "git branch destructive (-D / --force / --delete --force)"),
    # gh (GitHub CLI) - mutating operations
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"gh\s+pr\s+(?:create|merge|edit|close|reopen|ready)\b"),
     "gh pr create/merge/edit/close/reopen/ready"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"gh\s+issue\s+(?:create|edit|close|reopen)\b"),
     "gh issue create/edit/close/reopen"),
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"gh\s+api\s+(?:-X\s+)?(?:PUT|PATCH|POST|DELETE)\b"),
     "gh api PUT/PATCH/POST/DELETE"),
    # dw (Citadel CLI) - mutating operations
    (re.compile(CMD_BOUNDARY + ENV_PREFIX + r"dw\s+(?:close|create|claim|status|depends)\b"),
     "dw close/create/claim/status/depends"),
]

# Read-only patterns. If a command matches ANY of these, it passes
# through regardless of the ack file state. Keeps the hook from
# blocking ordinary status checks and the ack-refresh command itself.
READ_ONLY_PATTERNS = [
    re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+(?:log|diff|status|show|describe|rev-parse|rev-list|fetch|branch\s*$|worktree\s+list|tag\s+-l|tag\s*$|config\s+--get)"),
    re.compile(CMD_BOUNDARY + ENV_PREFIX + r"git\s+pull\s+--ff-only\b"),
    re.compile(CMD_BOUNDARY + ENV_PREFIX + r"gh\s+(?:pr|issue|repo)\s+(?:view|list)\b"),
    re.compile(CMD_BOUNDARY + ENV_PREFIX + r"gh\s+api\s+(?!\-X\s+(?:PUT|PATCH|POST|DELETE))"),  # gh api without explicit mutating method
    re.compile(CMD_BOUNDARY + ENV_PREFIX + r"dw\s+(?:list|show|ready|stats|projects)\b"),
    # The ack-refresh command itself - explicitly allowed so the agent
    # can write the ack file without recursive blocking.
    re.compile(r"echo\s+\$\(date\s+\+%s\)\s*>\s*~/.claude/agent_mail_last_check"),
    re.compile(r"echo\s+\$\(date\s+\+%s\)\s*>\s*\$HOME/.claude/agent_mail_last_check"),
]


def get_ttl_seconds() -> int:
    """Return the configured TTL, falling back to DEFAULT_TTL_SECONDS."""
    raw = os.environ.get("AGENT_MAIL_CHECK_TTL")
    if raw is None:
        return DEFAULT_TTL_SECONDS
    try:
        v = int(raw)
        if v < 0:
            return DEFAULT_TTL_SECONDS
        return v
    except (TypeError, ValueError):
        return DEFAULT_TTL_SECONDS


def check_ack_freshness() -> tuple[bool, str]:
    """
    Return (is_fresh, detail_message).
    """
    if not ACK_FILE.exists():
        return False, f"ack file does not exist: {ACK_FILE}"
    try:
        ts_str = ACK_FILE.read_text(encoding="utf-8").strip()
        last_check = int(ts_str)
    except (OSError, ValueError) as e:
        return False, f"ack file unreadable or malformed: {e}"
    age = int(time.time() - last_check)
    ttl = get_ttl_seconds()
    if age > ttl:
        return False, f"ack file age {age}s > TTL {ttl}s"
    return True, f"ack file age {age}s (within TTL {ttl}s)"


def main() -> int:
    try:
        evt = json.load(sys.stdin)
    except json.JSONDecodeError:
        # Fail-OPEN on hook bugs - don't break legitimate work.
        return 0
    except Exception:
        return 0

    if evt.get("tool_name") != "Bash":
        return 0

    cmd = (evt.get("tool_input") or {}).get("command", "")
    if not cmd or not isinstance(cmd, str):
        return 0

    # Read-only commands always pass through.
    for ro_pat in READ_ONLY_PATTERNS:
        if ro_pat.search(cmd):
            return 0

    # Is the command state-mutating?
    matched_mutation = None
    for mut_pat, label in STATE_MUTATING_PATTERNS:
        if mut_pat.search(cmd):
            matched_mutation = label
            break

    if matched_mutation is None:
        # Not a tracked mutation - pass through.
        return 0

    # Honor bypass env var with stderr log.
    if os.environ.get(BYPASS_ENV_VAR) == "1":
        print(
            f"[require-agent-mail-check] BYPASS: {BYPASS_ENV_VAR}=1 for command "
            f"({matched_mutation}): {cmd[:80]}",
            file=sys.stderr,
        )
        return 0

    fresh, detail = check_ack_freshness()
    if fresh:
        return 0

    # BLOCK.
    print(
        f"[require-agent-mail-check] BLOCK: state-mutating operation requires "
        f"a fresh Agent Mail check.\n"
        f"  Matched pattern: {matched_mutation}\n"
        f"  Command (first 120 chars): {cmd[:120]}\n"
        f"  Ack state: {detail}\n"
        f"  To proceed:\n"
        f"    1. Run Agent Mail tools (e.g. list_window_identities + fetch_inbox)\n"
        f"       via the MCP agent-mail server in this session.\n"
        f"    2. Refresh the ack file:\n"
        f"         echo $(date +%s) > ~/.claude/agent_mail_last_check\n"
        f"    3. Retry the command.\n"
        f"  Emergency bypass: {BYPASS_ENV_VAR}=1 (logged to stderr).\n"
        f"  Tracks: Strycher/LoRa#249",
        file=sys.stderr,
    )
    return 2


if __name__ == "__main__":
    sys.exit(main())
