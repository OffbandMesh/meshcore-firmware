# Release notes

One file per release: `release-notes/<version as tagged>.md`, e.g.
`release-notes/1.5.0-beta7.md` for `offband-v1.5.0-beta7`.

`release.yml` publishes this file as the GitHub release page's body when it exists.
The `.github/release-footer.md` flashing guide is appended to it, as it is to any
other body.

## Naming

The filename carries the **full** tagged version, pre-release suffix included —
`1.5.0-beta7.md`, `1.5.0-rc1.md`, `1.5.0.md`. Strip `offband-v`, nothing else.

This differs from `discord/`, which drops the suffix so a whole release series shares
one community post. Release notes are per-tag and permanent: a beta and its eventual
stable describe different builds, and collapsing them onto one filename would mean
one overwriting the other.

## Style

Prose. What a reader needs in order to decide whether to take this build, and what to
expect if they do — organized by what matters, not by changelog category. This is the
owner's writing; it is not generated from `CHANGELOG.md`.

## Relationship to the CHANGELOG

`CHANGELOG.md` stays the changelog: Keep a Changelog, Added / Fixed / Changed, one
section per version. It is the record. These notes are the announcement.

Where both exist, the notes win for the release page. With no notes file for a tag,
`release.yml` falls back to the CHANGELOG section for that version, then to the base
version for an `-rc`, then to a placeholder — unchanged from before this directory
existed (#1294, #1306).

## Where else a release is announced

A tagged release has four pieces of copy, and all four are reviewed before the tag is
cut, because tagging is what publishes them:

| Artifact | Lives in |
|---|---|
| Changelog section | `CHANGELOG.md` |
| Release notes | `release-notes/<version>.md` — this directory |
| Discord post | `discord/<version>.md` |
| Discussions announcement | the repo's Announcements category |

Nothing here posts automatically except the release page itself. Discord and
Discussions are pasted by the owner — announcing to a community is a human action.
