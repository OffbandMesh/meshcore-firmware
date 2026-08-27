r"""Canonical per-host state locations for this repo (#1012).

ONE registry and ONE flash history for meshcore-firmware, living OUTSIDE every
checkout. Not in the clone, not in a worktree, not shared with any other repo.

Why this module exists
----------------------
Before #1012 each consumer computed its own path as
``Path(__file__).parent.parent / "hardware-devices.yaml"`` -- i.e. relative to
the checkout the script happened to be running from. Every worktree that ran a
flash silently forked the registry, the history and the backups. Measured
2026-08-27: 80 worktrees, three registry copies (one 7 days stale) and the
flash audit trail split 2805 / 6 across two files.

That is a safety problem. #503 made the registry the thing that decides WHICH
PHYSICAL DEVICE gets written, and the history is the record of what was
written to it.

Scope
-----
THIS REPO ONLY. C:\Dev\LoRa and C:\Dev\wadamesh keep their own registries and
their own histories; nothing here reads, writes or merges them. Their device
short-names are not guaranteed to mean the same hardware.

Override precedence (matches the #27 pattern used by pio-flash FIRMWARE_DIR):
    per-artifact env var  >  OFFBAND_STATE_DIR  >  platform default
"""

from __future__ import annotations

import os
from pathlib import Path


def default_state_dir() -> Path:
    """Beside the clones, never inside one."""
    if os.name == "nt":
        # NB: the trailing separator matters -- Path("C:") / "Dev" is
        # DRIVE-RELATIVE on Windows, not C:\Dev.
        return Path(os.environ.get("SystemDrive", "C:") + "/") / "Dev" / ".offband"
    return Path.home() / ".offband"


def state_dir() -> Path:
    return Path(os.environ.get("OFFBAND_STATE_DIR") or default_state_dir())


def registry_path() -> Path:
    return Path(os.environ.get("PIO_FLASH_REGISTRY") or (state_dir() / "hardware-devices.yaml"))


def flash_history_path() -> Path:
    return Path(os.environ.get("PIO_FLASH_HISTORY") or (state_dir() / "flash-history.jsonl"))


def backups_dir() -> Path:
    return Path(os.environ.get("PIO_FLASH_BACKUPS") or (state_dir() / "flash-backups"))
