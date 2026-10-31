#!/usr/bin/env python3
"""Positive and negative tests for the Citadel sync (#1933). No network: a fake
stands in for curl and records every write.

Run from the repository root (config-lint does):  python scripts/test_citadel_sync.py
"""

import json
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import citadel_sync  # noqa: E402
from citadel_sync import (  # noqa: E402
    Citadel, MAX_RECONCILE_CLOSES, SYNC_TOKEN_HEADER, SyncError, TASK_PAGE_LIMIT, curl_json,
    on_close, plan_reconcile, reconcile, render_report,
)

URL = "https://github.com/OffbandMesh/meshcore-firmware/issues/"
OLD_URL = "https://github.com/Strycher/Crosswire/issues/"
BASE = "https://citadel.invalid"

failures = []
checks = 0


def check(name, condition):
    global checks
    checks += 1
    if not condition:
        failures.append(name)
        print(f"FAIL: {name}")


def task(short_id, number, status="in_progress", url=URL):
    return {"short_id": short_id, "external_issue_number": number, "status": status,
            "external_issue_url": f"{url}{number}" if url else None}


class FakeApi:
    """Answers the two task queries and records closes."""

    def __init__(self, tasks, close_status=200, query_status=200):
        self.tasks = tasks
        self.close_status = close_status
        self.query_status = query_status
        self.closed = []
        self.bodies = []

    def __call__(self, args):
        url = args[-1]
        if "-X" in args:
            short_id = url.rsplit("/close", 1)[0].rsplit("/", 1)[-1]
            status = self.close_status(short_id) if callable(self.close_status) else self.close_status
            if status == 200:
                self.closed.append(short_id)
            self.bodies.append(json.loads(args[args.index("-d") + 1]))
            return status, {}
        if "external_issue_number=" in url:
            number = int(url.rsplit("=", 1)[-1])
            return self.query_status, [t for t in self.tasks if t["external_issue_number"] == number]
        return self.query_status, self.tasks


def citadel(api):
    return Citadel(BASE, request=api)


# --- closing one issue

api = FakeApi([task("a", 1), task("b", 1, "backlog"), task("c", 1, "deferred"), task("z", 2)])
check("on_close returns no failures", on_close(citadel(api), 1, "completed") == 0)
check("every unfinished task of the issue closes", sorted(api.closed) == ["a", "b", "c"])
check("another issue's task is untouched", "z" not in api.closed)
check("the reason names the issue and how it closed",
      "#1" in api.bodies[0]["reason"] and "completed" in api.bodies[0]["reason"])

api = FakeApi([task("a", 1, "done"), task("b", 1, "closed")])
on_close(citadel(api), 1, "completed")
check("finished tasks are not closed again", api.closed == [])

api = FakeApi([])
check("an issue with no task is not a failure", on_close(citadel(api), 1, "completed") == 0)
check("an issue with no task writes nothing", api.closed == [])

api = FakeApi([task("a", 1, url=OLD_URL)])
on_close(citadel(api), 1, "not_planned")
check("the repository's old name still counts as this repository", api.closed == ["a"])
check("the state reason reaches the task", "not_planned" in api.bodies[0]["reason"])

api = FakeApi([task("a", 1, url="https://github.com/Strycher/LoRa/issues/"),
               task("b", 1, url=None)])
on_close(citadel(api), 1, "completed")
check("a same-numbered issue in another repository closes nothing", api.closed == [])

api = FakeApi([task("bad", 1), task("good", 1)],
              close_status=lambda short_id: 500 if short_id == "bad" else 200)
check("a failed close is reported", on_close(citadel(api), 1, "completed") == 1)
check("a failed close does not stop the next one", api.closed == ["good"])

api = FakeApi([task("a", 1)], query_status=503)
try:
    on_close(citadel(api), 1, "completed")
    check("Citadel down raises", False)
except SyncError:
    check("Citadel down raises", True)
check("Citadel down writes nothing", api.closed == [])

# --- the nightly plan

tasks = [
    task("open-closed", 1), task("done-closed", 2, "done"),
    task("done-open", 3, "done"), task("mixed-a", 4, "done"), task("mixed-b", 4),
    task("deferred-open", 5, "deferred"), task("unknown", 6), task("elsewhere", 7, url=None),
]
states = {1: "closed", 2: "closed", 3: "open", 4: "open", 5: "open", 8: "open", 7: "closed"}
to_close, all_finished, no_task = plan_reconcile(tasks, states)
check("closed issue with an open task is planned", list(to_close) == [1])
check("an issue missing from the listing is never closed", 6 not in to_close)
check("a task with no repository link is never closed", 7 not in to_close)
check("open issue with every task done is reported, not changed", all_finished == [3])
check("deferred is not reported as finished", 5 not in all_finished)
check("open issue with no task is reported", 8 in no_task)
check("issue 7 has no task of ours, so it counts as no task only if open", 7 not in no_task)

api = FakeApi(tasks)
failed, report = reconcile(citadel(api), states)
check("reconcile closes what it planned", api.closed == ["open-closed"] and failed == 0)
check("report names the closed issue", "#1" in report.split("Open issue")[0])
check("report names the finished issue", "#3" in report)

over = [task(f"t{n}", n + 1) for n in range(MAX_RECONCILE_CLOSES + 1)]
api = FakeApi(over)
failed, report = reconcile(citadel(api), {n + 1: "closed" for n in range(MAX_RECONCILE_CLOSES + 1)})
check("over the limit closes nothing", api.closed == [])
check("over the limit fails the run", failed == 1)
check("over the limit is in the report", "Refused" in report)

at_limit = [task(f"t{n}", n + 1) for n in range(MAX_RECONCILE_CLOSES)]
api = FakeApi(at_limit)
reconcile(citadel(api), {n + 1: "closed" for n in range(MAX_RECONCILE_CLOSES)})
check("exactly at the limit still closes", len(api.closed) == MAX_RECONCILE_CLOSES)

check("an empty report says none", "none" in render_report([], [], []))

# --- the public repository must not carry the address

def foreign_urls(text):
    """Any web address that is not GitHub's. The test can't name the host it guards
    against, since that would publish it."""
    urls = re.findall(r"https?://[^\s\"'`)]+", text)
    return [u for u in urls if not u.startswith("https://github.com/")]


source = (Path(__file__).resolve().parent / "citadel_sync.py").read_text(encoding="utf-8")
check("the sync script names no web address but GitHub's", foreign_urls(source) == [])
check("that check can fail", foreign_urls("curl https://citadel.invalid/x") != [])

workflow = Path(__file__).resolve().parent.parent / ".github/workflows/sync-citadel-on-close.yml"
text = workflow.read_text(encoding="utf-8")
check("the workflow names no web address but GitHub's", foreign_urls(text) == [])
check("the workflow reads the address from a secret", "secrets.CITADEL_URL" in text)
# An expression inside run: is pasted into the shell before it runs, so an issue
# field would be code. Everything reaches the script through env.
run_lines = [l for l in text.splitlines() if "python scripts/citadel_sync.py" in l]
check("the workflow calls the script", len(run_lines) == 2)
check("no expression is pasted into a run line", all("${{" not in l for l in run_lines))

# curl_json must not put the address in an error: run logs on this repo are public.
def curl_error(run):
    real = citadel_sync.subprocess.run
    citadel_sync.subprocess.run = run
    try:
        curl_json(["http://citadel.invalid/secret-path"])
        return None
    except SyncError as e:
        return str(e)
    finally:
        citadel_sync.subprocess.run = real


def curl_missing(*_args, **_kwargs):
    raise FileNotFoundError(2, "no such file", "http://citadel.invalid/secret-path")


def curl_fails(*_args, **_kwargs):
    return citadel_sync.subprocess.CompletedProcess([], 6, "", "citadel.invalid: no host")


for name, run in (("curl not installed", curl_missing), ("curl failing", curl_fails)):
    error = curl_error(run)
    check(f"{name} is a SyncError", error is not None)
    check(f"{name} does not print the address", "citadel.invalid" not in (error or ""))

# --- the CDN token

def curl_argv(token):
    """The argv curl would get, with the token set or not. Restores both things it touches."""
    seen = []

    def run(argv, **_kwargs):
        seen.extend(argv)
        return citadel_sync.subprocess.CompletedProcess(argv, 0, "[]\n200", "")

    real_run, real_token = citadel_sync.subprocess.run, os.environ.pop("CITADEL_SYNC_TOKEN", None)
    citadel_sync.subprocess.run = run
    if token:
        os.environ["CITADEL_SYNC_TOKEN"] = token
    try:
        result = curl_json([f"{BASE}/x"])
    finally:
        citadel_sync.subprocess.run = real_run
        os.environ.pop("CITADEL_SYNC_TOKEN", None)
        if real_token:
            os.environ["CITADEL_SYNC_TOKEN"] = real_token
    return seen, result


argv, result = curl_argv("tok-value")
check("the token goes out as the header", f"{SYNC_TOKEN_HEADER}: tok-value" in argv)
check("the header comes before the request", argv.index("-H") < argv.index(f"{BASE}/x"))
check("a request with a token still parses", result == (200, []))
argv, _ = curl_argv(None)
check("no token means no header", "-H" not in argv)
argv, _ = curl_argv("")
check("an empty token means no header", "-H" not in argv)


def curl_failing_with_token(*_args, **_kwargs):
    return citadel_sync.subprocess.CompletedProcess([], 6, "", "tok-value rejected")


os.environ["CITADEL_SYNC_TOKEN"] = "tok-value"
error = curl_error(curl_failing_with_token)
del os.environ["CITADEL_SYNC_TOKEN"]
check("a curl failure does not print the token", "tok-value" not in (error or "tok-value"))

check("the workflow reads the token from a secret", "secrets.CITADEL_SYNC_TOKEN" in text)
check("both jobs that reach Citadel get the token",
      text.count("CITADEL_SYNC_TOKEN: ${{ secrets.CITADEL_SYNC_TOKEN }}") == 2)
check("the token value is not in the script", "tok-" not in source)

# --- a full page cannot be told from a cut-off list

api = FakeApi([task(f"t{n}", n + 1) for n in range(TASK_PAGE_LIMIT)])
try:
    reconcile(citadel(api), {})
    check("a full page of tasks is refused", False)
except SyncError:
    check("a full page of tasks is refused", True)
check("a full page of tasks writes nothing", api.closed == [])

print(f"{checks - len(failures)}/{checks} checks passed")
sys.exit(1 if failures else 0)
