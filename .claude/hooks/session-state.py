#!/usr/bin/env python3
"""session-state.py — compaction recovery state management.

Canonical implementation per REPOCONFIG.md. Snapshots volatile session state
that the post-compaction summary doesn't preserve reliably (claimed Citadel
tasks, active worktree, Agent Mail identity, open PRs the session owns, last
Tier 2 approval, agent investigation notes).

Read at session start by preflight.sh §6, written by post-commit hook and by
the agent on demand.

Subcommands:
    read                        Print human-readable summary to stdout.
    write                       Snapshot live state into .claude/agent-session.json.
    clear                       Remove the state file.
    note <text>                 Append a freeform timestamped note.
    identity <name>             Set agent_mail_identity.
    approval <action...>        Record a Tier 2 approval ("merge PR #N", etc.).

State file schema v1 (.claude/agent-session.json):
    {
      "version": 1,
      "project": "<name>",
      "last_updated": "ISO8601 UTC",
      "agent_mail_identity": "<window identity>",
      "active_worktree": {"path": "...", "branch": "..."},
      "claimed_tasks": [{"task_id": "...", "external_issue_number": N,
                         "claimed_at": "..."}],
      "open_prs": [{"number": N, "title": "...", "branch": "..."}],
      "last_tier2_approval": {"action": "...", "approved_at": "..."} | null,
      "notes": "freeform"
    }

Failure mode: every subcommand is best-effort. If dw, gh, or git are
missing, or any sub-query fails, degrade to whatever data IS available and
write the file anyway. Never raises out of the hook context — the calling
git operation must not fail because of state-snapshot trouble.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

SCHEMA_VERSION = 1


def iso_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def project_root() -> Path:
    """Resolve the project directory: git toplevel if available, else cwd.

    Note: in a git worktree, --show-toplevel returns the worktree path, not
    the main repo path. The state file lives in the worktree's .claude/
    (each worktree gets its own snapshot, which is what we want for tracking
    *that* worktree's branch and PR state).
    """
    env = os.environ.get("PROJECT_DIR")
    if env:
        return Path(env)
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            check=True, capture_output=True, text=True, timeout=5,
        ).stdout.strip()
        if out:
            return Path(out)
    except Exception:
        pass
    return Path.cwd()


def project_name(root: Path) -> str:
    """Resolve the Citadel project name.

    Precedence:
      1. $DW_PROJECT env var (most reliable; user-set per session)
      2. Parent-of-common-dir basename (correct in worktrees: --git-common-dir
         points at the MAIN .git/, whose parent is the repo root)
      3. git toplevel basename (correct in main checkout, wrong in worktrees)
      4. cwd basename (final fallback)
    """
    env = os.environ.get("DW_PROJECT")
    if env:
        return env

    # Worktree-aware: --git-common-dir resolves to the main .git/ even when
    # invoked from inside a worktree. Its parent is the main repo root.
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
            check=True, capture_output=True, text=True, timeout=5,
            cwd=str(root),
        ).stdout.strip()
        if out:
            common = Path(out)
            # If the git common dir is named ".git", its parent is the repo
            # root. (Worktrees keep .git as a file pointing at the main one.)
            if common.name == ".git":
                return common.parent.name
            # Bare repo or unusual layout — fall through.
    except Exception:
        pass

    if root.name:
        return root.name
    return "unknown"


def state_file(root: Path) -> Path:
    return root / ".claude" / "agent-session.json"


def load_state(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}


def save_state(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data["last_updated"] = iso_now()
    path.write_text(
        json.dumps(data, indent=2, sort_keys=False) + "\n",
        encoding="utf-8",
    )


def run_capture(cmd: list[str], timeout: int = 10) -> str | None:
    """Run a command, return stdout on success, None on any failure."""
    try:
        res = subprocess.run(
            cmd, check=True, capture_output=True, text=True, timeout=timeout,
        )
        return res.stdout
    except Exception:
        return None


def query_claimed_tasks(project: str) -> list[dict]:
    """`dw --project <p> list --status in_progress --json` -> normalized list."""
    if project == "unknown":
        return []
    raw = run_capture(["dw", "--json", "--project", project, "list",
                       "--status", "in_progress"])
    if not raw:
        return []
    try:
        items = json.loads(raw)
    except Exception:
        return []
    out = []
    for t in items if isinstance(items, list) else []:
        out.append({
            "task_id": t.get("short_id") or t.get("id") or "",
            "title": t.get("title", ""),
            "external_issue_number": t.get("external_issue_number") or 0,
            "claimed_at": (t.get("claimed_at") or t.get("updated_at")
                           or t.get("created_at") or ""),
        })
    return out


def query_open_prs() -> list[dict]:
    """`gh pr list --author @me --state open --json ...` -> normalized list."""
    raw = run_capture(["gh", "pr", "list", "--author", "@me",
                       "--state", "open",
                       "--json", "number,title,headRefName"])
    if not raw:
        return []
    try:
        items = json.loads(raw)
    except Exception:
        return []
    out = []
    for p in items if isinstance(items, list) else []:
        out.append({
            "number": p.get("number"),
            "title": p.get("title", ""),
            "branch": p.get("headRefName", ""),
        })
    return out


def query_worktree(root: Path) -> dict:
    branch = run_capture(["git", "-C", str(root), "rev-parse",
                          "--abbrev-ref", "HEAD"])
    return {
        "path": str(root),
        "branch": branch.strip() if branch else "",
    }


# ─── subcommands ─────────────────────────────────────────────────────────

def cmd_read(args: list[str]) -> int:
    root = project_root()
    path = state_file(root)
    if not path.exists():
        # No state file = nothing to surface. preflight §6 treats empty
        # stdout as "no active session" — correct for first-run.
        return 0
    data = load_state(path)
    if not data:
        return 0

    print(f"Last snapshot: {data.get('last_updated', 'unknown')}")
    identity = data.get("agent_mail_identity") or ""
    if identity:
        print(f"Agent Mail identity: {identity}")
    wt = data.get("active_worktree") or {}
    if wt.get("path"):
        print(f"Active worktree: {wt.get('branch', '?')}  ({wt['path']})")
    tasks = data.get("claimed_tasks") or []
    if tasks:
        print(f"Claimed Citadel tasks ({len(tasks)}):")
        for t in tasks:
            issue = t.get("external_issue_number") or 0
            issue_str = f"#{issue}" if issue else "no-issue"
            print(f"  - {t.get('task_id', '?')} ({issue_str}) "
                  f"claimed {t.get('claimed_at', '?')}")
    prs = data.get("open_prs") or []
    if prs:
        print(f"Open PRs ({len(prs)}):")
        for p in prs:
            print(f"  - #{p.get('number', '?')} {p.get('title', '')}  "
                  f"({p.get('branch', '')})")
    approval = data.get("last_tier2_approval")
    if approval and approval.get("action"):
        print(f"Last Tier 2 approval: {approval['action']}  "
              f"({approval.get('approved_at', '?')})")
    notes = data.get("notes") or ""
    if notes:
        print("Notes:")
        for line in notes.splitlines():
            print(f"  {line}")
    return 0


def cmd_write(args: list[str]) -> int:
    root = project_root()
    path = state_file(root)
    name = project_name(root)

    prior = load_state(path)

    data = {
        "version": SCHEMA_VERSION,
        "project": name,
        "last_updated": iso_now(),
        "agent_mail_identity": prior.get("agent_mail_identity", ""),
        "active_worktree": query_worktree(root),
        "claimed_tasks": query_claimed_tasks(name),
        "open_prs": query_open_prs(),
        "last_tier2_approval": prior.get("last_tier2_approval"),
        "notes": prior.get("notes", ""),
    }
    save_state(path, data)
    print(f"Wrote {path}")
    return 0


def cmd_clear(args: list[str]) -> int:
    path = state_file(project_root())
    if path.exists():
        path.unlink()
        print(f"Cleared {path}")
    return 0


def cmd_note(args: list[str]) -> int:
    if not args:
        print("Usage: session-state.py note <text>", file=sys.stderr)
        return 1
    root = project_root()
    path = state_file(root)
    text = " ".join(args)
    now = iso_now()
    data = load_state(path)
    if not data:
        data = {
            "version": SCHEMA_VERSION,
            "project": project_name(root),
            "agent_mail_identity": "",
            "active_worktree": {},
            "claimed_tasks": [],
            "open_prs": [],
            "last_tier2_approval": None,
            "notes": "",
        }
    existing = data.get("notes") or ""
    entry = f"{now}: {text}"
    data["notes"] = f"{existing}\n{entry}" if existing else entry
    save_state(path, data)
    print("Note added")
    return 0


def cmd_identity(args: list[str]) -> int:
    if not args:
        print("Usage: session-state.py identity <name>", file=sys.stderr)
        return 1
    root = project_root()
    path = state_file(root)
    data = load_state(path)
    if not data:
        data = {
            "version": SCHEMA_VERSION,
            "project": project_name(root),
            "active_worktree": {},
            "claimed_tasks": [],
            "open_prs": [],
            "last_tier2_approval": None,
            "notes": "",
        }
    data["agent_mail_identity"] = args[0]
    save_state(path, data)
    print(f"Identity set: {args[0]}")
    return 0


def cmd_approval(args: list[str]) -> int:
    if not args:
        print("Usage: session-state.py approval <action description>",
              file=sys.stderr)
        return 1
    root = project_root()
    path = state_file(root)
    action = " ".join(args)
    now = iso_now()
    data = load_state(path)
    if not data:
        data = {
            "version": SCHEMA_VERSION,
            "project": project_name(root),
            "agent_mail_identity": "",
            "active_worktree": {},
            "claimed_tasks": [],
            "open_prs": [],
            "notes": "",
        }
    data["last_tier2_approval"] = {"action": action, "approved_at": now}
    save_state(path, data)
    print(f"Approval recorded: {action}")
    return 0


COMMANDS = {
    "read": cmd_read,
    "write": cmd_write,
    "clear": cmd_clear,
    "note": cmd_note,
    "identity": cmd_identity,
    "approval": cmd_approval,
}


def main() -> int:
    argv = sys.argv[1:]
    if not argv:
        # Default subcommand = read (so preflight can call without args).
        return cmd_read([])
    sub = argv[0]
    rest = argv[1:]
    if sub not in COMMANDS:
        print(f"Usage: {sys.argv[0]} {{{'|'.join(COMMANDS)}}}",
              file=sys.stderr)
        return 1
    try:
        return COMMANDS[sub](rest)
    except Exception as e:
        # Defensive: never raise out of a hook context.
        print(f"session-state.py {sub}: {e}", file=sys.stderr)
        return 0


if __name__ == "__main__":
    sys.exit(main())
