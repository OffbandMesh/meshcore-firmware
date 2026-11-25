#!/usr/bin/env python
"""Offband #2273: this project's PlatformIO package store is keyed by platform version.

Why this exists
---------------
Every pioarduino platform installs its precompiled IDF libraries into one folder,
`framework-arduinoespressif32-libs`. When two pioarduino versions build on one host,
they replace that folder under each other and builds fail with false compile errors.
So `[platformio]` points `packages_dir` and `platforms_dir` at a store named for the
pioarduino version, `<core_dir>/offband-<tag>/...`, and nothing else on the host
writes there.

That only holds while the tag names the version actually in use. A platform pin that
changes without the tag puts the new version's libraries into the old store, which
is the collision this store exists to prevent. Nothing else would catch it: the build
still works, until another project or branch with the old pin builds on the same host.

What it checks, on the resolved config (`pio project config --json-output`)
--------------------------------------------------------------------------
1. `[offband] pio_store_tag` is set.
2. `[platformio] packages_dir` and `platforms_dir` both sit under `offband-<tag>`.
3. Every pioarduino platform URL in every env names that same version.
4. At least one env uses pioarduino, so the tag is about something.

Anything it can't confirm is a failure: a config it can't read, a missing section,
a URL whose version it can't parse.

Exit: 0 clean, 1 on any finding, 2 if the config can't be read.
"""

import json
import re
import subprocess
import sys

PIOARDUINO = re.compile(
    r"github\.com/pioarduino/platform-espressif32/releases/download/([^/\s]+)/platform-espressif32\.zip")


def flatten(value):
    """Resolved options come back as either a string or a list of strings."""
    if value is None:
        return ""
    if isinstance(value, (list, tuple)):
        return "\n".join(str(v) for v in value)
    return str(value)


def check_config(config):
    """config: the parsed `pio project config --json-output` list of [section, options].
    Returns a list of human-readable findings; empty means clean."""
    sections = {name: dict(opts) for name, opts in config}
    findings = []

    tag = flatten(sections.get("offband", {}).get("pio_store_tag")).strip()
    if not tag:
        return ["[offband] pio_store_tag is not set, so the package store has no version key"]
    store = "offband-" + tag

    plat = sections.get("platformio", {})
    for key in ("packages_dir", "platforms_dir"):
        path = flatten(plat.get(key)).replace("\\", "/")
        if store not in path.split("/"):
            findings.append("[platformio] %s is %r, not under %s" % (key, path or "(unset)", store))

    pioarduino_envs = 0
    for name, opts in sorted(sections.items()):
        if not name.startswith("env:"):
            continue
        platform = flatten(opts.get("platform"))
        if "pioarduino/platform-espressif32" not in platform:
            continue
        pioarduino_envs += 1
        m = PIOARDUINO.search(platform)
        if not m:
            findings.append("%s: pioarduino platform %r has no release version this check can read"
                            % (name[4:], platform))
        elif m.group(1) != tag:
            findings.append("%s: pioarduino %s, but the store is tagged %s" % (name[4:], m.group(1), tag))

    if pioarduino_envs == 0:
        findings.append("no env uses a pioarduino platform, so the store tag %s names nothing" % tag)
    return findings


def main():
    try:
        raw = subprocess.check_output(["pio", "project", "config", "--json-output"],
                                      stderr=subprocess.STDOUT)
        config = json.loads(raw.decode("utf-8", "replace"))
    except (subprocess.CalledProcessError, OSError, ValueError) as exc:
        out = getattr(exc, "output", b"")
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        print("ERROR: could not read PlatformIO config:\n%s" % (out or exc))
        return 2

    findings = check_config(config)
    if findings:
        print("FAIL: the package store tag does not match the platform in use.\n")
        for f in findings:
            print("  " + f)
        print("\nSet [offband] pio_store_tag in platformio.ini to the pioarduino version every env pins.")
        return 1
    tag = flatten(dict(dict(config).get("offband", [])).get("pio_store_tag"))
    print("OK  package store tagged %s, matching every pioarduino env" % tag)
    return 0


if __name__ == "__main__":
    sys.exit(main())
