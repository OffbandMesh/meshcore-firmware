#!/usr/bin/env python3
"""A release build must compile from scratch -- never from a cached object tree (#1824).

PR CI caches `.pio/build/<env>` so a leg rebuilds only what changed. That is a
throughput measure and it belongs on PRs only. A release is the one place where
saving a few minutes is worthless and a wrong binary is expensive: a stale object
linking into a tagged image is the image-identity failure class this project has
already been bitten by, and it would be invisible -- the image would build, flash
and boot, just not from the source the tag claims.

So this asserts the absence of a build-object cache in every workflow that
produces release artifacts. Absence is easy to lose by accident: somebody copies
a step from ci.yml into release.yml to make a slow release faster, and nothing
would otherwise complain.

What counts as a build-object cache: an `actions/cache` step whose `path:` names
`.pio/build`. Caching `~/.platformio` is fine and expected -- that is toolchains
and packages, reproducible inputs rather than compiler output.

Exit 0 when clean, 1 with the offending file and line when not.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORKFLOWS = REPO / ".github" / "workflows"

# Workflows that publish artifacts a human will flash. A build-object cache in
# any of these is a defect.
RELEASE_WORKFLOWS = ("release.yml", "diag-build.yml")

# `.pio/build` appears legitimately in release workflows as an artifact PATH --
# uploading firmware.bin out of .pio/build/<env>/ is the normal way to collect
# the product. Only a *cache* of that directory is the problem, so the check is
# scoped to cache steps rather than to the string.
CACHE_ACTION = re.compile(r"uses:\s*actions/cache(@|\s|$)")
BUILD_DIR = re.compile(r"\.pio[/\\]build")
STEP_START = re.compile(r"^\s*-\s")


def cache_step_blocks(lines: list[str]) -> list[tuple[int, list[str]]]:
    """Every `- uses: actions/cache` step, as (1-indexed start line, its lines).

    A step runs from its own `- ` bullet to the next bullet at the same or lower
    indent, which is enough structure to read a `path:` without a YAML parser --
    deliberately, so the guard has no dependency the rest of config-lint lacks.
    """
    blocks: list[tuple[int, list[str]]] = []
    i = 0
    while i < len(lines):
        if CACHE_ACTION.search(lines[i]):
            # Walk back to the bullet that opened this step.
            start = i
            while start > 0 and not STEP_START.match(lines[start]):
                start -= 1
            indent = len(lines[start]) - len(lines[start].lstrip())
            end = start + 1
            while end < len(lines):
                line = lines[end]
                if line.strip() and STEP_START.match(line):
                    if (len(line) - len(line.lstrip())) <= indent:
                        break
                end += 1
            blocks.append((start + 1, lines[start:end]))
            i = end
        else:
            i += 1
    return blocks


def _display(path: Path) -> str:
    """Repo-relative when it is in the repo, absolute otherwise.

    The self-test lints synthetic files in a temp directory, so relative_to()
    cannot be assumed -- it raises rather than falling back.
    """
    try:
        return path.relative_to(REPO).as_posix()
    except ValueError:
        return path.as_posix()


def check_file(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    failures: list[str] = []
    for start_line, block in cache_step_blocks(lines):
        for offset, line in enumerate(block):
            stripped = line.split("#", 1)[0]
            if not stripped.strip():
                continue
            if BUILD_DIR.search(stripped):
                failures.append(
                    f"{_display(path)}:{start_line + offset}: "
                    f"actions/cache step caches a build-object directory "
                    f"({stripped.strip()}). A release must build clean (#1824). "
                    f"Caching ~/.platformio is fine; caching .pio/build is not."
                )
    return failures


def main() -> int:
    failures: list[str] = []
    checked = 0
    for name in RELEASE_WORKFLOWS:
        path = WORKFLOWS / name
        if not path.exists():
            # Not an error: diag-build.yml may legitimately not exist in a tree
            # being linted. A missing release.yml would be caught by every other
            # release check long before this one.
            continue
        checked += 1
        failures.extend(check_file(path))

    if checked == 0:
        print(
            "check_release_no_build_cache: no release workflows found in "
            f"{WORKFLOWS.relative_to(REPO).as_posix()} -- nothing asserted. "
            "If release.yml moved, this guard needs its path updated.",
            file=sys.stderr,
        )
        return 1

    if failures:
        print("Release workflows must not cache build objects:", file=sys.stderr)
        for f in failures:
            print(f"  {f}", file=sys.stderr)
        return 1

    print(f"check_release_no_build_cache: {checked} release workflow(s) build clean. OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
