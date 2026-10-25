#!/usr/bin/env python3
"""Validate the public user-manual boundary, applicability and source evidence.

These checks establish structure and catch known disclosure patterns. They do not
prove semantic accuracy, hardware operation, or that arbitrary prose is secret-free.
External URLs are not fetched. Epic C will wire impact decisions into PR policy;
the optional impact interface here provides its tested foundation.
"""
import argparse
from html.parser import HTMLParser
import ipaddress
from pathlib import Path, PurePosixPath
import re
import sys
from urllib.parse import unquote, urlsplit

import yaml

ROLES = {'companion-observer', 'companion', 'repeater', 'room-server', 'bridge'}
CHANNELS = {'stable', 'beta', 'development'}
COVERAGE = {'documented', 'planned', 'needs-review'}
SECTIONS = ('Applies to', 'Before you start', 'Steps', 'Check it worked',
            'Troubleshooting', 'Limits and recovery')
PRIVATE_COMPONENTS = {'vendor', 'plans', 'handoffs', 'assessments', 'superpowers',
                      'llm-consultations', 'llm-consult-prompts', 'docs-internal'}
IMAGE_SUFFIXES = {'.png', '.jpg', '.jpeg', '.webp', '.gif', '.ico'}
CODE_PREFIXES = ('src/', 'examples/', 'variants/', 'include/', 'lib/', 'boards/')
RELEASE = re.compile(r'offband-v\d+\.\d+\.\d+(?:-(?:beta|rc)\d*)?\Z')
LINK = re.compile(r'!?\[[^\]]*\]\(([^\s)]+)(?:\s+[^)]*)?\)')
SECRET = re.compile(r'\b(?:ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|'
                    r'AKIA[A-Z0-9]{16}|xox[baprs]-[A-Za-z0-9-]{10,})\b|'
                    r'-----BEGIN [A-Z ]*PRIVATE KEY-----')
PRIVATE_NETS = tuple(ipaddress.ip_network(n) for n in ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16'))


class UniqueLoader(yaml.SafeLoader):
    """Reject duplicate metadata keys instead of silently retaining the last."""


def _mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        try:
            if key in result:
                raise ValueError('duplicate YAML key')
            result[key] = loader.construct_object(value_node, deep=deep)
        except TypeError as exc:
            raise ValueError('YAML mapping key must be scalar') from exc
    return result


UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _mapping)


def read_yaml(path: Path):
    try:
        return yaml.load(path.read_text(encoding='utf-8-sig'), Loader=UniqueLoader)
    except (OSError, UnicodeError, yaml.YAMLError, ValueError, RecursionError) as exc:
        raise ValueError(f'{path.name}: invalid or unreadable YAML ({type(exc).__name__})') from exc


def _path(root: Path, name):
    """Resolve a portable repository-relative path, refusing links at every level."""
    if not isinstance(name, str) or not name or '\\' in name or ':' in name:
        return None
    relative = PurePosixPath(name)
    if relative.is_absolute() or '..' in relative.parts:
        return None
    target = root
    for part in relative.parts:
        target = target / part
        if target.is_symlink():
            return None
    try:
        target.resolve().relative_to(root.resolve())
    except (ValueError, OSError):
        return None
    return target


def _strings(value):
    return isinstance(value, list) and bool(value) and all(isinstance(x, str) and x.strip() for x in value)


def _front_matter(text):
    lines = text.splitlines()
    if not lines or lines[0] != '---':
        return {}
    try:
        end = lines.index('---', 1)
        value = yaml.load('\n'.join(lines[1:end]), Loader=UniqueLoader)
    except (ValueError, TypeError, yaml.YAMLError) as exc:
        raise ValueError('invalid page front matter') from exc
    if not isinstance(value, dict):
        raise ValueError('page front matter must be a mapping')
    return value


def check_catalog(repo_root: Path, catalog_path: Path, content_root: Path) -> list[str]:
    root, content = Path(repo_root).resolve(), Path(content_root)
    errors = []
    expected = _path(root, 'docs/user')
    if expected is None or content.resolve() != expected.resolve() or not content.is_dir():
        return ['content root must be the repository docs/user directory, without symlinks']
    if Path(catalog_path).resolve() != (content / 'features.yml').resolve() or Path(catalog_path).is_symlink():
        return ['catalog must be docs/user/features.yml, without symlinks']
    try:
        data = read_yaml(Path(catalog_path))
    except ValueError as exc:
        return [str(exc)]
    if not isinstance(data, dict) or type(data.get('schema_version')) is not int or data['schema_version'] != 1:
        return ['catalog schema_version must be 1']
    features = data.get('features')
    if not isinstance(features, list) or not features:
        return ['catalog features must be a nonempty list']
    known_boards = {p.name for p in (root / 'variants').iterdir() if p.is_dir()} if (root / 'variants').is_dir() else set()
    ids, guides, entries = set(), set(), {}
    for pos, feature in enumerate(features):
        label = f'feature[{pos}]'
        if not isinstance(feature, dict):
            errors.append(f'{label}: expected a mapping')
            continue
        fid = feature.get('id')
        if not isinstance(fid, str) or not re.fullmatch(r'[a-z][a-z0-9-]*', fid):
            errors.append(f'{label}: invalid feature id')
            continue
        label = fid
        if fid in ids:
            errors.append(f'{label}: duplicate feature id')
        ids.add(fid)
        entries[fid] = feature
        if not isinstance(feature.get('title'), str) or not feature['title'].strip():
            errors.append(f'{label}: missing title')
        if not isinstance(feature.get('coverage'), str) or feature['coverage'] not in COVERAGE:
            errors.append(f'{label}: invalid coverage')
        for field, allowed in (('roles', ROLES), ('boards', known_boards)):
            values = feature.get(field)
            if not _strings(values) or any(v not in allowed for v in values):
                errors.append(f'{label}: unknown or empty {field}')
        availability = feature.get('availability')
        if not isinstance(availability, dict) or not isinstance(availability.get('channel'), str) or availability['channel'] not in CHANNELS:
            errors.append(f'{label}: missing/invalid release applicability')
        else:
            releases = availability.get('verified_releases')
            if not isinstance(releases, list) or any(not isinstance(v, str) or not RELEASE.fullmatch(v) for v in releases):
                errors.append(f'{label}: invalid verified release list')
            elif not releases and (feature.get('coverage') == 'documented' or availability['channel'] != 'development'):
                errors.append(f'{label}: documented/released behavior needs a verified release')
            elif availability['channel'] == 'stable' and any('-beta' in v or '-rc' in v for v in releases):
                errors.append(f'{label}: prerelease cannot be labeled stable')
        if not isinstance(feature.get('verification'), str) or not feature['verification'].strip():
            errors.append(f'{label}: missing verification limits')
        evidence = feature.get('evidence')
        if not isinstance(evidence, list) or not evidence:
            errors.append(f'{label}: missing source/test evidence')
        else:
            for source in evidence:
                if not isinstance(source, dict):
                    errors.append(f'{label}: invalid evidence mapping')
                    continue
                path = _path(root, source.get('path'))
                snippets = source.get('contains')
                if path is None or not path.is_file() or not _strings(snippets):
                    errors.append(f'{label}: missing/unsafe evidence path or assertions')
                    continue
                try:
                    code = path.read_text(encoding='utf-8')
                    if any(s not in code for s in snippets):
                        errors.append(f'{label}: source evidence no longer matches; review the claim')
                except (OSError, UnicodeError):
                    errors.append(f'{label}: unreadable evidence')
        if feature.get('coverage') == 'documented':
            relative = feature.get('guide')
            path = _path(content, relative)
            if path is None or not path.is_file() or path.suffix != '.md':
                errors.append(f'{label}: missing/unsafe guide')
                continue
            if relative in guides:
                errors.append(f'{label}: duplicate guide')
            guides.add(relative)
            try:
                text = path.read_text(encoding='utf-8')
                meta = _front_matter(text)
                if meta.get('feature') != fid:
                    errors.append(f'{label}: guide feature does not match catalog')
                for heading in SECTIONS:
                    if not re.search(r'^## ' + re.escape(heading) + r'\s*$', text, re.M):
                        errors.append(f'{label}: missing guide section: {heading}')
            except (ValueError, OSError, UnicodeError):
                errors.append(f'{label}: unreadable guide/front matter')
    for page in content.rglob('*.md'):
        if page.is_symlink():
            continue
        try:
            meta = _front_matter(page.read_text(encoding='utf-8'))
            if 'feature' in meta:
                fid = meta['feature']
                if not isinstance(fid, str) or fid not in entries:
                    errors.append(f'{page.name}: unregistered feature page')
                elif entries[fid].get('coverage') != 'documented' or page.relative_to(content).as_posix() != entries[fid].get('guide'):
                    errors.append(f'{page.name}: feature page is not its documented catalog guide')
        except (ValueError, OSError, UnicodeError):
            errors.append(f'{page.name}: invalid front matter or unreadable page')
    return errors


def _anchors(text):
    found, counts = set(), {}
    for heading in re.findall(r'^#{1,6}\s+(.+)$', text, re.M):
        explicit = re.search(r'\{\s*#([^ }]+)\s*\}', heading)
        if explicit:
            found.add(explicit[1])
            continue
        slug = re.sub(r'[^\w\s-]', '', heading.lower().strip())
        slug = re.sub(r'[-\s]+', '-', slug)
        n = counts.get(slug, 0)
        counts[slug] = n + 1
        found.add(slug + (f'_{n}' if n else ''))
    return found


class _HtmlNavigation(HTMLParser):
    """Guides use Markdown navigation so validation cannot miss opaque HTML."""
    def __init__(self):
        super().__init__()
        self.found = False

    def handle_starttag(self, tag, attrs):
        navigation = {'a', 'img', 'iframe', 'script', 'style', 'link', 'base',
                      'meta', 'form', 'object', 'embed', 'source', 'video', 'audio'}
        if tag in navigation or any(k in {'href', 'src', 'srcset', 'action', 'data', 'style'} or
                                    k.startswith('on') for k, _ in attrs):
            self.found = True


def check_content(content_root: Path) -> list[str]:
    root = Path(content_root)
    if not root.is_dir() or root.is_symlink():
        return ['missing or symlinked public content root']
    errors = []
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        label = relative.as_posix()
        if path.is_symlink():
            errors.append(f'{label}: symlinks are not public content')
            continue
        if any(part.startswith('.') or part.lower() in PRIVATE_COMPONENTS or '.local.' in part.lower() for part in relative.parts):
            errors.append(f'{label}: internal/local path in publication tree')
            continue
        if path.is_dir():
            continue
        metadata = label == 'features.yml'
        if not metadata and path.suffix != '.md' and not (relative.parts[0] == 'assets' and path.suffix.lower() in IMAGE_SUFFIXES):
            errors.append(f'{label}: file type is not approved public content')
            continue
        if path.suffix != '.md' and not metadata:
            continue
        try:
            text = path.read_text(encoding='utf-8')
        except (OSError, UnicodeError):
            errors.append(f'{label}: unreadable text')
            continue
        if SECRET.search(text):
            errors.append(f'{label}: possible credential; manual review required')
        for addr in re.findall(r'(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])', text):
            try:
                ip = ipaddress.ip_address(addr)
            except ValueError:
                continue
            if addr != '192.168.4.1' and any(ip in net for net in PRIVATE_NETS):
                errors.append(f'{label}: private network example; use documentation addresses')
                break
        if re.search(r'(?im)\bset\s+wifi\.pwd\s+(?!<)[^\s`]+', text):
            errors.append(f'{label}: use an angle-bracket placeholder for the WiFi password')
        if metadata:
            continue
        # Ignore fenced examples rather than treating their sample links as navigation.
        prose = re.sub(r'^```[^\n]*\n.*?^```\s*$', '', text, flags=re.M | re.S)
        html = _HtmlNavigation()
        html.feed(prose)
        if html.found:
            errors.append(f'{label}: HTML navigation/embeds are not supported; use Markdown links and images')
        for raw in LINK.findall(prose):
            parsed = urlsplit(raw.strip('<>'))
            if parsed.scheme:
                if parsed.scheme not in {'https', 'http', 'mailto'}:
                    errors.append(f'{label}: unsafe link scheme')
                continue
            if parsed.netloc:
                errors.append(f'{label}: scheme-relative links are not allowed')
                continue
            name = unquote(parsed.path)
            if name.startswith('/'):
                errors.append(f'{label}: local links must be relative to the manual')
                continue
            target = path.parent / name if name else path
            try:
                rel = target.resolve().relative_to(root.resolve()).as_posix()
                safe = _path(root, rel)
            except (ValueError, OSError):
                safe = None
            if safe is None or not safe.is_file() or safe.name == 'features.yml':
                errors.append(f'{label}: broken or out-of-manual local link')
                continue
            if parsed.fragment and safe.suffix == '.md':
                if unquote(parsed.fragment) not in _anchors(safe.read_text(encoding='utf-8')):
                    errors.append(f'{label}: broken local heading link')
    return errors


def check_build_config(repo_root: Path, config_path: Path) -> list[str]:
    """Make the build's input match the checker boundary; don't just trust a name."""
    try:
        data = read_yaml(config_path)
    except ValueError as exc:
        return [str(exc)]
    if not isinstance(data, dict):
        return ['build configuration must be a mapping']
    errors = []
    if data.get('docs_dir') != 'docs/user' or _path(Path(repo_root), 'docs/user') is None:
        errors.append('build docs_dir must be the validated docs/user tree')
    if data.get('site_dir') != 'site-user':
        errors.append('build site_dir must be the separate site-user output directory')
    exclusion = data.get('exclude_docs')
    if not isinstance(exclusion, str) or exclusion.strip() != 'features.yml':
        errors.append('build must exclude features.yml catalog metadata')
    if data.get('plugins') != ['search'] or data.get('hooks'):
        errors.append('standalone build permits only the approved search plugin and no hooks')
    return errors


def check_impact(repo_root: Path, changed_paths: list[str], record_paths: list[Path]) -> list[str]:
    """Shape-check impact decisions; Epic C owns their CI activation and review policy."""
    code = [p for p in changed_paths if p.startswith(CODE_PREFIXES) or p == 'platformio.ini']
    if not code:
        return []
    if not record_paths:
        return ['firmware changes require a documentation-impact decision']
    errors = []
    for path in record_paths:
        try:
            relative = Path(path).resolve().relative_to(Path(repo_root).resolve()).as_posix()
            if _path(Path(repo_root), relative) is None:
                raise ValueError('unsafe impact path')
            record = read_yaml(Path(path))
        except ValueError:
            errors.append('invalid or unreadable impact record')
            continue
        if not isinstance(record, dict) or type(record.get('issue')) is not int or record['issue'] <= 0:
            errors.append('impact record must name a positive issue number')
            continue
        if record.get('impact') == 'none':
            if not isinstance(record.get('reason'), str) or len(record['reason'].strip()) < 20:
                errors.append('no-impact decision requires a specific reason')
        elif record.get('impact') == 'updated':
            guides = record.get('guides')
            if not _strings(guides) or any(not g.startswith('docs/user/') or not g.endswith('.md') or g not in changed_paths or
                                          (p := _path(Path(repo_root), g)) is None or not p.is_file() for g in guides):
                errors.append('updated impact must name existing, changed public guides')
        else:
            errors.append('impact must be updated or none')
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--catalog', type=Path, default=Path('docs/user/features.yml'))
    parser.add_argument('--content-root', type=Path, default=Path('docs/user'))
    parser.add_argument('--build-config', type=Path, default=Path('mkdocs.user.yml'))
    parser.add_argument('--changed-files', type=Path)
    parser.add_argument('--impact', type=Path, action='append', default=[])
    args = parser.parse_args(argv)
    root = args.repo_root.resolve()
    try:
        catalog = args.catalog if args.catalog.is_absolute() else root / args.catalog
        content = args.content_root if args.content_root.is_absolute() else root / args.content_root
        errors = check_catalog(root, catalog, content)
        if not errors:
            errors.extend(check_content(content))
            config = args.build_config if args.build_config.is_absolute() else root / args.build_config
            errors.extend(check_build_config(root, config))
        if args.changed_files:
            changed = args.changed_files.read_text(encoding='utf-8').splitlines()
            errors.extend(check_impact(root, changed, [p if p.is_absolute() else root / p for p in args.impact]))
    except (OSError, UnicodeError, ValueError) as exc:
        errors = [f'cannot validate documentation inputs ({type(exc).__name__})']
    for error in errors:
        print(f'ERROR: {error}', file=sys.stderr)
    if errors:
        return 1
    print('PASS: public user documentation catalog, evidence, content and local links')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
