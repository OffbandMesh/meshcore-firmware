#!/usr/bin/env python3
"""
scripts/firmware_identity.py - Extract Offband identity from firmware .bin

#200 / LoRa-wek: closes the audit-log integrity bug where
get_firmware_identity() in scripts/pio-flash.py + scripts/ota-push.py
shelled out to `git describe` against the live working tree at flash time,
snapshotting newer HEAD instead of the commit that produced the .bin sitting
on disk. The .bin can sit for hours or days between build and push; the
working tree advances; the audit log records the wrong commit.

Fix: at build time, scripts/inject_offband_version.py embeds a marker-
wrapped identity blob in the firmware binary's .rodata via a
OFFBAND_IDENTITY_BLOB CPPDEFINE consumed by an __attribute__((used))
static in helpers/CommonCLI.cpp. At flash time, this parser scans the .bin
for those markers and recovers the BUILD-time identity. The previous git-
derived behavior is kept as a fallback for pre-#200 builds, but tagged
with firmware_identity_source="git-fallback" in the audit log so stale-
format entries are visible.

Marker format (ON-WIRE ABI, see inject_offband_version.py docstring):

  OBND_BEGIN|OBND_VER=<ver>|OBND_SHA=<sha>|OBND_BD=<date>|OBND_BR=<branch>|OBND_END

Field order is fixed; OBND_BR is last so a branch containing | cannot
break boundary detection.
"""
from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Marker constants - must match scripts/inject_offband_version.py
# ---------------------------------------------------------------------------
_BEGIN = b"OBND_BEGIN"
_END = b"OBND_END"

# Field prefixes in their fixed order. Each prefix appears EXACTLY ONCE in a
# valid blob. The parser uses these as boundaries (looking for the *next*
# prefix to find where the current field's value ends), which is what lets a
# branch name like "feat/foo|bar" still parse correctly: the | inside the
# value is bounded by the literal `|OBND_<next>=` marker, not by a naive
# split on `|`.
_FIELDS_IN_ORDER = [
    ("offband_version", b"|OBND_VER="),
    ("offband_git_sha", b"|OBND_SHA="),
    ("offband_build_date", b"|OBND_BD="),
    ("offband_branch", b"|OBND_BR="),
]


# ---------------------------------------------------------------------------
# Binary scan - the primary identity source
# ---------------------------------------------------------------------------
def parse_identity_from_binary(firmware_path: Path) -> Optional[dict]:
    """
    Scan firmware .bin for the Offband identity blob.

    Returns a dict with offband_version / offband_git_sha /
    offband_branch / offband_build_date on success, or None if the
    file exists and is readable but contains no marker / a malformed
    marker (pre-#200 build, or corruption).

    Raises:
      FileNotFoundError: if firmware_path does not exist.
      OSError: for other read failures (permission denied, etc.).

    The "raise on file-access error" behavior (rather than swallowing
    and returning None) is required for #200 audit-log integrity. Before
    #228 this function caught all OSError/FileNotFoundError and returned
    None, making it impossible for the orchestrator to distinguish:
      * file doesn't exist (operator typo'd the path), vs.
      * file exists but has no marker (legitimate pre-#200 fallback).
    Both produced "git-fallback" in the audit log -- so an operator who
    pushed firmware from a bogus path got an entry indistinguishable
    from a real pre-#200 build. Surfacing FileNotFoundError lets the
    wrappers refuse to flash with a clear error instead of silently
    recording garbage.

    Implementation notes:
      * Reads the whole .bin into memory. Firmware images are ~1-2 MB so
        this is cheap and lets us use simple bytes.find() rather than a
        streaming scan.
      * Uses *boundary-based* field extraction (find the next field's
        prefix to locate the current field's end) rather than splitting
        on the | delimiter, so | inside a value (technically legal in
        git branch names) does not corrupt the parse.
      * Returns None ONLY on missing/malformed marker structure inside
        a readable file. File-access errors propagate.
    """
    data = firmware_path.read_bytes()

    begin = data.find(_BEGIN)
    if begin < 0:
        return None

    end = data.find(_END, begin + len(_BEGIN))
    if end < 0:
        return None

    # Slice the blob exclusive of BEGIN/END markers themselves. Each field
    # value runs from end-of-its-prefix to start-of-next-prefix (or to the
    # final | before OBND_END for the last field).
    blob = data[begin + len(_BEGIN):end]

    fields: dict[str, str] = {}
    for idx, (key, prefix) in enumerate(_FIELDS_IN_ORDER):
        # Find this field's prefix inside the blob.
        p = blob.find(prefix)
        if p < 0:
            return None  # required field missing -> malformed
        value_start = p + len(prefix)
        # Find next field's prefix (or end-of-blob for the last field).
        if idx + 1 < len(_FIELDS_IN_ORDER):
            next_prefix = _FIELDS_IN_ORDER[idx + 1][1]
            value_end = blob.find(next_prefix, value_start)
            if value_end < 0:
                return None
        else:
            # Last field: value runs to the trailing | (right before OBND_END,
            # which we sliced out above). If no trailing | found, fall back to
            # end-of-blob.
            value_end = blob.rfind(b"|", value_start)
            if value_end < 0:
                value_end = len(blob)
        try:
            fields[key] = blob[value_start:value_end].decode("utf-8")
        except UnicodeDecodeError:
            return None

    return fields


# ---------------------------------------------------------------------------
# Git fallback - mirrors pre-#200 behavior, only used when binary scan fails
# ---------------------------------------------------------------------------
def git_fallback_identity(firmware_dir: Path) -> dict:
    """
    Re-run git in the firmware source tree. This is the pre-#200 behavior
    and is structurally unreliable (working tree may have advanced past the
    build), but is the only thing we have for older builds without markers.
    All git failures degrade gracefully to "unknown".
    """
    def _git(*args, default="unknown"):
        try:
            r = subprocess.run(
                ["git", *args],
                cwd=str(firmware_dir),
                capture_output=True, text=True, check=False,
            )
            if r.returncode == 0:
                v = r.stdout.strip()
                return v if v else default
        except (subprocess.SubprocessError, FileNotFoundError):
            pass
        return default

    return {
        "offband_version": _git(
            "describe", "--tags", "--match", "offband-v*",
            "--abbrev=7", "--dirty",
            default="offband-untagged",
        ),
        "offband_git_sha": _git("rev-parse", "--short=7", "HEAD"),
        "offband_branch": _git("rev-parse", "--abbrev-ref", "HEAD"),
        "offband_build_date": _git(
            "log", "-1", "--format=%cd", "--date=format:%Y-%m-%d",
            default="unknown",
        ),
    }


# ---------------------------------------------------------------------------
# Orchestrator - what the wrappers actually call
# ---------------------------------------------------------------------------
_WEAK_FIELD_VALUES = frozenset({"", "unknown", "offband-untagged"})


def _is_binary_parse_trustworthy(parsed: dict) -> bool:
    """
    Validity gate for a non-None parse_identity_from_binary() result.

    Defends against the realistic threat: a build host where git is missing
    or has no history, causing inject_offband_version.py to silently emit
    its fallback strings ("unknown" / "offband-untagged") into all four
    fields. Without this gate, those bogus values would be recorded in the
    audit log as firmware_identity_source="binary-markers" - structurally
    valid but semantically useless for post-incident reconstruction.

    Policy: any field that matches a known weak value forces a fall through
    to the git path. The git-derived values at least describe the live
    working tree (the pre-#200 behavior we're replacing) instead of a row
    of "unknown"s.
    """
    for key in ("offband_version", "offband_git_sha",
                "offband_branch", "offband_build_date"):
        if parsed.get(key, "") in _WEAK_FIELD_VALUES:
            return False
    return True


def get_firmware_identity(firmware_path: Path, firmware_dir: Path) -> dict:
    """
    Primary entry point for the flash wrappers.

    Returns a dict shaped:
      {
        "offband_version":        str,
        "offband_git_sha":        str,
        "offband_branch":         str,
        "offband_build_date":     str,
        "firmware_identity_source": "binary-markers" | "git-fallback",
      }

    Order of preference:
      1. Scan firmware.elf (sibling of firmware_path). #213: the .elf is
         the universal source for post-#213 builds — every PlatformIO
         target produces firmware.elf (ESP32, nRF52, RP2040, STM32) and
         the constructor-based keepalive in helpers/CommonCLI.cpp ensures
         the marker bytes survive --gc-sections on every toolchain. The
         marker appears as raw bytes in .rodata, which the ELF format
         stores raw, so the existing substring scan works without an ELF
         parser.
      2. Scan firmware_path. Legacy fallback for post-#200 / pre-#213
         builds: ESP32 firmware.bin had the marker because the original
         in-handler keepalive worked for that platform. Kept for
         backward-compat with stale build artifacts that may still be
         floating around.
      3. Fall back to git in firmware_dir, annotated git-fallback.

    The schema annotation stays "binary-markers" for either successful
    binary source — consumers don't need to care which file the parser
    used, only that the identity came from build-time artifacts rather
    than from a stale git query at flash time.
    """
    # 1. Try sibling .elf first (universal across platforms post-#213).
    elf_path = firmware_path.parent / "firmware.elf"
    if elf_path.exists() and elf_path != firmware_path:
        parsed = parse_identity_from_binary(elf_path)
        if parsed is not None and _is_binary_parse_trustworthy(parsed):
            result = dict(parsed)
            result["firmware_identity_source"] = "binary-markers"
            return result

    # 2. Try the originally-passed firmware path (legacy: ESP32 .bin had
    #    the marker pre-#213). Also handles the case where the caller
    #    explicitly passed firmware.elf in step 1 (and step 1 was skipped
    #    by the elf_path != firmware_path guard).
    parsed = parse_identity_from_binary(firmware_path)
    if parsed is not None and _is_binary_parse_trustworthy(parsed):
        result = dict(parsed)
        result["firmware_identity_source"] = "binary-markers"
        return result

    # 3. Git fallback.
    fallback = git_fallback_identity(firmware_dir)
    fallback["firmware_identity_source"] = "git-fallback"
    return fallback
