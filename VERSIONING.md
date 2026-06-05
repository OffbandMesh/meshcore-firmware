# Crosswire Versioning

This fork uses **Pattern B** versioning: an independent fork version, with the
upstream MeshCore version captured as a separate baseline field.

## Why Pattern B (vs. upstream-suffix)

A suffix-style version (e.g., `v1.15.0-strycher.3`) anchors the fork's
identity to a specific upstream release and resets at every rebase. Pattern B
keeps a monotonic fork-version counter that survives upstream rebases —
better for a fork with feature lines that outlive multiple upstream releases.

The two versions (upstream + fork) are exposed separately in MQTT, CLI, and
firmware identifiers. Neither one alone identifies a build; both together do.

## Tag schemes

### Fork dev versioning (Crosswire as a whole)

Tag format: `crosswire-vMAJOR.MINOR.PATCH`

Increments per CLAUDE-BASE §Versioning:

| Increment | When |
|-----------|------|
| **PATCH** | Bug fixes, small changes, a landing that adds no user-facing capability (incl. docs) |
| **MINOR** | A fork feature lands (e.g., a new LED behavior, MQTT command queue, SafeBoot bench-validates) |
| **MAJOR** | Release milestones (first public release, breaking compatibility changes) |

### Cadence (when each thing happens)

| Action | Trigger | Result |
|---|---|---|
| **Commit** | Each completed task, **and after every successful compile** | granular history + clean rollback; shows as the `+N` suffix in `git describe` |
| **PR** | A confirmed-working iteration of a **closed epic** | review surface + merge to `firmware-base` |
| **Version bump (tag)** | Each landing on `firmware-base` (the epic PR) | a `crosswire-vX.Y.Z` tag; `CHANGELOG.md` `[Unreleased]` rolls into the new version section |

Between version tags, commits accumulate as `+N` dev builds (e.g.
`crosswire-v0.13.1-4-gABCDEF` = 4 commits past the tag); each epic landing cuts a
new tag. **Every landing updates `CHANGELOG.md`.**

Tags are pushed to the **`crosswire` remote** (`Strycher/Crosswire`) and are
accessible to all working trees of the fork.

### Feature-scoped release tags

For specific shippable feature artifacts that produce per-variant binaries via CI:

| Feature | Tag scheme | Example |
|---|---|---|
| SafeBoot | `safeboot-<example>-vX.Y.Z[-rcN]` | `safeboot-simple-repeater-v1.15.0-rc2` |

Feature-scoped tags use **upstream's** version number, not Crosswire's. This is
intentional: SafeBoot may be accepted into upstream MeshCore via PR; if so,
the SafeBoot release tags travel cleanly without Crosswire-specific branding.

### Future consideration

A `crosswire-<feature>-vX.Y.Z` form (e.g., `crosswire-safeboot-v0.1.0`) is on
the table for feature-specific Crosswire releases that are intentionally NOT
upstream-bound — i.e., features that only make sense under Crosswire branding
(deployment-config, MQTT topic conventions tied to SWOH infrastructure, etc.).
Not used yet; will be defined when the first such release ships.

## Release channels (community availability)

A *version tag* is an internal milestone; a *release* is the subset of tags
published to the community. The split uses GitHub's native flags — no custom
labels:

| Channel | Mechanism | Audience |
|---|---|---|
| **dev** | CI build **artifact** only (downloadable from the Actions run); no GitHub Release | maintainer only |
| **pre-release (`-rcN`)** | tag `crosswire-vX.Y.Z-rc1` → GitHub Release with the **pre-release** flag set | community testers; shown as "Pre-release", not "Latest" |
| **stable** | tag `crosswire-vX.Y.Z` → GitHub Release marked **"Latest"** | everyone; the recommended download |

Publishing a GitHub Release is the deliberate "make this available to the
community" action. The **release gate is hardware validation** (the maintainer's
hands-on test on real devices), not CI-green — CI-green only means "worth
flashing to test." `-rc` is the single pre-release tier for now: testers arrive
after the internal minimum bar, so they're already past "alpha". Add `-beta` only
if a rougher tier below `-rc` is ever needed.

## Upstream baseline tracking

The upstream MeshCore version that a given Crosswire build rides on is
captured separately at build time as `UPSTREAM_VERSION` (see FF2 / #179).
The fork version `crosswire-vX.Y.Z` does NOT need to match upstream's
version — they are independent counters.

Example: Crosswire v0.5.0 riding on upstream MeshCore v1.15.0 is reported by
firmware as:

| Field | Value |
|---|---|
| `sw_version` (MQTT/HA) | `MC v1.15.0 / Crosswire v0.5.0` |
| `crosswire_version` (MQTT state) | `v0.5.0` |
| `upstream_version` (MQTT state) | `v1.15.0` |
| `manufacturer` (MQTT/HA `dev.mf`) | `Crosswire` |

When upstream releases v1.16.0 and we rebase, the Crosswire version increments
independently based on what NEW fork-side work landed during/around the
rebase — not because of the upstream change itself.

## Initial backfill (2026-05-21)

The first Crosswire version tag is **`crosswire-v0.5.0`**, applied to
`crosswire` (the fork's active integration branch — renamed from
`deploy/issues-84-86-87-combined` 2026-05-23 per LoRa#212) as of 2026-05-21.

### Why v0.5.0 (not v0.1.0 or v1.0.0)

- **Substantial work already in flight**: SafeBoot port, WiFi/MQTT telemetry,
  OTA discipline + safety log + rollback infrastructure, partial MQTT command
  queue (#86)
- **Pre-1.0 maturity**: SafeBoot pre-bench-validation; MQTT command queue
  partial; no field-deployment soak under Crosswire branding yet
- **Room to grow**: v1.0.0 is reserved for the first major milestone
  (bench-validated SafeBoot in production + full MQTT command queue shipped +
  patio operational under Crosswire identity for at least a 7-day soak)

### Retroactive tagging

Earlier branch states (e.g., the `feat/wifi-telemetry-stock` state deployed to
patio on 2026-05-15) MAY be tagged retroactively if the historical value
exceeds the tagging cost. Not done by default; case-by-case.

## How to increment the Crosswire version

After an epic lands on `firmware-base` (and `CHANGELOG.md` `[Unreleased]` has been
rolled into the new version section):

```bash
# PATCH (small fix / docs / non-feature landing)
git tag -a crosswire-v0.13.2 -m "PATCH: <one-line description>"
git push crosswire crosswire-v0.13.2

# MINOR (a feature lands)
git tag -a crosswire-v0.14.0 -m "MINOR: <feature> landed"
git push crosswire crosswire-v0.14.0

# Pre-release for community testers (then publish a GitHub Release, pre-release flag)
git tag -a crosswire-v0.14.0-rc1 -m "RC1: <feature> -- community test"
git push crosswire crosswire-v0.14.0-rc1
```

The firmware build picks up the latest matching tag via
`git describe --tags --match 'crosswire-v*'` (configured in `platformio.ini`,
see FF2 / #179).

## Related references

- CLAUDE-BASE §Versioning (`C:\Dev\DifferentWire\standards\CLAUDE-BASE.md`)
- `docs/cli-and-mqtt-commands.md` — CLI + MQTT command reference (`version` command, `wifi on N`, MQTT `ota_enable`, etc.)
- `docs/safeboot-maintenance.md` — SafeBoot-specific tag scheme details
- Epic #176 / LoRa-edl — this versioning discipline initiative
- FF1 (#178 / LoRa-ry7) — this file's establishment
- FF2 (#179 / LoRa-nnc) — embedding identity in firmware build (depends on this scheme)
