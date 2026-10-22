# On-demand diagnostic firmware builds

How to get a **diagnostic** firmware image to a tester **without cutting a release** —
and give them a **self-serve download link** (no download-and-forward through Discord).

Mechanism: the `.github/workflows/diag-build.yml` `workflow_dispatch` builds one env
with the diagnostic triad and publishes the `.bin` to the public **`diag-builds`**
rolling pre-release. Issue: [#957](https://github.com/OffbandMesh/meshcore-firmware/issues/957).

## What a diagnostic build is

Any env built with the diagnostic triad (owner directive 2026-08-22):

| flag | effect |
|---|---|
| `-DOFFBAND_FORCE_CAPLOG` | caplog pinned ON (serial/mesh log capture, cannot be turned off) |
| `-DOFFBAND_BOOT_BEACON` | raw-UART0 boot beacon (survives early-boot crashes) |
| `-DOFFBAND_MESHLOG_UART0` | mirror the mesh log to UART0 |

These make a diag build **self-announce at runtime** (`[BEACON]` lines + pinned caplog).
The output filename carries `-diag-<sha>`. (A runtime version-string marker is tracked
in [#958](https://github.com/OffbandMesh/meshcore-firmware/issues/958).)

## Dispatch a build

Actions UI: **Actions → "Diagnostic Build (on-demand)" → Run workflow → pick `env`**,
or from the CLI:

```bash
gh workflow run diag-build.yml -R OffbandMesh/meshcore-firmware \
  --ref firmware-base -f env=Heltec_v3_companion_observer_wifi
```

`env` is any PlatformIO env (default `Heltec_v3_companion_observer_wifi`). The workflow
validates it against `pio project config` (so typos fail with a clear message), builds
with a dummy secrets stub (WiFi/MQTT are configured on-device at runtime), and verifies
a `.bin` was produced before publishing.

**Dispatch only works from the default branch** (`firmware-base`) — a `workflow_dispatch`
workflow must be on the default branch to appear.

## Where the file lands — the tester link

Each build's assets are published to the rolling **`diag-builds`** pre-release. On this
public repo those assets have **anonymous direct-download URLs** — hand a tester the link,
no login, no forwarding:

```
https://github.com/OffbandMesh/meshcore-firmware/releases/download/diag-builds/<env>-diag-<sha>.bin
https://github.com/OffbandMesh/meshcore-firmware/releases/download/diag-builds/<env>-diag-<sha>-merged.bin
```

The same files are also a **run artifact** (`diag-<env>`) for maintainer convenience, but
artifacts are auth-gated — use the release link for testers.

> `diag-builds` is a **rolling pre-release, NOT a version release**: no CHANGELOG, no
> beta/stable semantics, assets replaced as needed. It never touches the `offband-v*`
> release line. Do not treat its contents as a shipped version.

## Which `.bin` to flash

| file | offset | NVS |
|---|---|---|
| `<env>-diag-<sha>.bin` (app) | app slot `0x10000` | **preserved** — keeps the tester's WiFi/broker config; reproduces their exact setup |
| `<env>-diag-<sha>-merged.bin` (full) | `0x0` | **wiped** — factory reset, tester reconfigures |

Prefer the **app** `.bin` for diagnosing a deployed device (keeps config). Use merged only
if the tester's flasher does full images only.

## Reproducing a specific released version

A diag build off `firmware-base` HEAD is only faithful to a shipped beta if no `src/`/
`examples/` code changed between that tag and HEAD. Check first:

```bash
git diff --stat offband-v1.5.0-betaN..firmware-base -- src/ examples/
```

If firmware code differs, dispatch off the tag instead (`--ref offband-v1.5.0-betaN`) so
the diag build matches the version the tester is actually running.
