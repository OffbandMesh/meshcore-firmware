#!/usr/bin/env python3
"""Rig boot smoke test: flash a badge, capture its boot, assert it actually RUNS (#1641).

Everything else in the #1636 regression-guards epic defends source text. This is the only
piece that defends behaviour.

THE STRUCTURAL GAP IT CLOSES. CI proves the badge LINKS. The native suite proves PURE
LOGIC. Nothing anywhere proves the badge RUNS. Whether it boots, sounds, renders or hears
a radio message has no automated coverage at all and is verified only by the owner, by
hand, at a bench. The precedent for why that matters is in this repo: RC32's companion
build linked cleanly for weeks and hung inside crashLogStandardInit() before
display.begin() -- a dark screen that looked exactly like broken display code which had
never executed (#740/#741).

WHAT IT ASSERTS, in descending order of value:

  1. setup:DONE is reached. The single highest-value assertion available: the firmware
     got to the end of setup() without hanging.
  2. The reported sha= matches the ARTIFACT that was flashed. This closes the #1127 stale
     image trap in the same motion -- build, commit, rebuild and firmware.bin still holds
     the OLD sha, so a bench session can spend an hour diagnosing code that is not on the
     device. The sha comes from scripts/firmware_identity.py, which already knows how to
     read it out of the binary, rather than from a second copy of that logic here.
  3. The [BEACON] stages appear, in the order main.cpp declares them, so a hang is
     reported AT the stage it hung rather than as a timeout.
  4. role, interfaces and the radio line match what the env compiled.

  NEGATIVE, and the reason the stage list is read from main.cpp rather than hardcoded: a
  boot that stops mid-sequence FAILS loudly and names the last stage reached.

HONEST LIMITS, because this is easy to oversell.

  It is BENCH AUTOMATION, NOT CI. It needs the rig attached and a badge present, so it
  cannot gate a PR. It gates a FLASH.

  It cannot hear sound, see the display, or judge whether Morse is recognisable. Those
  stay human.

  It does not flash anything by itself and holds no authority to. The flash goes through
  scripts/pio-flash, which has its own approval gate, and that gate refuses without a
  grant exactly as it would if a person typed the command. --check-only skips the flash
  entirely and assesses a capture that already exists.

  It is still worth more than every grep assertion in the epic combined, because it is
  the only one that runs the firmware.

THE PURE HALF IS UNIT-TESTED. parse_boot/declared_stages/assess take and return plain
data, and scripts/test_rig_boot_smoke.py drives them in CI with a healthy capture, a
hang at every single declared stage, a stale sha, a reordered sequence and an empty log.
So the assertions are covered even though the hardware run is not.

Usage:
  # assess a capture that already exists -- no hardware, no flash, no grant needed
  python scripts/rig_boot_smoke.py --check-only C:/Dev/.offband/captures/wroom-sniffer-2/qcc-badge.log \\
      --env QCC_Badge_companion_radio_ble_diag

  # full run: flash through pio-flash (which asks for its own approval), then capture
  python scripts/rig_boot_smoke.py --device qcc-pyramid-1 \\
      --env QCC_Badge_companion_radio_ble_diag --rig-port COM12
"""
import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAIN_CPP = ROOT / "examples" / "companion_radio" / "main.cpp"
CAPTURE_PY = ROOT / "tools" / "diag" / "rc32-boot-740" / "scripts" / "capture.py"
PIO_FLASH = ROOT / "scripts" / "pio-flash.py"

# How long to watch for setup:DONE before calling it a hang. A cold nRF52 boot with
# SafeBoot's battery check reaches DONE in a couple of seconds; the margin is for a
# SafeBoot recheck and a slow display probe.
DEFAULT_BOOT_TIMEOUT_S = 45


# ---------------------------------------------------------------------------------------
# the pure half: parsing and assessment. No I/O, no hardware, no subprocesses.
# ---------------------------------------------------------------------------------------

def declared_stages(main_cpp_text):
    """The boot stages main.cpp declares, in source order.

    READ FROM SOURCE rather than hardcoded, for two reasons. The list changes whenever
    somebody adds a phase, and a hardcoded copy would drift into either false failures or
    silent gaps -- the copies-not-links problem #1638 exists to catch. And many stages are
    conditionally compiled (the ESP32: ones never appear on an nRF52 badge), so the check
    has to be "the stages present are a subsequence of these, in order" rather than
    "exactly these".
    """
    return [m.group(1) for m in
            re.finditer(r'(?:OFFBAND_BEACON|CW_PHASE)\("([^"]*)"\)', main_cpp_text)]


class BootReport(object):
    """What one capture says about one boot. Every field may be None: absence is a result."""

    def __init__(self):
        self.build = None
        self.sha = None
        self.role = None
        self.ifaces = []
        self.caplog = None
        self.mirror = None
        self.freq = None
        self.bw = None
        self.sf = None
        self.cr = None
        self.tx = None
        self.name = None
        self.stages = []

    @property
    def last_stage(self):
        return self.stages[-1] if self.stages else None

    @property
    def reached_done(self):
        return any(s == "setup:DONE" for s in self.stages)

    @property
    def reached_entry(self):
        return any(s.startswith("setup:ENTRY") for s in self.stages)


# The two banner lines, as main.cpp actually emits them. Written out here because the
# formats are printf strings over there and cannot be imported.
#
#   [boot] id build=%s sha=%s role=companion if=%s caplog=%s/%s mirror=%d
#   [boot] radio freq=%s bw=%s sf=%u cr=%u tx=%d name=%s pk=%s mv=%u shutdown_mv=%d ext=%d
RE_ID = re.compile(
    r"\[boot\]\s+id\s+build=(?P<build>\S+)\s+sha=(?P<sha>\S+)\s+role=(?P<role>\S+)"
    r"\s+if=(?P<ifaces>\S+)\s+caplog=(?P<caplog>\S+)\s+mirror=(?P<mirror>\d+)")
RE_RADIO = re.compile(
    r"\[boot\]\s+radio\s+freq=(?P<freq>\S+)\s+bw=(?P<bw>\S+)\s+sf=(?P<sf>\d+)"
    r"\s+cr=(?P<cr>\d+)\s+tx=(?P<tx>-?\d+)\s+name=(?P<name>.*?)\s+pk=")
RE_BEACON = re.compile(r"\[BEACON\]\s*(?P<stage>.+?)\s*$")


def parse_boot(text):
    """A capture -> BootReport. Takes the LAST boot in the file.

    The capture is append-only and survives a port drop, so a long-lived log holds many
    boots. Assessing the first one would report a boot from hours ago as this flash's
    result -- the same class of mistake as diagnosing against the wrong image.
    """
    lines = text.splitlines()

    # Find the last boot's start. ENTRY is the first beacon any boot emits; if no beacon
    # is present at all (a non-diag build, or a mirror that never came up) fall back to
    # the whole text so the banner checks still run and say what they can.
    starts = [i for i, l in enumerate(lines) if "[BEACON]" in l and "setup:ENTRY" in l]
    window = lines[starts[-1]:] if starts else lines

    r = BootReport()
    for line in window:
        m = RE_BEACON.search(line)
        if m:
            r.stages.append(m.group("stage"))
            continue
        m = RE_ID.search(line)
        if m:
            r.build = m.group("build")
            r.sha = m.group("sha")
            r.role = m.group("role")
            r.ifaces = m.group("ifaces").split()
            r.caplog = m.group("caplog")
            r.mirror = int(m.group("mirror"))
            continue
        m = RE_RADIO.search(line)
        if m:
            r.freq = m.group("freq")
            r.bw = m.group("bw")
            r.sf = int(m.group("sf"))
            r.cr = int(m.group("cr"))
            r.tx = int(m.group("tx"))
            r.name = m.group("name")
    return r


def _subsequence_break(observed, declared):
    """The first observed stage that is out of declared order, or None.

    Conditionally-compiled stages are expected to be MISSING, never reordered, so this
    walks the declared list forward and reports the first stage that cannot be reached
    without going backwards.
    """
    at = 0
    for stage in observed:
        try:
            nxt = declared.index(stage, at)
        except ValueError:
            if stage in declared:
                return stage          # appears, but earlier than we already are
            continue                   # unknown stage: not this check's business
        at = nxt + 1
    return None


def assess(report, declared, expected=None):
    """Did this boot pass? Returns (failures, skipped) -- both lists of strings.

    `skipped` exists so this cannot look like it verified more than it did. Every
    expectation that was not supplied is reported by name, because "0 failures" over an
    empty expectation set is the vacuous pass that turned up three times in the source
    guards tonight.
    """
    expected = expected or {}
    bad, skipped = [], []

    # 1. the headline assertion
    if not report.stages:
        bad.append(
            "no [BEACON] lines in the capture at all. Either the boot produced nothing -- "
            "check the rig wiring and that the flashed env is a _diag one, since "
            "OFFBAND_BOOT_BEACON is only set there -- or the capture is of the wrong port.")
    elif not report.reached_entry:
        bad.append("the capture has beacon lines but no setup:ENTRY, so it does not "
                   "contain the start of a boot. Reset the badge and capture again.")
    elif not report.reached_done:
        bad.append(
            "THE BADGE DID NOT FINISH BOOTING. Last stage reached: %r. setup() hung at or "
            "just after that point -- this is the #740/#741 shape, where the firmware "
            "links cleanly and stops partway through setup with a dark screen."
            % report.last_stage)

    out_of_order = _subsequence_break(report.stages, declared)
    if out_of_order is not None:
        bad.append(
            "boot stage %r appears out of the order main.cpp declares. Either setup() was "
            "reordered without this being reconsidered, or the capture has two boots "
            "interleaved on one wire." % out_of_order)

    # 2. the image actually on the device
    if "sha" in expected and expected["sha"]:
        if report.sha is None:
            bad.append("no '[boot] id' line, so the running image cannot be identified. "
                       "The sha check could not run.")
        elif report.sha == "?":
            bad.append("the running image reports sha=?, meaning it was built without "
                       "OFFBAND_GIT_SHA. It cannot be matched to an artifact.")
        else:
            want, got = expected["sha"], report.sha
            # The firmware prints a short sha; compare on the shorter of the two.
            n = min(len(want), len(got))
            if n < 7:
                bad.append("sha values too short to compare: expected %r, got %r"
                           % (want, got))
            elif want[:n].lower() != got[:n].lower():
                bad.append(
                    "THE DEVICE IS NOT RUNNING THE IMAGE THAT WAS FLASHED. Expected sha "
                    "%s, device reports %s. This is the #1127 trap: a build, then a "
                    "commit, then a rebuild leaves the OLD sha in the artifact, and a "
                    "bench session can be spent diagnosing code that is not on the "
                    "device." % (want, got))
    else:
        skipped.append("sha (no artifact identity supplied -- the #1127 stale-image check "
                       "did NOT run)")

    # 3. what the env claims to be
    for key, got, label in (("role", report.role, "role"),
                            ("freq", report.freq, "radio frequency"),
                            ("bw", report.bw, "radio bandwidth"),
                            ("sf", report.sf, "spreading factor"),
                            ("cr", report.cr, "coding rate"),
                            ("tx", report.tx, "TX power")):
        if key not in expected or expected[key] is None:
            if key in ("freq", "bw", "sf", "cr", "tx"):
                skipped.append(
                    "%s (a FIRST-BOOT SEED that loadPrefs overwrites, so a configured "
                    "badge legitimately differs from the env -- pass --fresh-badge only "
                    "if this unit has no stored prefs)" % label)
            else:
                skipped.append("%s (not supplied)" % label)
        elif got is None:
            bad.append("the boot banner carried no %s, so it could not be checked against "
                       "the env's %s." % (label, expected[key]))
        elif str(got) != str(expected[key]):
            bad.append("%s is %s on the device, %s in the env that was flashed."
                       % (label, got, expected[key]))

    if "ifaces" in expected and expected["ifaces"]:
        want = set(expected["ifaces"])
        if not report.ifaces:
            bad.append("the boot banner reported no interfaces; expected %s"
                       % ", ".join(sorted(want)))
        elif not want.issubset(set(report.ifaces)):
            bad.append("interfaces on the device are %s; expected at least %s"
                       % (" ".join(report.ifaces), " ".join(sorted(want))))
    else:
        skipped.append("interfaces (not supplied)")

    return bad, skipped


def expected_from_env(ini_text, env, fresh_badge=False):
    """What a badge built from `env` should report. Unknowns stay None rather than guessed.

    Textual, like test_qcc_badge_env.py and for the same reason: `pio project config`
    resolves `extends`, and here that is actually what we want -- so this walks the
    extends chain itself rather than pretending one section holds everything.

    THE RADIO VALUES ARE NOT EXPECTATIONS unless `fresh_badge` is set, and that is the
    most important thing in this file.

    LORA_FREQ, LORA_BW, LORA_SF, LORA_CR and LORA_TX_POWER are FIRST-BOOT SEEDS. MyMesh
    assigns them to _prefs and loadPrefs() then overwrites them from the stored prefs
    file, which DFU preserves -- that is the #1363 property test_qcc_variant_pins guards,
    and #1637 guards the same thing for the node name. So a badge that has ever been
    configured legitimately reports something else.

    This is not theoretical. The first version of this tool compared them unconditionally.
    Run against a real boot captured from the owner's badge on 2026-10-19, the env
    compiles 919.5/500 and the device reported 910.525/62.5 -- correct behaviour on both
    sides. Had the env-name lookup not also been broken, every real badge would have
    failed its smoke test for doing exactly what it is supposed to do.

    They are only expectations on a badge with no stored prefs: straight out of a factory
    reset, or genuinely first-flashed. That is what fresh_badge asserts, and it is the
    operator's claim to make, not something this can detect.
    """
    sections, name, buf = {}, None, []
    for line in ini_text.splitlines():
        m = re.match(r"\s*\[([^\]]+)\]\s*$", line)
        if m:
            if name is not None:
                sections[name] = "\n".join(buf)
            name, buf = m.group(1), []
        elif name is not None:
            buf.append(line)
    if name is not None:
        sections[name] = "\n".join(buf)

    # Accept both `X` and `env:X`. The CLI form people type is the bare env name, the ini
    # section is `[env:X]`, and the first version looked up only the latter -- so every
    # value came back None and the tool reported five SKIPs that looked deliberate.
    if env not in sections and ("env:" + env) in sections:
        env = "env:" + env

    # Walk `extends` so a value set on the base section is still found. Guarded against a
    # cycle rather than trusted.
    chain, cur, seen = [], env, set()
    while cur in sections and cur not in seen:
        seen.add(cur)
        chain.append(sections[cur])
        m = re.search(r"^\s*extends\s*=\s*(\S+)", sections[cur], re.M)
        cur = m.group(1) if m else None
    body = "\n".join(chain)
    body = "\n".join(l.split(";", 1)[0] for l in body.splitlines())

    def flag(n):
        m = re.search(r"-D\s+%s=('[^']*'|\"[^\"]*\"|\S+)" % re.escape(n), body)
        return m.group(1) if m else None

    def num(n):
        v = flag(n)
        if v is None:
            return None
        try:
            f = float(v)
        except ValueError:
            return None
        return int(f) if f == int(f) else f

    freq = num("LORA_FREQ")
    bw = num("LORA_BW")
    # Compile-time facts. The role is baked in and loadPrefs cannot change it, and the
    # interfaces come from #ifdefs over the build, so both are safe to assert always.
    out = {
        "role": "companion" if "companion" in env else None,
        "ifaces": ["ble"] if "_ble" in env else None,
    }
    # Stored-pref values: expectations ONLY on a badge with no stored prefs. See above.
    seeds = {
        # The firmware prints freq to the kHz and bw to the tenth.
        "freq": ("%.3f" % float(freq)) if freq is not None else None,
        "bw": ("%.1f" % float(bw)) if bw is not None else None,
        "sf": num("LORA_SF"),
        "cr": num("LORA_CR"),
        "tx": num("LORA_TX_POWER"),
    }
    out["seeds"] = seeds
    if fresh_badge:
        out.update(seeds)
    return out


# ---------------------------------------------------------------------------------------
# the hardware half. Shells out; never reimplements a tool it already has.
# ---------------------------------------------------------------------------------------

def artifact_sha(env):
    """The sha baked into the built artifact, via the module that already knows how.

    Not re-derived here. scripts/firmware_identity.py reads the binary markers and falls
    back to git with the source recorded, and a second implementation would be one more
    thing to drift (#1127).
    """
    sys.path.insert(0, str(ROOT / "scripts"))
    try:
        from firmware_identity import get_firmware_identity
    except ImportError as e:
        return None, "cannot import scripts/firmware_identity.py: %s" % e

    build = ROOT / ".pio" / "build" / env
    for candidate in ("firmware.bin", "firmware.hex", "firmware.zip"):
        p = build / candidate
        if p.exists():
            try:
                ident = get_firmware_identity(p, ROOT)
            except Exception as e:                       # noqa: BLE001 - reported, not raised
                return None, "firmware_identity failed on %s: %s" % (p, e)
            sha = ident.get("sha") or ident.get("git_sha") or ident.get("firmware_sha")
            src = ident.get("firmware_identity_source", "?")
            if sha and src != "binary-markers":
                return sha, ("identity came from %s, not the binary. A git fallback cannot "
                             "prove what is IN the artifact, so the #1127 check is weaker "
                             "than it looks here." % src)
            return sha, None
    return None, "no built artifact under .pio/build/%s -- build the env first" % env


def run_capture(port, out_path, seconds, baud=115200):
    """Start capture.py, let the boot happen, stop it. Returns the captured text.

    One continuous capture into a durable file, which is the standing rule for this rig --
    and the file is what gets read, not the port.
    """
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    before = out_path.stat().st_size if out_path.exists() else 0

    proc = subprocess.Popen(
        [sys.executable, str(CAPTURE_PY), "--port", port, "--baud", str(baud),
         "--out", str(out_path), "--no-dtr"],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        deadline = time.time() + seconds
        while time.time() < deadline:
            time.sleep(1.0)
            if out_path.exists():
                text = out_path.read_text(errors="replace")[before:]
                if "setup:DONE" in text:
                    break
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
    return out_path.read_text(errors="replace")[before:] if out_path.exists() else ""


def do_flash(device, env):
    """Stage and execute a flash through pio-flash.

    THE GATE STAYS WHERE IT BELONGS. This does not hold, mint or bypass any approval --
    pio-flash asks for its own, and refuses without a grant exactly as it would for a
    person typing the command. If that refusal happens, this reports it and stops.
    """
    for stage in (["preview", device, "--env", env], ["confirm", device]):
        print("--- pio-flash %s" % " ".join(stage), flush=True)
        rc = subprocess.call([sys.executable, str(PIO_FLASH)] + stage)
        if rc != 0:
            return rc
    return 0


def report(bad, skipped, rep, verbose=True):
    if verbose:
        print("device reports: build=%s sha=%s role=%s if=%s" % (
            rep.build, rep.sha, rep.role, " ".join(rep.ifaces) or "-"))
        print("radio: freq=%s bw=%s sf=%s cr=%s tx=%s name=%s" % (
            rep.freq, rep.bw, rep.sf, rep.cr, rep.tx, rep.name))
        print("stages: %d, last=%r" % (len(rep.stages), rep.last_stage))
    for s in skipped:
        print("SKIP  %s" % s)
    if bad:
        print("\nFAIL: the badge did not pass its boot smoke test")
        for b in bad:
            print("  - %s" % b)
        return 1
    print("\nOK: reached setup:DONE, %d stages in order, image identity matched"
          % len(rep.stages))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check-only", metavar="CAPTURE",
                    help="assess an existing capture file; no hardware, no flash")
    ap.add_argument("--device", help="registered device name, e.g. qcc-pyramid-1")
    ap.add_argument("--env", required=True, help="the pio env that was (or will be) flashed")
    ap.add_argument("--rig-port", help="the sniffer rig's port, e.g. COM12")
    ap.add_argument("--out", help="capture file (default: the rig's durable log)")
    ap.add_argument("--no-flash", action="store_true",
                    help="capture and assess without flashing (badge must be reset by hand)")
    ap.add_argument("--timeout", type=int, default=DEFAULT_BOOT_TIMEOUT_S)
    ap.add_argument("--expect-sha", help="override the artifact sha to match against")
    ap.add_argument("--fresh-badge", action="store_true",
                    help="this unit has NO stored prefs (factory reset or genuinely first "
                         "flash), so the env's LORA_* first-boot seeds are what it should "
                         "report. Without this the radio line is reported, not asserted, "
                         "because loadPrefs legitimately overwrites those values.")
    args = ap.parse_args(argv)

    declared = declared_stages(MAIN_CPP.read_text())
    ini_path = ROOT / "variants" / "qcc_badge" / "platformio.ini"
    expected = expected_from_env(ini_path.read_text() if ini_path.exists() else "",
                                 args.env, fresh_badge=args.fresh_badge)

    if args.expect_sha:
        expected["sha"] = args.expect_sha
    else:
        sha, why = artifact_sha(args.env)
        if why:
            print("NOTE: %s" % why)
        expected["sha"] = sha

    if args.check_only:
        text = Path(args.check_only).read_text(errors="replace")
    else:
        if not args.rig_port:
            ap.error("--rig-port is required unless --check-only is given")
        if not args.no_flash:
            if not args.device:
                ap.error("--device is required to flash; use --no-flash to skip")
            rc = do_flash(args.device, args.env)
            if rc != 0:
                print("\nFAIL: the flash did not complete (pio-flash exit %d). Nothing was "
                      "assessed -- a boot smoke test over an unknown image proves nothing."
                      % rc)
                return rc
        out = args.out or ("C:/Dev/.offband/captures/wroom-sniffer-2/qcc-badge.log"
                           if os.name == "nt" else
                           os.path.expanduser("~/.offband/captures/wroom-sniffer-2/qcc-badge.log"))
        text = run_capture(args.rig_port, out, args.timeout)

    rep = parse_boot(text)
    bad, skipped = assess(rep, declared, expected)
    return report(bad, skipped, rep)


if __name__ == "__main__":
    sys.exit(main())
