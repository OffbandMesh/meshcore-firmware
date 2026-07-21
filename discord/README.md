# Discord announcement

One file per release: `discord/<version>.md`, e.g. `discord/1.2.0.md` (matching the
`offband-v*` tag core, without the `offband-v` prefix or any `-rc`/`-beta` suffix).

## Style

Casual, paste-ready community announcement. What landed, in plain excited-but-honest
terms, with the download link. Emoji fine. This is the message the owner posts to Discord.

**The owner pastes this into Discord manually.** Nothing here posts automatically — posting
to a community is an external, human-triggered action.

Mirrors the convention in the client repo (`OffbandMesh/meshcore-client`), which keeps
`discord/`, `release-notes/`, and `play/` files per version. Firmware's GitHub release body
is built from the matching `CHANGELOG.md` section (see `.github/workflows/release.yml`), so
there is no separate `release-notes/` file here; the CHANGELOG entry is the release notes.
