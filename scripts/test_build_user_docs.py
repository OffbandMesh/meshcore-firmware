#!/usr/bin/env python3
"""Build the real manual in isolation and verify output, links, search and canaries."""
import json
from html.parser import HTMLParser
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from check_user_docs import check_build_config, check_catalog, check_content

ROOT = Path(__file__).resolve().parent.parent
CANARY = 'PRIVATE-INTERNAL-CANARY-NOT-FOR-PUBLICATION-1795'


class CodeBlocks(HTMLParser):
    def __init__(self):
        super().__init__()
        self.blocks = []
        self.current = None

    def handle_starttag(self, tag, attrs):
        if tag == 'pre':
            self.current = []

    def handle_data(self, data):
        if self.current is not None:
            self.current.append(data)

    def handle_endtag(self, tag):
        if tag == 'pre' and self.current is not None:
            self.blocks.append(''.join(self.current).strip())
            self.current = None


class RenderedManualTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.fixture = Path(cls.tmp.name)
        cls.output = cls.fixture / 'site-user'
        errors = (check_catalog(ROOT, ROOT / 'docs/user/features.yml', ROOT / 'docs/user') +
                  check_content(ROOT / 'docs/user') + check_build_config(ROOT, ROOT / 'mkdocs.user.yml'))
        if errors:
            raise AssertionError('Manual input validation failed: ' + '; '.join(errors))
        shutil.copytree(ROOT / 'docs/user', cls.fixture / 'docs/user')
        shutil.copy2(ROOT / 'mkdocs.user.yml', cls.fixture / 'mkdocs.user.yml')
        for name in ('handoffs/private.md', 'plans/private.md', 'llm-consultations/private.md', 'HARDWARE.local.md'):
            path = cls.fixture / 'docs' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(CANARY, encoding='utf-8')
        p = subprocess.run([sys.executable, '-m', 'mkdocs', 'build', '--strict',
                            '--config-file', str(cls.fixture / 'mkdocs.user.yml')],
                           cwd=cls.fixture, capture_output=True, text=True, encoding='utf-8', errors='replace')
        if p.returncode:
            raise AssertionError('Strict manual build failed:\n' + p.stdout + p.stderr)

    def test_manual_home_and_wifi_guide_render(self):
        home = (self.output / 'index.html').read_text(encoding='utf-8')
        guide = (self.output / 'guides/observer-wifi/index.html').read_text(encoding='utf-8')
        self.assertIn('Offband firmware manual', home)
        self.assertIn('Connect an observer to WiFi', guide)
        self.assertIn('StaConnected', guide)
        self.assertIn('hardware validation', guide)

    def test_commands_in_numbered_steps_render_without_language_labels(self):
        parser = CodeBlocks()
        parser.feed((self.output / 'guides/observer-wifi/index.html').read_text(encoding='utf-8'))
        expected = ['wifi status', 'set wifi.ssid <your-network-name>',
                    'set wifi.pwd <your-password>', 'wifi enable', 'wifi status']
        self.assertEqual(parser.blocks[:5], expected)

    def test_edit_links_target_firmware_user_sources(self):
        guide = (self.output / 'guides/observer-wifi/index.html').read_text(encoding='utf-8')
        self.assertIn('https://github.com/OffbandMesh/meshcore-firmware/edit/firmware-base/docs/user/guides/observer-wifi.md', guide)

    def test_search_finds_the_wifi_journey(self):
        search = json.loads((self.output / 'search/search_index.json').read_text(encoding='utf-8'))
        self.assertTrue(any('StaConnected' in d.get('text', '') and
                            'guides/observer-wifi/' in d.get('location', '') for d in search['docs']))

    def test_manual_does_not_fetch_fonts_from_a_third_party(self):
        home = (self.output / 'index.html').read_text(encoding='utf-8')
        self.assertNotIn('fonts.googleapis.com', home)
        self.assertNotIn('fonts.gstatic.com', home)

    def test_internal_sources_and_catalog_are_not_published(self):
        for path in self.output.rglob('*'):
            if path.is_file():
                self.assertNotEqual(path.name, 'features.yml')
                self.assertNotIn(CANARY.encode(), path.read_bytes(), str(path))
        self.assertFalse((self.output / 'handoffs').exists())
        self.assertFalse((self.output / 'plans').exists())
        self.assertFalse((self.output / 'llm-consultations').exists())
        self.assertFalse((self.output / 'HARDWARE.local.md').exists())


if __name__ == '__main__':
    unittest.main()
