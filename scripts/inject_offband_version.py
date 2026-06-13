#!/usr/bin/env python
"""
Inject Offband identity into the build as preprocessor defines.

Called from platformio.ini per-platform `extra_scripts`. Sets:

  OFFBAND_VERSION         - from `git describe --tags --match 'offband-v*'`
  OFFBAND_GIT_SHA         - from `git rev-parse --short HEAD`
  OFFBAND_BRANCH          - from `git rev-parse --abbrev-ref HEAD`
  OFFBAND_BUILD_DATE      - current UTC date (YYYY-MM-DD)
  OFFBAND_IDENTITY_BLOB   - combined marker-wrapped form for binary scanners

The upstream baseline is already exposed by MeshCore's existing
FIRMWARE_VERSION / FIRMWARE_BUILD_DATE macros (set per-example in
MyMesh.h with `#ifndef` guards). Offband-aware code combines the two
via the format defined in VERSIONING.md:

  "MC <FIRMWARE_VERSION> / Offband <OFFBAND_VERSION>"

See VERSIONING.md for the full scheme. Epic #176 / LoRa-edl; FF2 #179.

================================================================
OFFBAND_IDENTITY_BLOB FORMAT  --  ON-WIRE ABI WARNING (#200)
================================================================
The blob is embedded in the firmware binary's .rodata and parsed back
out by scripts/firmware_identity.py at flash time. The parser is
deployed separately from the firmware image, so the format is an
on-wire ABI between the build and the flash wrappers.

  OBND_BEGIN|OBND_VER=<ver>|OBND_SHA=<sha>|OBND_BD=<date>|OBND_BR=<branch>|OBND_END

Rules:
  * Field order is fixed (parsers may rely on it for boundary detection).
  * OBND_BR is intentionally LAST before OBND_END so a branch name
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


offband_version = _git(
    "describe", "--tags",
    "--match", "offband-v*",
    "--abbrev=7", "--dirty",
    # --always: when no offband-v* tag is reachable (e.g. the unified
    # firmware-base line before its first release tag), fall back to the
    # abbreviated commit SHA instead of a non-distinguishing "untagged"
    # string, so every build self-identifies on the OLED splash + CLI
    # (Strycher/LoRa#319). `default` only fires if git itself is unavailable.
    "--always",
    default="offband-untagged",
)
offband_git_sha = _git("rev-parse", "--short=7", "HEAD")
offband_branch = _git("rev-parse", "--abbrev-ref", "HEAD")
offband_build_date = _build_date_utc()


# Combined marker-wrapped blob for binary scanners. See "ON-WIRE ABI WARNING"
# block in the module docstring above before changing format. Field order is
# fixed: branch is last because branch names can contain `|`.
offband_identity_blob = (
    "OBND_BEGIN"
    f"|OBND_VER={offband_version}"
    f"|OBND_SHA={offband_git_sha}"
    f"|OBND_BD={offband_build_date}"
    f"|OBND_BR={offband_branch}"
    "|OBND_END"
)

# Emit the version macros via a GENERATED HEADER that we force-include (-include),
# rather than command-line -D macros. OFFBAND_IDENTITY_BLOB contains '|'
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
_hdr = os.path.join(_build_dir, "offband_version.h")
with open(_hdr, "w", encoding="utf-8") as _f:
    _f.write(
        "#pragma once\n"
        f"#define OFFBAND_VERSION {_cstr(offband_version)}\n"
        f"#define OFFBAND_GIT_SHA {_cstr(offband_git_sha)}\n"
        f"#define OFFBAND_BRANCH {_cstr(offband_branch)}\n"
        f"#define OFFBAND_BUILD_DATE {_cstr(offband_build_date)}\n"
        f"#define OFFBAND_IDENTITY_BLOB {_cstr(offband_identity_blob)}\n"
    )
env.Append(CCFLAGS=["-include", _hdr])  # type: ignore[name-defined]  # noqa: F821

print("[offband] embedded identity:")
print(f"[offband]   OFFBAND_VERSION    = {offband_version}")
print(f"[offband]   OFFBAND_GIT_SHA    = {offband_git_sha}")
print(f"[offband]   OFFBAND_BRANCH     = {offband_branch}")
print(f"[offband]   OFFBAND_BUILD_DATE = {offband_build_date}")
print(f"[offband]   OFFBAND_IDENTITY_BLOB embedded ({len(offband_identity_blob)} bytes)")
