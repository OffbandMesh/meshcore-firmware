#!/usr/bin/env python3
"""Tests for the CLI surface sweep (#852).

Pure. No serial port, no device, no `pio-flash` invocation.

The sweep is split into two testable halves with the human-approved
`pio-flash send-batch` run in between:

    enumerate_keys(source)  ->  commands file  ->  [operator runs pio-flash]
                                                   ->  analyse(rows)  ->  findings

That split is deliberate. The sweep does NOT invoke `pio-flash` itself: the
flash discipline is one human approval per RUN of that tool, and a wrapper that
shells out to it hides the run behind another layer. The operator issues the
run and sees it.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cli_sweep


# ------------------------------------------------- enumeration, from source ---
# Hand-listing is exactly how `radio`, `agc.reset.interval` and `pwrmgt.bootmv`
# spent ten months returning empty replies while everyone assumed the surface
# was covered. The key list must come from the source every run.

SAMPLE = '''
void CommonCLI::handleGetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  const char* config = &command[4];
  if (isKey(config, "dutycycle")) {
    sprintf(reply, "> %d", dc);
  } else if (isKey(config, "radio")) {
    sprintf(reply, "> %s", freq);
  } else if (memcmp(config, "extra.sf", 8) == 0) {
    sprintf(reply, "> %d", sf);
  } else if (sender_timestamp == 0 && isKey(config, "prv.key")) {
    sprintf(reply, "> %s", key);
  } else {
    sprintf(reply, "??: %s", config);
  }
}

void CommonCLI::handleSetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  const char* config = &command[4];
  if (memcmp(config, "freq ", 5) == 0) {
    strcpy(reply, "ok");
  } else if (isKey(config, "name")) {
    strcpy(reply, "ok");
  }
}
'''


def test_get_keys_are_found_regardless_of_matcher():
    gets, _ = cli_sweep.enumerate_keys(SAMPLE)
    assert "dutycycle" in gets
    assert "radio" in gets
    assert "extra.sf" in gets, "memcmp arms must be enumerated too, not just isKey"


def test_set_keys_are_separated_from_get_keys():
    gets, sets = cli_sweep.enumerate_keys(SAMPLE)
    assert "name" in sets
    assert "freq" in sets, "trailing space in the matcher must be stripped"
    assert "name" not in gets


def test_serial_only_keys_are_still_enumerated():
    """`get prv.key` is gated on sender_timestamp == 0, which is the serial
    path -- exactly the path this sweep uses. It must not be skipped."""
    gets, _ = cli_sweep.enumerate_keys(SAMPLE)
    assert "prv.key" in gets


def test_the_fallback_marker_is_not_mistaken_for_a_key():
    gets, _ = cli_sweep.enumerate_keys(SAMPLE)
    assert not any("??" in k for k in gets), gets


def test_commands_are_emitted_one_per_line_with_the_get_verb():
    cmds = cli_sweep.build_commands(["radio", "name"])
    assert cmds == ["get radio", "get name"], cmds


# --------------------------------------------------------------- analysis ---

def row(cmd, reply="> x", answered=True, empty=False, elapsed=0.05, **kw):
    # round_trip_s mirrors elapsed_s here because a real row carries BOTH, and
    # slow-detection reads round_trip_s -- elapsed_s includes the tool's own
    # idle wait (#896) and would flag every command on a loose --idle-time.
    r = {"command": cmd, "reply": reply, "answered": answered,
         "empty_reply": empty, "elapsed_s": elapsed, "round_trip_s": elapsed,
         "first_byte_s": None, "reply_bytes": len(reply)}
    r.update(kw)
    return r


def test_a_healthy_reply_produces_no_finding():
    f = cli_sweep.analyse([row("get radio", "> 910.525,62.5,7,5")])
    assert f == [], f


def test_an_empty_reply_is_the_headline_finding():
    """#764's exact shape: the handler matched the key and wrote nothing."""
    f = cli_sweep.analyse([row("get radio", reply="", answered=True, empty=True)])
    assert len(f) == 1 and f[0]["kind"] == "empty-reply", f


def test_a_timeout_is_reported_separately_from_an_empty_reply():
    """Different failures. One is a broken handler, the other is a device that
    never answered -- conflating them destroys the diagnosis."""
    f = cli_sweep.analyse([row("get radio", reply="", answered=False, elapsed=3.0)])
    assert len(f) == 1 and f[0]["kind"] == "no-reply", f


def test_an_unknown_key_fallback_is_a_finding_for_a_REAL_key():
    """A key enumerated from source that comes back `??:` means the dispatch
    does not match what the source says -- which is #657's shape."""
    f = cli_sweep.analyse([row("get radio", reply="??: radio")])
    assert len(f) == 1 and f[0]["kind"] == "unknown-key", f


def test_a_junk_suffix_probe_EXPECTS_the_fallback():
    """`get radioX` must reach `??:`. Getting a real value back is the
    prefix-match defect (#764's `cad`/`extra.sf` half), so the expectation is
    inverted for probes."""
    ok = cli_sweep.analyse([row("get radioX", reply="??: radioX", probe=True)])
    assert ok == [], ok
    bad = cli_sweep.analyse([row("get cadfoo", reply="> off", probe=True)])
    assert len(bad) == 1 and bad[0]["kind"] == "prefix-match", bad


def test_non_printable_bytes_in_a_reply_are_a_finding():
    """The reported symptom was mojibake -- uninitialised stack on the wire."""
    f = cli_sweep.analyse([row("get radio", reply="> \x01\x02")])
    assert len(f) == 1 and f[0]["kind"] == "non-printable", f


def test_a_redacted_secret_reply_is_healthy_not_a_finding():
    """Redaction must not read as a defect. #849 redacts at source, so the
    sweep sees the marker and has to treat it as a normal answer."""
    f = cli_sweep.analyse([row("get prv.key", reply="> <redacted:cli-reply:64>")])
    assert f == [], f


def test_a_slow_key_is_surfaced_with_its_time():
    f = cli_sweep.analyse([row("get radio", elapsed=4.5)], slow_s=1.0)
    assert len(f) == 1 and f[0]["kind"] == "slow", f
    assert "4.5" in str(f[0]["detail"]), f


def test_every_finding_names_its_command():
    f = cli_sweep.analyse([row("get agc.reset.interval", reply="", empty=True)])
    assert f[0]["command"] == "get agc.reset.interval", f


# ===========================================================================
# Gemini review follow-ups (#852)
# ===========================================================================

def test_unbalanced_braces_fail_closed_not_open():
    """If brace matching runs off the end of the file, returning the tail would
    make the sweep enumerate keys from unrelated functions -- inventing keys.
    Fail closed instead."""
    broken = '''
void CommonCLI::handleGetCmd(uint32_t t, char* command, char* reply) {
  if (isKey(config, "radio")) { sprintf(reply, "x");
// missing closing brace

void SomethingElse::other() {
  if (isKey(config, "not_a_cli_key")) { }
}
'''
    gets, _ = cli_sweep.enumerate_keys(broken)
    assert "not_a_cli_key" not in gets, gets


def test_zero_keys_is_an_error_not_an_empty_sweep():
    """A renamed function or a moved dispatch chain would yield no keys. Emitting
    an empty command list would report a clean sweep having tested nothing --
    the exact silent no-op this whole effort exists to eliminate."""
    try:
        cli_sweep.enumerate_keys("void Unrelated::thing() { }", require_keys=True)
    except cli_sweep.NoKeysFound:
        pass
    else:
        assert False, "expected NoKeysFound"


def test_probes_are_deduplicated_by_root():
    """One probe per prefix root, not one per key. 47 keys would otherwise
    double the run to 94 commands for no extra coverage: `radio` and
    `radio.rxgain` share a root, so probing both tests the same arm twice."""
    probes = cli_sweep.build_probes(["radio", "radio.rxgain", "radio.fem.rxgain", "cad"])
    roots = {p.split()[1].rstrip("X").split(".")[0] for p in probes}
    assert roots == {"radio", "cad"}, probes
    assert len(probes) == 2, probes


def test_a_fallback_reply_is_not_redacted_so_unknown_key_stays_visible():
    """Guards a real coupling. If a `??:` reply were ever redacted, an
    unknown-key failure would read as a healthy answer and the sweep would
    report a broken key as working. Verified against the shared redactor rather
    than assumed."""
    import log_redact
    for s in ("??: prv.key", "??: guest.password", "??: bridge.secret"):
        assert log_redact.redact_line(s) == s, (s, log_redact.redact_line(s))


def test_a_redacted_fallback_would_still_be_caught_if_it_ever_happened():
    """Belt and braces for the above: even if redaction one day swallowed a
    fallback, the analyser should not silently pass it."""
    f = cli_sweep.analyse([{
        "command": "get prv.key", "reply": "??: <redacted:cli-reply:8>",
        "answered": True, "empty_reply": False, "elapsed_s": 0.05, "reply_bytes": 26,
    }])
    assert any(x["kind"] == "unknown-key" for x in f), f



def test_a_noisy_capture_voids_its_own_timings_loudly():
    """Adversarial review, HIGH: last-byte advances on ANY input, so an
    unrelated log line is charged to whatever command was in flight. On ST-P
    that produced a 0.17-17.2s spread unrelated to command latency. A run whose
    timings are void must SAY so, not suppress silently."""
    # TWO rows, not one: interleaving from a chattering device is systemic
    # (79/79 on ST-P). A single log-carrying reply is a legitimate answer and
    # must not void a run -- see the companion test below.
    noisy = [row("get radio", reply="[472110][E][Pref.cpp:483] x  -> > 0.5",
                 elapsed=9.0),
             row("get tx", reply="[472118][E][Pref.cpp:483] y  -> > 22",
                 elapsed=7.0)]
    f = cli_sweep.analyse(noisy, slow_s=1.0)
    kinds = {x["kind"] for x in f}
    assert "timings-void" in kinds, f
    assert "slow" not in kinds, "slow-detection must be disabled on a noisy run"


def test_a_quiet_capture_still_reports_slow_commands():
    """The guard must not disable slow-detection on a clean run."""
    r = row("get radio", reply="> 910.525", elapsed=4.5)
    r["round_trip_s"] = 4.5
    f = cli_sweep.analyse([r], slow_s=1.0)
    assert {x["kind"] for x in f} == {"slow"}, f



def test_ONE_reply_containing_a_log_line_does_not_void_the_run():
    """MEDIUM, second-pass review. `get last_error` can legitimately return a
    stored log line. Voiding a whole run's timings on a single such reply would
    suppress real slow findings. Interleaving is SYSTEMIC, never exactly one."""
    r = row("get last_error", reply="> [12345][E][radio.cpp:55] TX failed", elapsed=4.5)
    f = cli_sweep.analyse([r], slow_s=1.0)
    kinds = {x["kind"] for x in f}
    assert "timings-void" not in kinds, f
    assert "slow" in kinds, "a real slow finding was suppressed"


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as e:
                failures += 1
                print(f"FAIL {name}: {e}")
            except Exception as e:
                failures += 1
                print(f"ERROR {name}: {type(e).__name__}: {e}")
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
