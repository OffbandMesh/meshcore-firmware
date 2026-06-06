# Architectural Plan: CI Release Pipeline (Epic #14) -- T1 Design of Record (DoR)

> Status: AWAITING human acceptance. No implementation code (T2+) until accepted.
> Approach approved (Gate 1) + structure approved 2026-06-06. Scope choices confirmed by maintainer.
> This plan specifies WHAT each task builds, HOW it is verified (automated by agent or manual by
> maintainer), and the ACCEPTANCE CRITERIA that prove it done. Actual YAML/code is written in each
> implementation task, not here.

---

## 1. Goal / non-goals

**Goal:** following the documented process produces downloadable firmware.
- dev: every `firmware-base` push / PR -> downloadable Actions artifact.
- release: tagging `crosswire-vX.Y.Z` (or `-rcN`) -> builds the curated board set -> GitHub Release
  with the correct pre-release / Latest flag and self-describing assets.

**Non-goals:** no change to version *generation* (manual tags, deliberate); no real secrets in public
binaries; no download-hosting site; no flashing by CI.

## 2. Scope (maintainer-confirmed 2026-06-06)

- Boards: RAK4631; WisMesh = RAK 3401 **and** WisMesh Tag; Wio Tracker; XIAO ESP32 = S3 WIO **+** C3 **+** C6;
  XIAO nRF; Solar SenseCap; T1000E; Heltec T096; T1114; V2; V3; V4 (**OLED and TFT**); T-Echo; T-Deck.
  T-Deck Pro: no env -> follow-up issue, not in this release.
- Sub-variants: base only, except Heltec V4 ships OLED (`heltec_v4`) + TFT (`heltec_v4_tft`).
- Roles: MeshCore's curated set (companion ble/usb, repeater, room_server) + Crosswire
  `companion_observer_wifi` + `repeater_telemetry`. No kiss/sensor/terminal/bridge/espnow/wifi/serial.

## 3. Curated release env set -- 73 envs (verified against `variants/*/platformio.ini`)

Materialized in T3 as `.github/release-envs.txt` (one env per line; `#` = comment/exclude; blanks ignored).
72 ship immediately; `heltec_v4_repeater_telemetry` is carried as a commented line until #20 (runtime-config
secrets) lands, then uncommented.

```
RAK_4631:        companion_radio_ble, companion_radio_usb, repeater, room_server          (nRF52)
RAK_3401:        companion_radio_ble, companion_radio_usb, repeater, room_server          (nRF52)
RAK_WisMesh_Tag: companion_radio_ble, companion_radio_usb, repeater, room_server          (nRF52)
WioTrackerL1:    companion_radio_ble, companion_radio_usb, repeater, room_server          (nRF52)
Xiao_S3_WIO:     companion_observer_wifi, companion_radio_ble, companion_radio_usb, repeater, room_server (ESP32-S3)
Xiao_C3:         companion_radio_ble, companion_radio_usb, repeater, room_server          (ESP32-C3)
Xiao_C6:         companion_radio_ble_, repeater_   (only these two exist; trailing underscore is real) (ESP32-C6)
Xiao_nrf52:      companion_radio_ble, companion_radio_usb, repeater, room_server          (nRF52)
SenseCap_Solar:  companion_radio_ble, companion_radio_usb, repeater, room_server          (ESP32)
t1000e:          companion_radio_ble, companion_radio_usb, repeater, room_server          (nRF52)
Heltec_t096:     companion_radio_ble, companion_radio_usb, repeater, room_server          (nRF52)
Heltec_t114:     companion_radio_ble, companion_radio_usb, repeater, room_server          (nRF52)
Heltec_v2:       companion_radio_ble, companion_radio_usb, repeater, room_server          (ESP32)
Heltec_v3:       companion_observer_wifi, companion_radio_ble, companion_radio_usb, repeater, room_server (ESP32-S3)
heltec_v4:       companion_observer_wifi, companion_radio_ble, companion_radio_usb, repeater, repeater_telemetry[GATED #20], room_server (ESP32-S3)
heltec_v4_tft:   companion_radio_ble, companion_radio_usb, repeater, room_server          (ESP32-S3)
LilyGo_T-Echo:   companion_radio_ble, companion_radio_usb, repeater, room_server          (nRF52)
LilyGo_TDeck:    companion_radio_ble, companion_radio_usb, repeater   (no room_server env) (ESP32-S3)
```

## 4. End-to-end architecture

```
DEV path:     git push firmware-base  ->  ci.yml (6 curated envs)  ->  upload-artifact per env
                                                                       (downloadable from the Actions run)

RELEASE path: git tag crosswire-vX.Y.Z[-rcN] && git push <tag>
                 -> release.yml
                    [job: list]    read .github/release-envs.txt -> JSON array (drop # and blanks)
                    [job: build]   matrix=fromJSON(list)  (fail-fast:false)
                                   per env: setup-build-environment -> dummy secrets stub
                                            -> build.sh build-firmware <env>  -> out/<files>
                                            -> upload-artifact  out/*  (per-env)
                    [job: release] needs:[list,build], if: always()
                                   download all artifacts -> softprops/action-gh-release
                                   prerelease = contains(tag,'-rc'); make_latest = !contains(tag,'-rc')
```

## 5. Artifact naming + which file to flash (so a Release is usable)

`build.sh` names every file `<env>-<FIRMWARE_VERSION>-<shortsha>.<ext>`
(e.g. `RAK_4631_repeater-v0.14.0-abc1234.uf2`). Assets are self-describing: board + role + version.
Per platform `build.sh` emits, into `out/`:

| Platform | Files emitted | Flash which |
|---|---|---|
| ESP32 (S3/C3/C6/classic) | `<name>.bin`, `<name>-merged.bin` | **`-merged.bin`** for a fresh USB install (esptool @ 0x0); `.bin` for OTA/update |
| nRF52 | `<name>.uf2`, `<name>.zip` | **`.uf2`** drag-drop to the bootloader drive; `.zip` for adafruit-nrfutil DFU |

The Release body documents this mapping so a community member knows what to download and flash.
(Open item: do we publish ELF? Proposed NO for releases -- debug-only, bloats assets. dev artifacts keep ELF.)

## 6. Per-task plan (design + test + acceptance)

Dependency/sequence: **T1 accepted -> T2 -> T3 -> T4 -> T5**. All work on branch
`epic/14-ci-release-pipeline`; one PR at epic completion (after T5). Each task is a commit (per CLAUDE-BASE).

### T2 (#16) -- ci.yml dev-channel artifact
- **Design:** add one `actions/upload-artifact@v4` step after the existing `Build ${{ matrix.env }}` step;
  name `crosswire-dev-${{ matrix.env }}`; paths `.pio/build/${{ matrix.env }}/firmware.{bin,elf,hex,zip,uf2}`;
  `if-no-files-found: warn`; `retention-days: 90`. No matrix or trigger change.
- **Edge cases:** envs that emit only some extensions (e.g. ESP32 has no `.uf2`) -> `warn` not `error`.
- **Test (automated, agent):** push the branch / open the epic PR; `gh run view <id> --json jobs` +
  `gh api .../artifacts` confirm all 6 ci.yml envs produced a downloadable artifact; CI stays green.
- **Test (manual, you, optional):** download one artifact zip, confirm it contains a `firmware.bin`.
- **Acceptance:** every ci.yml env yields a named, downloadable artifact; CI green; nothing else changed.

### T3 (#17) -- release.yml on `crosswire-v*`
- **Design:** new workflow, `permissions: contents: write`, triggers `push: tags: ['crosswire-v*']` +
  `workflow_dispatch`. Three jobs:
  - `list`: read `.github/release-envs.txt`, strip `#`/blank lines, emit JSON array as an output.
  - `build`: `strategy: matrix: env: ${{ fromJSON(needs.list.outputs.envs) }}`, `fail-fast: false`;
    per env: `setup-build-environment` -> generate dummy secrets stub (same as ci.yml) ->
    `FIRMWARE_VERSION=${{ env.GIT_TAG_VERSION }} build.sh build-firmware <env>` -> upload `out/*` per env.
  - `release`: `needs: [list, build]`, `if: always()`; download all artifacts;
    `softprops/action-gh-release@v2` with `prerelease: ${{ contains(github.ref_name,'-rc') }}`,
    `make_latest: ${{ !contains(github.ref_name,'-rc') }}`, body = release notes (see sec 7), `files: **/out*/*`.
- **Edge cases / decisions:**
  - **Partial build failure** (1 of 72 envs fails): `fail-fast:false` keeps the rest going; `release` runs
    `if: always()` and publishes what built, and the body/job-summary lists any missing envs.
    *Decision for you:* publish-partial-and-flag (proposed) vs abort-on-any-failure. Proposed = publish partial,
    because one flaky board should not block a 71-board release; failures are surfaced, you can re-run.
  - **#20 gate:** `heltec_v4_repeater_telemetry` stays a commented line in `release-envs.txt`; uncommenting
    after #20 is the only change needed to ship it.
  - **Secrets stub coverage:** ci.yml's stub provides wifi/mqtt/ota/cmdrelay -> covers observer + telemetry
    envs (they compile against placeholders, configured at runtime). Verified in T3 by building an observer env.
- **Test (automated, agent):** `workflow_dispatch` (or push throwaway tag `crosswire-v0.0.0-rc.test`):
  confirm `list` emits 72 envs; `build` runs all; per-env artifacts present; `release` creates a Release with
  `prerelease=true` and the expected asset count + naming (`gh release view`); then delete the test
  tag + Release (sec 8).
- **Test (manual, you):** from the test Release, download one ESP32 `-merged.bin` and one nRF52 `.uf2`;
  confirm sane size + (overlaps T5) flashes/boots on a bench board.
- **Acceptance:** a tag yields a correctly-flagged Release whose assets match the env set and naming
  convention; `-rc` -> Pre-release, plain -> Latest; test tag/Release cleaned up.

### T4 (#18) -- reconcile inherited workflows
- **Design (per-file):** DELETE `pr-build-check.yml` (triggers `main`/`dev`, dead; ci.yml covers PRs),
  `auto-promote.yml` (`if: repo==Strycher/LoRa`, inert), `github-pages.yml` (triggers `main`,
  `cname: docs.meshcore.io` upstream domain), `build-companion-firmwares.yml`,
  `build-repeater-firmwares.yml`, `build-room-server-firmwares.yml` (upstream-tag, superseded by release.yml).
  KEEP `build-safeboot-firmwares.yml` (feature-scoped tags per VERSIONING.md), `sync-labels-to-board.yml`.
  REVIEW `branch-cleanup.yml` (read it; keep if it only prunes merged branches, else flag).
- **Edge cases:** confirm nothing references the deleted workflows (no `workflow_call`, no badge links in README).
- **Test (automated, agent):** `gh workflow list` shows only the intended set; open a no-op PR ->
  only `ci.yml` (+ board-sync if path matches) triggers, no deleted/dead workflow fires; CI green.
- **Test (manual, you):** glance at the Actions tab -> no orphaned/failing inherited workflows.
- **Acceptance:** dead workflows removed; intended set remains; a PR triggers only intended workflows; README/badges consistent.

### T5 (#19) -- integration test (epic gate, human sign-off)
- **Design:** end-to-end proof on real tags + real hardware. Steps:
  1. cut `crosswire-vX.Y.Z-rc1` -> full release runs -> Release published (Pre-release) with all assets.
  2. confirm a `firmware-base` push produced a dev artifact (T2).
  3. confirm a plain `crosswire-vX.Y.Z` (later, real) marks the Release "Latest".
- **Test (manual, you = the release gate):** download from the Release and flash **>=1 ESP32**
  (`-merged.bin`) and **>=1 nRF52** (`.uf2`); confirm each boots; for an observer build, confirm WiFi/MQTT
  configures at runtime. This hands-on hardware pass is the gate, not CI-green.
- **Test (automated, agent):** assert asset set completeness vs `release-envs.txt`; assert flags
  (prerelease/latest) via `gh release view --json`.
- **Acceptance:** downloadable, flashable firmware for the matrix; one ESP32 + one nRF52 boot from Release
  assets; channel flags correct. **You sign off -> epic #14 closes** (human-only).

## 7. Release notes (decision)

Proposed: Release body = the `CHANGELOG.md` section for that version (curated, human-written) followed by
`generate_release_notes: true` auto-notes (merged PRs/commits since last tag). Gives both the intentional
summary and the full change list. *Confirm or simplify to auto-only.*

## 8. Failure + recovery runbook

- **Re-run a failed env:** re-run the failed matrix job from the Actions UI, or re-dispatch.
- **Bad/typo tag already published:** `gh release delete crosswire-vX.Y.Z --yes --cleanup-tag`
  (removes Release + tag), or `git push crosswire --delete <tag>` then delete the Release.
- **Wrong assets:** delete the Release, fix `release-envs.txt`, re-tag (or re-dispatch on the same tag).

## 9. Build-time estimate (assumptions stated)

72 envs, GitHub-hosted ubuntu runners, `~/.platformio` cached. Per-env build ~2-5 min warm (~5-8 cold).
Account concurrency ~20-40 Linux jobs (Pro). Wall-clock: warm full release ~15-30 min (cold first run longer).
Cost: GitHub-hosted minutes; matrix parallel. Mitigation if too slow: the env list is a tunable file.

## 10. Risks

- 72-env runs are long (mitigated: parallel, tunable list).
- A `crosswire-v*` typo publishes a Release (auto pre-release flag + sec-8 recovery; tagging is deliberate/manual).
- Telemetry intentionally absent from public Releases until #20.
- Partial-failure policy (sec 6, T3) is a maintainer decision.

## 11. Open items for your acceptance

1. Partial-build policy: **publish-partial-and-flag** (proposed) vs abort-on-any-failure.
2. Release notes: **CHANGELOG section + auto** (proposed) vs auto-only.
3. Publish ELF in releases: **no** (proposed) vs yes.
4. Otherwise: accept the plan -> T2 begins.
