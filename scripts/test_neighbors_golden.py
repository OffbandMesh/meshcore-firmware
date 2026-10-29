#!/usr/bin/env python3
"""The golden /neighbors report, read the way CoreScope reads it (#1399).

test/test_neighbors_json/golden_neighbors.json is the exact text the firmware's
buildNeighborsJson must produce (the native test test_neighbors_json asserts that,
byte for byte). This script parses the same file with a real JSON parser and applies
the rules of CoreScope's ingestor, cmd/ingestor/main.go handleNeighborsReport:

  - timestamp and origin_id are strings; self.scopes is a string, always trusted;
  - only neighbors whose status is "responded" have their scopes recorded, keyed by
    the lowercased pubkey; any other status is skipped, not treated as "cleared".

It also pins the shape the plan of record fixes for the report, and proves the
checks reject a broken report.
"""
import copy
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent
GOLDEN = ROOT / 'test' / 'test_neighbors_json' / 'golden_neighbors.json'
STATUSES = {'responded', 'timeout', 'send_failed'}
ENTRY_KEYS = {'pubkey', 'snr', 'rssi', 'heard_secs_ago', 'scopes', 'status'}


def normalize_report_ts(raw):
    """CoreScope db.go normalizeReportTS: RFC 3339 (with a zone) or nothing.

    An empty result makes CoreScope skip its newest-report-wins check and always
    write, so an old report could overwrite a newer one.
    """
    if not isinstance(raw, str) or not raw:
        return ''
    try:
        t = datetime.fromisoformat(raw.replace('Z', '+00:00'))
    except ValueError:
        return ''
    if t.tzinfo is None or 'T' not in raw:
        return ''   # Go's RFC3339 layout requires the zone and the T
    return t.astimezone(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')


def ingest(report, topic_observer_id='topic-observer'):
    """CoreScope handleNeighborsReport, reduced to what it records."""
    recorded = {}
    origin = report.get('origin_id')
    if not isinstance(origin, str) or not origin:
        origin = topic_observer_id   # Go falls back to the topic's observer id
    if isinstance(origin, str) and origin and isinstance(report.get('self'), dict):
        sc = report['self'].get('scopes')
        if isinstance(sc, str):
            recorded[origin.lower()] = sc
    for n in report.get('neighbors') or []:
        if not isinstance(n, dict) or n.get('status') != 'responded':
            continue
        key = n.get('pubkey')
        if isinstance(key, str) and key:
            sc = n.get('scopes')
            recorded[key.lower()] = sc if isinstance(sc, str) else ''
    return recorded


def contract_errors(report):
    errors = []
    for field in ('timestamp', 'origin', 'origin_id'):
        if not isinstance(report.get(field), str):
            errors.append(f'{field} is not a string')
    if normalize_report_ts(report.get('timestamp')) == '':
        errors.append('timestamp is not RFC 3339 with a zone (CoreScope would drop it)')
    self_ = report.get('self')
    if not isinstance(self_, dict) or not isinstance(self_.get('scopes'), str) \
            or not isinstance(self_.get('default_scope'), str):
        errors.append('self is not {scopes, default_scope} strings')
    for field in ('total_neighbors', 'queried_neighbors'):
        if not isinstance(report.get(field), int) or isinstance(report.get(field), bool):
            errors.append(f'{field} is not an integer')
    if not isinstance(report.get('truncated'), bool):
        errors.append('truncated is not a boolean')
    neighbors = report.get('neighbors')
    if not isinstance(neighbors, list):
        return errors + ['neighbors is not a list']
    for i, n in enumerate(neighbors):
        if not isinstance(n, dict) or set(n) != ENTRY_KEYS:
            errors.append(f'neighbor {i}: keys are not {sorted(ENTRY_KEYS)}')
            continue
        key = n['pubkey']
        if not (isinstance(key, str) and len(key) == 64 and key == key.upper()
                and all(c in '0123456789ABCDEF' for c in key)):
            errors.append(f'neighbor {i}: pubkey is not 64 uppercase hex')
        if n['status'] not in STATUSES:
            errors.append(f'neighbor {i}: status {n["status"]!r} is not a contract status')
        if not isinstance(n['snr'], (int, float)) or isinstance(n['snr'], bool):
            errors.append(f'neighbor {i}: snr is not a number')
        for field in ('rssi', 'heard_secs_ago'):
            v = n[field]
            if v is not None and (not isinstance(v, int) or isinstance(v, bool)):
                errors.append(f'neighbor {i}: {field} is not an integer or null')
        if not isinstance(n['scopes'], str):
            errors.append(f'neighbor {i}: scopes is not a string')
    if isinstance(report.get('queried_neighbors'), int) and report.get('truncated') is False \
            and len(neighbors) != report['queried_neighbors']:
        errors.append('untruncated report lists a different number of neighbors than it queried')
    return errors


class GoldenNeighbors(unittest.TestCase):
    def setUp(self):
        self.report = json.loads(GOLDEN.read_text(encoding='utf-8'))

    def test_golden_meets_the_contract(self):
        self.assertEqual(contract_errors(self.report), [])

    def test_corescope_records_only_responded_neighbors_and_self(self):
        recorded = ingest(self.report)
        self.assertEqual(recorded, {
            '0011aabb': 'hao',
            '101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f': '*,hao',
        })

    def test_a_timeout_carries_no_scopes(self):
        for n in self.report['neighbors']:
            if n['status'] != 'responded':
                self.assertEqual(n['scopes'], '', n['pubkey'])

    # --- the checks catch what they are for ---------------------------------
    def broken(self, mutate):
        r = copy.deepcopy(self.report)
        mutate(r)
        return contract_errors(r)

    def test_rejects_lowercase_pubkey(self):
        self.assertTrue(self.broken(lambda r: r['neighbors'][0].update(pubkey=r['neighbors'][0]['pubkey'].lower())))

    def test_rejects_unknown_status(self):
        self.assertTrue(self.broken(lambda r: r['neighbors'][0].update(status='pending')))

    def test_rejects_missing_or_extra_entry_fields(self):
        self.assertTrue(self.broken(lambda r: r['neighbors'][0].pop('rssi')))
        self.assertTrue(self.broken(lambda r: r['neighbors'][0].update(extra=1)))

    def test_rejects_string_numbers_and_bad_self(self):
        self.assertTrue(self.broken(lambda r: r['neighbors'][0].update(snr='2.0')))
        self.assertTrue(self.broken(lambda r: r['neighbors'][0].update(heard_secs_ago='100')))
        self.assertTrue(self.broken(lambda r: r.update(self={'scopes': 1, 'default_scope': ''})))
        self.assertTrue(self.broken(lambda r: r.update(truncated='false')))

    def test_rejects_a_zoneless_timestamp(self):
        # the /status format: valid ISO 8601, but no zone, so CoreScope drops it
        self.assertTrue(self.broken(lambda r: r.update(timestamp='2026-10-28T09:00:00.000000')))
        self.assertTrue(self.broken(lambda r: r.update(timestamp='')))

    def test_ingest_falls_back_to_the_topic_id(self):
        r = copy.deepcopy(self.report)
        r['origin_id'] = ''
        self.assertIn('topic-observer', ingest(r))

    def test_rejects_a_silently_short_list(self):
        self.assertTrue(self.broken(lambda r: r['neighbors'].pop()))

    def test_ingest_skips_non_responded(self):
        r = copy.deepcopy(self.report)
        for n in r['neighbors']:
            n['status'] = 'timeout'
        self.assertEqual(ingest(r), {'0011aabb': 'hao'})


if __name__ == '__main__':
    result = unittest.main(exit=False, verbosity=1).result
    sys.exit(0 if result.wasSuccessful() else 1)
