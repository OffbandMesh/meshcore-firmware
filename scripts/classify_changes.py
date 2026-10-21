#!/usr/bin/env python3
"""Classify a set of changed paths as docs-only, or not (#1707).

WHY THIS EXISTS

Every PR and every push to `firmware-base` builds the whole curated matrix --
80 legs. A one-word README fix pays that in full: runner minutes, queue
contention with real builds, and merge latency, for zero information.

WHY A SCRIPT RATHER THAN INLINE SHELL IN ci.yml

Two jobs need this answer. `classify` computes it to decide whether `build`
runs at all, and `ci-green` recomputes it to decide whether a skipped `build`
is acceptable. Those two computations must agree about the RULE while staying
independent about the VALUE.

`OffbandMesh/meshcore-client#904` is why the second computation exists: a forced
classifier output reached their default branch and every job trusted it, so
seven heavy checks reported green having built nothing, and real code merged
with no CI behind it for ~19 hours. A disarmed gate looks exactly like a working
one -- green in seconds either way. `ci-green` recomputing from the diff means a
wrong or forced `classify` output fails the gate instead of opening it.

Inlining the rule twice would defend against the same attack but introduce a
worse failure: two copies that drift. One script called from two jobs gives
independence where it matters (the value) and identity where it matters (the
rule). The rule itself is covered by test_classify_changes.py.

THE RULE

docs_only is true if and only if the changed set is NON-EMPTY and EVERY path in
it is docs. Anything unrecognized is not docs. The failure mode of a wrong
allowlist must be a wasted build, never a skipped one.

An empty changed set is NOT docs-only. No information is not evidence of safety:
a failed diff, a force-push, a new branch and `workflow_dispatch` all produce an
empty list, and every one of them must build.

Usage:
    classify_changes.py < paths-one-per-line
    classify_changes.py path [path ...]

Prints `docs_only=true` or `docs_only=false` on stdout (GITHUB_OUTPUT format),
and the reasoning to stderr. Exit status is 0 whether or not it is docs-only;
a non-zero exit means the classifier itself failed.
"""

import sys
from fnmatch import fnmatch

# Directories and files that cannot reach `pio run -e <env>`. Each was
# established by reading what the build declares it consumes, not from the
# directory name. Note `fnmatch`'s `*` spans `/`, so `docs/*` covers every depth.
#
# Owner-approved 2026-10-21 on #1707.
DOCS_PATTERNS = (
    "docs/*",
    "release-notes/*",
    "discord/*",
    "build-logs/*",
    "logo/*",
    "license.txt",
)

# Spelled out so a reader does not have to infer the complement, and so moving
# one of these into DOCS_PATTERNS requires deleting it here first -- which is the
# moment to ask for the evidence. test_classify_changes.py asserts that no build
# input consumes anything in DOCS_PATTERNS.
#
# `arch/` is the one that punishes name-based reasoning: it reads like
# architecture documentation and is vendored C++ plus a post-build script
# (platformio.ini: file://arch/esp32/AsyncElegantOTA,
# file://arch/stm32/Adafruit_LittleFS_stm32, post:arch/stm32/build_hex.py).
#
# `.github/` is never docs even for a comment change: a workflow edit changes CI
# itself, including this classifier.
NEVER_DOCS_PREFIXES = (
    ".github/",
    "arch/",
    "scripts/",
    "tools/",
    "bin/",
    "src/",
    "examples/",
    "lib/",
    "include/",
    "boards/",
    "variants/",
    "test/",
)

NEVER_DOCS_EXACT = (
    "platformio.ini",
    "library.json",
    "build.sh",
    "build_as_lib.py",
    "create-uf2.py",
    "merge-bin.py",
    "default.nix",
    "mkdocs.yml",
    "hardware-devices.example.yaml",
)


def is_docs_path(path: str) -> bool:
    """True when this one path cannot affect a firmware build."""
    p = path.strip().replace("\\", "/")
    while p.startswith("./"):
        p = p[2:]
    if not p:
        return False

    # `.github/` is never docs, not even a markdown file inside it. Everywhere
    # else, markdown IS docs -- including inside vendored source, since a `.md`
    # is never compiled, never executed, and not read by any build input (every
    # reference to a doc from the build surface is a comment pointing a human at
    # it, which a build does not follow). That ordering keeps the approved
    # `**/*.md` intact for `arch/<vendor>/README.md` while honoring the rule that
    # a change to CI must be validated BY CI.
    if p.startswith(".github/"):
        return False
    if p.endswith(".md"):
        return True

    if p in NEVER_DOCS_EXACT:
        return False
    if any(p.startswith(prefix) for prefix in NEVER_DOCS_PREFIXES):
        return False
    return any(fnmatch(p, pattern) for pattern in DOCS_PATTERNS)


def classify(paths) -> tuple:
    """Return (docs_only, offending_paths).

    offending_paths is every path that forced a False, so a caller can report
    WHY it is building rather than only that it is.
    """
    cleaned = [p.strip() for p in paths if p and p.strip()]
    if not cleaned:
        return False, []
    offenders = [p for p in cleaned if not is_docs_path(p)]
    return (not offenders), offenders


def main(argv) -> int:
    paths = argv[1:] if len(argv) > 1 else sys.stdin.read().splitlines()
    docs_only, offenders = classify(paths)

    count = len([p for p in paths if p and p.strip()])
    if count == 0:
        print("classify: empty changed set -- building "
              "(no information is not evidence of safety)", file=sys.stderr)
    elif docs_only:
        print(f"classify: {count} changed path(s), all docs -- build can be skipped",
              file=sys.stderr)
    else:
        print(f"classify: {count} changed path(s); {len(offenders)} require a build:",
              file=sys.stderr)
        for p in offenders[:20]:
            print(f"  {p}", file=sys.stderr)
        if len(offenders) > 20:
            print(f"  ... and {len(offenders) - 20} more", file=sys.stderr)

    print(f"docs_only={'true' if docs_only else 'false'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
