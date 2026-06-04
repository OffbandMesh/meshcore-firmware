# Crosswire -- Project CLAUDE.md

> **Read and follow [`C:\Dev\DifferentWire\standards\SAFELANE.md`](../../DifferentWire/standards/SAFELANE.md). No exceptions.**
> **Read and follow [`C:\Dev\DifferentWire\standards\CLAUDE-BASE.md`](../../DifferentWire/standards/CLAUDE-BASE.md). No exceptions.**

These two documents are the canonical inheritance for this project. Anything below extends or parameterizes them; nothing below overrides them. If there is a conflict, SAFELANE and CLAUDE-BASE win.

---

## What Crosswire is

A standalone MIT fork of [MeshCore](https://github.com/meshcore-dev/MeshCore) for cross-role firmware enhancements and optimization (companion/observer + repeater active; room/bridge not yet). See `README.md`.

## Project Parameters

| Parameter | Value |
|-----------|-------|
| `PROJECT_NAME` | Crosswire |
| `PROJECT_DIR` | `C:\Dev\LoRa\Crosswire` (nested under the LoRa workspace, gitignored by LoRa) |
| `INFRA_PROFILE` | Maker |
| `BUILD_COMMAND` | `pio run -e <env>` (once firmware migrates here; see Migration status) |
| `DEPLOY_TARGET` | Device flash over USB / OTA (no SCP deploy; firmware is flashed, not server-deployed) |
| `CITADEL_PROJECT` | `Crosswire` (Strycher/Crosswire) |
| `GITHUB_PROJECT_ID` | TBD -- Projects-v2 board deferred until after initial bootstrap |
| `AGENT_MAIL_STATUS` | Canonical hooks installed (preflight, pre-commit, post-commit, pre-push, commit-msg, block-direct-citadel-db). Firmware flash/OTA/agent-mail PreToolUse hooks are NOT yet installed -- see Migration status. |

## Migration status (IMPORTANT for agents)

The firmware currently lives on the **`crosswire` branch of `Strycher/MeshCore`**, not in this repo yet. It migrates here after the compose-not-inherit architecture refactor (design-of-record: `docs/architecture/2026-06-01-observer-architecture-review.md`). Until then:

- **Code/build/flash work happens in the MeshCore fork** under the LoRa workspace (`C:\Dev\LoRa\meshcore-firmware`), tracked under the **LoRa** Citadel project + Strycher/LoRa issues.
- **This repo is the home for new requests, issues, and design-of-record.** New Crosswire feature requests / bugs filed here are tracked under the **Crosswire** Citadel project.
- **Flash discipline** (the `hardware-devices.yaml` registry, the `pio-flash` wrapper, the `block-raw-flash` / `block-raw-curl-ota` / `require-agent-mail-check` PreToolUse hooks) currently lives in the LoRa project. Those hooks MUST be ported into this repo when the firmware + flash workflow migrate. Do not flash hardware from this repo until that discipline is installed here.

## Security

- MIT fork: preserve upstream copyright in `LICENSE.txt`.
- No secrets in the repo, ever. WiFi PSK / MQTT credentials / OTA tokens live only in gitignored per-host files.
- Never echo a configured PSK into logs, commit messages, issues, or chat. Log only derived properties (length, checksum) when diagnostics need it.

## Follow-ups (bootstrap gaps to close)

- `/work` slash command (`work.md`) and `session-state` compaction-recovery hook: not present in the canonical templates at bootstrap time; add when available.
- Projects-v2 board + `sync-labels-to-board.yml` workflow: deferred (per owner) until after initial bootstrap.
- `ci.yml` + firmware flash/OTA hooks: deferred until the firmware code migrates into this repo.

---

**Last updated:** 2026-06-03 (repo establishment; code migration pending the architecture refactor).
