#!/usr/bin/env python3
"""Guard the docs-only classifier and the property it depends on (#1707).

Three things are tested, and the third is the one that keeps the rule honest as
the repository grows:

  A. The rule itself -- positive and negative cases, including the ones a
     careless allowlist gets wrong.
  B. Every file input the build DECLARES is classified not-docs. If someone adds
     `extra_scripts = docs/gen.py`, this fails. That is the drift the allowlist
     cannot survive silently.
  C. No build input CONSUMES an allowlisted path. Every reference from the build
     surface to `docs/`, `release-notes/` and friends must be a comment pointing
     a human at it. A non-comment reference means the path is load-bearing and
     must leave the allowlist.

Run from the repository root (config-lint does):  python scripts/test_classify_changes.py
"""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from classify_changes import classify, is_docs_path, DOCS_PATTERNS  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

failures = []
checks = 0


def check(label, got, want):
    global checks
    checks += 1
    if got != want:
        failures.append(f"{label}: got {got!r}, want {want!r}")


# ---------------------------------------------------------------------------
# A. the rule
# ---------------------------------------------------------------------------

DOCS_ONLY_SETS = {
    "single root markdown": ["README.md"],
    "several markdown": ["README.md", "CONTRIBUTING.md", "SECURITY.md"],
    "the changelog": ["CHANGELOG.md"],
    "release notes": ["release-notes/1.5.0-beta7.md"],
    "release-notes readme": ["release-notes/README.md"],
    "discord post": ["discord/1.5.0.md"],
    "docs at depth": ["docs/architecture/2026-06-01-observer-architecture-review.md"],
    "non-markdown under docs": ["docs/radiocore/vendor/RCC6/schematic.pdf"],
    "build log": ["build-logs/plan1-v3-companion-crashlog.log"],
    "logo sources": ["logo/meshcore.svg", "logo/meshcore.afdesign"],
    "the license": ["license.txt"],
    "markdown inside vendored source": ["arch/stm32/Adafruit_LittleFS_stm32/README.md"],
    "mixed docs directories": ["docs/x.md", "discord/1.5.0.md", "build-logs/a.log"],
    "windows separators": ["docs\\architecture\\note.md"],
    "leading ./": ["./README.md"],
}

MUST_BUILD_SETS = {
    "empty set": [],
    "blank lines only": ["", "  "],
    "firmware source": ["src/Mesh.cpp"],
    "a header": ["src/helpers/ui/TuneTrigger.h"],
    "the build config": ["platformio.ini"],
    "a variant config": ["variants/heltec_v3/platformio.ini"],
    "vendored C++": ["arch/esp32/AsyncElegantOTA/src/AsyncElegantOTA.cpp"],
    "the version injector": ["scripts/inject_offband_version.py"],
    "this classifier": ["scripts/classify_changes.py"],
    "this test": ["scripts/test_classify_changes.py"],
    "the workflow": [".github/workflows/ci.yml"],
    "a workflow comment-only edit is still a workflow": [".github/workflows/release.yml"],
    # Gemini review 2026-10-21: markdown under .github/ must still build. A
    # change to CI gets validated BY CI, even when the changed file is prose.
    "markdown under .github": [".github/workflows/README.md"],
    "an issue template": [".github/ISSUE_TEMPLATE/bug.md"],
    "the classify action itself": [".github/actions/classify-changes/action.yml"],
    "the library manifest": ["library.json"],
    "the mkdocs config": ["mkdocs.yml"],
    "a native test": ["test/test_tune_trigger/main.cpp"],
    "a second pio project": ["tools/diag/lvgl-bench-1326/src/main.cpp"],
    "packaging": ["bin/uf2conv/uf2conv.py"],
    "an unknown new path": ["newthing/whatever.rs"],
    "an unknown root file": ["Makefile"],
    "docs plus one source file": ["README.md", "src/Mesh.cpp"],
    "many docs plus one variant": ["docs/a.md", "docs/b.md", "variants/heltec_v4/platformio.ini"],
    "release notes plus the release workflow": ["release-notes/1.6.0.md", ".github/workflows/release.yml"],
}

for label, paths in DOCS_ONLY_SETS.items():
    docs_only, offenders = classify(paths)
    check(f"docs-only / {label}", docs_only, True)
    check(f"docs-only / {label} / no offenders", offenders, [])

for label, paths in MUST_BUILD_SETS.items():
    docs_only, _ = classify(paths)
    check(f"must-build / {label}", docs_only, False)

# A docs path on its own is docs; the same path alongside code is not. The rule
# is all-or-nothing, and that is the property a per-file filter gets wrong.
check("one code file poisons a large docs set",
      classify(["docs/%d.md" % i for i in range(50)] + ["src/Mesh.cpp"])[0], False)


# ---------------------------------------------------------------------------
# B. every declared build input classifies not-docs
# ---------------------------------------------------------------------------

INI_FILES = [ROOT / "platformio.ini"] + sorted((ROOT / "variants").glob("*/platformio.ini"))
DECLARED = re.compile(
    r"(?:extra_scripts|extra_configs)\s*=\s*(.*)"
    r"|(?:pre:|post:)([A-Za-z0-9_./-]+)"
    r"|file://([A-Za-z0-9_./-]+)"
)

declared_inputs = set()
for ini in INI_FILES:
    if not ini.exists():
        continue
    for raw in ini.read_text(encoding="utf-8", errors="replace").splitlines():
        # Strip a trailing `;` comment before matching. `extra_scripts =
        # scripts/foo.py  ; why` survives the regex by accident today, because
        # .split() discards the comment tokens -- but only by accident, and a
        # blind spot in the safety net is worth closing rather than relying on.
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        for m in DECLARED.finditer(line):
            for g in m.groups():
                if not g:
                    continue
                for token in g.replace(",", " ").split():
                    # removeprefix, NOT lstrip: lstrip takes a CHARACTER SET, so
                    # lstrip("post:") on "scripts/..." eats the leading "s" and
                    # silently yields "cripts/...". A mangled token can classify
                    # differently from the real path, which would make this check
                    # pass while the input it was meant to guard is misfiled.
                    token = token.strip()
                    for prefix in ("pre:", "post:"):
                        token = token[len(prefix):] if token.startswith(prefix) else token
                    if token.startswith("${") or "*" in token or not token:
                        continue
                    if "/" in token or token.endswith((".py", ".ini")):
                        declared_inputs.add(token)

if not declared_inputs:
    failures.append("B: parsed zero declared build inputs -- the parser is broken, "
                    "not the repository (a vacuous pass here would hide real drift)")
checks += 1

for token in sorted(declared_inputs):
    if is_docs_path(token):
        failures.append(f"B: declared build input {token!r} classifies as DOCS. "
                        "Either the allowlist is wrong or this input is misplaced.")
    checks += 1


# ---------------------------------------------------------------------------
# C. no build input consumes an allowlisted path
# ---------------------------------------------------------------------------

BUILD_SURFACE_DIRS = ("src", "examples", "lib", "include", "boards", "arch", "variants")
BUILD_SURFACE_FILES = ("platformio.ini", "scripts/inject_offband_version.py")
TEXT_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".ini", ".py", ".json", ".txt", ".S", ".s"}

# A line is allowed to NAME an allowlisted path only as a comment. Anything else
# -- an include, an open(), a config value -- means the path is load-bearing.
COMMENT = re.compile(r"^\s*(//|/\*|\*|;|#|--)")
MENTION = re.compile(r"(?<![A-Za-z0-9_.-])(docs|release-notes|discord|build-logs|logo)/")

candidates = []
for d in BUILD_SURFACE_DIRS:
    base = ROOT / d
    if base.exists():
        candidates += [p for p in base.rglob("*") if p.is_file() and p.suffix in TEXT_SUFFIXES]
for f in BUILD_SURFACE_FILES:
    p = ROOT / f
    if p.exists():
        candidates.append(p)

if len(candidates) < 50:
    failures.append(f"C: only {len(candidates)} build-surface files found -- "
                    "run from the repository root; a vacuous pass hides real consumption")
checks += 1

consuming = []
for path in candidates:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        continue
    for n, line in enumerate(text.splitlines(), 1):
        if MENTION.search(line) and not COMMENT.match(line):
            consuming.append(f"{path.relative_to(ROOT).as_posix()}:{n}: {line.strip()[:120]}")
checks += 1

for hit in consuming:
    failures.append("C: a build input references an allowlisted path OUTSIDE a comment, "
                    f"so that path may be load-bearing:\n      {hit}")


# ---------------------------------------------------------------------------

if failures:
    print(f"FAIL: {len(failures)} problem(s) in {checks} checks\n", file=sys.stderr)
    for f in failures:
        print(f"  - {f}", file=sys.stderr)
    sys.exit(1)

print(f"OK, {checks} checks "
      f"({len(DOCS_ONLY_SETS)} docs-only sets, {len(MUST_BUILD_SETS)} must-build sets, "
      f"{len(declared_inputs)} declared build inputs, {len(candidates)} build-surface files)")
