# Crosswire Firmware Base Unification: Observer -> feature/safeboot

> **For agentic workers:** execute phase-by-phase with the build-verify gate after each. Every step has an exact command + a rollback. Tier-2 actions (compile, push) require explicit approval per SAFELANE.

**Goal:** Establish one canonical Crosswire firmware base by making `feature/safeboot` the base (full history, Crosswire rebrand, SafeBoot + variant matrix, MeshCore v1.15.0) and porting the Observer/companion line onto it via granular cherry-pick.

**Why this direction:** both branches are MeshCore **v1.15.0** (verified, identical `FIRMWARE_VERSION`). `crosswire` is a *shallow* clone (history truncated at the graft); `feature/safeboot` has full history + the rebrand + SafeBoot + the variant matrix. The observer feature set is smaller and largely additive, so moving it onto the richer base is less work and lands on the better base. No meaningful history loss (cherry-pick preserves content/message/author; only SHAs change; the GitHub PR/issue trail is independent; full upstream history is *gained*).

**Architectural decision this forces (OWNER SIGN-OFF REQUIRED — see S6):** SafeBoot edits MeshCore core (`Mesh.cpp`, `NRF52Board.cpp`) + variant inis, so **Crosswire is a true MeshCore fork, not a "compose MeshCore as a dependency" consumer.** This *supersedes* the compose-not-inherit conclusion of the 2026-06-01 architecture review for the firmware-as-a-whole. The observer's decoupling (Session isolation, command bus, transport router) becomes an **internal refactor within the fork**, not a repo-boundary mechanism. The new `Strycher/Crosswire` repo therefore eventually holds the **full fork tree** (this unified base), not a thin consumer.

**Overlap analysis (verified):** `feature/safeboot` has NO `wifi_observer`, NO `wifi_telemetry`, and a Bluedroid BLE stack. So: `wifi_observer`/`wifi_telemetry` port purely additively (new files, zero conflict); the NimBLE migration (#288) replays onto the same Bluedroid starting point it originally migrated from; the only real conflict hotspot is `examples/companion_radio/main.cpp` (SafeBoot's `setup()` hook vs observer init wiring).

---

## Phase 0 — Prep & safety (Tier 1)

- [ ] **0.1 Archive-tag both tips (rollback anchors).**
  `git tag archive/crosswire-pre-migration strycher/crosswire`
  `git tag archive/feature-safeboot-pre-migration strycher/feature/safeboot`
  `git push strycher archive/crosswire-pre-migration archive/feature-safeboot-pre-migration`
  Rollback for the entire migration = check out these tags.

- [ ] **0.2 Un-shallow the clone** so merge-base + cherry-pick tooling are honest.
  `git -C meshcore-firmware fetch --unshallow origin` (origin = meshcore-dev/MeshCore)
  Verify: `git rev-parse --is-shallow-repository` -> `false`.
  Rollback: none needed (read-only history extension).

- [ ] **0.3 Create the working worktree off feature/safeboot.**
  `git -C meshcore-firmware worktree add .worktrees/crosswire-base -b feat/crosswire-base strycher/feature/safeboot`
  All porting happens here. Rollback: `git worktree remove .worktrees/crosswire-base` + `git branch -D feat/crosswire-base`.

## Phase 1 — Identify the observer commit set (Tier 0)

- [ ] **1.1 Enumerate substantive observer feature commits** (exclude merges/build-flag-only). From `git log --oneline 528bf3f6..strycher/crosswire`, the cherry-pick set is the ~30-40 in three groups:
  - **Additive (clean):** ConfigSchema, MqttAuth, MqttBroker, MqttBrokerPool, MqttPayload, ObserverPipeline, ObserverCli, WifiBootstrap, CrashLog v5/6/7, the `wifi_observer/` + `wifi_telemetry/` dirs.
  - **Integration (NimBLE #288):** N1-N5 + the v2.x API fixes (SerialBLEInterface Bluedroid->NimBLE, companion_radio_ble env flags).
  - **Shared-file:** CliPassthrough + SystemChannelCli slot-40 (Plan 3 Task 10), #313/#319/#322 — these touch `main.cpp`/`MyMesh.cpp`.
  Produce an ordered SHA list (oldest-first) as `docs/plans/observer-cherrypick-set.txt`.

- [ ] **1.2 Mark the coex/secrets commits** that may already differ on the SafeBoot base (e.g., the #209 PIO_SECRETS refactor, the V4 coex fix) — decide keep/skip per commit.

## Phase 2 — Dry-run conflict surface (Tier 1, no commits)

- [ ] **2.1 Replay the set with `git cherry-pick -n` (no-commit), one group at a time**, capturing conflicts WITHOUT committing. Expected clean: the additive group. Expected conflict: `companion_radio/main.cpp` (SafeBoot hook), possibly `MyMesh.cpp`.
- [ ] **2.2 Document the per-file resolution strategy**, then `git cherry-pick --abort` / `git reset --hard`. Output: a conflict-resolution note appended to this plan.

## Phase 3 — Execute the port (Tier 1 edits)

- [ ] **3.1 Cherry-pick the additive group** (expect clean — new files). Commit each (preserves granular history).
- [ ] **3.2 Cherry-pick the NimBLE group**; resolve `SerialBLEInterface` Bluedroid->NimBLE (same transition as the original migration).
- [ ] **3.3 Cherry-pick the Plan-3/CLI group**; resolve `main.cpp` so SafeBoot's pre-init `setup()` hook runs FIRST (power guard before any peripheral), then observer init.
- [ ] **3.4 Re-apply the in-flight #325 (serial CLI) + #327 (mqtt leak) patches** onto the new base (currently uncommitted in their worktrees). These were authored at v1.15.0 so they re-apply cleanly; re-run their Gemini-reviewed form.

## Phase 4 — Build verification (Tier 2 — per-build approval) [GATE]

- [ ] **4.1** Build `heltec_v4_companion_observer_wifi` -> SUCCESS (observer intact).
- [ ] **4.2** Build the repeater env (`simple_repeater` + burst-wifi/wifi_telemetry variant) -> SUCCESS.
- [ ] **4.3** Build a SafeBoot-enabled variant -> SUCCESS (SafeBoot intact post-port).
- [ ] **4.4** Build V3 + XIAO observer envs -> SUCCESS (the matrix the observer line targeted).
  Any failure -> stop, fix on the worktree, do not proceed. main stays clean.

## Phase 5 — Land as the canonical Crosswire firmware (Tier 2 — explicit approval) [GATE]

- [ ] **5.1** Decide the canonical branch + how the unified tree becomes `Strycher/Crosswire`'s content (push as the repo's firmware = the "code migration" endpoint finally executed, now as a fork tree). See S6.
- [ ] **5.2** Port the LoRa flash-discipline (`hardware-devices.yaml`, `pio-flash`, `block-raw-flash`/`block-raw-curl-ota`/`require-agent-mail-check` hooks) into Crosswire BEFORE any flash happens from it.
- [ ] **5.3** On-hardware smoke test (ST-P V4 observer + an hv3 repeater) before declaring the base canonical. NO PR/merge without the owner's hands-on test.

## S6 — Open decisions for the owner

1. **Fork vs compose (load-bearing):** confirm Crosswire is a full MeshCore fork (SafeBoot requires it), superseding the compose-not-inherit firmware framing. The observer decoupling becomes internal.
2. **Granular cherry-pick (recommended) vs squash-port** the observer delta.
3. **When the unified base populates `Strycher/Crosswire`** (now, vs after build+hardware verify).
4. **#325/#327 disposition** — fold into the port, or keep as follow-on PRs against the new base.

## Rollback (whole migration)
Everything happens on `feat/crosswire-base` (throwaway worktree) off `feature/safeboot`. `crosswire` and `feature/safeboot` are untouched and archive-tagged (0.1). Abandon = remove the worktree + branch; nothing canonical changes until Phase 5.
