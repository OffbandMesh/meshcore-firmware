# Draft user guides

A guide for behavior that no release carries yet. Each one becomes a page of the
[user manual](../user/index.md) at the release that ships its feature.

## The convention

- **One file per feature: `docs/drafts/<feature-id>.md`.** The name is the feature's
  `id` in `docs/user/features.yml`, and nothing else belongs in this folder.
- **Write it exactly as it will publish.** Front matter `title:` (the catalog title) and
  `feature:` (the id), the six sections of `docs/templates/USER-GUIDE.md`, and no
  "draft" banner, because the folder already says it is a draft.
- **Write links from `docs/user/guides/`**, where the page will live: `install-update.md`
  for another guide, `../index.md` for the start page. The checker resolves them from
  there and fails a link that would break once published.
- **Its catalog entry is `coverage: planned`**, with no `guide:` field and these two lines
  exactly, which promotion rewrites:

  ```yaml
      coverage: planned
      availability: {channel: development, verified_releases: []}
  ```

A planned entry without its draft, a draft without its planned entry, and any other
file here all fail `python scripts/check_user_docs.py`.

## At release

The release PR runs:

```sh
python scripts/promote_drafts.py --release offband-vX.Y.Z[-betaN|-rcN]
```

That moves every draft into `docs/user/guides/`, marks its entry `documented` for the
release (`beta` for a beta or rc tag, `stable` otherwise) and links it from the manual's
start page. Review each promoted entry's `verification:` text afterwards, since it was
written before the release.

The release workflow will not publish a tag while a draft is waiting (`--check`), and
lists the guides published for the tag on the release page (`--notes`).

## Keeping one back

To ship a release without publishing a draft, add a reason to its catalog entry:

```yaml
    hold: waits for the on-air trial of discover.scopes
```

Promotion skips a held draft, and the release gate reports it instead of failing. Remove
the hold to publish it at the next release.
