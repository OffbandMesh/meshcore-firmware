#!/usr/bin/env python3
"""A release page's description comes from the tagged version's own CHANGELOG section (#1294).

Pure, no network, no hardware. Run: python scripts/test_release_notes_section.py

release.yml used to strip `-beta*` from the tag before looking up a heading, so
every beta published the base version's notes: `offband-v1.5.0-beta6` shipped the
`## [1.5.0]` section, dated six weeks earlier, while `## [1.5.0-beta6]` sat unread
in the file. Stripping is right for an `-rc` (a candidate IS that version) and
wrong for a beta.

This runs the workflow's own extraction step -- lifted out of release.yml as text,
so the test cannot drift from what CI executes -- against fixture CHANGELOGs, and
pins four behaviors:

  * a beta with its own section publishes that section;
  * a beta without one falls back to the base version;
  * an `-rc` still takes the base version even when a beta section exists;
  * neither present leaves the "(No CHANGELOG entry...)" body.

It also pins that release-footer.md is appended, since the same step does it.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE_YML = os.path.join(ROOT, ".github", "workflows", "release.yml")
STEP_NAME = "Extract CHANGELOG section for release body"

FIXTURE_BOTH = """\
# Changelog

## [Unreleased]
- nothing yet

## [1.5.0-beta7] - 2026-09-26
- BETA7 LINE

## [1.5.0] - 2026-08-15
- BASE LINE

## [1.4.0] - 2026-08-13
- OLDER LINE
"""

FIXTURE_BASE_ONLY = """\
# Changelog

## [1.5.0] - 2026-08-15
- BASE LINE

## [1.4.0] - 2026-08-13
- OLDER LINE
"""

FOOTER = "FOOTER LINE\n"


def extraction_block() -> str:
    """The `run:` script of the extraction step, dedented, as CI runs it."""
    with open(RELEASE_YML, encoding="utf-8") as fh:
        lines = fh.read().splitlines()
    start = None
    for i, line in enumerate(lines):
        if line.strip() == f"- name: {STEP_NAME}":
            start = i
            break
    if start is None:
        fail(f"release.yml has no step named {STEP_NAME!r}")
    run_at = None
    for i in range(start + 1, min(start + 6, len(lines))):
        if re.match(r"\s*run: \|", lines[i]):
            run_at = i
            break
    if run_at is None:
        fail(f"the {STEP_NAME!r} step does not start a `run: |` block")
    body_indent = len(lines[run_at + 1]) - len(lines[run_at + 1].lstrip())
    out = []
    for line in lines[run_at + 1:]:
        if line.strip() and (len(line) - len(line.lstrip())) < body_indent:
            break
        out.append(line[body_indent:] if line.strip() else "")
    return "\n".join(out) + "\n"


def run(block: str, tag: str, changelog: str, footer: str | None = FOOTER) -> str:
    """Run the extraction in a throwaway tree and return the body it produced."""
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "CHANGELOG.md"), "w", encoding="utf-8") as fh:
            fh.write(changelog)
        if footer is not None:
            os.mkdir(os.path.join(tmp, ".github"))
            with open(os.path.join(tmp, ".github", "release-footer.md"), "w",
                      encoding="utf-8") as fh:
                fh.write(footer)
        env = dict(os.environ, GITHUB_REF_NAME=tag)
        try:
            # `-e` because GitHub runs every `run:` step as `bash -e {0}`: a step that
            # works in a plain shell and aborts under -e is a release that does not
            # publish.
            proc = subprocess.run(["bash", "-e", "-c", block], cwd=tmp, env=env,
                                  capture_output=True, text=True)
        except FileNotFoundError:
            fail("bash is required to run the workflow's own extraction step")
            return ""
        if proc.returncode != 0:
            fail(f"the extraction exited {proc.returncode} for {tag}: {proc.stderr.strip()}")
        with open(os.path.join(tmp, "body.md"), encoding="utf-8") as fh:
            return fh.read()


FAILURES = []


def fail(msg: str) -> None:
    FAILURES.append(msg)
    print(f"FAIL: {msg}")


def check(cond: bool, msg: str) -> None:
    if not cond:
        fail(msg)


def main() -> int:
    block = extraction_block()
    if FAILURES:
        return 1

    body = run(block, "offband-v1.5.0-beta7", FIXTURE_BOTH)
    check("BETA7 LINE" in body, "a beta with its own section must publish it")
    check("BASE LINE" not in body,
          "a beta with its own section must NOT publish the base version's notes")
    check("OLDER LINE" not in body, "the section must stop at the next heading")
    check("FOOTER LINE" in body, "release-footer.md must still be appended")

    body = run(block, "offband-v1.5.0-beta9", FIXTURE_BASE_ONLY)
    check("BASE LINE" in body,
          "a beta with no section of its own must fall back to the base version")

    body = run(block, "offband-v1.5.0-rc1", FIXTURE_BOTH)
    check("BASE LINE" in body, "an -rc must take the base version's section")
    check("BETA7 LINE" not in body,
          "an -rc must not pick up a beta's section")

    body = run(block, "offband-v1.6.0", FIXTURE_BOTH)
    check("(No CHANGELOG entry for v1.6.0.)" in body,
          "a version with no section must leave the placeholder, naming the version")
    check("BASE LINE" not in body and "BETA7 LINE" not in body,
          "the placeholder must not carry another version's notes")

    # A stable tag whose section exists takes it, exact match and no suffix to strip.
    body = run(block, "offband-v1.5.0", FIXTURE_BOTH)
    check("BASE LINE" in body, "a stable tag must publish its own section")
    check("BETA7 LINE" not in body, "a stable tag must not pick up a beta's section")

    # The suffix strip takes everything from `-beta` on, so an unusual pre-release name
    # still resolves to the right base version rather than to a heading that cannot exist.
    body = run(block, "offband-v1.5.0-betamax", FIXTURE_BOTH)
    check("BASE LINE" in body,
          "a pre-release with no section of its own must fall back to 1.5.0, not to '1.5.0-'")

    # The footer is optional: the step swallows a missing one, and the notes still publish.
    body = run(block, "offband-v1.5.0-beta7", FIXTURE_BOTH, footer=None)
    check("BETA7 LINE" in body, "an absent release-footer.md must not cost the notes")
    check("FOOTER LINE" not in body, "no footer file, no footer text")

    if FAILURES:
        print(f"\n{len(FAILURES)} failure(s).")
        return 1
    print("OK: release notes come from the tagged version's section (#1294).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
