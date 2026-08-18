# Offband (meshcore-firmware) -- Project CLAUDE.md

> **Read and follow [`C:\Dev\DifferentWire\standards\SAFELANE.md`](../../DifferentWire/standards/SAFELANE.md). No exceptions.**
> **Read and follow [`C:\Dev\DifferentWire\standards\CLAUDE-BASE.md`](../../DifferentWire/standards/CLAUDE-BASE.md). No exceptions.**

These two documents are the canonical inheritance for this project. Anything below extends or parameterizes them; nothing below overrides them. If there is a conflict, SAFELANE and CLAUDE-BASE win.

---

## What Offband is

Offband is a standalone MIT fork of [MeshCore](https://github.com/meshcore-dev/MeshCore) for cross-role firmware enhancements and optimization (companion/observer + repeater active; room/bridge not yet). Firmware repo: **`OffbandMesh/meshcore-firmware`**. See `README.md`.

> Formerly **Crosswire** — the product was rebranded to **Offband** (#100) and the repo moved `Strycher/Crosswire` → `OffbandMesh/meshcore-firmware` (#107, 2026-06-13). The **Citadel project and Agent-Mail key intentionally keep the legacy `Crosswire` name** (Citadel has no project-rename/batch-move — DifferentWire/citadel#81; renaming would strand 80+ tasks and the `Crosswire-xxx` short-ids). The name mismatch is cosmetic.

## Project identity (READ FIRST)

This firmware is **Offband**, repo **`OffbandMesh/meshcore-firmware`**, working dir **`C:\Dev\meshcore-firmware`**. For ALL work here (including worktrees):

| Channel | Value |
|---|---|
| Citadel | **`DW_PROJECT=Crosswire` — MANDATORY to export.** The git hooks default the project to the repo basename (`meshcore-firmware`), which is NOT a Citadel project, so commits/pushes fail unless `DW_PROJECT=Crosswire` is set. **Never** `LoRa`. (worktree project-resolution bug: #22.) |
| GitHub issues/PRs | `OffbandMesh/meshcore-firmware` |
| GitHub Project board | OffbandMesh org board **#1** (`PVT_kwDOEXsS3c4BaleW`) |
| Agent Mail `project_key` | **`app-c-dev-crosswire`** — register + send + read HERE. Register with `register_agent(project_key="app-c-dev-crosswire", ...)` directly; do **NOT** `ensure_project` on the path (would mint a stray `app-c-dev-meshcore-firmware` and break coordination). NOT `app-c-dev-lora`, NOT `app-crosswire`. |
| Do NOT conflate | `Strycher/LoRa` (separate personal origin repo) and `Strycher/meshcore-open` (separate client fork — not this project). |

Worktrees coordinate in the SAME `app-c-dev-crosswire` (resolve from the repo common dir, not the worktree path).

## Project Parameters

| Parameter | Value |
|-----------|-------|
| `PROJECT_NAME` | Offband (Citadel project + `DW_PROJECT` = `Crosswire`; see Project identity) |
| `PROJECT_DIR` | `C:\Dev\meshcore-firmware` (renamed from `C:\Dev\Crosswire` 2026-06-13, #107) |
| `INFRA_PROFILE` | Maker |
| `BUILD_COMMAND` | `pio run -e <env>` (run from this repo's working tree at `C:\Dev\meshcore-firmware`) |
| `DEPLOY_TARGET` | Device flash over USB / OTA (no SCP deploy; firmware is flashed, not server-deployed) |
| `CITADEL_PROJECT` | `Crosswire` (now → `OffbandMesh/meshcore-firmware`; name kept, citadel#81). Always `DW_PROJECT=Crosswire`. |
| `GITHUB_PROJECT_ID` | `PVT_kwDOEXsS3c4BaleW` (OffbandMesh org board #1) |
| `AGENT_MAIL_STATUS` | Canonical git hooks installed (preflight, pre-commit, post-commit, pre-push, commit-msg, block-direct-citadel-db). Firmware flash/OTA/agent-mail PreToolUse hooks PORTED (P5.2): block-raw-flash, block-raw-curl-ota, require-agent-mail-check (registered in `.claude/settings.json`). |

## Project board field IDs (OffbandMesh org board #1)

Recorded per REPOCONFIG (board field IDs captured in project CLAUDE.md). Consumed by `.github/workflows/sync-labels-to-board.yml`.

- `PROJECT_ID` = `PVT_kwDOEXsS3c4BaleW`
- Status field `PVTSSF_lADOEXsS3c4BaleWzhVcBL8`: backlog `91d35710`, todo `7bf7b9ef`, ready `ec816a33`, in-progress `ee71a6e3`, testing `3dc70fe4`, deferred `fc4959bc`, done `4a67db65`
- Priority field `PVTSSF_lADOEXsS3c4BaleWzhVcBMs`: P0 `43b5c396`, P1 `40c7b471`, P2 `3ed2b368`, P3 `2406bdd1`
- Agent field `PVTF_lADOEXsS3c4BaleWzhfvB7E` — **REST id `401541041`**, TEXT. MANDATORY at claim; see "Agent board field" below (#826).

### Agent board field — MANDATORY at claim (owner directive 2026-08-18, #826)

**Every issue you pick up and work gets your agent identity written to the board's
Agent field. No exceptions.** Set it at the same moment you claim the Citadel task
and announce pickup on Agent Mail. Value format = the standards#230 identity stamp:
`YourName (session <8-char-uuid>)`.

```bash
item=$(gh api "orgs/OffbandMesh/projectsV2/1/items?q=<issue number>" --jq '.[0].id')
gh api -X PATCH "orgs/OffbandMesh/projectsV2/1/items/$item" \
  --input - <<< '{"fields":[{"id":401541041,"value":"YourName (session xxxxxxxx)"}]}'
```

Gotchas (each one has already cost a 422 or a mis-stamp):
- REST wants the **numeric** field id (`401541041`), NOT the `PVTF_…` node id.
- `fields` must be an **array** of `{id, value}` objects, not a map.
- The `q=` search is fuzzy — read back `.content.number`/`.content.title` and confirm it
  is your issue **before** the PATCH, or you stamp someone else's item.
- Works with plain session `gh` auth (verified live 2026-08-18); no PAT needed.

**Required secret:** the sync workflow needs repo secret `PROJECT_PAT` — a **classic** PAT with `project` + `repo` scope (the default `GITHUB_TOKEN` cannot mutate an org-owned Projects v2 board). ⚠ **Must be classic, not fine-grained:** OffbandMesh rejects fine-grained PATs with >366-day lifetime for org-Projects access, so `GITHUB_PERSONAL_ACCESS_TOKEN` (fine-grained) does **not** work (DifferentWire/standards#148). **Set + verified** 2026-06-14 (board sync confirmed live).

## Migration status (IMPORTANT for agents)

**Firmware has migrated here.** `firmware-base` (this repo's default branch) is the canonical Offband (meshcore-firmware) firmware tree. The old `crosswire` branch of `Strycher/MeshCore` is retired and that **fork is archived** (2026-06-04, read-only; reversible via `gh repo unarchive`). Full history is preserved in this repo: branches (patch-id verified), all `crosswire-v*` release tags + `archive/*` tags, and Plan 3 (see Preserved artifacts). Design-of-record: `docs/architecture/2026-06-01-observer-architecture-review.md`.

Working-tree cutover COMPLETE (OffbandMesh/meshcore-firmware#10, 2026-06-10) -- the legacy pre-relocation clone is **retired/deleted** (distinct from the current `C:\Dev\meshcore-firmware` working dir, which was `C:\Dev\Crosswire` until the 2026-06-13 rename):
- **Build/flash run from THIS repo's working tree** (`C:\Dev\meshcore-firmware`). The upstream MeshCore remote lives here: `upstream` = meshcore-dev/MeshCore (**fetch-only**, `no-push`); `origin` = OffbandMesh/meshcore-firmware (push). `firmware-base` is a self-contained MeshCore fork (full `src/` tree) **on the MeshCore 1.16.0 base** — the 1.16.0 base-update landed in `offband-v1.0.0` (#126/#134, 2026-06-17). **Offband does NOT merge from upstream MeshCore — there is no intent to track upstream.** The `upstream` remote is kept **fetch-only for reference/diffing, not merging.** Do, however, **keep nomenclature + coding standards consistent with MeshCore** (member/method naming, base-class conventions) — it keeps any future rebasing/cherry-picking clean (#197). Firmware work is tracked under the **Crosswire** Citadel project + OffbandMesh/meshcore-firmware issues.
- **Net-new Crosswire requests / bugs / design-of-record are filed here** under the **Crosswire** Citadel project.
- **Flash discipline PORTED (P5.2):** `scripts/pio-flash` (+`.py`), `scripts/ota-push.py`, and the `block-raw-flash` / `block-raw-curl-ota` / `require-agent-mail-check` PreToolUse hooks live in this repo. The device registry `hardware-devices.yaml` is **gitignored** (per-host; holds LAN IPs/MACs) — copy `hardware-devices.example.yaml` to `hardware-devices.yaml` and populate it before flashing from this repo.

## Preserved artifacts / test fixtures

Intentionally-kept tags/branches that are **NOT on the build line** and must **NOT** be flagged as stranded or cleanup. CI never builds these; they are off `firmware-base` by design. Tags (not branches) are used so stale-branch tooling never touches them.

| Artifact | Type | What it is | Revive |
|---|---|---|---|
| `archive/plan3-web-ui-crash-fixture` | tag (→ `447cf206`) | Plan 3 observer web UI / HTTPS / web auth / AP setup — 7 file-pairs (WebServer, WebApi, WebUiAssets, WebAuth, WebSession, WebCertStore, ApSetupForm) + the `Strycher/LoRa#282` heap fix. Deferred dead-path (heap/TLS instability, `Strycher/LoRa#281/#282/#312`). Kept for salvageable code **and** as a deliberate crash/boot-cycle fixture for SafeBoot / rapid-reboot recovery testing (`Strycher/LoRa#264/#265/#267`). | `git checkout -b plan3-revive archive/plan3-web-ui-crash-fixture` |

Decision record: **OffbandMesh/meshcore-firmware#5** (closed, preserved-by-design). Do not casually merge any of these into `firmware-base`.

## Hardware facts — read before any hardware work

Device inventory, RF chain (FEM / TX power), per-device MACs/roles, slot/pin maps, and flash paths live in **`HARDWARE.local.md`** (gitignored, per-host; symlink to the canonical `C:\Dev\LoRa\HARDWARE.md`). **Read it before asking about or acting on hardware**, and record newly-stated hardware facts there so the user never has to restate them. It holds LAN IPs / MACs / SSID / BLE PINs / GPS coordinates — **never commit it or paste its contents** into issues/PRs/chat.

## Security

- MIT fork: preserve upstream copyright in `LICENSE.txt`.
- No secrets in the repo, ever. WiFi PSK / MQTT credentials / OTA tokens live only in gitignored per-host files.
- Never echo a configured PSK into logs, commit messages, issues, or chat. Log only derived properties (length, checksum) when diagnostics need it.

## Release discipline

- **Release approval gate** — never push a release tag (`-rc` or stable) without first posting a concise **release preview** (version, `-rc`/stable, the CHANGELOG entry, any user-facing string change) and getting an explicit human "ship it." Validation/feedback ("it works" / "happy with it") is **not** release authorization. Scale the preview to the change — keep it light, not ceremony. Full rationale + format in [`VERSIONING.md`](VERSIONING.md) ("Release approval gate").

## PR definition-of-done (#477 — CI matrix is a required merge gate)

- `firmware-base` branch protection requires the **`ci-green`** check (aggregates `config-lint` + the FULL curated build matrix, incl. nRF52 + room_server envs). `gh pr merge --auto --rebase` therefore **waits for the matrix** — enabling auto-merge is no longer merging.
- **A PR task is NOT complete — and must not be closed in Citadel — until `gh pr view <n> --json state` reports `MERGED`.** A red matrix leaves the PR sitting unmerged; closing the task anyway is the SAFELANE §5 "declaring success without the artifact" violation. If `ci-green` fails, fixing the build (all matrix envs, not just the ones you tested on — the #350/#463 lesson) is part of the same task.
- Do not shrink or bypass the matrix to get green; matrix changes are owner-approved only.

## Follow-ups (bootstrap gaps to close)

- `/work` slash command: published canonically (standards @27b3ec7, `/work` #112) and auto-synced into `.claude/commands/` by preflight.
- `session-state.py` compaction-recovery hook: **INSTALLED AND WIRED — use it.** (standards @27b3ec7, #107.) It is a hook copy-in rather than auto-synced, and it is wired the canonical way — **not** via `.claude/settings.json`: `preflight.sh` §6 **reads** the snapshot at SessionStart, `post-commit` **writes** it (REPOCONFIG.md §Enforcement Hooks). State file: `.claude/agent-session.json`.
  - **Agents: record state as you go** — `session-state.py note <text>`, `approval <action>`, `identity <name>`, `read`, `write`. This is what makes a compacted or fresh session resume with real context instead of guesswork. Long sessions are not a reason to stop work; unrecorded state is.
  - ⚠ **It has not actually been firing** — `core.hooksPath` is relative, so `post-commit` (and every other git-lifecycle hook) is inert inside worktrees, which is where all work happens. Tracked as **#639 (P0)**. Until that lands, write state **explicitly** and be aware that invoking the script *from a worktree* writes an orphaned `agent-session.json` there that preflight never reads — run it from the primary clone.
  - This line previously read "Port deferred by owner." **That was wrong: the owner did not defer it** (corrected 2026-08-11, #640).
- Projects-v2 board + `sync-labels-to-board.yml` workflow: re-pointed to **OffbandMesh org board #1** (2026-06-13, #107; was Strycher board #14). Remaining: set repo secret `PROJECT_PAT` on `OffbandMesh/meshcore-firmware` (workflow inert until then); board-view column grouping is a one-click UI step.
- `ci.yml`: **DONE (2026-06-04)** — matrix CI added + green (6 envs). Firmware flash/OTA hooks: **PORTED (P5.2).**

---

**Last updated:** 2026-08-18 (#826: board **Agent field** added — `PVTF_lADOEXsS3c4BaleWzhfvB7E` / REST id `401541041`; MANDATORY at claim per owner directive, recipe + gotchas in "Agent board field" under Project board field IDs). Prior: 2026-08-11 (#640: corrected the session-state.py line — it is INSTALLED AND WIRED, not "deferred by owner"; added the agent usage contract and the #639 caveat that git-lifecycle hooks are inert in worktrees). Prior: 2026-06-24 (#197: recorded **no-upstream-merge policy** — Offband does not merge from upstream MeshCore, no intent to track it; `upstream` remote stays fetch-only for reference; keep MeshCore nomenclature/coding-standard consistency for clean rebasing). Prior: 2026-06-17 (offband-v1.0.0 shipped — MeshCore 1.16.0 base-update landed #126/#134, + RAK3401 GPS #104, display always-on #141, display rotation 0/180 #148; corrected the migration-status note: 1.16.0 is no longer "deferred" and `iotthinks` is no longer a remote — #130). Prior: 2026-06-13 (#107 OffbandMesh cutover: repo → `OffbandMesh/meshcore-firmware`, working dir → `C:\Dev\meshcore-firmware`, board → OffbandMesh org #1, preflight path fixed; Citadel project + Agent-Mail key intentionally remain `Crosswire` / `app-c-dev-crosswire` — citadel#81).
