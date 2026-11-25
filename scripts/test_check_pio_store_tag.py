#!/usr/bin/env python
"""Self-test for check_pio_store_tag.py (#2273). Positive and negative cases."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_pio_store_tag as chk  # noqa: E402

FAILURES = []
URL = "https://github.com/pioarduino/platform-espressif32/releases/download/%s/platform-espressif32.zip"


def expect(condition, label):
    print("  %s  %s" % ("pass" if condition else "FAIL", label))
    if not condition:
        FAILURES.append(label)


def config(tag="55.03.311", packages="C:/pio/offband-55.03.311/packages",
           platforms="C:/pio/offband-55.03.311/platforms", envs=None):
    """A minimal resolved config, shaped like `pio project config --json-output`."""
    cfg = [["platformio", [["packages_dir", packages], ["platforms_dir", platforms]]]]
    if tag is not None:
        cfg.append(["offband", [["pio_store_tag", tag]]])
    for name, platform in (envs if envs is not None else {"c6_x": URL % "55.03.311"}).items():
        cfg.append(["env:" + name, [["platform", platform]]])
    return cfg


def main():
    print("clean configs that must pass")
    expect(chk.check_config(config()) == [], "matching tag, store and platform")
    expect(chk.check_config(config(envs={"s3_x": URL % "55.03.311", "nrf_x": "nordicnrf52",
                                          "native": "native"})) == [],
           "non-pioarduino envs are ignored")
    expect(chk.check_config(config(packages="C:\\pio\\offband-55.03.311\\packages")) == [],
           "a Windows path to the store passes")
    expect(chk.check_config(config(tag="53.03.13-1", packages="/home/u/.platformio/offband-53.03.13-1/packages",
                                   platforms="/home/u/.platformio/offband-53.03.13-1/platforms",
                                   envs={"c6_x": URL % "53.03.13-1",
                                         "s3_x": "platformio/espressif32@6.11.0"})) == [],
           "today's tree shape: pioarduino 53 beside espressif32 6.11.0")

    print("defects that must fail")
    f = chk.check_config(config(envs={"c6_x": URL % "55.03.313"}))
    expect(len(f) == 1 and "55.03.313" in f[0], "a platform pin that moved without the tag (got %r)" % f)
    f = chk.check_config(config(envs={"a": URL % "55.03.311", "b": URL % "53.03.13-1"}))
    expect(len(f) == 1 and "b:" in f[0], "two pioarduino versions in one store (got %r)" % f)
    f = chk.check_config(config(tag=None))
    expect(len(f) == 1 and "pio_store_tag is not set" in f[0], "no tag at all (got %r)" % f)
    f = chk.check_config(config(packages="C:/pio/packages"))
    expect(len(f) == 1 and "packages_dir" in f[0], "packages_dir outside the tagged store (got %r)" % f)
    f = chk.check_config(config(platforms=""))
    expect(len(f) == 1 and "platforms_dir" in f[0], "platforms_dir unset (got %r)" % f)
    f = chk.check_config(config(packages="C:/pio/offband-55.03.311x/packages"))
    expect(len(f) == 1 and "packages_dir" in f[0], "a store whose name only starts with the tag (got %r)" % f)
    f = chk.check_config(config(envs={"c6_x": "https://github.com/pioarduino/platform-espressif32.git#develop"}))
    expect(any("no release version" in x for x in f), "a pioarduino URL with no readable version (got %r)" % f)
    f = chk.check_config(config(envs={"nrf_x": "nordicnrf52"}))
    expect(len(f) == 1 and "names nothing" in f[0], "a tag with no pioarduino env (got %r)" % f)

    if FAILURES:
        print("\n%d failure(s)." % len(FAILURES))
        return 1
    print("\nall cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
