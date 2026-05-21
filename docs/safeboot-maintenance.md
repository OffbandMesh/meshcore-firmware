# SafeBoot Maintenance Workflows

This document describes the two workflows that keep `feature/safeboot` useful
across upstream MeshCore releases AND across our own custom fleet builds.

Both workflows live in this doc because they have to stay in sync — when
upstream ships a new release, both the fork branch AND the deploy branch
need to be updated, in order.

## Branch model

| Branch | Repo | Purpose |
|---|---|---|
| `main` | `Strycher/MeshCore` (fork) | Tracks upstream `meshcore-dev/MeshCore:main` exactly. No local modifications. |
| `feature/safeboot` | `Strycher/MeshCore` (fork) | Long-lived feature branch carrying the SafeBoot port. Always rebased onto (or merged with) the latest upstream release. **Never squash, never delete.** This is the branch the upstream PR is filed from. |
| `safeboot-vX.Y.Z` | `Strycher/MeshCore` tags | Release tags applied to `feature/safeboot` at each upstream-aligned build distributed to the community. |
| `deploy/issues-84-86-87-combined` (or successor) | `meshcore-firmware/` working clone (origin = `meshcore-dev/MeshCore`) | Our fleet-deployment branch. Contains custom features (#84/#86/#87 today, more later). SafeBoot is **merged or cherry-picked INTO** this branch — it does NOT live on this branch as a separate concern. |

## Remotes

### In the fork clone (`meshcore-fork/`)

```
origin    https://github.com/Strycher/MeshCore.git    (our fork)
upstream  https://github.com/meshcore-dev/MeshCore.git (community/canonical)
```

### In the deploy clone (`meshcore-firmware/`)

```
origin    https://github.com/meshcore-dev/MeshCore.git    (upstream — historical original)
iotthinks https://github.com/IoTThinks/MeshCore.git       (sibling fork, reference only)
strycher  https://github.com/Strycher/MeshCore.git        (our fork — fetches feature/safeboot)
```

The `strycher` remote is what makes the deploy-merge workflow below possible
without re-pointing origin.

---

## Workflow 1: Upstream-rebase (when upstream tags a new release)

When `meshcore-dev/MeshCore` ships a new release (e.g., `v1.10.0`):

```bash
cd C:/Dev/LoRa/meshcore-fork
git fetch upstream --tags --prune

# Confirm what we're targeting
git log --oneline upstream/main -5
git tag -l 'v*' --sort=-version:refname | head -5

# Switch to feature branch
git checkout feature/safeboot

# Rebase onto upstream main (preferred — keeps SafeBoot commits at the tip)
git rebase upstream/main

# Conflict expectations: SafeBoot is mostly additive (new SafeBoot.h/.cpp +
# setup() hook insertion + safety event enum entries + variant.h flag defines).
# Conflicts typically only affect:
#   - setup() insertion site if upstream restructured setup()
#   - SafetyEventType enum if upstream also added entries at our slot numbers
#   - variants/heltec_v4/platformio.ini if upstream touched the same build_flags block
# Resolve manually, preserving SafeBoot::checkAndMaybeSleep() as the FIRST
# executable line of setup() (before any board init).

git push --force-with-lease origin feature/safeboot

# Bring fork main up to date (fast-forward only)
git checkout main
git merge --ff-only upstream/main
git push origin main
```

### Validation after rebase (before tagging a release)

1. **Build verification**: `pio run -e heltec_v4_repeater` succeeds; firmware.bin
   within ~5 KB of bare-upstream-main build (SafeBoot's additive footprint).
2. **Bench test on ST-P** (per epic #96 SB6):
   - Normal boot at 4.0V → SafeBoot does not intervene
   - Low-voltage 3.5V → SafeBoot sleeps, wake observed, exponential backoff
   - Brownout reset → SafeBoot honors brownout cooldown
3. **Soak test**: ST-P runs the new build for ≥24 hours without regressions.
4. **Field validation**: only after bench is clean, OTA-deploy to patio.

### Tagging a release

#### Tag-name convention

Tags follow the pattern:

```
safeboot-<example-dir-hyphenated>[-<build-axis>]-vX.Y.Z[-<prerelease>]
```

- `safeboot-` — fork qualifier (distinguishes from upstream MeshCore tags)
- `<example-dir-hyphenated>` — MeshCore example directory the release builds
  from, with underscores converted to hyphens (e.g., `examples/simple_repeater/`
  → `simple-repeater`)
- `<build-axis>` — optional further scope when one example produces multiple
  release-types (e.g., `bridge-espnow`, `tft`)
- `vX.Y.Z` — semver aligned to the upstream MeshCore version the rebase is
  based on (e.g., `v1.15.0` when the fork rebases onto upstream `v1.15.0`)
- `[-<prerelease>]` — optional pre-release identifier (`-rc1`, `-rc2`, ...)
  for unvalidated builds

Examples:

| Tag | Scope |
|---|---|
| `safeboot-simple-repeater-v1.15.0-rc1` | `simple_repeater/` × 5 hardware variants, RC pre-bench-validation |
| `safeboot-simple-repeater-v1.15.0` | Same, post bench validation (drop `-rc1` suffix) |
| `safeboot-simple-repeater-bridge-espnow-v1.15.0` | `simple_repeater/` + ESPNow bridge variants (future) |
| `safeboot-simple-repeater-tft-v1.15.0` | `simple_repeater/` + TFT display variants (future) |
| `safeboot-companion-radio-v1.15.0` | `companion_radio/` × variants (future) |
| `safeboot-simple-room-server-v1.15.0` | `simple_room_server/` × variants (future) |
| `safeboot-simple-sensor-v1.15.0` | `simple_sensor/` × variants (future) |
| `safeboot-simple-secure-chat-v1.15.0` | `simple_secure_chat/` × variants (future) |
| `safeboot-kiss-modem-v1.15.0` | `kiss_modem/` × variants (future) |

Each release-type has (or will have) its own dedicated GitHub Actions
workflow scoped to a specific tag glob. The current shipping workflow
(`.github/workflows/build-safeboot-firmwares.yml`) triggers on
`safeboot-simple-repeater-*` only.

#### Multi-hyphen tag extraction

The `setup-build-environment` composite action uses a sed regex to extract
the version from tag names, replacing the upstream's `${GIT_TAG_NAME##*-}`
parameter expansion which only worked for single-hyphen formats. The
extraction:

```bash
GIT_TAG_VERSION=$(echo "$GIT_TAG_NAME" | sed -E 's/.*-(v[0-9].*)$/\1/')
```

is backwards-compatible with upstream's existing tags (`repeater-v1.15.0`
→ `v1.15.0`) AND supports the SafeBoot multi-hyphen format
(`safeboot-simple-repeater-v1.15.0-rc1` → `v1.15.0-rc1`).

#### Cutting a release

```bash
git checkout feature/safeboot

# First-RC pattern (pre-bench-validation): include the -rcN suffix
git tag -a safeboot-simple-repeater-v1.15.0-rc1 \
  -m "First SafeBoot RC, pre-bench-validation. Aligned to upstream
MeshCore v1.15.0+. See Strycher/LoRa#96."

# Post-bench-validation pattern: drop the -rcN suffix
# git tag -a safeboot-simple-repeater-v1.15.0 \
#   -m "Bench-validated. Aligned to upstream MeshCore v1.15.0."

git push origin safeboot-simple-repeater-v1.15.0-rc1
```

GitHub Actions builds and publishes per-variant artifacts (as a **draft**
release) to GitHub Releases automatically on tag push. For RC tags, leave
the release as draft until bench validation completes (F6 sub-tasks). For
non-RC tags, promote the draft to published after bench validation passes.

---

## Workflow 2: Deploy-merge (combining SafeBoot with our custom features)

Our fleet (patio, ST-P, and future devices) runs firmware built from
`deploy/issues-84-86-87-combined` (or its successor) in the
`meshcore-firmware/` clone. That branch carries our custom features (#84
neighbors schema, #86 MQTT remote OTA, #87 TX power tuning, future work)
that aren't in upstream MeshCore.

SafeBoot needs to be **merged in** to that branch for fleet deployment.

### When to execute this workflow

Every time `feature/safeboot` advances in a way the fleet should pick up:

- New SafeBoot code lands (e.g., SB2 ports the actual code, SB3 wires it
  into setup())
- Workflow 1 (upstream rebase) updates `feature/safeboot` against new
  upstream main — those upstream commits should also flow into the
  deploy branch

### The merge step

```bash
cd C:/Dev/LoRa/meshcore-firmware
git fetch strycher                               # pull latest feature/safeboot

# Option A: merge (preserves history of both sides)
git checkout deploy/issues-84-86-87-combined
git merge strycher/feature/safeboot
# Resolve conflicts if upstream changes hit the same code paths as our customs
# Likely conflict surface: any file SafeBoot modifies (setup() in main.cpp,
# MeshCore.h SafetyEventType enum, ESP32Board.cpp, variants/heltec_v4/platformio.ini)
# that also has our patches landed.

# Option B: cherry-pick (cleaner history, more work per release)
git checkout deploy/issues-84-86-87-combined
git log strycher/feature/safeboot ^upstream/main --oneline  # see SafeBoot-only commits
git cherry-pick <sha-range>
```

### Choosing between merge and cherry-pick

| Use merge when... | Use cherry-pick when... |
|---|---|
| You want full SafeBoot history in deploy branch | You want a flatter deploy branch history |
| Multiple SafeBoot commits, easier to merge all at once | Only a few commits, want them as distinct deploy-branch commits |
| You're routinely re-running this (e.g., after each Workflow 1) | One-time integration moments |

**Recommendation**: merge on routine SafeBoot updates; cherry-pick on the
INITIAL SafeBoot integration into the deploy branch so the deploy branch
gets clean per-feature commits.

### Validation after merge

1. **Build verification**: `pio run -e heltec_v4_repeater_telemetry` (our
   custom env with all the patio features) builds cleanly with SafeBoot
   included.
2. **Bench test on ST-P with the COMBINED build** (not just SafeBoot alone):
   - Verify SafeBoot still gates pre-init (bench supply at 3.5V triggers
     sleep)
   - Verify our custom features still work (HA neighbor card rendering,
     #86 cmd round-trip, TX power setting honored)
3. **OTA push to patio** via `scripts/ota-push.py` (per Strycher/LoRa#88)
   only after bench validation is clean.

### Deploy-branch tagging

```bash
git checkout deploy/issues-84-86-87-combined
git tag -a deploy-vX.Y.Z-safeboot -m "Combined fleet build incl SafeBoot vX.Y.Z"
# This tag stays in the meshcore-firmware/ clone; no need to push
# unless we want shareable deploy-branch firmware artifacts.
```

---

## The two workflows in concert

Typical sequence after a new upstream MeshCore release:

```
1. Workflow 1: rebase feature/safeboot onto new upstream/main
   - In meshcore-fork/, on feature/safeboot branch
   - Run bench validation
   - Tag safeboot-vX.Y.Z
   - Push to Strycher/MeshCore

2. Workflow 2: merge feature/safeboot into deploy branch
   - In meshcore-firmware/, on deploy/issues-84-86-87-combined
   - git fetch strycher → git merge strycher/feature/safeboot
   - Resolve conflicts (custom-vs-upstream)
   - Run bench validation on COMBINED build
   - OTA push to patio via scripts/ota-push.py
   - Tag deploy-vX.Y.Z-safeboot
```

Both workflows must complete before the fleet is fully up-to-date with both
SafeBoot and upstream changes.

## When upstream merges (or rejects) SafeBoot

### If merged

- `feature/safeboot` history becomes redundant with upstream main.
- Keep the branch alive as a historical reference — do not delete.
- Workflow 1 still applies (the branch tracks upstream as before).
- Workflow 2 is unaffected — deploy branch still needs merge-from-upstream
  cycles to pick up combined SafeBoot + new upstream changes.

### If rejected (or upstream silent for 6+ months)

- Continue both workflows indefinitely.
- Each upstream release: Workflow 1 then Workflow 2.
- Our fork branch remains the canonical SafeBoot source for our deployments
  AND for any community users we distribute to.

## Conflict-handling expectations

SafeBoot's diff against upstream is intentionally narrow:

- `src/SafeBoot.h` — new file (no conflict possible)
- `src/SafeBoot.cpp` — new file (no conflict possible)
- `examples/simple_repeater/main.cpp::setup()` — ONE line insertion at top
  (conflicts only if upstream restructures setup() significantly)
- `examples/companion_radio/main.cpp::setup()` — ONE line insertion at top
- `src/MeshCore.h::SafetyEventType` enum — TWO new entries
- `examples/simple_repeater/MyMesh.cpp` (or wherever safety_event_type_str
  lives) — TWO new string mappings
- `variants/heltec_v4/platformio.ini` — FOUR `-D` flag definitions

Workflow 1 conflicts: when upstream changes touch the same lines.
Workflow 2 conflicts: when our custom #84/#86/#87 work happens to touch the
same lines as SafeBoot. The #84/#86/#87 work primarily lives in
`src/helpers/wifi_telemetry/*` which SafeBoot does NOT touch, so
deploy-merge conflicts should be rare.

If a conflict appears outside the narrow surface listed above, that's a
signal that SafeBoot has been "leaking" into other files and should be
refactored back to its minimal additive shape.

## Workflow 1 Runbook (step-by-step checklist)

Use this when upstream MeshCore tags a new release and you want to align
`feature/safeboot` with it. The narrative sections above explain *why*;
this checklist gives you *where am I right now*.

Target wall-clock: **70-85 minutes** (excluding 24h soak). See per-section
estimates below.

### Pre-flight (~5 min)

- [ ] ST-P is on the bench and accessible via USB serial (`pio device list`
      shows ST-P's port; cross-check VID:PID and MAC against
      `hardware-devices.yaml` per SAFELANE pre-touch checklist).
- [ ] Bench supply present with LiIon-range current settings ready
      (3.5V test point + 4.0V test point).
- [ ] `meshcore-fork/` working tree clean: `git status` shows no
      uncommitted changes on `feature/safeboot`.
- [ ] On the right branch: `git checkout feature/safeboot &&
      git pull origin feature/safeboot`.
- [ ] Baseline noted for rollback:
      `git log -1 --format='%H %s' > /tmp/safeboot-pre-rebase.txt`
- [ ] Upstream MeshCore release tag known (e.g., `v1.10.0`); note it.

### Rebase (~15-30 min, conflict-dependent)

- [ ] `git fetch upstream --tags --prune`
- [ ] Confirm target: `git log --oneline upstream/main -5` and
      `git tag -l 'v*' --sort=-version:refname | head -5`
- [ ] `git rebase upstream/main`
- [ ] If conflicts: resolve manually, preserving
      `SafeBoot::checkAndMaybeSleep()` placement before `board.begin()` in
      every example main.cpp. See "Conflict-handling expectations" section
      above for the narrow surface where conflicts are expected.
- [ ] `git push --force-with-lease origin feature/safeboot`
- [ ] Fast-forward fork main:
      `git checkout main && git merge --ff-only upstream/main &&
      git push origin main`

### Per-variant build verification (~15 min, parallel)

Build the F5/F6 variant slate. Run all 5 in parallel (separate shells or
background) to compress wall-clock. Failures here mean SafeBoot's adapter
overrides or thresholds need re-tuning for whatever upstream changed.

- [ ] `pio run -e heltec_v4_repeater`
- [ ] `pio run -e Heltec_v3_repeater`
- [ ] `pio run -e RAK_4631_repeater`
- [ ] `pio run -e Xiao_nrf52_repeater`
- [ ] `pio run -e t1000e_repeater`
- [ ] `firmware.bin` / `.elf` size deltas vs. pre-rebase baseline are
      within expected band (~5 KB per variant maximum; bigger growth
      means upstream pulled in something unexpected — investigate).

### Bench validation on ST-P (~30 min, human-driven)

- [ ] Flash the heltec_v4_repeater build to ST-P:
      `python scripts/pio-flash.py flash st-p --env=heltec_v4_repeater`
      (per SAFELANE flashing discipline, named-target only).
- [ ] **Normal-voltage boot** (bench supply at 4.0V): SafeBoot does NOT
      intervene; boot completes; banner observed in serial monitor.
- [ ] **Low-voltage** (bench supply at 3.5V): SafeBoot sleeps; serial
      shows `[SafeBoot] Vbat=... below safe threshold`; sleep current
      ≤ 10 µA (ESP32) when measured at the supply.
- [ ] **Recovery** (bench back to 4.0V): SafeBoot wakes on backoff timer;
      boot continues; serial shows `[SafeBoot] ... continuing boot`.
- [ ] **Brownout cooldown**: trigger reset via bench supply dip; observe
      forced cooldown sleep on the next attempt even with Vbat back to
      4.0V (anti-bootloop safeguard).

### Tag + push (~5 min)

- [ ] `git checkout feature/safeboot`
- [ ] `git tag -a safeboot-v<X.Y.Z> -m "Aligned to upstream MeshCore v<X.Y.Z>"`
      (use the actual upstream version, e.g., `safeboot-v1.10.0`).
- [ ] `git push origin safeboot-v<X.Y.Z>`
- [ ] When F8 release pipeline ships: confirm GitHub Actions builds +
      publishes per-variant .bin to the GH Release page.

### Soak (24h, async; do NOT block the rebase)

- [ ] Leave ST-P running the rebased build. Check periodically: no
      unexpected reboots, no SafeBoot-induced sleeps unless legitimate
      low-V conditions, no MeshCore radio anomalies vs. pre-rebase
      behavior.
- [ ] After 24h clean: OTA-deploy to patio per `scripts/ota-push.py`
      (Workflow 2 takes over from here).

### If something goes wrong mid-rebase

| Symptom | Action |
|---|---|
| Rebase conflict you can't resolve | `git rebase --abort`. Baseline tag in `/tmp/safeboot-pre-rebase.txt` is your rollback. Open a tracker issue describing the conflict; reach out to upstream PR author for context if it's in their code. |
| Variant fails to build after rebase | Likely upstream renamed a macro SafeBoot depends on (e.g., `PIN_VBAT_READ` → something else). Update F13's adapter overrides or the affected variant's platformio.ini. File a follow-up against the relevant F5x sub-task. |
| Bench validation regresses | SafeBoot's behavior is wrong on the new upstream main. DO NOT push the rebased branch. Open a sub-task under #96 documenting the regression; investigate which upstream change broke us before pushing. |
| Tag push rejected | Tag already exists (`git tag -l safeboot-v<X.Y.Z>`). Either the rebase covered the same upstream version (use `-rc1` suffix or pick the next patch version) or someone else pushed first (rare; investigate). |
| Soak surfaces issues | Roll back ST-P to the pre-rebase tag: `python scripts/pio-flash.py flash st-p --env=heltec_v4_repeater --commit=<baseline-sha>`. Don't OTA patio. Open issue documenting symptoms. |

## Reference

- Source of concept: Meshtastic PR
  [#10391](https://github.com/meshtastic/firmware/pull/10391)
- Epic + sub-task tracking: Strycher/LoRa#96 (epic), #97 (this foundation
  step), #98–#101 (related community-shareable firmware epics)
- OTA deployment wrapper used for patio field validation:
  Strycher/LoRa#88 / `scripts/ota-push.py`
- Custom features that combine with SafeBoot in the deploy branch:
  Strycher/LoRa#84, #86, #87 (and ongoing #92, #93, #94, #95)
