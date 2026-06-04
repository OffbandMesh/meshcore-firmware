#!/usr/bin/env python
"""
Inject Crosswire identity into the build as preprocessor defines.

Called from platformio.ini per-platform `extra_scripts`. Sets:

  CROSSWIRE_VERSION         - from `git describe --tags --match 'crosswire-v*'`
  CROSSWIRE_GIT_SHA         - from `git rev-parse --short HEAD`
  CROSSWIRE_BRANCH          - from `git rev-parse --abbrev-ref HEAD`
  CROSSWIRE_BUILD_DATE      - current UTC date (YYYY-MM-DD)
  CROSSWIRE_IDENTITY_BLOB   - combined marker-wrapped form for binary scanners

The upstream baseline is already exposed by MeshCore's existing
FIRMWARE_VERSION / FIRMWARE_BUILD_DATE macros (set per-example in
MyMesh.h with `#ifndef` guards). Crosswire-aware code combines the two
via the format defined in VERSIONING.md:

  "MC <FIRMWARE_VERSION> / Crosswire <CROSSWIRE_VERSION>"

See VERSIONING.md for the full scheme. Epic #176 / LoRa-edl; FF2 #179.

================================================================
CROSSWIRE_IDENTITY_BLOB FORMAT  --  ON-WIRE ABI WARNING (#200)
================================================================
The blob is embedded in the firmware binary's .rodata and parsed back
out by scripts/firmware_identity.py at flash time. The parser is
deployed separately from the firmware image, so the format is an
on-wire ABI between the build and the flash wrappers.

  XWIRE_BEGIN|XWIRE_VER=<ver>|XWIRE_SHA=<sha>|XWIRE_BD=<date>|XWIRE_BR=<branch>|XWIRE_END

Rules:
  * Field order is fixed (parsers may rely on it for boundary detection).
  * XWIRE_BR is intentionally LAST before XWIRE_END so a branch name
    containing the (theoretically-legal) | character cannot break the
    parser by being confused for a field boundary.
  * Do not change the begin / end markers, field prefixes, or delimiter
    without bumping a format version AND updating both the firmware
    consumer (helpers/CommonCLI.cpp marker line) AND the parser
    (scripts/firmware_identity.py).
================================================================
"""
import datetime
import subprocess

Import("env")  # type: ignore[name-defined]  # noqa: F821


def _git(*args, default="unknown"):
    """Run a git command; return stdout stripped, or default on any failure."""
    try:
        result = subprocess.run(
            ["git", *args],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode == 0:
            out = result.stdout.strip()
            return out if out else default
    except (subprocess.SubprocessError, FileNotFoundError):
        pass
    return default


def _build_date_utc():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")


crosswire_version = _git(
    "describe", "--tags",
    "--match", "crosswire-v*",
    "--abbrev=7", "--dirty",
    default="crosswire-untagged",
)
crosswire_git_sha = _git("rev-parse", "--short=7", "HEAD")
crosswire_branch = _git("rev-parse", "--abbrev-ref", "HEAD")
crosswire_build_date = _build_date_utc()


# Combined marker-wrapped blob for binary scanners. See "ON-WIRE ABI WARNING"
# block in the module docstring above before changing format. Field order is
# fixed: branch is last because branch names can contain `|`.
crosswire_identity_blob = (
    "XWIRE_BEGIN"
    f"|XWIRE_VER={crosswire_version}"
    f"|XWIRE_SHA={crosswire_git_sha}"
    f"|XWIRE_BD={crosswire_build_date}"
    f"|XWIRE_BR={crosswire_branch}"
    "|XWIRE_END"
)

# Emit the version macros via a GENERATED HEADER that we force-include (-include),
# rather than command-line -D macros. CROSSWIRE_IDENTITY_BLOB contains '|'
# delimiters; as a -D value those are word-split by POSIX sh on Linux CI and
# break the compile (xtensa/arm g++ "no input files", Error 127). Inside a header
# the pipes are ordinary string-literal bytes -> portable on Windows + Linux.
# Macro names + values are unchanged, so consumers and the in-binary marker that
# firmware_identity.py scans are identical. (#334 -- CI portability fix)
def _cstr(s):  # minimal C string-literal escape
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'

import os
_build_dir = env.subst("$BUILD_DIR")  # type: ignore[name-defined]  # noqa: F821
os.makedirs(_build_dir, exist_ok=True)
_hdr = os.path.join(_build_dir, "crosswire_version.h")
with open(_hdr, "w", encoding="utf-8") as _f:
    _f.write(
        "#pragma once\n"
        f"#define CROSSWIRE_VERSION {_cstr(crosswire_version)}\n"
        f"#define CROSSWIRE_GIT_SHA {_cstr(crosswire_git_sha)}\n"
        f"#define CROSSWIRE_BRANCH {_cstr(crosswire_branch)}\n"
        f"#define CROSSWIRE_BUILD_DATE {_cstr(crosswire_build_date)}\n"
        f"#define CROSSWIRE_IDENTITY_BLOB {_cstr(crosswire_identity_blob)}\n"
    )
env.Append(CCFLAGS=["-include", _hdr])  # type: ignore[name-defined]  # noqa: F821

print("[crosswire] embedded identity:")
print(f"[crosswire]   CROSSWIRE_VERSION    = {crosswire_version}")
print(f"[crosswire]   CROSSWIRE_GIT_SHA    = {crosswire_git_sha}")
print(f"[crosswire]   CROSSWIRE_BRANCH     = {crosswire_branch}")
print(f"[crosswire]   CROSSWIRE_BUILD_DATE = {crosswire_build_date}")
print(f"[crosswire]   CROSSWIRE_IDENTITY_BLOB embedded ({len(crosswire_identity_blob)} bytes)")
