#!/usr/bin/env python
"""
Inject Crosswire identity into the build as preprocessor defines.

Called from platformio.ini per-platform `extra_scripts`. Sets:

  CROSSWIRE_VERSION     - from `git describe --tags --match 'crosswire-v*'`
  CROSSWIRE_GIT_SHA     - from `git rev-parse --short HEAD`
  CROSSWIRE_BRANCH      - from `git rev-parse --abbrev-ref HEAD`
  CROSSWIRE_BUILD_DATE  - current UTC date (YYYY-MM-DD)

The upstream baseline is already exposed by MeshCore's existing
FIRMWARE_VERSION / FIRMWARE_BUILD_DATE macros (set per-example in
MyMesh.h with `#ifndef` guards). Crosswire-aware code combines the two
via the format defined in VERSIONING.md:

  "MC <FIRMWARE_VERSION> / Crosswire <CROSSWIRE_VERSION>"

See VERSIONING.md for the full scheme. Epic #176 / LoRa-edl; FF2 #179.
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


# Use BUILD_FLAGS form for explicit -D injection. The escaped backslash-quote
# ensures the value reaches the preprocessor as a quoted C string literal.
env.Append(  # type: ignore[name-defined]  # noqa: F821
    BUILD_FLAGS=[
        f'-DCROSSWIRE_VERSION=\\"{crosswire_version}\\"',
        f'-DCROSSWIRE_GIT_SHA=\\"{crosswire_git_sha}\\"',
        f'-DCROSSWIRE_BRANCH=\\"{crosswire_branch}\\"',
        f'-DCROSSWIRE_BUILD_DATE=\\"{crosswire_build_date}\\"',
    ]
)

print("[crosswire] embedded identity:")
print(f"[crosswire]   CROSSWIRE_VERSION    = {crosswire_version}")
print(f"[crosswire]   CROSSWIRE_GIT_SHA    = {crosswire_git_sha}")
print(f"[crosswire]   CROSSWIRE_BRANCH     = {crosswire_branch}")
print(f"[crosswire]   CROSSWIRE_BUILD_DATE = {crosswire_build_date}")
