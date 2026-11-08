#!/usr/bin/env python3
"""Print the firmware-docs dispatch that tells offband-site to rebuild the manual.

The payload is offband-site's version-1 contract (its scripts/build_docs.py
validate_event): exactly these four fields, or the site refuses the run.
"""
import json
import re
import sys

REPOSITORY = 'OffbandMesh/meshcore-firmware'
REF = 'refs/heads/firmware-base'
SHA = re.compile(r'[0-9a-f]{40}')


def payload(source_sha):
    if not isinstance(source_sha, str) or not SHA.fullmatch(source_sha):
        raise ValueError('source SHA must be a full 40-character lowercase commit SHA')
    return {'event_type': 'firmware-docs',
            'client_payload': {'schema_version': 1, 'source_repository': REPOSITORY,
                               'source_ref': REF, 'source_sha': source_sha}}


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: docs_site_dispatch.py <source-sha>')
    try:
        print(json.dumps(payload(sys.argv[1])))
    except ValueError as exc:
        sys.exit(f'ERROR: {exc}')
