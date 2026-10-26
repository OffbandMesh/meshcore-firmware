#!/usr/bin/env python3
"""Require a changed, reviewable docs-impact decision for firmware changes."""
import argparse
from pathlib import Path
import re
import subprocess
import sys

from check_user_docs import check_impact, read_yaml

SHA = re.compile(r'[0-9a-f]{40}\Z')
RECORD = re.compile(r'docs/changes/([1-9][0-9]*)-[a-z0-9][a-z0-9-]*\.yml\Z')


class ImpactError(Exception):
    pass


def git(root, *args):
    result = subprocess.run(['git', '-C', str(root), *args], capture_output=True)
    if result.returncode:
        raise ImpactError('Required Git history is missing or the comparison failed')
    try:
        return result.stdout.decode('utf-8')
    except UnicodeDecodeError as exc:
        raise ImpactError('Changed paths must be valid UTF-8') from exc


def check_change(root: Path, base: str, head: str, mode='pr') -> list[str]:
    root = Path(root).resolve()
    if not SHA.fullmatch(base) or not SHA.fullmatch(head) or mode not in {'pr', 'push'}:
        raise ImpactError('Comparison requires full lowercase commit SHAs and pr/push mode')
    for sha in (base, head):
        git(root, 'cat-file', '-e', sha + '^{commit}')
    if git(root, 'rev-parse', 'HEAD').strip() != head:
        raise ImpactError('Checked-out tree must match the requested comparison head')
    start = git(root, 'merge-base', base, head).strip() if mode == 'pr' else base
    # No rename detection: both the old deletion and new addition remain visible.
    fields = git(root, 'diff', '--no-renames', '--name-status', '-z', start, head, '--').split('\0')
    if fields[-1] != '' or (len(fields) - 1) % 2:
        raise ImpactError('Unexpected Git path listing')
    changes = list(zip(fields[:-1:2], fields[1:-1:2]))
    changed = [name for _, name in changes]
    records, errors = [], []
    for status, name in changes:
        if not name.startswith('docs/changes/') or not name.endswith(('.yml', '.yaml')):
            continue
        match = RECORD.fullmatch(name)
        if match is None:
            errors.append(f'{name}: use docs/changes/<issue>-<description>.yml')
            continue
        if status == 'D':
            errors.append(f'{name}: retain historical documentation-impact records')
            continue
        path = root / name
        try:
            if path.is_symlink() or not path.resolve().is_relative_to(root) or not path.is_file():
                raise ValueError('unsafe decision path')
            record = read_yaml(path)
        except (OSError, UnicodeError, ValueError):
            errors.append(f'{name}: invalid or unreadable decision')
            continue
        required = {'schema_version', 'issue', 'impact', 'reason'}
        if (not isinstance(record, dict) or not required.issubset(record) or
                not set(record).issubset(required | {'guides'}) or
                type(record.get('schema_version')) is not int or record['schema_version'] != 1 or
                type(record.get('issue')) is not int or record['issue'] != int(match[1])):
            errors.append(f'{name}: schema version, fields and filename issue must match the decision')
            continue
        if not isinstance(record['reason'], str) or len(record['reason'].strip()) < 20:
            errors.append(f'{name}: provide a concrete documentation-impact reason')
        if record['impact'] == 'none' and 'guides' in record:
            errors.append(f'{name}: none must not list updated guides')
        records.append(path)
    if not errors:
        errors.extend(check_impact(root, changed, records))
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--base', required=True)
    parser.add_argument('--head', required=True)
    parser.add_argument('--mode', choices=['pr', 'push'], default='pr')
    args = parser.parse_args()
    try:
        errors = check_change(args.repo_root, args.base, args.head, args.mode)
    except (ImpactError, OSError, UnicodeError, ValueError) as exc:
        errors = [f'Cannot validate documentation impact: {exc}']
    for error in errors:
        print(f'ERROR: {error}', file=sys.stderr)
    if errors:
        return 1
    print('PASS: documentation-impact decisions match the compared change')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
