#!/usr/bin/env python3
"""Every role's path-hash default comes from the one shared constant (#1753).

`path_hash_mode` is declared in two separate prefs structs -- the companion's
(examples/companion_radio/NodePrefs.h) and the one repeater, room server and sensor share
(src/helpers/CommonCLI.h). Both must initialise from offband::DEFAULT_PATH_HASH_MODE in
src/helpers/PathHashDefault.h, which test/test_path_hash_mode pins at 1 (2-byte hashes).

WHY THIS IS A SOURCE CHECK. The native test can pin the constant, but it cannot read the
two struct declarations without dragging Arduino in. So reverting one struct to `= 0`
would leave every native test green while fresh nodes of that role went back to 1-byte
hashes -- review found exactly that gap. This closes it: a struct that initialises from
anything but the constant fails here.

Run: python scripts/test_path_hash_default.py
"""
from __future__ import annotations
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STRUCTS = (
    ROOT / "examples" / "companion_radio" / "NodePrefs.h",
    ROOT / "src" / "helpers" / "CommonCLI.h",
)
DECL_RE = re.compile(r"^\s*uint8_t\s+path_hash_mode\s*=\s*([^;]+);", re.MULTILINE)
WANT = "offband::DEFAULT_PATH_HASH_MODE"
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]helpers/PathHashDefault\.h[>"]', re.MULTILINE)


def failures(texts: dict[str, str]) -> list[str]:
    bad = []
    for name, text in texts.items():
        decls = DECL_RE.findall(text)
        if len(decls) != 1:
            bad.append(f"{name}: expected one `uint8_t path_hash_mode = ...;`, found {len(decls)}")
        elif decls[0].strip() != WANT:
            bad.append(f"{name}: path_hash_mode defaults to `{decls[0].strip()}`, not {WANT} -- "
                       f"fresh nodes of this role would not get the shared default (#1753)")
        if not INCLUDE_RE.search(text):
            bad.append(f"{name}: does not include helpers/PathHashDefault.h")
    return bad


def _real() -> dict[str, str]:
    return {p.relative_to(ROOT).as_posix(): p.read_text(encoding="utf-8") for p in STRUCTS}


def test_both_structs_use_the_shared_default():
    assert failures(_real()) == [], failures(_real())


def test_negative_a_literal_zero_is_caught():
    for name, text in _real().items():
        mutated = dict(_real())
        mutated[name] = text.replace(f"path_hash_mode = {WANT}", "path_hash_mode = 0")
        assert any("defaults to `0`" in b for b in failures(mutated)), name


def test_negative_a_literal_one_is_still_caught():
    # Right value, wrong source: a literal drifts the next time the constant changes.
    for name, text in _real().items():
        mutated = dict(_real())
        mutated[name] = text.replace(f"path_hash_mode = {WANT}", "path_hash_mode = 1")
        assert any("defaults to `1`" in b for b in failures(mutated)), name


def test_negative_an_include_only_mentioned_in_a_comment_is_caught():
    for name, text in _real().items():
        mutated = dict(_real())
        mutated[name] = INCLUDE_RE.sub("// see helpers/PathHashDefault.h", text)
        assert any("does not include" in b for b in failures(mutated)), name


def test_negative_a_deleted_declaration_is_caught():
    for name, text in _real().items():
        mutated = dict(_real())
        mutated[name] = DECL_RE.sub("", text)
        assert any("found 0" in b for b in failures(mutated)), name


if __name__ == "__main__":
    n_fail = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as e:
                n_fail += 1
                print(f"FAIL {name}: {e}")
    print(f"\n{n_fail} failure(s)")
    sys.exit(1 if n_fail else 0)
