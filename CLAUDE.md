# Crosswire -- Project CLAUDE.md

> **Read and follow [`C:\Dev\DifferentWire\standards\SAFELANE.md`](../../DifferentWire/standards/SAFELANE.md). No exceptions.**
> **Read and follow [`C:\Dev\DifferentWire\standards\CLAUDE-BASE.md`](../../DifferentWire/standards/CLAUDE-BASE.md). No exceptions.**

These two documents are the canonical inheritance for this project. Anything below extends or parameterizes them; nothing below overrides them. If there is a conflict, SAFELANE and CLAUDE-BASE win.

---

## What Crosswire is

A standalone MIT fork of [MeshCore](https://github.com/meshcore-dev/MeshCore) for cross-role firmware enhancements and optimization (companion/observer + repeater active; room/bridge not yet). See `README.md`.

## Project identity (READ FIRST -- do NOT conflate with LoRa)

Crosswire is its **own** project. `Strycher/LoRa` is the **separate personal origin** repo; the Crosswire fork is no longer part of LoRa. For ALL work in `C:\Dev\Crosswire` (including worktrees under `.worktrees/*`):

| Channel | Value |
|---|---|
| Citadel | `DW_PROJECT=Crosswire` (Strycher/Crosswire). **Never** `LoRa`. |
| GitHub issues/PRs | `Strycher/Crosswire` |
| Agent Mail `project_key` | **`app-c-dev-crosswire`** (path slug of `C:\Dev\Crosswire`). Register + send + read HERE. NOT `app-c-dev-lora` (the LoRa workspace), NOT `app-crosswire` (stray), NOT `app-c-dev-lora-crosswire` (RETIRED 2026-06-10 — pitched in the relocation). |
| Active agents (2026-06-10) | DustyFox, RedCreek |

Worktrees coordinate in the SAME `app-c-dev-crosswire` (resolve from the repo common dir, not the worktree path). The stale "in-flight firmware work tracked under the LoRa Citadel project" note in Migration status below is RETIRED: net-new Crosswire work is tracked under Crosswire (Citadel) + Strycher/Crosswire (issues) + `app-c-dev-crosswire` (Agent Mail).

## Project Parameters

| Parameter | Value |
|-----------|-------|
| `PROJECT_NAME` | Crosswire |
| `PROJECT_DIR` | `C:\Dev\Crosswire` (standalone repo; relocated out of the LoRa workspace 2026-06-10) |
| `INFRA_PROFILE` | Maker |
| `BUILD_COMMAND` | `pio run -e <env>` (run from this repo's working tree at `C:\Dev\Crosswire`) |
| `DEPLOY_TARGET` | Device flash over USB / OTA (no SCP deploy; firmware is flashed, not server-deployed) |
| `CITADEL_PROJECT` | `Crosswire` (Strycher/Crosswire) |
| `GITHUB_PROJECT_ID` | `PVT_kwHODGcOBc4BZuj8` (Project #14, "Crosswire Project Board") |
| `AGENT_MAIL_STATUS` | Canonical git hooks installed (preflight, pre-commit, post-commit, pre-push, commit-msg, block-direct-citadel-db). Firmware flash/OTA/agent-mail PreToolUse hooks PORTED (P5.2): block-raw-flash, block-raw-curl-ota, require-agent-mail-check (registered in `.claude/settings.json`). |

## Project board field IDs (project #14)

Recorded per REPOCONFIG (board field IDs captured in project CLAUDE.md). Consumed by `.github/workflows/sync-labels-to-board.yml`.

- `PROJECT_ID` = `PVT_kwHODGcOBc4BZuj8`
- Status field `PVTSSF_lAHODGcOBc4BZuj8zhUrOWo`: backlog `21424ffd`, todo `e440428b`, ready `1bbb2e8b`, in-progress `7f81218e`, testing `8275aca6`, deferred `c6ce415d`, done `da0f8be7`
- Priority field `PVTSSF_lAHODGcOBc4BZuj8zhUrPDQ`: P0 `4c4b0f45`, P1 `48f7b50e`, P2 `1a66b940`, P3 `a5ffd218`

**Required secret:** the sync workflow needs repo secret `PROJECT_PAT` (PAT with `project` + `repo` scope) — the default `GITHUB_TOKEN` cannot mutate a user-owned Projects v2 board. Not yet set as of board creation (2026-06-04).

## Migration status (IMPORTANT for agents)

**Firmware has migrated here.** `firmware-base` (this repo's default branch) is the canonical Crosswire firmware tree. The old `crosswire` branch of `Strycher/MeshCore` is retired and that **fork is archived** (2026-06-04, read-only; reversible via `gh repo unarchive`). Full history is preserved in this repo: branches (patch-id verified), all `crosswire-v*` release tags + `archive/*` tags, and Plan 3 (see Preserved artifacts). Design-of-record: `docs/architecture/2026-06-01-observer-architecture-review.md`.

Working-tree cutover COMPLETE (Strycher/Crosswire#10, 2026-06-10) -- the legacy `meshcore-firmware` clone is **retired/deleted**:
- **Build/flash run from THIS repo's working tree** (`C:\Dev\Crosswire`). Upstream MeshCore remotes live here: `upstream` = meshcore-dev/MeshCore and `iotthinks`, both **fetch-only** (`no-push`); `origin` = Strycher/Crosswire (push). `firmware-base` is a self-contained MeshCore fork (full `src/` tree), so future upstream merges (e.g. the deferred 1.16.0 base-update) fetch + merge directly here. Firmware work is tracked under the **Crosswire** Citadel project + Strycher/Crosswire issues (legacy Strycher/LoRa firmware issues are being migrated).
- **Net-new Crosswire requests / bugs / design-of-record are filed here** under the **Crosswire** Citadel project.
- **Flash discipline PORTED (P5.2):** `scripts/pio-flash` (+`.py`), `scripts/ota-push.py`, and the `block-raw-flash` / `block-raw-curl-ota` / `require-agent-mail-check` PreToolUse hooks live in this repo. The device registry `hardware-devices.yaml` is **gitignored** (per-host; holds LAN IPs/MACs) — copy `hardware-devices.example.yaml` to `hardware-devices.yaml` and populate it before flashing from this repo.

## Preserved artifacts / test fixtures

Intentionally-kept tags/branches that are **NOT on the build line** and must **NOT** be flagged as stranded or cleanup. CI never builds these; they are off `firmware-base` by design. Tags (not branches) are used so stale-branch tooling never touches them.

| Artifact | Type | What it is | Revive |
|---|---|---|---|
| `archive/plan3-web-ui-crash-fixture` | tag (→ `447cf206`) | Plan 3 observer web UI / HTTPS / web auth / AP setup — 7 file-pairs (WebServer, WebApi, WebUiAssets, WebAuth, WebSession, WebCertStore, ApSetupForm) + the `Strycher/LoRa#282` heap fix. Deferred dead-path (heap/TLS instability, `Strycher/LoRa#281/#282/#312`). Kept for salvageable code **and** as a deliberate crash/boot-cycle fixture for SafeBoot / rapid-reboot recovery testing (`Strycher/LoRa#264/#265/#267`). | `git checkout -b plan3-revive archive/plan3-web-ui-crash-fixture` |

Decision record: **Strycher/Crosswire#5** (closed, preserved-by-design). Do not casually merge any of these into `firmware-base`.

## Security

- MIT fork: preserve upstream copyright in `LICENSE.txt`.
- No secrets in the repo, ever. WiFi PSK / MQTT credentials / OTA tokens live only in gitignored per-host files.
- Never echo a configured PSK into logs, commit messages, issues, or chat. Log only derived properties (length, checksum) when diagnostics need it.

## Follow-ups (bootstrap gaps to close)

- `/work` slash command + `session-state.py` compaction-recovery hook: **now published canonically** (standards @27b3ec7 — `/work` #112, `session-state.py` #107). `/work` auto-syncs into `.claude/commands/` via preflight; `session-state.py` is a hook copy-in (not auto-synced). Port deferred by owner — do when picked up.
- Projects-v2 board + `sync-labels-to-board.yml` workflow: **DONE (2026-06-04)** — board #14 created, `board:*`/`priority:*` labels created, workflow committed + validated. Remaining: set repo secret `PROJECT_PAT` (workflow inert until then); board-view column grouping is a one-click UI step.
- `ci.yml`: **DONE (2026-06-04)** — matrix CI added + green (6 envs). Firmware flash/OTA hooks: **PORTED (P5.2).**

---

**Last updated:** 2026-06-04 (firmware migration complete; Strycher/MeshCore fork archived; board + CI + flash-discipline landed).
