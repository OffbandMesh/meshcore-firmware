#!/usr/bin/env python3
"""Self-test for check_release_no_build_cache.py (#1824).

A guard that cannot fail is worthless, so this proves the check rejects what it
claims to reject before anyone relies on it passing. Both directions, per the
standing positive-and-negative rule.

Run: python scripts/test_check_release_no_build_cache.py
"""
from __future__ import annotations

import sys
import textwrap
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_release_no_build_cache as guard  # noqa: E402

PASSES = 0
FAILURES: list[str] = []


def expect(name: str, condition: bool, detail: str = "") -> None:
    global PASSES
    if condition:
        PASSES += 1
    else:
        FAILURES.append(f"{name}{(': ' + detail) if detail else ''}")


def write(tmp: Path, body: str) -> Path:
    path = tmp / "wf.yml"
    path.write_text(textwrap.dedent(body).lstrip("\n"), encoding="utf-8")
    return path


def main() -> int:
    import tempfile

    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)

        # --- MUST FAIL: a build-object cache in a release workflow -----------
        expect(
            "rejects a .pio/build cache",
            guard.check_file(write(tmp, """
                jobs:
                  build:
                    steps:
                      - uses: actions/checkout@v5
                      - uses: actions/cache@v5
                        with:
                          path: .pio/build/${{ matrix.env }}
                          key: pioobj-${{ matrix.env }}
                      - name: Build
                        run: pio run -e ${{ matrix.env }}
            """)),
            "a cache step naming .pio/build was accepted",
        )

        expect(
            "rejects it under a multi-line path: block",
            guard.check_file(write(tmp, """
                jobs:
                  build:
                    steps:
                      - uses: actions/cache@v5
                        with:
                          path: |
                            ~/.platformio
                            .pio/build
                          key: mixed
            """)),
            "a multi-line path containing .pio/build was accepted",
        )

        expect(
            "rejects a Windows-style separator",
            guard.check_file(write(tmp, """
                jobs:
                  build:
                    steps:
                      - uses: actions/cache@v5
                        with:
                          path: .pio\\build\\env
                          key: win
            """)),
            "a backslash build path was accepted",
        )

        # --- MUST PASS -------------------------------------------------------
        expect(
            "accepts a toolchain-only cache",
            not guard.check_file(write(tmp, """
                jobs:
                  build:
                    steps:
                      - uses: actions/cache@v5
                        with:
                          path: ~/.platformio
                          key: pio-${{ runner.os }}
                      - name: Build
                        run: pio run -e ${{ matrix.env }}
            """)),
            "caching ~/.platformio was wrongly rejected",
        )

        expect(
            "accepts .pio/build as an ARTIFACT path, not a cache",
            not guard.check_file(write(tmp, """
                jobs:
                  build:
                    steps:
                      - uses: actions/cache@v5
                        with:
                          path: ~/.platformio
                          key: pio
                      - uses: actions/upload-artifact@v4
                        with:
                          path: |
                            .pio/build/${{ matrix.env }}/firmware.bin
                            .pio/build/${{ matrix.env }}/firmware.uf2
            """)),
            "uploading firmware out of .pio/build was wrongly rejected -- that is "
            "how the product is collected, and flagging it would make the guard "
            "unusable in exactly the file it guards",
        )

        expect(
            "accepts a commented-out build cache",
            not guard.check_file(write(tmp, """
                jobs:
                  build:
                    steps:
                      - uses: actions/cache@v5
                        with:
                          # path: .pio/build/${{ matrix.env }}   <- deliberately not enabled
                          path: ~/.platformio
                          key: pio
            """)),
            "a commented path was treated as live config",
        )

        expect(
            "accepts a workflow with no cache step at all",
            not guard.check_file(write(tmp, """
                jobs:
                  build:
                    steps:
                      - uses: actions/checkout@v5
                      - run: pio run -e ${{ matrix.env }}
            """)),
        )

    # --- the real tree must pass ------------------------------------------
    expect(
        "the repository's own release workflows build clean",
        guard.main() == 0,
        "check_release_no_build_cache failed against the real tree",
    )

    print(f"{PASSES} passed, {len(FAILURES)} failed")
    for f in FAILURES:
        print(f"  FAIL {f}", file=sys.stderr)
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
