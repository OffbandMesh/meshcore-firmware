# Offband Versioning

This fork uses **Pattern B** versioning: an independent fork version, with the
upstream MeshCore version captured as a separate baseline field.

> **Brand note (2026-06-13):** the fork was previously named **Crosswire** and
> tagged `crosswire-v*` (`crosswire-v0.5.0` through `crosswire-v0.16.0`). Those
> historical tags are preserved as-is. Going forward the fork is **Offband** and
> tags `offband-v*`; the build matches both patterns (preferring `offband-v*`),
> so existing release history stays intact.

## Why Pattern B (vs. upstream-suffix)

A suffix-style version (e.g., `v1.15.0-strycher.3`) anchors the fork's
identity to a specific upstream release and resets at every rebase. Pattern B
keeps a monotonic fork-version counter that survives upstream rebases --
better for a fork with feature lines that outlive multiple upstream releases.

The two versions (upstream + fork) are exposed separately in MQTT, CLI, and
firmware identifiers. Neither one alone identifies a build; both together do.

## Tag schemes

### Fork dev versioning (Offband as a whole)

Tag format: `offband-vMAJOR.MINOR.PATCH`

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
| **Version bump (tag)** | Each landing on `firmware-base` (the epic PR) | an `offband-vX.Y.Z` tag; `CHANGELOG.md` `[Unreleased]` rolls into the new version section |

Between version tags, commits accumulate as `+N` dev builds (e.g.
`offband-v0.16.0-4-gABCDEF` = 4 commits past the tag); each epic landing cuts a
new tag. **Every landing updates `CHANGELOG.md`.**

**Issue refs in CHANGELOG entries are full-URL markdown links** (owner rule,
2026-08-20, applied from `[1.5.0-beta4]` forward — not retroactively): bare `#N`
does not render as a link in `.md` files or release bodies, so write
`[#N](https://github.com/OffbandMesh/meshcore-firmware/issues/N)` (or `/pull/N`
for PRs).

Tags are pushed to the **`origin` remote** (`OffbandMesh/meshcore-firmware`; moves to the
Offband org repo after the rename) and are accessible to all working trees of the
fork.

### Feature-scoped release tags

For specific shippable feature artifacts that produce per-variant binaries via CI:

| Feature | Tag scheme | Example |
|---|---|---|
| SafeBoot | `safeboot-<example>-vX.Y.Z[-rcN]` | `safeboot-simple-repeater-v1.15.0-rc2` |

Feature-scoped tags use **upstream's** version number, not Offband's. This is
intentional: SafeBoot may be accepted into upstream MeshCore via PR; if so,
the SafeBoot release tags travel cleanly without Offband-specific branding.

### Future consideration

An `offband-<feature>-vX.Y.Z` form (e.g., `offband-safeboot-v0.1.0`) is on
the table for feature-specific Offband releases that are intentionally NOT
upstream-bound -- i.e., features that only make sense under Offband branding
(deployment-config, MQTT topic conventions tied to SWOH infrastructure, etc.).
Not used yet; will be defined when the first such release ships.

## Release channels (community availability)

A *version tag* is an internal milestone; a *release* is the subset of tags
published to the community. The split uses GitHub's native flags -- no custom
labels:

| Channel | Mechanism | Audience |
|---|---|---|
| **dev** | CI build **artifact** only (downloadable from the Actions run); no GitHub Release | maintainer only |
| **pre-release (`-rcN`)** | tag `offband-vX.Y.Z-rc1` → GitHub Release with the **pre-release** flag set | community testers; shown as "Pre-release", not "Latest" |
| **stable** | tag `offband-vX.Y.Z` → GitHub Release marked **"Latest"** | everyone; the recommended download |

Publishing a GitHub Release is the deliberate "make this available to the
community" action. The **release gate is hardware validation** (the maintainer's
hands-on test on real devices), not CI-green -- CI-green only means "worth
flashing to test." `-rc` is the single pre-release tier for now: testers arrive
after the internal minimum bar, so they're already past "alpha". Add `-beta` only
if a rougher tier below `-rc` is ever needed.

## Release approval gate

Before pushing **any release tag** (`-rc` or stable), post a concise **release preview** to the maintainer and wait for an explicit "ship it":

- the **version** + whether it's **`-rc` or stable**
- the **CHANGELOG entry** being released
- any **user-facing string change** in the release (exact before → after)

Scale the preview to the change — a line or two for a patch, a short list for a feature; don't make it a wall. Two rules behind it:

- **Validation is not authorization.** A maintainer saying a build "works" / "is happy with it" means the *hardware gate is met* — it is not "publish the release." The tag is pushed only on an explicit go.
- **No unreviewed copy ships.** Anything users read (firmware reply strings, release notes, the CHANGELOG entry) goes in front of the maintainer *before* the tag, not after.

(Adopted 2026-06-14 after a stable release was cut on an inferred go and a user-facing wording change shipped to production unreviewed.)

## Upstream baseline tracking

The upstream MeshCore version that a given Offband build rides on is
captured separately at build time as `UPSTREAM_VERSION` (see FF2 / #179).
The fork version `offband-vX.Y.Z` does NOT need to match upstream's
version -- they are independent counters.

Example: Offband v0.5.0 riding on upstream MeshCore v1.15.0 is reported by
firmware as:

| Field | Value |
|---|---|
| `sw_version` (MQTT/HA) | `MC v1.15.0 / Offband v0.5.0` |
| `offband_version` (MQTT state) | `v0.5.0` |
| `upstream_version` (MQTT state) | `v1.15.0` |
| `manufacturer` (MQTT/HA `dev.mf`) | `Offband` |

When upstream releases v1.16.0 and we rebase, the Offband version increments
independently based on what NEW fork-side work landed during/around the
rebase -- not because of the upstream change itself.

## Version history (initial backfill, 2026-05-21)

The fork's first version tag was **`crosswire-v0.5.0`** (under the prior
Crosswire brand), applied to the `crosswire` integration branch -- renamed from
`deploy/issues-84-86-87-combined` 2026-05-23 per LoRa#212 -- as of 2026-05-21.
The `crosswire-v*` line ran `crosswire-v0.5.0` … `crosswire-v0.16.0`; all those
tags are preserved, and the `offband-v*` line continues from there.

### Why v0.5.0 (not v0.1.0 or v1.0.0)

- **Substantial work already in flight**: SafeBoot port, WiFi/MQTT telemetry,
  OTA discipline + safety log + rollback infrastructure, partial MQTT command
  queue (#86)
- **Pre-1.0 maturity**: SafeBoot pre-bench-validation; MQTT command queue
  partial; no field-deployment soak yet
- **Room to grow**: v1.0.0 is reserved for the first major milestone
  (bench-validated SafeBoot in production + full MQTT command queue shipped +
  patio operational under the fork's identity for at least a 7-day soak)

### Retroactive tagging

Earlier branch states (e.g., the `feat/wifi-telemetry-stock` state deployed to
patio on 2026-05-15) MAY be tagged retroactively if the historical value
exceeds the tagging cost. Not done by default; case-by-case.

## How to increment the Offband version

After an epic lands on `firmware-base` (and `CHANGELOG.md` `[Unreleased]` has been
rolled into the new version section):

```bash
# PATCH (small fix / docs / non-feature landing)
git tag -a offband-v0.16.1 -m "PATCH: <one-line description>"
git push origin offband-v0.16.1

# MINOR (a feature lands)
git tag -a offband-v0.17.0 -m "MINOR: <feature> landed"
git push origin offband-v0.17.0

# Pre-release for community testers (then publish a GitHub Release, pre-release flag)
git tag -a offband-v0.17.0-rc1 -m "RC1: <feature> -- community test"
git push origin offband-v0.17.0-rc1
```

The firmware build picks up the latest matching tag via
`git describe --tags --match 'offband-v*'` (configured in `platformio.ini`,
see FF2 / #179); the release workflow also accepts legacy `crosswire-v*` tags.

## Related references

- CLAUDE-BASE §Versioning (`C:\Dev\DifferentWire\standards\CLAUDE-BASE.md`)
- `docs/cli-and-mqtt-commands.md` -- CLI + MQTT command reference (`version` command, `wifi on N`, MQTT `ota_enable`, etc.)
- `docs/safeboot-maintenance.md` -- SafeBoot-specific tag scheme details
- Epic #176 / LoRa-edl -- this versioning discipline initiative
- FF1 (#178 / LoRa-ry7) -- this file's establishment
- FF2 (#179 / LoRa-nnc) -- embedding identity in firmware build (depends on this scheme)
