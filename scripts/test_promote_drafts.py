#!/usr/bin/env python3
"""Draft-guide convention, release gate and promotion: positive and negative cases (#1968)."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_user_docs import check_catalog, check_content
import promote_drafts as pd

BODY = """# {title}
## Applies to
Heltec V4 observers, development build.
## Before you start
Pair your radio.
## Steps
Send `nbr status`. See [WiFi](observer-wifi.md) first.
## Check it worked
Read the reply.
## Troubleshooting
Check that this is the observer image.
## Limits and recovery
No hardware test is claimed. [Back to the manual](../index.md).
"""


def page(fid, title):
    return f'---\ntitle: {title}\nfeature: {fid}\n---\n\n' + BODY.format(title=title)


CATALOG = """schema_version: 1
features:
  - id: observer-wifi
    title: Connect an observer to WiFi
    coverage: documented
    guide: guides/observer-wifi.md
    roles: [companion-observer]
    boards: [heltec_v4]
    availability: {{channel: stable, verified_releases: [offband-v1.4.0]}}
    verification: Source-checked; no hardware trial.
    evidence:
      - path: src/wifi.cpp
        contains: ['wifi status']
  - id: neighbors
    title: Report the repeaters an observer hears
    coverage: planned
{extra}    roles: [companion-observer]
    boards: [heltec_v4]
    availability: {{channel: development, verified_releases: []}}
    verification: Source-checked; not in any release.
    evidence:
      - path: src/wifi.cpp
        contains: ['nbr status']
"""

INDEX = """---
title: Manual
---

# Manual

## Start here

- [Connect an observer to WiFi](guides/observer-wifi.md).
- [Download firmware](https://github.com/OffbandMesh/meshcore-firmware/releases).
"""


class DraftTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'docs/user/guides').mkdir(parents=True)
        (self.root / 'docs/drafts').mkdir(parents=True)
        (self.root / 'variants/heltec_v4').mkdir(parents=True)
        (self.root / 'src').mkdir()
        (self.root / 'src/wifi.cpp').write_text('wifi status\nnbr status\n', encoding='utf-8')
        self.write('docs/user/guides/observer-wifi.md', page('observer-wifi', 'Connect an observer to WiFi'))
        self.write('docs/user/index.md', INDEX)
        self.catalog()
        self.write('docs/drafts/neighbors.md', page('neighbors', 'Report the repeaters an observer hears'))

    def write(self, rel, text, nl='\n'):
        with open(self.root / rel, 'w', encoding='utf-8', newline='') as f:
            f.write(text.replace('\n', nl))

    def catalog(self, extra='', nl='\n'):
        self.write('docs/user/features.yml', CATALOG.format(extra=extra), nl)

    def read(self, rel):
        return pd._read_raw(self.root / rel)

    def manual_clean(self):
        user = self.root / 'docs/user'
        return check_catalog(self.root, user / 'features.yml', user) + check_content(user)

    def assertDraftError(self, phrase):
        errors = pd.check_drafts(self.root)
        self.assertTrue(any(phrase in e for e in errors), f'expected {phrase!r}; got {errors or "NO ERRORS"}')

    # ---- the convention holds -------------------------------------------------------

    def test_a_valid_draft_checks_clean(self):
        self.assertEqual(pd.check_drafts(self.root), [])
        self.assertEqual(self.manual_clean(), [])

    def test_a_planned_entry_without_its_draft_fails(self):
        (self.root / 'docs/drafts/neighbors.md').unlink()
        self.assertDraftError('its draft must be docs/drafts/neighbors.md')

    def test_a_draft_with_no_planned_entry_fails(self):
        self.write('docs/drafts/orphan.md', page('orphan', 'Orphan'))
        self.assertDraftError('no planned catalog entry orphan')

    def test_a_draft_for_a_documented_feature_fails(self):
        self.write('docs/drafts/observer-wifi.md', page('observer-wifi', 'Connect an observer to WiFi'))
        self.assertDraftError('no planned catalog entry observer-wifi')

    def test_anything_but_a_markdown_draft_fails(self):
        self.write('docs/drafts/notes.txt', 'scratch')
        self.assertDraftError('only <feature-id>.md drafts belong here')

    def test_the_readme_is_allowed(self):
        self.write('docs/drafts/README.md', '# Drafts\n')
        self.assertEqual(pd.check_drafts(self.root), [])

    def test_a_front_matter_title_that_differs_from_the_catalog_fails(self):
        self.write('docs/drafts/neighbors.md', page('neighbors', 'Something else'))
        self.assertDraftError('draft front matter must be')

    def test_a_front_matter_feature_that_differs_fails(self):
        self.write('docs/drafts/neighbors.md', page('other', 'Report the repeaters an observer hears'))
        self.assertDraftError('draft front matter must be')

    def test_a_missing_section_fails(self):
        text = page('neighbors', 'Report the repeaters an observer hears').replace('## Troubleshooting\n', '')
        self.write('docs/drafts/neighbors.md', text)
        self.assertDraftError('draft is missing section: Troubleshooting')

    def test_a_link_that_breaks_once_published_fails(self):
        text = page('neighbors', 'Report the repeaters an observer hears').replace('observer-wifi.md', 'nowhere.md')
        self.write('docs/drafts/neighbors.md', text)
        self.assertDraftError('as published:')

    def test_a_link_written_from_the_drafts_folder_fails(self):
        # Correct from docs/drafts, wrong from docs/user/guides where it will live.
        text = page('neighbors', 'Report the repeaters an observer hears').replace(
            '(observer-wifi.md)', '(../user/guides/observer-wifi.md)')
        self.write('docs/drafts/neighbors.md', text)
        self.assertDraftError('as published:')

    def test_a_planned_entry_naming_a_guide_fails(self):
        self.catalog(extra='    guide: guides/neighbors.md\n')
        self.assertDraftError('a planned entry has a draft, not a guide')

    def test_a_draft_whose_guide_already_exists_fails(self):
        self.write('docs/user/guides/neighbors.md', 'x')
        self.assertDraftError('already exists, so the draft cannot publish')

    def test_a_planned_entry_promotion_cannot_rewrite_fails(self):
        text = self.read('docs/user/features.yml').replace(
            '    availability: {channel: development, verified_releases: []}\n    verification: Source-checked; not',
            '    availability:\n      channel: development\n      verified_releases: []\n    verification: Source-checked; not')
        self.write('docs/user/features.yml', text)
        self.assertDraftError('planned entry must carry the line(s)')

    def test_a_hold_needs_a_real_reason(self):
        self.catalog(extra='    hold: later\n')
        self.assertDraftError('a hold needs a concrete reason')

    def test_only_a_planned_entry_can_be_held(self):
        text = self.read('docs/user/features.yml').replace(
            '    guide: guides/observer-wifi.md\n',
            '    guide: guides/observer-wifi.md\n    hold: the owner wants this one back in review first\n')
        self.write('docs/user/features.yml', text)
        self.assertDraftError('only a planned entry can be on hold')

    def test_a_published_guide_missing_from_the_start_page_fails(self):
        self.write('docs/user/index.md', INDEX.replace('- [Connect an observer to WiFi](guides/observer-wifi.md).\n', ''))
        self.assertTrue(any('not linked from index.md' in e for e in self.manual_clean()))

    def test_a_start_page_link_hidden_in_a_comment_does_not_count(self):
        self.write('docs/user/index.md', INDEX.replace(
            '- [Connect an observer to WiFi](guides/observer-wifi.md).',
            '<!-- [Connect an observer to WiFi](guides/observer-wifi.md) -->'))
        self.assertTrue(any('not linked from index.md' in e for e in self.manual_clean()))

    def test_a_dot_slash_start_page_link_counts(self):
        self.write('docs/user/index.md', INDEX.replace('(guides/observer-wifi.md)', '(./guides/observer-wifi.md)'))
        self.assertFalse(any('not linked from index.md' in e for e in self.manual_clean()))

    # ---- the release gate ------------------------------------------------------------

    def test_the_gate_fails_while_a_draft_waits(self):
        self.assertEqual(pd.main(['--repo-root', str(self.root), '--check']), 1)

    def test_the_gate_passes_with_only_held_drafts(self):
        self.catalog(extra='    hold: waits for the on-air trial the owner asked for\n')
        self.assertEqual(pd.check_drafts(self.root), [])
        self.assertEqual(pd.main(['--repo-root', str(self.root), '--check']), 0)

    def test_the_gate_passes_with_no_drafts(self):
        pd.promote(self.root, 'offband-v1.7.0')
        self.assertEqual(pd.main(['--repo-root', str(self.root), '--check']), 0)

    # ---- promotion -------------------------------------------------------------------

    def test_promotion_publishes_the_draft(self):
        done = pd.promote(self.root, 'offband-v1.7.0-beta1')
        self.assertEqual(done, [('neighbors', 'Report the repeaters an observer hears')])
        self.assertFalse((self.root / 'docs/drafts/neighbors.md').exists())
        self.assertTrue((self.root / 'docs/user/guides/neighbors.md').is_file())
        cat = self.read('docs/user/features.yml')
        self.assertIn('    coverage: documented\n    guide: guides/neighbors.md\n', cat)
        self.assertIn('availability: {channel: beta, verified_releases: [offband-v1.7.0-beta1]}', cat)
        self.assertIn('- [Report the repeaters an observer hears](guides/neighbors.md).',
                      self.read('docs/user/index.md'))
        self.assertEqual(self.manual_clean(), [])
        self.assertEqual(pd.check_drafts(self.root), [])

    def test_a_stable_tag_publishes_as_stable(self):
        pd.promote(self.root, 'offband-v1.7.0')
        self.assertIn('{channel: stable, verified_releases: [offband-v1.7.0]}', self.read('docs/user/features.yml'))

    def test_an_rc_tag_publishes_as_beta(self):
        pd.promote(self.root, 'offband-v1.7.0-rc1')
        self.assertIn('{channel: beta, verified_releases: [offband-v1.7.0-rc1]}', self.read('docs/user/features.yml'))

    def test_the_new_link_follows_the_last_guide_link(self):
        pd.promote(self.root, 'offband-v1.7.0')
        lines = self.read('docs/user/index.md').splitlines()
        at = lines.index('- [Report the repeaters an observer hears](guides/neighbors.md).')
        self.assertEqual(lines[at - 1], '- [Connect an observer to WiFi](guides/observer-wifi.md).')

    def test_promotion_keeps_crlf_line_endings(self):
        self.catalog(nl='\r\n')
        self.write('docs/user/index.md', INDEX, '\r\n')
        pd.promote(self.root, 'offband-v1.7.0')
        for rel in ('docs/user/features.yml', 'docs/user/index.md'):
            text = self.read(rel)
            self.assertNotIn('\n', text.replace('\r\n', ''), rel)

    def test_promotion_leaves_a_held_draft_alone(self):
        self.catalog(extra='    hold: waits for the on-air trial the owner asked for\n')
        self.assertEqual(pd.promote(self.root, 'offband-v1.7.0'), [])
        self.assertTrue((self.root / 'docs/drafts/neighbors.md').is_file())
        self.assertIn('    coverage: planned', self.read('docs/user/features.yml'))

    def test_promotion_refuses_a_bad_tag_and_touches_nothing(self):
        before = self.read('docs/user/features.yml')
        with self.assertRaises(pd.PromoteError):
            pd.promote(self.root, 'v1.7.0')
        self.assertEqual(self.read('docs/user/features.yml'), before)
        self.assertTrue((self.root / 'docs/drafts/neighbors.md').is_file())

    def test_promotion_refuses_a_broken_draft_and_touches_nothing(self):
        text = page('neighbors', 'Report the repeaters an observer hears').replace('## Steps\n', '')
        self.write('docs/drafts/neighbors.md', text)
        before = self.read('docs/user/index.md')
        with self.assertRaises(pd.PromoteError):
            pd.promote(self.root, 'offband-v1.7.0')
        self.assertEqual(self.read('docs/user/index.md'), before)
        self.assertFalse((self.root / 'docs/user/guides/neighbors.md').exists())

    def test_promotion_refuses_a_broken_manual_and_touches_nothing(self):
        (self.root / 'src/wifi.cpp').write_text('nbr status\n', encoding='utf-8')   # evidence gone
        with self.assertRaises(pd.PromoteError):
            pd.promote(self.root, 'offband-v1.7.0')
        self.assertTrue((self.root / 'docs/drafts/neighbors.md').is_file())

    # ---- release notes ---------------------------------------------------------------

    def test_notes_list_the_guides_published_for_the_tag(self):
        pd.promote(self.root, 'offband-v1.7.0')
        text = pd.notes(self.root, 'offband-v1.7.0')
        self.assertIn('## In the user manual', text)
        self.assertIn('[Report the repeaters an observer hears](https://docs.offband.org/firmware/guides/neighbors/)', text)
        self.assertNotIn('observer-wifi', text)

    def test_notes_are_empty_when_nothing_was_published(self):
        self.assertEqual(pd.notes(self.root, 'offband-v1.7.0'), '')


if __name__ == '__main__':
    unittest.main(verbosity=1)
