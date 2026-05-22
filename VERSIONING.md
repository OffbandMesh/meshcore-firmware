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
| **PATCH** | Every successful compile of fork code (bug fixes, small changes) |
| **MINOR** | When a fork feature lands on a deploy branch (e.g., MQTT command queue ships, SafeBoot bench-validates) |
| **MAJOR** | Release milestones (e.g., first public release, breaking compatibility changes) |

Tags are pushed to the Strycher/MeshCore remote and are accessible to all
working trees of the fork.

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
`deploy/issues-84-86-87-combined` as of 2026-05-21.

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

After landing a fork-side commit on the deploy branch:

```bash
# PATCH (most common: build artifact, small fix)
git tag -a crosswire-v0.5.1 -m "PATCH: <one-line description>"
git push strycher crosswire-v0.5.1

# MINOR (a feature lands)
git tag -a crosswire-v0.6.0 -m "MINOR: <feature> landed"
git push strycher crosswire-v0.6.0
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
