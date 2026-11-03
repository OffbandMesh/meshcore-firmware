#!/usr/bin/env python3
"""Draft user guides: validate them, gate a release on them, publish them (#1968).

A feature not yet in a release is documented as `docs/drafts/<feature-id>.md`, written
exactly as it will publish, with its catalog entry at `coverage: planned`. Promotion is
then mechanical: the file moves to `docs/user/guides/<feature-id>.md`, the entry becomes
`documented` for the release, and the manual's start page links it. Because location and
form are fixed, the release workflow finds every draft without being told about it.

  --check           release gate: fail while a draft that is not on hold remains
  --release TAG     promote every draft that is not on hold, for that release
  --notes TAG       the release-page section listing guides published for that release

A `hold: <reason>` on a planned entry keeps its draft unpublished through a release.
"""
import argparse
from pathlib import Path
import re
import shutil
import sys
import tempfile

from check_user_docs import RELEASE, SECTIONS, _front_matter, _path, check_catalog, check_content, read_yaml

DRAFTS = 'docs/drafts'
GUIDES = 'docs/user/guides'
CATALOG = 'docs/user/features.yml'
INDEX = 'docs/user/index.md'
MANUAL_URL = 'https://docs.offband.org/firmware/'
README = 'README.md'
# Planned entries carry these exact lines so promotion is a rewrite, not a YAML re-dump
# that would reformat the whole catalog.
PLANNED_COVERAGE = '    coverage: planned'
PLANNED_AVAILABILITY = '    availability: {channel: development, verified_releases: []}'
START_HERE_LINK = re.compile(r'^- \[[^\]]+\]\(guides/[^)]+\.md\)\.?$')


class PromoteError(Exception):
    pass


def _read_raw(path):
    """Text with its line endings intact (Path.read_text has no newline= before 3.13)."""
    with open(path, encoding='utf-8', newline='') as f:
        return f.read()


def _catalog(root):
    data = read_yaml(root / CATALOG)
    features = data.get('features') if isinstance(data, dict) else None
    if not isinstance(features, list):
        raise PromoteError('catalog has no feature list')
    return [f for f in features if isinstance(f, dict)]


def planned(root):
    """[(id, title, hold or None)] for every planned catalog entry."""
    return [(f.get('id'), f.get('title'), f.get('hold')) for f in _catalog(root)
            if f.get('coverage') == 'planned']


def _entry_span(lines, fid):
    """Line range of one catalog entry, from its `  - id:` line to the next."""
    start = next((i for i, l in enumerate(lines) if l.rstrip() == f'  - id: {fid}'), None)
    if start is None:
        raise PromoteError(f'{fid}: no `  - id: {fid}` line in the catalog')
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith('  - id: ')), len(lines))
    return start, end


def rewrite_entry(text, fid, tag):
    """The catalog text with one planned entry made documented for `tag`."""
    channel = 'beta' if re.search(r'-(beta|rc)\d*\Z', tag) else 'stable'
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(nl)
    start, end = _entry_span(lines, fid)
    found = {PLANNED_COVERAGE: None, PLANNED_AVAILABILITY: None}
    for i in range(start, end):
        if lines[i].rstrip() in found:
            found[lines[i].rstrip()] = i
    missing = [k.strip() for k, v in found.items() if v is None]
    if missing:
        raise PromoteError(f'{fid}: planned entry must carry the line(s) {missing}')
    lines[found[PLANNED_AVAILABILITY]] = (f'    availability: {{channel: {channel}, '
                                         f'verified_releases: [{tag}]}}')
    at = found[PLANNED_COVERAGE]
    lines[at:at + 1] = ['    coverage: documented', f'    guide: guides/{fid}.md']
    return nl.join(lines)


def rewrite_index(text, fid, title):
    """The start page with a link to the new guide after the last guide link."""
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(nl)
    last = max((i for i, l in enumerate(lines) if START_HERE_LINK.match(l.strip())), default=None)
    if last is None:
        raise PromoteError('index.md has no guide links to add to')
    lines.insert(last + 1, f'- [{title}](guides/{fid}.md).')
    return nl.join(lines)


def check_drafts(repo_root):
    """Every planned entry has a draft, every draft would publish clean, nothing strays."""
    root = Path(repo_root).resolve()
    drafts = _path(root, DRAFTS)
    try:
        entries = _catalog(root)
    except (PromoteError, ValueError) as exc:
        return [str(exc)]
    errors = []
    by_id = {f.get('id'): f for f in entries}
    for f in entries:
        fid = f.get('id')
        if 'hold' in f and f.get('coverage') != 'planned':
            errors.append(f'{fid}: only a planned entry can be on hold')
        if f.get('coverage') != 'planned':
            continue
        hold = f.get('hold')
        if hold is not None and (not isinstance(hold, str) or len(hold.strip()) < 20):
            errors.append(f'{fid}: a hold needs a concrete reason')
        if 'guide' in f:
            errors.append(f'{fid}: a planned entry has a draft, not a guide')
        page = None if drafts is None else _path(root, f'{DRAFTS}/{fid}.md')
        if page is None or not page.is_file():
            errors.append(f'{fid}: planned, so its draft must be {DRAFTS}/{fid}.md')
            continue
        if (root / GUIDES / f'{fid}.md').exists():
            errors.append(f'{fid}: {GUIDES}/{fid}.md already exists, so the draft cannot publish')
        try:
            text = page.read_text(encoding='utf-8')
            meta = _front_matter(text)
        except (ValueError, OSError, UnicodeError):
            errors.append(f'{fid}: unreadable draft or front matter')
            continue
        if meta.get('feature') != fid or meta.get('title') != f.get('title'):
            errors.append(f'{fid}: draft front matter must be `title: <catalog title>` and `feature: {fid}`')
        for heading in SECTIONS:
            if not re.search(r'^## ' + re.escape(heading) + r'\s*$', text, re.M):
                errors.append(f'{fid}: draft is missing section: {heading}')
        try:
            rewrite_entry(_read_raw(root / CATALOG), fid, 'offband-v0.0.0')
        except PromoteError as exc:
            errors.append(str(exc))
    if drafts is not None and drafts.is_dir():
        for item in sorted(drafts.iterdir()):
            if item.name == README:
                continue
            fid = item.stem
            if item.is_symlink() or not item.is_file() or item.suffix != '.md':
                errors.append(f'{DRAFTS}/{item.name}: only <feature-id>.md drafts belong here')
            elif by_id.get(fid, {}).get('coverage') != 'planned':
                errors.append(f'{DRAFTS}/{item.name}: no planned catalog entry {fid}')
    if not errors:
        errors.extend(_publication_errors(root, [f.get('id') for f in entries if f.get('coverage') == 'planned']))
    return errors


def _publication_errors(root, fids):
    """Check the manual as it would stand with every draft published."""
    if not fids:
        return []
    with tempfile.TemporaryDirectory() as tmp:
        staged = Path(tmp) / 'repo'
        shutil.copytree(root / 'docs/user', staged / 'docs/user')
        for fid in fids:
            shutil.copy2(root / DRAFTS / f'{fid}.md', staged / GUIDES / f'{fid}.md')
        return [f'as published: {e}' for e in check_content(staged / 'docs/user')]


def gate(root):
    """(waiting, held): drafts a release would leave unpublished, and deliberate holds."""
    waiting = [(fid, title) for fid, title, hold in planned(root) if hold is None]
    held = [(fid, hold) for fid, _, hold in planned(root) if hold is not None]
    return waiting, held


def promote(repo_root, tag):
    root = Path(repo_root).resolve()
    if not RELEASE.fullmatch(tag):
        raise PromoteError(f'{tag}: not an offband-vX.Y.Z[-betaN|-rcN] release tag')
    # Everything is checked before anything moves, so a refusal leaves the tree untouched.
    errors = (check_catalog(root, root / CATALOG, root / 'docs/user')
              + check_content(root / 'docs/user') + check_drafts(root))
    if errors:
        raise PromoteError('the manual or its drafts are not ready:\n  ' + '\n  '.join(errors))
    waiting, _ = gate(root)
    catalog = _read_raw(root / CATALOG)
    index = _read_raw(root / INDEX)
    for fid, title in waiting:
        catalog = rewrite_entry(catalog, fid, tag)
        index = rewrite_index(index, fid, title)
    for fid, _ in waiting:
        (root / DRAFTS / f'{fid}.md').replace(root / GUIDES / f'{fid}.md')
    (root / CATALOG).write_text(catalog, encoding='utf-8', newline='')
    (root / INDEX).write_text(index, encoding='utf-8', newline='')
    errors = check_catalog(root, root / CATALOG, root / 'docs/user') + check_content(root / 'docs/user')
    if errors:
        raise PromoteError('promoted manual does not check clean:\n  ' + '\n  '.join(errors))
    return waiting


def notes(repo_root, tag):
    """Markdown for the release page: guides whose verified releases include `tag`."""
    rows = [(f.get('title'), f.get('guide')) for f in _catalog(Path(repo_root).resolve())
            if f.get('coverage') == 'documented' and tag in (f.get('availability') or {}).get('verified_releases', [])]
    if not rows:
        return ''
    body = ['', '## In the user manual', '',
            f'Guides checked against this release, at [docs.offband.org/firmware]({MANUAL_URL}):', '']
    body += [f'- [{t}]({MANUAL_URL}{g[:-len(".md")]}/)' for t, g in sorted(rows)]
    return '\n'.join(body) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--repo-root', type=Path, default=Path(__file__).resolve().parent.parent)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--release', metavar='TAG')
    mode.add_argument('--notes', metavar='TAG')
    args = parser.parse_args(argv)
    try:
        if args.notes:
            sys.stdout.write(notes(args.repo_root, args.notes))
            return 0
        if args.release:
            done = promote(args.repo_root, args.release)
            for fid, title in done:
                print(f'published {GUIDES}/{fid}.md -- {title}')
            if done:
                print('Review each entry\'s verification text in docs/user/features.yml: it was '
                      'written before the release and may still say the feature is unreleased.')
            else:
                print('no drafts to publish')
            return 0
        waiting, held = gate(args.repo_root)
        for fid, reason in held:
            print(f'held: {DRAFTS}/{fid}.md -- {reason}')
        if waiting:
            for fid, title in waiting:
                print(f'::error::unpublished draft {DRAFTS}/{fid}.md ({title}). Run '
                      f'`python scripts/promote_drafts.py --release <tag>` in the release PR, '
                      f'or put the entry on hold with a reason.', file=sys.stderr)
            return 1
        print('PASS: no draft guide is waiting to publish')
        return 0
    except (PromoteError, ValueError, OSError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
