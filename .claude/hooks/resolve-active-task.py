#!/usr/bin/env python3
r"""
resolve-active-task.py - deterministic "which task is this work for?" resolver.

Shared by the canonical pre-commit / pre-push / preflight hooks so they all agree
on THE active task, instead of grabbing whichever in_progress task Citadel returns
first. That coarse "any in_progress" check misreports the task when two are open
AND is exploitable (the gate can be satisfied by an unrelated stale claim) -
Strycher/LoRa#241, DifferentWire/standards#172.

Reads the project's in_progress tasks as a JSON list on stdin (the hook does the
curl; keeping I/O in the hook leaves this pure and unit-testable). Resolves in
priority order:

  1. --dw-task <short_id>   explicit per-worktree override (DW_TASK env)
  2. --branch-issue <N>     from a `<type>/<N>-...` branch -> in_progress task
                            whose external_issue_number == N
  3. exactly one in_progress task -> that task (unambiguous)
  4. otherwise -> AMBIGUOUS; the caller FAILS CLOSED (standards#172 decision)

The issue-link requirement (--require-issue-link) is then applied to the SELECTED
task only - never to "some other in_progress task".

Output: one line on stdout (first word is the code) + a matching exit code so the
tool is usable both from a shell `case` and directly:

  OK <short_id> <#issue|->     0    resolved + satisfies the issue-link rule
  NO_TASK                      10   no in_progress task at all
  NO_ISSUE <short_id>          11   resolved, but strict mode + no issue link
  AMBIGUOUS <n> <branch|->     12   multiple in_progress, none matched -> block
  DW_TASK_BAD <short_id>       13   DW_TASK is not an in_progress task
  BRANCH_NO_MATCH <N>          14   branch implies #N but no in_progress task linked
  AMBIGUOUS_ISSUE <N>          15   two in_progress tasks linked to the same issue #N
  BAD_INPUT                    1    stdin was not a JSON list of tasks
"""
from __future__ import annotations

import argparse
import json
import sys


def _issue_of(task: dict) -> str:
    v = task.get("external_issue_number")
    return str(v) if v not in (None, "") else ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dw-task", default="")
    ap.add_argument("--branch-issue", default="")
    ap.add_argument("--require-issue-link", default="true")
    args = ap.parse_args()

    require = args.require_issue_link.strip().lower() == "true"
    dw_task = args.dw_task.strip()
    branch_issue = args.branch_issue.strip()

    try:
        tasks = json.load(sys.stdin)
        if not isinstance(tasks, list):
            raise ValueError("expected a JSON list of tasks")
    except Exception:
        print("BAD_INPUT")
        return 1

    inprog = [t for t in tasks if isinstance(t, dict) and t.get("status") == "in_progress"]
    if not inprog:
        print("NO_TASK")
        return 10

    # 1. Explicit per-worktree override wins, but must be a real in_progress task.
    if dw_task:
        match = [t for t in inprog if t.get("short_id") == dw_task]
        if not match:
            print(f"DW_TASK_BAD {dw_task}")
            return 13
        selected = match[0]
    # 2. Branch issue number -> the in_progress task linked to it.
    elif branch_issue:
        match = [t for t in inprog if _issue_of(t) == branch_issue]
        if not match:
            print(f"BRANCH_NO_MATCH {branch_issue}")
            return 14
        if len(match) > 1:
            # Two in_progress tasks linked to the same issue -> still ambiguous;
            # don't silently pick one. Disambiguate with DW_TASK.
            print(f"AMBIGUOUS_ISSUE {branch_issue}")
            return 15
        selected = match[0]
    # 3. Exactly one in_progress task is unambiguous.
    elif len(inprog) == 1:
        selected = inprog[0]
    # 4. Multiple, with no deterministic signal -> fail closed (caller blocks).
    else:
        print(f"AMBIGUOUS {len(inprog)} {branch_issue or '-'}")
        return 12

    iss = _issue_of(selected)
    sid = selected.get("short_id", "?")
    if require and not iss:
        print(f"NO_ISSUE {sid}")
        return 11
    print(f"OK {sid} {('#' + iss) if iss else '-'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
