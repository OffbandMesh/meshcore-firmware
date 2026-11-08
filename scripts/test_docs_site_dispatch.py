#!/usr/bin/env python3
"""The docs-site dispatch matches offband-site's contract and fires only for docs on firmware-base."""
from pathlib import Path
import unittest
import yaml

import docs_site_dispatch as dispatch

ROOT = Path(__file__).resolve().parent.parent
SHA = '0123456789abcdef0123456789abcdef01234567'


class PayloadTests(unittest.TestCase):
    def test_payload_is_exactly_the_site_contract(self):
        self.assertEqual(dispatch.payload(SHA), {
            'event_type': 'firmware-docs',
            'client_payload': {'schema_version': 1,
                               'source_repository': 'OffbandMesh/meshcore-firmware',
                               'source_ref': 'refs/heads/firmware-base', 'source_sha': SHA}})

    def test_negative_a_malformed_sha_is_refused(self):
        for bad in ('', SHA[:39], SHA.upper(), SHA + '0', 'g' * 40, None):
            with self.subTest(sha=bad), self.assertRaises(ValueError):
                dispatch.payload(bad)


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.workflow = yaml.safe_load((ROOT / '.github/workflows/docs-notify.yml').read_text())
        self.triggers = self.workflow[True]  # PyYAML reads the `on:` key as True

    def test_fires_on_a_docs_push_to_firmware_base(self):
        self.assertEqual(self.triggers['push'], {'branches': ['firmware-base'], 'paths': ['docs/user/**']})
        self.assertEqual(self.workflow['jobs']['notify']['if'], "github.ref == 'refs/heads/firmware-base'")

    def test_negative_only_a_push_fires(self):
        self.assertEqual(set(self.triggers), {'push'})
        for event in ('pull_request', 'pull_request_target', 'workflow_run', 'workflow_dispatch'):
            with self.subTest(event=event):
                self.assertNotIn(event, self.triggers)

    def test_sends_the_contract_with_the_firmware_pat(self):
        step = self.workflow['jobs']['notify']['steps'][-1]
        self.assertEqual(step['env']['GH_TOKEN'], '${{ secrets.PROJECT_PAT }}')
        self.assertIn('scripts/docs_site_dispatch.py', step['run'])
        self.assertIn('repos/OffbandMesh/offband-site/dispatches', step['run'])
        self.assertEqual(self.workflow['permissions'], {'contents': 'read'})


if __name__ == '__main__':
    unittest.main()
