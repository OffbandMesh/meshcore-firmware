"""Unit tests for the rig boot smoke test's assessment logic (#1641).

The hardware RUN cannot be in CI -- it needs a rig and a badge. The ASSERTIONS can be,
and this is them: a synthetic capture in, a verdict out.

That split is the point. If the assessment logic were only ever exercised by running it
against a real badge, it would be the one piece of this epic with no coverage, which is
precisely the shape the epic exists to complain about. A boot smoke test whose own pass
condition is untested can report OK for the wrong reason.

The cases that matter most:

  - a hang at EVERY declared stage, not one chosen example. 24 stages, each truncated in
    turn, each required to fail and to name that stage. A smoke test that detects a hang
    at ENTRY but not at post:ui_task.begin is worse than none, because it is trusted.
  - a stale sha, which is the #1127 trap and the thing that cost a bench session.
  - a vacuous pass: an empty expectation set must be REPORTED as skipped, not counted.

Run: python scripts/test_rig_boot_smoke.py
  or python -m pytest scripts/test_rig_boot_smoke.py -q
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rig_boot_smoke import (  # noqa: E402
    MAIN_CPP, ROOT, assess, declared_stages, expected_from_env, parse_boot)

DECLARED = declared_stages(MAIN_CPP.read_text())

ID_LINE = ("2026-10-19T04:12:00Z [boot] id build=offband-v1.5.0-beta7-112-g7a312de "
           "sha=7a312de role=companion if=ble caplog=on/DEBUG mirror=1")
RADIO_LINE = ("2026-10-19T04:12:00Z [boot] radio freq=919.500 bw=500.0 sf=7 cr=5 tx=22 "
              "name=Bench-Badge-A pk=A1B2C3D4 mv=4012 shutdown_mv=3400 ext=0")

# The stages an nRF52 badge actually emits: the declared list minus the ESP32-only ones,
# which are conditionally compiled and legitimately absent.
NRF_STAGES = [s for s in DECLARED if not s.startswith("ESP32:")]

EXPECTED = {"sha": "7a312de", "role": "companion", "freq": "919.500", "bw": "500.0",
            "sf": 7, "cr": 5, "tx": 22, "ifaces": ["ble"]}

# The same, plus the node name. Kept separate because the name is a STORED pref: the env
# cannot supply it, so a caller that has not claimed one must see it reported as skipped.
EXPECTED_FULL = dict(EXPECTED, name="Bench-Badge-A")


def capture(stages, id_line=ID_LINE, radio_line=RADIO_LINE, prefix=""):
    out = [prefix] if prefix else []
    for s in stages:
        out.append("2026-10-19T04:12:00Z [BEACON] %s" % s)
        # The banners land mid-boot, after the mirror is up.
        if s == "setup:post crashLogStandardInit":
            if id_line:
                out.append(id_line)
            if radio_line:
                out.append(radio_line)
    return "\n".join(out) + "\n"


# --- the source-derived stage list ------------------------------------------------------

def test_the_declared_stage_list_is_real():
    # Non-vacuity. If the regex stops matching, every ordering and hang check below
    # degenerates to "no stages declared, nothing to compare" and passes silently.
    assert len(DECLARED) >= 15, "expected main.cpp to declare many boot stages, got %d" % len(DECLARED)
    assert DECLARED[0].startswith("setup:ENTRY"), DECLARED[:3]
    assert DECLARED[-1] == "setup:DONE", DECLARED[-3:]
    assert len(set(DECLARED)) == len(DECLARED), "a duplicated stage makes ordering ambiguous"


# --- positive ---------------------------------------------------------------------------

def test_a_healthy_nrf52_boot_passes():
    bad, skipped = assess(parse_boot(capture(NRF_STAGES)), DECLARED, EXPECTED_FULL)
    assert not bad, bad
    assert not skipped, "nothing should be skipped when every expectation is supplied: %s" % skipped


def test_the_banner_is_parsed_field_by_field():
    r = parse_boot(capture(NRF_STAGES))
    assert r.build == "offband-v1.5.0-beta7-112-g7a312de"
    assert r.sha == "7a312de"
    assert r.role == "companion"
    assert r.ifaces == ["ble"]
    assert r.caplog == "on/DEBUG"
    assert r.mirror == 1
    assert (r.freq, r.bw, r.sf, r.cr, r.tx) == ("919.500", "500.0", 7, 5, 22)
    assert r.name == "Bench-Badge-A"
    assert r.reached_done and r.reached_entry
    assert r.last_stage == "setup:DONE"


def test_missing_esp32_stages_are_not_a_failure():
    # They are conditionally compiled. Absent is correct on this board; out of ORDER is not.
    assert any(s.startswith("ESP32:") for s in DECLARED), "sanity: some stages are ESP32-only"
    bad, _ = assess(parse_boot(capture(NRF_STAGES)), DECLARED, EXPECTED)
    assert not bad, bad


# --- negative: a hang at every single stage ---------------------------------------------

def test_a_hang_at_every_declared_stage_is_caught_and_named():
    # The whole value of this tool is reporting WHERE it stopped. Checked at every stage
    # rather than at one chosen example, because a smoke test that catches a hang at
    # ENTRY but not at post:ui_task.begin is worse than none -- it would be trusted.
    for i in range(1, len(NRF_STAGES)):          # 1.. so there is always a last stage
        truncated = NRF_STAGES[:i]
        if truncated[-1] == "setup:DONE":
            continue
        bad, _ = assess(parse_boot(capture(truncated)), DECLARED, EXPECTED)
        assert any("DID NOT FINISH BOOTING" in b for b in bad), \
            "a hang after %r was not reported: %s" % (truncated[-1], bad)
        assert any(repr(truncated[-1]) in b for b in bad), \
            "the failure must name the last stage reached (%r); got %s" % (truncated[-1], bad)


def test_a_boot_that_never_started_is_caught():
    bad, _ = assess(parse_boot(""), DECLARED, EXPECTED)
    assert any("no [BEACON] lines" in b for b in bad), bad
    assert any("_diag" in b for b in bad), \
        "the message should point at the likely cause -- a non-diag build has no beacons"


def test_a_capture_without_the_start_of_a_boot_is_caught():
    # Mid-boot fragment: beacons, but no ENTRY. Assessing this as a healthy boot because
    # it happens to contain setup:DONE from a PREVIOUS boot is the mistake.
    text = "\n".join("2026-10-19T04:12:00Z [BEACON] %s" % s for s in NRF_STAGES[3:6]) + "\n"
    bad, _ = assess(parse_boot(text), DECLARED, EXPECTED)
    assert any("no setup:ENTRY" in b for b in bad), bad


def test_stages_out_of_order_are_caught():
    scrambled = list(NRF_STAGES)
    scrambled[2], scrambled[6] = scrambled[6], scrambled[2]
    bad, _ = assess(parse_boot(capture(scrambled)), DECLARED, EXPECTED)
    assert any("out of the order" in b for b in bad), bad


# --- negative: the image on the device --------------------------------------------------

def test_a_stale_image_is_caught():
    # #1127, the one that costs a bench session: the artifact was rebuilt after a commit
    # and still carries the old sha, so the device is not running what was just built.
    stale = capture(NRF_STAGES, id_line=ID_LINE.replace("sha=7a312de", "sha=deadbee"))
    bad, _ = assess(parse_boot(stale), DECLARED, EXPECTED)
    assert any("NOT RUNNING THE IMAGE THAT WAS FLASHED" in b for b in bad), bad
    assert any("deadbee" in b and "7a312de" in b for b in bad), \
        "the failure must show both shas; got %s" % bad


def test_a_build_with_no_git_sha_is_caught():
    bad, _ = assess(parse_boot(capture(NRF_STAGES, id_line=ID_LINE.replace("sha=7a312de", "sha=?"))),
                    DECLARED, EXPECTED)
    assert any("without OFFBAND_GIT_SHA" in b for b in bad), bad


def test_a_missing_id_line_is_caught_rather_than_skipped():
    bad, _ = assess(parse_boot(capture(NRF_STAGES, id_line=None)), DECLARED, EXPECTED)
    assert any("cannot be identified" in b for b in bad), bad


def test_a_short_sha_comparison_is_refused_rather_than_guessed():
    bad, _ = assess(parse_boot(capture(NRF_STAGES)), DECLARED, dict(EXPECTED, sha="7a3"))
    assert any("too short to compare" in b for b in bad), bad


def test_sha_comparison_is_case_insensitive_and_length_tolerant():
    # The firmware prints a short sha; the artifact may hold a long one. Matching on the
    # shorter of the two is deliberate, and must not become a false MISmatch.
    bad, _ = assess(parse_boot(capture(NRF_STAGES)), DECLARED,
                    dict(EXPECTED, sha="7A312DEF0123456789"))
    assert not bad, bad


# --- negative: the env's own claims -----------------------------------------------------

def test_a_radio_mismatch_is_caught_field_by_field():
    for key, wrong, label in (("freq", "910.525", "radio frequency"),
                              ("bw", "62.5", "radio bandwidth"),
                              ("sf", 9, "spreading factor"),
                              ("cr", 8, "coding rate"),
                              ("tx", 14, "TX power"),
                              ("role", "repeater", "role")):
        bad, _ = assess(parse_boot(capture(NRF_STAGES)), DECLARED, dict(EXPECTED, **{key: wrong}))
        assert any(label in b for b in bad), "%s mismatch not reported: %s" % (label, bad)


def test_a_missing_interface_is_caught():
    bad, _ = assess(parse_boot(capture(NRF_STAGES, id_line=ID_LINE.replace("if=ble", "if=uart"))),
                    DECLARED, EXPECTED)
    assert any("interfaces on the device" in b for b in bad), bad


def test_a_missing_radio_line_is_caught_not_ignored():
    bad, _ = assess(parse_boot(capture(NRF_STAGES, radio_line=None)), DECLARED, EXPECTED)
    assert any("carried no radio frequency" in b for b in bad), bad


# --- the vacuous pass, which is the failure mode of every check in this epic ------------

def test_an_empty_expectation_set_is_reported_not_counted_as_a_pass():
    bad, skipped = assess(parse_boot(capture(NRF_STAGES)), DECLARED, {})
    assert not bad, bad
    joined = " ".join(skipped)
    assert "sha" in joined and "#1127" in joined, \
        "a run with no artifact identity must say the stale-image check did not run: %s" % skipped
    for label in ("role", "radio frequency", "spreading factor", "interfaces"):
        assert label in joined, "%s should be reported as skipped; got %s" % (label, skipped)


# --- only the last boot in the log counts -----------------------------------------------

def test_an_older_healthy_boot_does_not_excuse_a_newer_hang():
    # The capture is append-only and survives a port drop, so a durable log holds many
    # boots. Reading the first one would report a boot from hours ago as this flash's
    # result -- the same class of mistake as diagnosing against the wrong image.
    healthy = capture(NRF_STAGES)
    hung = capture(NRF_STAGES[:5])
    bad, _ = assess(parse_boot(healthy + hung), DECLARED, EXPECTED)
    assert any("DID NOT FINISH BOOTING" in b for b in bad), \
        "the LAST boot in the log is the one being assessed; got %s" % bad
    assert any(repr(NRF_STAGES[4]) in b for b in bad), bad


def test_a_newer_healthy_boot_is_not_condemned_by_an_older_hang():
    bad, _ = assess(parse_boot(capture(NRF_STAGES[:5]) + capture(NRF_STAGES)), DECLARED, EXPECTED)
    assert not bad, bad


# --- the env reader ---------------------------------------------------------------------

def test_expected_values_come_from_the_badge_env():
    ini = (ROOT / "variants" / "qcc_badge" / "platformio.ini").read_text()
    exp = expected_from_env(ini, "env:QCC_Badge_companion_radio_ble_diag")
    # Read through the extends chain (_diag -> shipping env), which is the point of
    # walking it rather than pretending one section holds everything.
    assert exp["seeds"]["sf"] == 7, exp
    assert exp["seeds"]["cr"] == 5, exp
    assert exp["seeds"]["freq"] == "919.500", exp
    assert exp["seeds"]["bw"] == "500.0", exp
    assert exp["seeds"]["tx"] == 22, exp
    assert exp["role"] == "companion", exp
    assert exp["ifaces"] == ["ble"], exp


def test_the_bare_env_name_resolves_as_well_as_the_section_name():
    # The form people type is the bare env; the ini section is [env:X]. The first version
    # looked up only the latter, so every value came back None and the tool printed five
    # SKIPs that looked deliberate -- and that masked the seeds bug below.
    ini = (ROOT / "variants" / "qcc_badge" / "platformio.ini").read_text()
    bare = expected_from_env(ini, "QCC_Badge_companion_radio_ble_diag")
    prefixed = expected_from_env(ini, "env:QCC_Badge_companion_radio_ble_diag")
    assert bare == prefixed, "the bare env name must resolve to the same thing"
    assert bare["seeds"]["sf"] == 7, bare


def test_the_radio_seeds_are_not_expectations_unless_the_badge_is_fresh():
    # THE ONE THAT MATTERS. LORA_* are first-boot seeds; loadPrefs overwrites them from
    # the stored prefs file, which DFU preserves. So a configured badge legitimately
    # reports something else, and asserting the env's values would fail every real unit
    # for behaving correctly.
    ini = (ROOT / "variants" / "qcc_badge" / "platformio.ini").read_text()
    default = expected_from_env(ini, "QCC_Badge_companion_radio_ble_diag")
    for key in ("freq", "bw", "sf", "cr", "tx"):
        assert key not in default, \
            "%s must NOT be a default expectation -- it is a first-boot seed" % key

    fresh = expected_from_env(ini, "QCC_Badge_companion_radio_ble_diag", fresh_badge=True)
    for key in ("freq", "bw", "sf", "cr", "tx"):
        assert fresh[key] == fresh["seeds"][key], key


def test_a_configured_badge_on_different_radio_settings_still_passes():
    # Captured from the owner's badge on 2026-10-19: the env compiles 919.5/500 and the
    # device reported 910.525/62.5, because it has stored prefs. Both sides correct.
    #
    # The node name here is NEUTRAL ON PURPOSE. The real capture carried the owner's
    # badge name, which ends in the CTF trigger mark -- committing that to this PUBLIC
    # repo would publish the first condition of the hidden gate. The structure is what
    # this test needs; the name is not.
    configured = RADIO_LINE.replace("freq=919.500", "freq=910.525").replace("bw=500.0", "bw=62.5")
    text = capture(NRF_STAGES, radio_line=configured)
    ini = (ROOT / "variants" / "qcc_badge" / "platformio.ini").read_text()
    exp = expected_from_env(ini, "QCC_Badge_companion_radio_ble_diag")
    exp["sha"] = "7a312de"
    bad, skipped = assess(parse_boot(text), DECLARED, exp)
    assert not bad, "a configured badge must not fail for keeping its stored radio: %s" % bad
    assert any("FIRST-BOOT SEED" in s for s in skipped), \
        "the skip must explain WHY the radio was not asserted; got %s" % skipped


def test_a_fresh_badge_on_the_wrong_radio_settings_fails():
    # The other half: when the operator says the unit has no stored prefs, the seeds ARE
    # what it should report, and a mismatch means the first-boot defaults did not take.
    ini = (ROOT / "variants" / "qcc_badge" / "platformio.ini").read_text()
    exp = expected_from_env(ini, "QCC_Badge_companion_radio_ble_diag", fresh_badge=True)
    exp["sha"] = "7a312de"
    wrong = RADIO_LINE.replace("freq=919.500", "freq=910.525")
    bad, _ = assess(parse_boot(capture(NRF_STAGES, radio_line=wrong)), DECLARED, exp)
    assert any("radio frequency" in b for b in bad), bad


# --- the node name: the hole adversarial review found ----------------------------------
#
# The first version PARSED the name, printed it, and never checked it. The reviewer's
# point was the sharpest of the night: a regression in the #1637 ordering invariant
# renames every configured badge on flash, and the smoke test would still have passed.
# #1637 guards that in source; this is the only thing that can catch it on hardware.
#
# Names in these fixtures are neutral. The owner's real badge name ends in the CTF
# trigger mark and this repo is public.

def test_a_renamed_badge_is_caught_when_the_name_was_asserted():
    bad, _ = assess(parse_boot(capture(NRF_STAGES)), DECLARED,
                    dict(EXPECTED, name="SomeOtherName"))
    assert any("THE BADGE WAS RENAMED" in b for b in bad), bad
    assert any("#1637" in b for b in bad), "the failure must point at the invariant: %s" % bad


def test_a_badge_that_kept_its_name_passes():
    bad, _ = assess(parse_boot(capture(NRF_STAGES)), DECLARED,
                    dict(EXPECTED, name="Bench-Badge-A"))
    assert not bad, bad


def test_the_node_name_is_reported_as_skipped_when_not_asserted():
    # It is a stored pref, so it cannot be asserted against the env by default -- but the
    # skip has to SAY that, and say how to assert it, or the gap is invisible again.
    _, skipped = assess(parse_boot(capture(NRF_STAGES)), DECLARED, EXPECTED)
    joined = " ".join(skipped)
    assert "node name" in joined, skipped
    assert "--expect-name" in joined and "#1637" in joined, \
        "the skip must name the flag and the invariant it measures: %s" % skipped


def test_a_fresh_badge_must_be_named_from_its_ble_mac():
    ini = (ROOT / "variants" / "qcc_badge" / "platformio.ini").read_text()
    exp = expected_from_env(ini, "QCC_Badge_companion_radio_ble_diag", fresh_badge=True)
    exp["sha"] = "7a312de"
    assert exp["name_prefix"] == "QCC", exp
    assert exp["name_hex_chars"] == 5, exp

    # The shape a fresh badge should produce: prefix + 5 hex.
    ok = RADIO_LINE.replace("name=Bench-Badge-A", "name=QCC1A2B3")
    bad, _ = assess(parse_boot(capture(NRF_STAGES, radio_line=ok)), DECLARED, exp)
    assert not bad, bad

    # The failure that matters: the FICR read failed and it fell through to pubkey hex.
    fell_through = RADIO_LINE.replace("name=Bench-Badge-A", "name=51205463")
    bad, _ = assess(parse_boot(capture(NRF_STAGES, radio_line=fell_through)), DECLARED, exp)
    assert any("pubkey-hex default" in b for b in bad), bad

    # And a configured name is not a fresh one.
    bad, _ = assess(parse_boot(capture(NRF_STAGES)), DECLARED, exp)
    assert any("not actually fresh" in b for b in bad), bad


def test_a_missing_node_name_is_caught_when_it_was_asserted():
    for exp in (dict(EXPECTED, name="Bench-Badge-A"),
                dict(EXPECTED, name_prefix="QCC", name_hex_chars=5)):
        bad, _ = assess(parse_boot(capture(NRF_STAGES, radio_line=None)), DECLARED, exp)
        assert any("no node name" in b for b in bad), bad


# --- the three bugs the first real hardware run exposed (2026-10-19) -------------------
#
# None of these were reachable by unit test alone: the first needed a real artifact on
# disk, the second needed a real check to have been skipped, and the third needed a real
# badge name. Recorded as cases so they cannot come back.

def test_the_artifact_sha_key_is_the_one_firmware_identity_actually_returns():
    # THE BUG: artifact_sha() looked for "sha"/"git_sha"/"firmware_sha". The key is
    # `offband_git_sha`. So it returned None on a perfectly good artifact and the #1127
    # stale-image check -- the most valuable assertion in the file -- SKIPPED rather than
    # ran. Nothing failed. Only the reported skip made it visible on the bench.
    #
    # Asserted against firmware_identity's real contract rather than a fixture, so a
    # rename over there breaks this test instead of silently disarming the check.
    import inspect

    import firmware_identity
    src = inspect.getsource(firmware_identity)
    assert "offband_git_sha" in src, \
        "firmware_identity no longer produces offband_git_sha; artifact_sha() must follow"

    import rig_boot_smoke
    fn = inspect.getsource(rig_boot_smoke.artifact_sha)
    assert "offband_git_sha" in fn, \
        "artifact_sha() must read offband_git_sha, or the #1127 check silently skips"
    assert "cannot run" in fn, \
        "a missing sha key must be REPORTED, not returned as a quiet None"


def test_a_skipped_sha_check_is_not_summarised_as_a_pass():
    # THE BUG: the OK line said "image identity matched" unconditionally, so a run whose
    # sha check never ran read as a full pass while the SKIP line above said otherwise.
    import inspect

    import rig_boot_smoke
    fn = inspect.getsource(rig_boot_smoke.report)
    assert "NOT CHECKED" in fn, \
        "the summary must not claim image identity when the sha check was skipped"


def test_a_node_name_that_could_carry_the_trigger_mark_is_never_printed():
    # THE BUG: a rename failure printed the full node name. A badge name is the FIRST
    # condition of the hidden trigger, and this output is exactly what gets pasted into
    # an issue when a check fails. On the first real run it put an owner badge's name,
    # mark and all, into a transcript.
    from rig_boot_smoke import safe_name

    # Pure ASCII stays readable -- it cannot hold the mark and it is useful at a bench.
    assert safe_name("Bench-Badge-A") == "'Bench-Badge-A'"
    assert safe_name(None) == "<none>"

    # BOTH forms a marked name arrives in, which is the whole lesson here. The second one
    # is what a real capture actually produces -- the log escapes non-printables, so the
    # name reaches the tool as printable ASCII full of literal backslash-x sequences. The
    # first version of safe_name() tested only encode("ascii"), so that form sailed
    # straight through and the name was printed in full on a real run. The original unit
    # test used real UTF-8 and passed happily, which is why it is now both.
    for marked in ("Strycher-QCC1\U0001F6F0️",
                   r"Strycher-QCC1\xF0\x9F\x9B\xB0\xEF\xB8\x8F"):
        out = safe_name(marked)
        assert "withheld" in out and "sha256" in out, (marked, out)
        for leak in ("\U0001F6F0", "QCC1", "Strycher", "F09F9BB0", "xF0"):
            assert leak not in out, "leaked %r via %r -> %s" % (leak, marked, out)
        # Distinguishable without being disclosed, and stable.
        assert safe_name(marked) == safe_name(marked)
        assert safe_name(marked) != safe_name(marked + "2")


def test_every_path_that_prints_a_name_goes_through_the_redactor():
    # Belt and braces: grep the module for a bare name print. The redactor is useless if
    # one of three call sites forgets it, and that is exactly what happened -- the rename
    # message was fixed first and two others still printed the name raw.
    import inspect

    import rig_boot_smoke
    src = inspect.getsource(rig_boot_smoke)
    offenders = []
    for line in src.splitlines():
        s = line.strip()
        if "safe_name(" in s or not ("report.name" in s or "rep.name" in s):
            continue
        # Only FORMATTED uses disclose. Comparisons (`!=`, `re.match`, `is None`) read the
        # name without emitting it, and must not be flagged -- the first version of this
        # check failed on `re.match(pat, report.name)`, which prints nothing.
        if "%" in s:
            offenders.append(s)
    assert not offenders, "name reaches a format string without safe_name(): %s" % offenders


def test_an_unknown_beacon_stage_is_tolerated():
    # Real captures carry stages this list does not declare: APP:CTOR from the static
    # ctor in BootBeacon.h, and post:display.begin(OK) whose text is built at runtime.
    # Neither is in main.cpp's literal list, and neither is an error.
    stages = ["APP:CTOR -- earliest static ctor; bootloader + platform startup COMPLETED"]
    for s in NRF_STAGES:
        stages.append(s)
        if s == "setup:before display.begin":
            stages.append("post:display.begin(OK)")
    bad, _ = assess(parse_boot(capture(stages)), DECLARED, EXPECTED)
    assert not bad, "unknown stages must not fail the boot: %s" % bad


def test_the_env_reader_survives_a_cycle_and_an_unknown_env():
    assert expected_from_env("[a]\nextends = b\n\n[b]\nextends = a\n", "a")["seeds"]["sf"] is None
    assert expected_from_env("", "env:does_not_exist")["seeds"]["sf"] is None


def test_the_env_reader_ignores_a_commented_out_flag():
    exp = expected_from_env("[env:x]\nbuild_flags =\n  ; -D LORA_SF=12\n  -D LORA_SF=7\n", "env:x")
    assert exp["seeds"]["sf"] == 7, exp


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("PASS %s" % name)
            except AssertionError as e:
                failures += 1
                print("FAIL %s: %s" % (name, e))
    print("\n%d failure(s)" % failures)
    sys.exit(1 if failures else 0)
