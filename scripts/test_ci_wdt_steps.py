#!/usr/bin/env python3
"""The watchdog tests are wired into CI as BLOCKING steps that gate ci-green (#1160).

Pure, no hardware. Run: python scripts/test_ci_wdt_steps.py

A test that exists but is not run, or runs but cannot fail the merge, is the
same "looks covered, isn't" failure #712 fixed for the googletest suites. This
pins the wiring itself:

  * each watchdog script is a `run:` step in the config-lint job;
  * none of those steps carries `continue-on-error` (they block);
  * ci-green `needs` both config-lint and native-tests (the googletest suite
    `test_wdt_timeout` rides in native-tests by discovery).

Reads the workflow as text with a light structural parse so it does not need
PyYAML on the runner.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CI = os.path.join(ROOT, ".github", "workflows", "ci.yml")

WDT_SCRIPTS = [
    "scripts/test_wdt_guard.py",
    "scripts/check_wdt_guard.py",
    "scripts/test_wdt_hang_verb.py",
    "scripts/test_wdt_self_hang.py",
    "scripts/test_ci_wdt_steps.py",
]

JOB = re.compile(r"^  ([a-z][a-z0-9-]*):\s*$")


def jobs(text):
    """Map job name -> its text block (until the next top-level job)."""
    out = {}
    name = None
    buf = []
    for line in text.splitlines():
        m = JOB.match(line)
        if m:
            if name:
                out[name] = "\n".join(buf)
            name, buf = m.group(1), []
        elif name:
            buf.append(line)
    if name:
        out[name] = "\n".join(buf)
    return out


def steps(job_text):
    """Yield the text of each `- name:` step in a job."""
    cur = []
    for line in job_text.splitlines():
        if re.match(r"^      - name:", line):
            if cur:
                yield "\n".join(cur)
            cur = [line]
        elif cur:
            cur.append(line)
    if cur:
        yield "\n".join(cur)


def real():
    with open(CI, encoding="utf-8") as f:
        return f.read()


def step_running(job_text, script):
    return [s for s in steps(job_text) if re.search(r"run:\s*python\s+%s\b" % re.escape(script), s)]


def test_every_wdt_script_is_a_config_lint_step():
    lint = jobs(real())["config-lint"]
    for script in WDT_SCRIPTS:
        assert len(step_running(lint, script)) == 1, script


def test_wdt_steps_are_blocking():
    lint = jobs(real())["config-lint"]
    for script in WDT_SCRIPTS:
        (s,) = step_running(lint, script)
        assert "continue-on-error" not in s, "%s must block, not advise" % script


def test_ci_green_requires_config_lint_and_native_tests():
    green = jobs(real())["ci-green"]
    needs = re.search(r"needs:\s*\[([^\]]*)\]", green).group(1)
    assert "config-lint" in needs and "native-tests" in needs, needs
    # and it actually tests both results, not just lists them
    assert 'needs.config-lint.result }}" = "success"' in green
    assert 'needs.native-tests.result }}" = "success"' in green


def test_native_tests_job_discovers_envs_rather_than_listing_them():
    native = jobs(real())["native-tests"]
    assert "platform', fallback='').strip() == 'native'" in native
    assert "pio test -e \"$e\"" in native


# --------------------------------------------------------- synthetic shapes ---

ADVISORY = '''
  config-lint:
    steps:
      - name: Watchdog guard self-test
        continue-on-error: true
        run: python scripts/test_wdt_guard.py
  ci-green:
    needs: [config-lint]
'''


def test_synthetic_advisory_step_is_detected():
    lint = jobs(ADVISORY)["config-lint"]
    (s,) = step_running(lint, "scripts/test_wdt_guard.py")
    assert "continue-on-error" in s


def test_synthetic_missing_step_is_detected():
    lint = jobs(ADVISORY)["config-lint"]
    assert step_running(lint, "scripts/check_wdt_guard.py") == []


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("ok   %s" % name)
            except AssertionError as e:
                failures += 1
                print("FAIL %s: %s" % (name, e))
    print("%d failure(s)" % failures)
    sys.exit(1 if failures else 0)
