# Offband #2277: PlatformIO post-build step. Fails the build when the real
# firmware.bin is above the size budget for its app slot (scripts/size_budget.json).
#
# PlatformIO's own "program size" check leaves out .eh_frame, so an image could
# overflow its slot while the build said it fit. This checks the file an OTA
# actually writes. If it can't measure (no image, no slot size), it fails: a
# missing measurement is never a pass.

import os
import sys

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "scripts"))  # noqa: F821
import size_gate_core as core  # noqa: E402

board = env.BoardConfig()  # noqa: F821
image = env.subst("$BUILD_DIR/${PROGNAME}.bin")  # noqa: F821


def size_gate_action(source, target, env):
    mcu = board.get("build.mcu", "")
    try:
        slot = int(board.get("upload.maximum_size", 0) or 0)
        size = os.path.getsize(image) if os.path.isfile(image) else 0
        fail, report = core.load_budget(mcu)
        ok, line = core.evaluate(size, slot, fail, report)
    except (core.GateError, ValueError, OSError) as exc:
        print("size-gate: FAIL  %s: %s" % (env.subst("$PIOENV"), exc))
        env.Exit(1)
        return
    print("size-gate: %s  [%s, %s]" % (line, env.subst("$PIOENV"), mcu))
    if not ok:
        env.Exit(1)


# An always-built alias that depends on the image, with buildprog depending on it.
# A post-action on the .bin, or on buildprog, only runs when that target is rebuilt,
# so a build with nothing changed (CI restores cached build folders) skipped the
# gate, and a tightened budget would have gone unchecked. Measured 2026-11-25 for
# both. AlwaysBuild runs the check on every build; the dependency on the image means
# the .bin exists by the time it runs.
gate = env.Alias("offband_size_gate", image, size_gate_action)  # noqa: F821
env.AlwaysBuild(gate)  # noqa: F821
env.Depends("buildprog", gate)  # noqa: F821
