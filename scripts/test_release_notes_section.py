#!/usr/bin/env python3
"""A release page's description: prose when there is any (#1306), else the tagged
version's own CHANGELOG section (#1294).

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

#1306 added a higher-priority source above all of that: `release-notes/<version as
tagged>.md`, the owner's prose. A CHANGELOG section is a changelog, and publishing
one as the page body was only ever done for want of anywhere else to look. Pinned
here too:

  * prose outranks the tagged version's own section;
  * prose outranks the base-version fallback;
  * an `-rc` takes its OWN prose and never the base version's -- the suffix strip
    governs CHANGELOG lookup only, because notes are per-tag and permanent;
  * a prose file named for another version is never published;
  * with no prose present, every behavior above is exactly as it was.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE_YML = os.path.join(ROOT, ".github", "workflows", "release.yml")
STEP_NAME = "Extract release body"

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


def run(block: str, tag: str, changelog: str, footer: str | None = FOOTER,
        prose: dict[str, str] | None = None, expect_fail: bool = False) -> str:
    """Run the extraction in a throwaway tree and return the body it produced.

    `prose` maps a release-notes basename (the version as tagged, no extension) to
    its content, e.g. {"1.5.0-beta7": "PROSE LINE\\n"} (#1306).
    """
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "CHANGELOG.md"), "w", encoding="utf-8") as fh:
            fh.write(changelog)
        if prose:
            os.mkdir(os.path.join(tmp, "release-notes"))
            for name, text in prose.items():
                with open(os.path.join(tmp, "release-notes", f"{name}.md"), "w",
                          encoding="utf-8") as fh:
                    fh.write(text)
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
            if expect_fail:
                return ""
            fail(f"the extraction exited {proc.returncode} for {tag}: {proc.stderr.strip()}")
        elif expect_fail:
            fail(f"the extraction was expected to fail for {tag} and exited 0")
        body = os.path.join(tmp, "body.md")
        if not os.path.exists(body):
            return ""
        with open(body, encoding="utf-8") as fh:
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

    # ---- #1306: release-notes/<version as tagged>.md is prose and outranks the changelog.

    prose = {"1.5.0-beta7": "PROSE LINE\n"}

    body = run(block, "offband-v1.5.0-beta7", FIXTURE_BOTH, prose=prose)
    check("PROSE LINE" in body, "a prose file must supply the body")
    check("BETA7 LINE" not in body,
          "prose must outrank the tagged version's own CHANGELOG section")
    check("BASE LINE" not in body, "prose must not be joined by the base version's notes")
    check("FOOTER LINE" in body, "the footer must still be appended on the prose path")

    # Prose also beats the base-version fallback -- the case where the changelog has no
    # section for this tag at all, so the old code would have published 1.5.0's notes.
    body = run(block, "offband-v1.5.0-beta7", FIXTURE_BASE_ONLY, prose=prose)
    check("PROSE LINE" in body, "prose must win when the tag has no section of its own")
    check("BASE LINE" not in body, "prose must outrank the base-version fallback")

    # An -rc takes its own prose file. The suffix strip governs CHANGELOG lookup only;
    # release notes are per-tag, so an -rc is not served its base version's prose.
    body = run(block, "offband-v1.5.0-rc1", FIXTURE_BOTH,
               prose={"1.5.0-rc1": "RC PROSE\n", "1.5.0": "STABLE PROSE\n"})
    check("RC PROSE" in body, "an -rc must take its own prose file")
    check("STABLE PROSE" not in body,
          "an -rc must NOT fall back to the base version's prose -- notes are per-tag")

    # A prose file for a DIFFERENT version is not a match, and must not be published.
    body = run(block, "offband-v1.5.0-beta7", FIXTURE_BOTH, prose={"1.5.0": "WRONG PROSE\n"})
    check("WRONG PROSE" not in body,
          "a prose file named for another version must never be published")
    check("BETA7 LINE" in body,
          "with no prose for this tag, the CHANGELOG chain must behave exactly as before")

    # No prose directory at all: every pre-#1306 behavior is untouched. This is the
    # regression guard for the change itself.
    body = run(block, "offband-v1.6.0", FIXTURE_BOTH)
    check("(No CHANGELOG entry for v1.6.0.)" in body,
          "with no prose and no section, the placeholder must still be the body")

    # An EMPTY prose file is a mistake, and must fail the release rather than quietly
    # publishing the changelog instead. The first cut of #1306 used `cp` then tested
    # `-s`, so a zero-byte notes file fell through to the changelog and shipped a body
    # nobody had reviewed -- this case is what catches that.
    run(block, "offband-v1.5.0-beta7", FIXTURE_BOTH, prose={"1.5.0-beta7": ""},
        expect_fail=True)

    # Hardening, not a reachable hole: git rejects `*`, spaces and `..` in a tag name,
    # so GITHUB_REF_NAME cannot carry a glob. Pinned anyway, because the version is
    # interpolated into both a path and a loop.
    body = run(block, "offband-v*", FIXTURE_BOTH)
    check("BETA7 LINE" not in body and "BASE LINE" not in body,
          "a glob in the version must not expand into a section match")
    check("(No CHANGELOG entry for v*.)" in body,
          "a glob version must fall through to the placeholder, named literally")

    if FAILURES:
        print(f"\n{len(FAILURES)} failure(s).")
        return 1
    print("OK: release body is prose when present (#1306), else the tagged "
          "version's section (#1294).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
