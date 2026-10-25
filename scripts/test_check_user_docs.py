#!/usr/bin/env python3
"""User-manual contract regression tests, including rejected publication inputs."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_user_docs import check_build_config, check_catalog, check_content, check_impact, read_yaml


GUIDE = """---
title: Connect an observer
feature: observer-wifi
---
# Connect an observer
## Applies to
Heltec V4 companion-observer, checked against offband-v1.4.0 source.
## Before you start
Pair your radio.
## Steps
Send `wifi status`.
## Check it worked
Read the reply.
## Troubleshooting
Check that this is the observer image.
## Limits and recovery
No hardware test is claimed.
"""


class ManualTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.content = self.root / 'docs/user'
        (self.content / 'guides').mkdir(parents=True)
        (self.root / 'variants/heltec_v4').mkdir(parents=True)
        (self.root / 'src').mkdir()
        (self.root / 'src/wifi.cpp').write_text('wifi status\n', encoding='utf-8')
        (self.content / 'index.md').write_text('[Connect](guides/wifi.md)\n', encoding='utf-8')
        (self.content / 'guides/wifi.md').write_text(GUIDE, encoding='utf-8')
        self.catalog = {
            'schema_version': 1,
            'features': [{
                'id': 'observer-wifi', 'title': 'Observer WiFi',
                'coverage': 'documented', 'guide': 'guides/wifi.md',
                'roles': ['companion-observer'], 'boards': ['heltec_v4'],
                'availability': {'channel': 'stable', 'verified_releases': ['offband-v1.4.0']},
                'verification': 'source-checked; hardware not verified',
                'evidence': [{'path': 'src/wifi.cpp', 'contains': ['wifi status']}],
            }],
        }
        self.catalog_path = self.content / 'features.yml'
        self.save()

    def save(self):
        # JSON is valid YAML, keeping fixtures independent of emitter details.
        self.catalog_path.write_text(json.dumps(self.catalog), encoding='utf-8')

    def errors(self):
        self.save()
        return check_catalog(self.root, self.catalog_path, self.content)

    def test_valid_catalog_and_content(self):
        self.assertEqual(self.errors(), [])
        self.assertEqual(check_content(self.content), [])

    def test_missing_documented_guide(self):
        self.catalog['features'][0]['guide'] = 'missing.md'
        self.assertTrue(any('guide' in x for x in self.errors()))

    def test_unknown_role_and_board(self):
        f = self.catalog['features'][0]
        f['roles'] = ['roomserver-typo']
        f['boards'] = ['not-a-board']
        errors = self.errors()
        self.assertTrue(any('role' in x for x in errors))
        self.assertTrue(any('board' in x for x in errors))

    def test_empty_and_invalid_release_applicability(self):
        for availability in ({}, {'channel': 'stable', 'verified_releases': []},
                             {'channel': 'stable', 'verified_releases': ['latest']}):
            with self.subTest(availability=availability):
                self.catalog['features'][0]['availability'] = availability
                self.assertTrue(self.errors())

    def test_beta_release_cannot_be_called_stable(self):
        self.catalog['features'][0]['availability']['verified_releases'] = ['offband-v1.5.0-beta7']
        self.assertTrue(any('stable' in x for x in self.errors()))

    def test_missing_and_changed_evidence(self):
        self.catalog['features'][0]['evidence'] = [{'path': 'missing.cpp', 'contains': ['wifi status']}]
        self.assertTrue(any('evidence' in x for x in self.errors()))
        self.catalog['features'][0]['evidence'] = [{'path': 'src/wifi.cpp', 'contains': ['invented reply']}]
        self.assertTrue(any('evidence' in x for x in self.errors()))

    def test_duplicate_feature_and_guide(self):
        self.catalog['features'].append(copy.deepcopy(self.catalog['features'][0]))
        self.assertTrue(any('duplicate' in x for x in self.errors()))

    def test_missing_required_section(self):
        p = self.content / 'guides/wifi.md'
        p.write_text(GUIDE.replace('## Check it worked', '## Something else'), encoding='utf-8')
        self.assertTrue(any('Check it worked' in x for x in self.errors()))

    def test_front_matter_feature_must_match_catalog(self):
        (self.content / 'guides/wifi.md').write_text(GUIDE.replace('feature: observer-wifi', 'feature: invented'), encoding='utf-8')
        self.assertTrue(any('feature' in x for x in self.errors()))

    def test_inventory_can_record_planned_coverage_without_publishing_it(self):
        f = self.catalog['features'][0]
        f['coverage'] = 'planned'
        f.pop('guide')
        (self.content / 'guides/wifi.md').unlink()
        (self.content / 'index.md').write_text('# Manual\n', encoding='utf-8')
        self.assertEqual(self.errors(), [])

    def test_feature_page_cannot_be_missing_from_catalog(self):
        (self.content / 'unregistered.md').write_text(GUIDE.replace('observer-wifi', 'unregistered'), encoding='utf-8')
        self.assertTrue(any('unregistered' in x for x in self.errors()))

    def test_malformed_catalog_shapes_fail_cleanly(self):
        for value in (None, [], {'schema_version': 1, 'features': []},
                      {'schema_version': 1, 'features': [None]}):
            with self.subTest(value=value):
                self.catalog_path.write_text(json.dumps(value), encoding='utf-8')
                self.assertTrue(check_catalog(self.root, self.catalog_path, self.content))

    def test_duplicate_yaml_keys_are_rejected(self):
        self.catalog_path.write_text('schema_version: 1\nschema_version: 2\n', encoding='utf-8')
        with self.assertRaises(ValueError):
            read_yaml(self.catalog_path)

    def test_content_path_must_be_the_public_user_tree(self):
        self.assertTrue(check_catalog(self.root, self.catalog_path, self.root / 'docs'))

    def test_missing_and_broken_local_links(self):
        for link in ('missing.md', '../internal.md', 'guides/wifi.md#missing-heading'):
            with self.subTest(link=link):
                (self.content / 'index.md').write_text(f'[Link]({link})\n', encoding='utf-8')
                self.assertTrue(check_content(self.content))

    def test_valid_local_anchor_and_external_link(self):
        (self.content / 'index.md').write_text('[Guide](guides/wifi.md#check-it-worked)\n[Source](https://example.com/source)\n', encoding='utf-8')
        self.assertEqual(check_content(self.content), [])

    def test_unsafe_link_scheme_rejected(self):
        (self.content / 'index.md').write_text('[Unsafe](javascript:alert)\n', encoding='utf-8')
        self.assertTrue(check_content(self.content))

    def test_raw_html_navigation_cannot_bypass_link_boundary(self):
        for html in ('<a href="../../private.md">Internal</a>',
                     '<IMG SRC="../../private.png">',
                     '<iframe src="https://example.com"></iframe>',
                     '<div style="background:url(../../private.png)">x</div>'):
            with self.subTest(html=html):
                (self.content / 'index.md').write_text(html, encoding='utf-8')
                self.assertTrue(check_content(self.content))

    def test_html_examples_in_fenced_code_are_not_navigation(self):
        (self.content / 'index.md').write_text('```html\n<a href="../../private.md">Example</a>\n```\n', encoding='utf-8')
        self.assertEqual(check_content(self.content), [])

    def test_unreviewed_file_types_and_local_names_rejected(self):
        for name in ('settings.local.md', '.env', 'archive.zip', 'script.py', 'vendor/notes.md'):
            with self.subTest(name=name):
                p = self.content / name
                p.parent.mkdir(exist_ok=True)
                p.write_text('not for publication', encoding='utf-8')
                self.assertTrue(check_content(self.content))
                p.unlink()

    def test_secret_and_private_address_examples_rejected(self):
        for text in ('broker: 10.99.88.77', 'set wifi.pwd real-password', 'ghp_' + 'x' * 30):
            with self.subTest(text=text):
                (self.content / 'index.md').write_text(text, encoding='utf-8')
                self.assertTrue(check_content(self.content))

    def test_documentation_addresses_and_placeholders_allowed(self):
        (self.content / 'index.md').write_text('192.0.2.1 and 192.168.4.1\n`set wifi.pwd <your-password>`\n', encoding='utf-8')
        self.assertEqual(check_content(self.content), [])

    def test_traversal_and_symlinked_evidence_rejected(self):
        self.catalog['features'][0]['evidence'][0]['path'] = '../outside.txt'
        self.assertTrue(self.errors())
        outside = self.root / 'outside.txt'
        outside.write_text('wifi status', encoding='utf-8')
        target = self.root / 'src/link.cpp'
        try:
            target.symlink_to(outside)
        except OSError:
            self.skipTest('Host does not permit creating test symlinks')
        self.catalog['features'][0]['evidence'][0]['path'] = 'src/link.cpp'
        self.assertTrue(self.errors())

    def test_code_change_requires_impact_record(self):
        self.assertTrue(check_impact(self.root, ['src/new_behavior.cpp'], []))

    def test_documentation_only_change_does_not_require_an_impact_record(self):
        self.assertEqual(check_impact(self.root, ['docs/user/index.md'], []), [])

    def test_no_impact_needs_a_specific_reason(self):
        record = self.root / 'impact.yml'
        record.write_text(json.dumps({'issue': 123, 'impact': 'none', 'reason': ''}), encoding='utf-8')
        self.assertTrue(check_impact(self.root, ['src/wifi.cpp'], [record]))
        record.write_text(json.dumps({'issue': 123, 'impact': 'none', 'reason': 'Refactors a helper without changing its public behavior.'}), encoding='utf-8')
        self.assertEqual(check_impact(self.root, ['src/wifi.cpp'], [record]), [])

    def test_updated_impact_must_name_a_changed_guide(self):
        record = self.root / 'impact.yml'
        record.write_text(json.dumps({'issue': 123, 'impact': 'updated', 'guides': ['docs/user/guides/wifi.md']}), encoding='utf-8')
        self.assertTrue(check_impact(self.root, ['src/wifi.cpp'], [record]))
        self.assertEqual(check_impact(self.root, ['src/wifi.cpp', 'docs/user/guides/wifi.md'], [record]), [])

    def test_cli_unknown_root_fails_without_traceback(self):
        p = subprocess.run([sys.executable, str(Path(__file__).with_name('check_user_docs.py')),
                            '--repo-root', str(self.root / 'missing')], capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)
        self.assertNotIn('Traceback', p.stderr)

    def test_build_cannot_widen_the_validated_public_tree(self):
        config = self.root / 'mkdocs.user.yml'
        for directory in ('docs', '../docs', '/tmp/docs', 'docs/user'):
            with self.subTest(directory=directory):
                config.write_text(json.dumps({'docs_dir': directory, 'site_dir': 'site-user',
                                              'exclude_docs': 'features.yml', 'plugins': ['search']}), encoding='utf-8')
                errors = check_build_config(self.root, config)
                self.assertEqual(bool(errors), directory != 'docs/user')

    def test_build_must_exclude_the_catalog_metadata(self):
        config = self.root / 'mkdocs.user.yml'
        for exclusion in ('', '!features.yml', 'features.yml\n!features.yml'):
            config.write_text(json.dumps({'docs_dir': 'docs/user', 'site_dir': 'site-user',
                                          'exclude_docs': exclusion, 'plugins': ['search']}), encoding='utf-8')
            self.assertTrue(check_build_config(self.root, config))

    def test_malformed_field_types_fail_without_crashing(self):
        for field in ('coverage', 'availability', 'verification', 'roles', 'boards', 'evidence', 'guide'):
            with self.subTest(field=field):
                original = self.catalog['features'][0][field]
                self.catalog['features'][0][field] = {'unexpected': []}
                self.assertTrue(self.errors())
                self.catalog['features'][0][field] = original


if __name__ == '__main__':
    unittest.main()
