#!/usr/bin/env python
"""Offband #2274: every PlatformIO install in CI names one exact version.

Why this exists
---------------
CI used to run `pip install --upgrade platformio`, so a PlatformIO release could
change every build without a change in this repo, and a red build could appear on
a PR that touched nothing. Pinning one version makes builds reproducible and turns
a PlatformIO change into a reviewed commit. The pin is easy to lose: a new workflow
copies an old install line, or someone "fixes" a failure with `--upgrade`.

What it checks
--------------
Every `pip install` line that installs `platformio`, in `.github/workflows/*.yml`
and `.github/actions/**/action.yml`, must name `platformio==X.Y.Z`, and all of them
must name the same version. `README.md` must name that version too, because it is
what developers install by hand.

Anything it can't confirm is a failure: no install found at all, or a README that
names no version.

Exit: 0 clean, 1 on any finding.
"""

import glob
import os
import re
import sys

INSTALL = re.compile(r"\bpip3?\s+install\b.*\bplatformio\b", re.I)
PINNED = re.compile(r"\bplatformio==(\d+\.\d+\.\d+)\b")
README_PIN = re.compile(r"platformio==(\d+\.\d+\.\d+)")


def workflow_files(root):
    files = glob.glob(os.path.join(root, ".github", "workflows", "*.yml"))
    files += glob.glob(os.path.join(root, ".github", "workflows", "*.yaml"))
    files += glob.glob(os.path.join(root, ".github", "actions", "**", "action.yml"), recursive=True)
    return sorted(files)


def check_tree(root):
    """Return (findings, version) for the tree at root."""
    findings, versions = [], {}
    for path in workflow_files(root):
        rel = os.path.relpath(path, root).replace("\\", "/")
        with open(path, encoding="utf-8") as fh:
            for n, line in enumerate(fh, 1):
                if line.lstrip().startswith("#") or not INSTALL.search(line):
                    continue
                m = PINNED.search(line)
                if not m:
                    findings.append("%s:%d: PlatformIO installed without an exact version: %s"
                                    % (rel, n, line.strip()))
                else:
                    versions.setdefault(m.group(1), []).append("%s:%d" % (rel, n))
    if not versions and not findings:
        findings.append("no PlatformIO install found in .github/, so nothing is pinned")
    if len(versions) > 1:
        detail = "; ".join("%s at %s" % (v, ", ".join(w)) for v, w in sorted(versions.items()))
        findings.append("PlatformIO is pinned to more than one version: " + detail)
    version = next(iter(versions)) if len(versions) == 1 else None

    readme = os.path.join(root, "README.md")
    text = open(readme, encoding="utf-8").read() if os.path.exists(readme) else ""
    named = set(README_PIN.findall(text))
    if not named:
        findings.append("README.md names no PlatformIO version (expected platformio==%s)" % (version or "X.Y.Z"))
    elif version and named != {version}:
        findings.append("README.md names PlatformIO %s, CI pins %s" % (", ".join(sorted(named)), version))
    return findings, version


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    findings, version = check_tree(root)
    if findings:
        print("FAIL: PlatformIO is not pinned to one version.\n")
        for f in findings:
            print("  " + f)
        print("\nInstall it as `pip install platformio==<version>`, the same version everywhere.")
        return 1
    print("OK  every PlatformIO install pins %s, and README.md names it" % version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
