#!/usr/bin/env python3
"""Exercise documentation-impact policy against real Git commit diffs."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import check_docs_impact as policy


class ImpactDiffTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git('init', '-b', 'firmware-base')
        self.git('config', 'user.name', 'Docs Policy Test')
        self.git('config', 'user.email', 'docs-policy@example.com')
        self.put('src/example.cpp', '// original behavior\n')
        self.put('docs/user/guides/example.md', '# Original guide\n')
        self.base = self.commit()

    def git(self, *args):
        p = subprocess.run(['git', '-C', str(self.root), *args], capture_output=True, text=True)
        if p.returncode:
            raise AssertionError(p.stderr)
        return p.stdout.strip()

    def put(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def record(self, **overrides):
        decision = {'schema_version': 1, 'issue': 1234, 'impact': 'none',
                    'reason': 'Moves a private helper; commands, replies, defaults and persistence are unchanged.'}
        decision.update(overrides)
        self.put('docs/changes/1234-fixture.yml', json.dumps(decision))

    def commit(self):
        self.git('add', '.')
        self.git('commit', '-m', 'Policy fixture')
        return self.git('rev-parse', 'HEAD')

    def check(self, **kwargs):
        return policy.check_change(self.root, self.base, self.git('rev-parse', 'HEAD'), **kwargs)

    def test_behavior_change_without_record_fails(self):
        self.put('src/example.cpp', '// changed behavior\n')
        self.commit()
        self.assertTrue(self.check())

    def test_specific_no_impact_record_passes(self):
        self.put('src/example.cpp', '// moved helper\n')
        self.record()
        self.commit()
        self.assertEqual(self.check(), [])

    def test_changed_guide_passes_but_unchanged_guide_does_not(self):
        self.put('src/example.cpp', '// changed default\n')
        self.record(impact='updated', guides=['docs/user/guides/example.md'])
        self.commit()
        self.assertTrue(self.check())
        self.put('docs/user/guides/example.md', '# Updated guide\nThe new default is documented.\n')
        self.commit()
        self.assertEqual(self.check(), [])

    def test_existing_unchanged_record_does_not_satisfy_new_code_change(self):
        self.record()
        self.base = self.commit()
        self.put('src/example.cpp', '// another change\n')
        self.commit()
        self.assertTrue(self.check())

    def test_empty_no_impact_reason_fails(self):
        self.put('src/example.cpp', '// changed\n')
        self.record(reason='N/A')
        self.commit()
        self.assertTrue(self.check())

    def test_deleted_record_is_not_a_decision(self):
        self.record()
        self.base = self.commit()
        (self.root / 'docs/changes/1234-fixture.yml').unlink()
        self.put('src/example.cpp', '// changed\n')
        self.commit()
        self.assertTrue(self.check())

    def test_deleted_guide_cannot_satisfy_updated_decision(self):
        self.put('src/example.cpp', '// changed\n')
        self.record(impact='updated', guides=['docs/user/guides/example.md'])
        (self.root / 'docs/user/guides/example.md').unlink()
        self.commit()
        self.assertTrue(self.check())

    def test_code_rename_and_board_configuration_require_decisions(self):
        self.git('mv', 'src/example.cpp', 'src/renamed.cpp')
        self.put('platformio.ini', '[env:test]\n')
        self.commit()
        self.assertTrue(self.check())

    def test_architecture_specific_runtime_changes_require_decisions(self):
        self.put('arch/esp32/AsyncElegantOTA/src/AsyncElegantOTA.cpp', '// Changed OTA behavior\n')
        self.commit()
        self.assertTrue(self.check())

    def test_doc_only_change_does_not_require_record(self):
        self.put('docs/user/guides/example.md', '# Corrected guide\n')
        self.commit()
        self.assertEqual(self.check(), [])

    def test_records_are_validated_even_without_code_changes(self):
        self.record(schema_version=True)
        self.commit()
        self.assertTrue(self.check())

    def test_typo_unknown_fields_and_issue_mismatch_fail(self):
        for overrides in ({'impact': 'updtaed'}, {'issue': 2222}, {'guidse': ['docs/user/index.md']}):
            with self.subTest(overrides=overrides):
                self.record(**overrides)
                self.commit()
                self.assertTrue(self.check())

    def test_duplicate_record_keys_fail(self):
        self.put('docs/changes/1234-fixture.yml', 'schema_version: 1\nissue: 1234\nissue: 2222\nimpact: none\n')
        self.commit()
        self.assertTrue(self.check())

    def test_missing_history_and_malformed_refs_fail_visibly(self):
        for base in ('0' * 40, 'not-a-sha', '--output=bad'):
            with self.subTest(base=base), self.assertRaises(policy.ImpactError):
                policy.check_change(self.root, base, self.git('rev-parse', 'HEAD'))

    def test_checked_out_tree_must_match_requested_head(self):
        self.put('docs/user/guides/example.md', '# Changed guide\n')
        self.commit()
        with self.assertRaises(policy.ImpactError):
            policy.check_change(self.root, self.base, self.base)

    def test_pr_uses_merge_base_not_unrelated_new_base_changes(self):
        self.git('checkout', '-b', 'feature')
        self.put('docs/user/guides/example.md', '# Corrected guide\n')
        head = self.commit()
        self.git('checkout', 'firmware-base')
        self.put('src/another.cpp', '// separate base change\n')
        newer_base = self.commit()
        self.git('checkout', 'feature')
        self.assertEqual(policy.check_change(self.root, newer_base, head, mode='pr'), [])
        self.assertTrue(policy.check_change(self.root, newer_base, head, mode='push'))


if __name__ == '__main__':
    unittest.main()
