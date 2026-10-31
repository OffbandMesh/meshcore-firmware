#!/usr/bin/env python3
"""Keep Citadel tasks in step with GitHub issue closes (#1933, epic #1932).

Runs one way, GitHub to Citadel. Closing an issue is the owner-authorized step,
so a finished task never closes an issue here: many issues carry several tasks,
and one finished task does not mean the issue is done.

  close --issue N --state-reason R   an issue just closed; close its tasks
  reconcile --report FILE            catch missed closes across every issue, and
                                     write what it will not change to FILE

Both exit 1 if any write failed, so the workflow run goes red, and both are safe
to run twice. The Citadel address comes from CITADEL_URL, never from this file:
this repository is public.

curl rather than urllib: Citadel's CDN answered 403 to Python's default client
when curl got through from the same machine.
"""

import argparse
import json
import os
import subprocess
import sys
from urllib.parse import quote

PROJECT = "Crosswire"

# Strycher/Crosswire is this repository's name before #107. It redirects here, so
# its issue numbers are this repository's, and 78 tasks still link through it.
THIS_REPO_PREFIXES = (
    "https://github.com/OffbandMesh/meshcore-firmware/issues/",
    "https://github.com/Strycher/Crosswire/issues/",
)

FINISHED = {"done", "closed"}
CHANGED_BY = "github-sync"

# More closes than this in one nightly run means something is wrong with the data
# rather than a few missed events, and closes are not cheap to undo by hand.
MAX_RECONCILE_CLOSES = 25

TASK_PAGE_LIMIT = 5000


class SyncError(Exception):
    pass


def curl_json(args):
    """Run curl and return (status, parsed body). The address stays out of errors."""
    try:
        out = subprocess.run(
            ["curl", "-sS", "--max-time", "30", "-w", "\n%{http_code}", *args],
            capture_output=True, text=True, encoding="utf-8",
        )
    except OSError as e:
        raise SyncError(f"curl did not run: {type(e).__name__}")
    if out.returncode != 0:
        raise SyncError(f"curl exit {out.returncode}")
    body, _, status = out.stdout.rpartition("\n")
    try:
        return int(status), json.loads(body) if body.strip() else None
    except ValueError:
        raise SyncError(f"HTTP {status}, body is not JSON")


class Citadel:
    def __init__(self, base_url, request=curl_json):
        self.base = f"{base_url.rstrip('/')}/projects/{PROJECT}/tasks"
        self.request = request

    def _get(self, query):
        status, body = self.request([f"{self.base}?{query}"])
        if status != 200 or not isinstance(body, list):
            raise SyncError(f"task query got HTTP {status}")
        return body

    def tasks_for_issue(self, number):
        return self._get(f"external_issue_number={number}")

    def all_tasks(self):
        tasks = self._get(f"limit={TASK_PAGE_LIMIT}")
        # A full page may be a cut-off list, and the report would then call issues
        # taskless when their tasks are simply past the end.
        if len(tasks) >= TASK_PAGE_LIMIT:
            raise SyncError(f"task list reached the {TASK_PAGE_LIMIT} limit")
        return tasks

    def close(self, short_id, reason):
        url = f"{self.base}/{quote(short_id)}/close?changed_by={CHANGED_BY}"
        status, _ = self.request([
            "-X", "POST", "-H", "Content-Type: application/json",
            "-d", json.dumps({"reason": reason}), url,
        ])
        if not 200 <= status < 300:
            raise SyncError(f"close {short_id} got HTTP {status}")


def links_this_repo(task):
    """A bare number is not enough: only a URL says which repository it means."""
    return (task.get("external_issue_url") or "").startswith(THIS_REPO_PREFIXES)


def is_unfinished(task):
    return task.get("status") not in FINISHED


def close_reason(number, state_reason):
    # No login: a reporter can close their own issue, and third-party handles stay
    # out of Citadel. The workflow run records who closed it.
    run = os.environ.get("GITHUB_RUN_ID", "local")
    return f"GitHub issue #{number} closed ({state_reason or 'completed'}); synced by run {run}"


def close_tasks(citadel, tasks, number, state_reason):
    """Close each unfinished task. One failure must not stop the others."""
    failed = 0
    for task in tasks:
        if not links_this_repo(task) or not is_unfinished(task):
            continue
        try:
            citadel.close(task["short_id"], close_reason(number, state_reason))
            print(f"closed {task['short_id']} for #{number}")
        except SyncError as e:
            print(f"FAILED #{number}: {e}", file=sys.stderr)
            failed += 1
    return failed


def on_close(citadel, number, state_reason):
    return close_tasks(citadel, citadel.tasks_for_issue(number), number, state_reason)


def plan_reconcile(tasks, issue_states):
    """Sort every linked issue. An issue with no known state is left alone: absence
    from a listing is never evidence that an issue closed."""
    by_issue = {}
    for task in tasks:
        number = task.get("external_issue_number")
        if number and links_this_repo(task):
            by_issue.setdefault(number, []).append(task)

    to_close, all_finished = {}, []
    for number, linked in sorted(by_issue.items()):
        state = issue_states.get(number)
        unfinished = [t for t in linked if is_unfinished(t)]
        if state == "closed" and unfinished:
            to_close[number] = unfinished
        elif state == "open" and not unfinished:
            all_finished.append(number)
    no_task = sorted(n for n, s in issue_states.items() if s == "open" and n not in by_issue)
    return to_close, all_finished, no_task


def render_report(closed, all_finished, no_task, refused=None):
    numbers = lambda items: ", ".join(f"#{n}" for n in items) or "none"
    lines = [
        "Written by the nightly run of `sync-citadel-on-close.yml`.",
        "",
        f"**Closed in Citadel this run:** {numbers(closed)}",
        "",
        "Everything below was left as it is, for a person to decide.",
        "",
    ]
    if refused is not None:
        lines += [
            f"**⚠ Refused:** {len(refused)} issues would have closed tasks, over the limit "
            f"of {MAX_RECONCILE_CLOSES}. Nothing was closed. {numbers(refused)}",
            "",
        ]
    lines += [
        f"**Open issue, every task finished ({len(all_finished)}):** {numbers(all_finished)}",
        "",
        f"**Open issue, no task ({len(no_task)}):** {numbers(no_task)}",
        "",
    ]
    return "\n".join(lines)


def reconcile(citadel, issue_states):
    """Return (failed writes, report text)."""
    to_close, all_finished, no_task = plan_reconcile(citadel.all_tasks(), issue_states)
    total = sum(len(t) for t in to_close.values())
    if total > MAX_RECONCILE_CLOSES:
        print(f"REFUSED: {total} closes is over {MAX_RECONCILE_CLOSES}", file=sys.stderr)
        return 1, render_report([], all_finished, no_task, refused=sorted(to_close))
    failed = sum(close_tasks(citadel, t, n, "found by nightly run") for n, t in to_close.items())
    return failed, render_report(sorted(to_close), all_finished, no_task)


def github_issue_states(repo):
    """REST, not `gh issue list`: GraphQL quota is shared with every session."""
    out = subprocess.run(
        ["gh", "api", "--paginate", f"repos/{repo}/issues?state=all&per_page=100",
         "--jq", ".[] | select(.pull_request | not) | [.number, .state] | @tsv"],
        capture_output=True, text=True, encoding="utf-8",
    )
    if out.returncode != 0:
        raise SyncError(f"gh api exit {out.returncode}")
    states = {}
    for line in out.stdout.splitlines():
        number, state = line.split("\t")
        states[int(number)] = state
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    close = sub.add_parser("close")
    close.add_argument("--issue", type=int, required=True)
    close.add_argument("--state-reason", default="")
    rec = sub.add_parser("reconcile")
    rec.add_argument("--report", required=True)
    rec.add_argument("--repo", default="OffbandMesh/meshcore-firmware")
    args = parser.parse_args()

    url = os.environ.get("CITADEL_URL")
    if not url:
        print("CITADEL_URL is not set", file=sys.stderr)
        return 2
    citadel = Citadel(url)
    try:
        if args.mode == "close":
            failed = on_close(citadel, args.issue, args.state_reason)
        else:
            failed, report = reconcile(citadel, github_issue_states(args.repo))
            with open(args.report, "w", encoding="utf-8") as f:
                f.write(report)
    except SyncError as e:
        print(f"FAILED: {e}", file=sys.stderr)
        return 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
