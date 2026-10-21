#!/usr/bin/env python
"""Offband #1602: tests for check_symbol_presence.py.

Run:  python scripts/test_check_symbol_presence.py
Exit: 0 on pass, nonzero with a clear failure message otherwise.
No external deps -- stdlib only, matching the other scripts/test_*.py. In particular it
needs no built artifact, so it runs in config-lint where nothing is compiled.

These are deliberately NEGATIVE tests. A guard exercised only against a good artifact
proves nothing about what it CATCHES. Every case below is either a regression the guard
must fail on, or a legitimate shape it must NOT flag.

Four of them are the ones that matter, because each is a way this guard could look like
it works while asserting nothing:

  * the BASE-CLASS case -- mesh::MainBoard carries do-nothing defaults for most board
    capabilities, and those defaults are linked into every image. A rule matching the
    bare name would be satisfied by the no-op and would pass on a board with no
    watchdog and no boot safety. A derived-class rule must REJECT the base symbol.
  * the LENGTH-PREFIX case -- matching mangled names by substring is only exact because
    the identifier is length-prefixed. `19resetReasonProvider` can only be that
    identifier; a bare `resetReasonProvider` search would also accept
    `myResetReasonProviderStub`, and the guard would pass on a stub.
  * the ROLE case -- the observer reaches the crash log through heartbeatBegin/
    heartbeatTick rather than crashLogStandardInit (CrashLog.h:295). Rules must be
    scoped by role in BOTH directions: the companion's wiring must not be demanded of
    an observer image, and the observer's must not be accepted in place of it.
  * the UNCLASSIFIABLE case -- an env whose platform or role cannot be determined must
    FAIL, never be skipped. A guard that quietly covers part of the matrix and prints OK
    is worse than no guard (the #1601 stale-table lesson, restated here).

The mangled names in the fixtures are not invented. They were read out of real
`.pio/build/*/firmware.elf` artifacts -- Heltec_v3_companion_radio_ble and
Heltec_v3_repeater and Heltec_v3_companion_observer_wifi for xtensa,
RAK_4631_companion_radio_usb for nRF52. That is also where the `Ej` vs `Em` difference
comes from: uint32_t mangles as `j` on xtensa and `m` on ARM EABI, which is exactly why
no rule may include a parameter list.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_symbol_presence as chk  # noqa: E402

FAILURES = []


def expect(condition, label):
    if condition:
        print("  pass  %s" % label)
    else:
        print("  FAIL  %s" % label)
        FAILURES.append(label)


def expect_raises(exc, fn, label):
    try:
        fn()
    except exc:
        print("  pass  %s" % label)
        return
    except Exception as other:  # noqa: BLE001 - the wrong exception type is a failure
        print("  FAIL  %s (raised %r instead)" % (label, other))
        FAILURES.append(label)
        return
    print("  FAIL  %s (raised nothing)" % label)
    FAILURES.append(label)


def rule(rule_id):
    return [r for r in chk.RULES if r.rule_id == rule_id][0]


# ---------------------------------------------------------------------------
# Fixtures: real mangled names, as observed in real artifacts.
# ---------------------------------------------------------------------------

UNIVERSAL = [
    "_ZN7offband13crashLogBeginEv",
    "_Z13mesh_log_linehPKcz",
    "_ZNK4mesh6Packet19calculatePacketHashEPh",
]

# The standard crash-log wrapper, used by every role but the observer.
STANDARD_CRASHLOG = [
    "_ZN7offband20crashLogStandardInitERN4mesh9MainBoardEPKc",
    "_ZN7offband26crashLogSetResetReasonHookEPFPKcvE",
    "_ZN7offband12_GLOBAL__N_119resetReasonProviderEv",
]

# heartbeatBegin is linked into EVERY ESP32 image, observer or not -- confirmed in the
# Heltec_v3_repeater and Heltec_v3_companion_radio_ble artifacts. It is in the common
# ESP32 fixture for that reason, and it is why the observer rule set needs heartbeatTick
# as well: on its own, heartbeatBegin distinguishes nothing.
ESP32_HEARTBEAT = ["_ZN7offband14heartbeatBeginEv"]

# What the observer has INSTEAD of the standard wrapper.
OBSERVER_CRASHLOG = ["_ZN7offband13heartbeatTickEj"]

ESP32_BOARD = [
    "_ZN10ESP32Board13startWatchdogEj",
    "_ZN10ESP32Board12feedWatchdogEv",
    "_ZNK10ESP32Board15isWatchdogArmedEv",
    "_ZN10ESP32Board15beginBootSafetyEv",
    "_ZN10ESP32Board13markBootValidEv",
    "_ZNK10ESP32Board23isBootValidationPendingEv",
    "_ZN10ESP32Board17appendSafetyEventEhPKc",
    "_ZN10ESP32Board16getSafetyLogTailEPcjh",
    "_ZN10ESP32Board14getSafetyStateEPcj",
    "_ZN10ESP32Board17getPartitionsInfoEPcj",
]

NRF52_BOARD = [
    "_ZN10NRF52Board13startWatchdogEm",
    "_ZN10NRF52Board12feedWatchdogEv",
    "_ZNK10NRF52Board15isWatchdogArmedEv",
    # The base-class no-ops that ARE linked into an nRF52 image. Present on purpose:
    # they must not satisfy any ESP32Board rule.
    "_ZN4mesh9MainBoard13markBootValidEv",
    "_ZN4mesh9MainBoard17appendSafetyEventEhPKc",
    "_ZN4mesh9MainBoard21startOTAUpdateOverSTAEPKcS2_Pc",
]

# uint32_t is `j` on xtensa and `m` on ARM EABI.
ESP32_TICKS = ["_ZN7offband20crashLogStandardTickEj",
               "_ZN7offband18crashLogUptimeTickEj"]
NRF52_TICKS = ["_ZN7offband20crashLogStandardTickEm",
               "_ZN7offband18crashLogUptimeTickEm"]

ESP32_COMPANION = (UNIVERSAL + STANDARD_CRASHLOG + ESP32_TICKS + ESP32_BOARD
                   + ESP32_HEARTBEAT)
ESP32_OBSERVER = (UNIVERSAL + OBSERVER_CRASHLOG + ["_ZN7offband18crashLogUptimeTickEj"]
                  + ESP32_BOARD + ESP32_HEARTBEAT)
# No crashLogUptimeTick here: on nRF52 that symbol is the no-op at CrashLog.cpp:1026
# (#378), so the rule is scoped to esp32 and the nRF52 fixture must not supply it --
# otherwise the "every applicable rule matched" count would hide the scoping.
NRF52_COMPANION = (UNIVERSAL + STANDARD_CRASHLOG
                   + ["_ZN7offband20crashLogStandardTickEm"] + NRF52_BOARD)

# One entry per family/role pair the CI build matrix actually produces;
# test_every_matrix_env_classifies asserts that correspondence in both directions, so a
# new pair entering the matrix cannot arrive without a fixture.
FIXTURES = (
    ("esp32", "companion", ESP32_COMPANION),
    ("esp32", "repeater", ESP32_COMPANION),
    ("esp32", "room_server", ESP32_COMPANION),
    ("esp32", "observer", ESP32_OBSERVER),
    ("nrf52", "companion", NRF52_COMPANION),
    ("nrf52", "repeater", NRF52_COMPANION),
    ("nrf52", "room_server", NRF52_COMPANION),
)


def filler(n):
    """Plausible unrelated symbols, to get a fixture over the coverage floor."""
    return ["_ZN4test%dfiller%dEv" % (len(str(i)) + 6, i) for i in range(n)]


# ---------------------------------------------------------------------------
# Mangling: if these are wrong, every match result below is meaningless.
# ---------------------------------------------------------------------------

def test_mangling():
    print("mangled-name construction")
    expect(chk.length_token("markBootValid") == "13markBootValid",
           "length_token prefixes the identifier with its length")
    expect(chk.length_token("") == "0",
           "length_token of an empty name is 0 (degenerate but defined)")
    expect(chk.mangled_prefixes(("mesh_log_line",)) == ("_Z13mesh_log_line",),
           "a one-component scope mangles as a free function, with no N...E")
    expect(chk.mangled_prefixes(("offband", "crashLogBegin"))
           == ("_ZN7offband13crashLogBeginE", "_ZNK7offband13crashLogBeginE"),
           "a nested scope yields both the plain and the const-qualified prefix")
    expect(chk.mangled_prefixes(("mesh", "Packet", "calculatePacketHash"))[1]
           == "_ZNK4mesh6Packet19calculatePacketHashE",
           "a three-component scope concatenates all three length tokens")
    # Against the real artifact name, not a hand-derived one.
    expect("_ZNK4mesh6Packet19calculatePacketHashEPh".startswith(
               chk.mangled_prefixes(("mesh", "Packet", "calculatePacketHash"))),
           "the constructed prefix actually matches the real observed symbol")
    expect_raises(ValueError, lambda: chk.mangled_prefixes(()),
                  "an empty scope is an error, not a prefix that matches everything")


# ---------------------------------------------------------------------------
# Matching, positive: the shapes a correct artifact presents.
# ---------------------------------------------------------------------------

def test_matching_positive():
    print("matching: shapes a correct artifact presents")
    tick = rule("crashlog-tick")
    expect(tick.satisfied_by(ESP32_COMPANION) is not None,
           "crashlog-tick matches with the xtensa parameter mangling (Ej)")
    expect(tick.satisfied_by(NRF52_COMPANION) is not None,
           "crashlog-tick matches with the ARM parameter mangling (Em)")
    expect(rule("esp32-watchdog-armed").satisfied_by(ESP32_COMPANION)
           == "_ZNK10ESP32Board15isWatchdogArmedEv",
           "a const member function is matched through the _ZNK prefix")
    expect(rule("crashlog-live-path").satisfied_by(ESP32_COMPANION) is not None,
           "the anonymous-namespace provider is matched without naming _GLOBAL__N_1")
    expect(rule("meshlog-line").satisfied_by(ESP32_COMPANION)
           == "_Z13mesh_log_linehPKcz",
           "a free function is matched through the _Z prefix")


# ---------------------------------------------------------------------------
# Matching, negative: the ways this guard could pass while asserting nothing.
# ---------------------------------------------------------------------------

def test_matching_rejects_base_class():
    print("matching: a base-class no-op must NOT satisfy a derived-class rule")
    # This is the whole reason rules name the owning class. mesh::MainBoard::
    # markBootValid is a do-nothing default and is linked into every image.
    expect(rule("esp32-boot-safety-mark-valid").satisfied_by(
               ["_ZN4mesh9MainBoard13markBootValidEv"]) is None,
           "mesh::MainBoard::markBootValid does not satisfy ESP32Board::markBootValid")
    expect(rule("esp32-safety-event-append").satisfied_by(
               ["_ZN4mesh9MainBoard17appendSafetyEventEhPKc"]) is None,
           "the MainBoard appendSafetyEvent default does not satisfy the ESP32 rule")
    expect(rule("nrf52-watchdog-start").satisfied_by(
               ["_ZN4mesh9MainBoard13startWatchdogEm"]) is None,
           "the MainBoard startWatchdog default does not satisfy the nRF52 rule")
    expect(rule("nrf52-watchdog-start").satisfied_by(
               ["_ZN10ESP32Board13startWatchdogEj"]) is None,
           "the ESP32Board watchdog does not satisfy the NRF52Board rule either")
    # An nRF52 image really does carry those MainBoard defaults, so this is not
    # hypothetical: the full fixture must still fail every ESP32 board rule.
    probs, _ = chk.check_symbols(NRF52_COMPANION, "esp32", "companion", min_symbols=0)
    expect(sum(1 for p in probs if p.startswith("esp32-")) >= 10,
           "a real nRF52 symbol table fails every ESP32 board rule, defaults and all")


def test_matching_respects_length_prefix():
    print("matching: the length prefix is what makes substring matching exact")
    live = rule("crashlog-live-path")
    # A bare substring search for "resetReasonProvider" would accept both of these.
    expect(live.satisfied_by(["_ZN7offband24myResetReasonProviderXEv"]) is None,
           "a longer identifier containing the name does not satisfy a parts rule")
    expect(live.satisfied_by(["_ZN7offband21resetReasonProviderExEv"]) is None,
           "a suffixed identifier does not satisfy a parts rule")
    expect(live.satisfied_by(
               ["_ZN7offband8somethingEv", "_ZN5other19resetReasonProviderEv"]) is None,
           "parts must co-occur in ONE symbol, not be spread across two")
    expect(live.satisfied_by(["_ZN7offband12_GLOBAL__N_119resetReasonProviderEv"])
           is not None,
           "the real anonymous-namespace symbol still satisfies it")

    begin = rule("crashlog-begin")
    expect(begin.satisfied_by(["_ZN5other13crashLogBeginEv"]) is None,
           "the right function name in the wrong namespace does not satisfy a scope rule")
    expect(begin.satisfied_by(["_ZN7offband18crashLogBeginLaterEv"]) is None,
           "a longer function name with the same start does not satisfy a scope rule")
    # Static-init thunks carry the mangled name as a SUFFIX. They are not the function,
    # and an observer image has the thunk but not the function.
    expect(rule("crashlog-reset-reason-hook").satisfied_by(
               ["_GLOBAL__sub_I__ZN7offband26crashLogSetResetReasonHookEPFPKcvE"])
           is None,
           "a _GLOBAL__sub_I_ static-init thunk does not satisfy the rule for its "
           "function (prefix match, not substring)")


def test_role_scoping_is_load_bearing_both_ways():
    print("matching: role scoping, in both directions")
    # Forward: the companion's crash-log wiring must not be demanded of an observer.
    probs, _ = chk.check_symbols(ESP32_OBSERVER, "esp32", "observer", min_symbols=0)
    expect(not probs,
           "a correct OBSERVER image passes, though it has no crashLogStandardInit "
           "(got %s)" % probs)
    # Reverse, and this is the half that stops the exemption from becoming a hole: an
    # observer image must still FAIL if checked as a companion, and a companion image
    # must fail as an observer. If either passed, the role scoping would just be a way
    # of asserting less.
    probs, _ = chk.check_symbols(ESP32_OBSERVER, "esp32", "companion", min_symbols=0)
    expect(any(p.startswith("crashlog-init:") for p in probs),
           "an observer image checked as a companion fails on the standard crash log")
    probs, _ = chk.check_symbols(ESP32_COMPANION, "esp32", "observer", min_symbols=0)
    expect(any(p.startswith("observer-heartbeat-tick:") for p in probs),
           "a companion image checked as an observer fails on heartbeatTick")
    # heartbeatBegin is linked into every ESP32 image, so it cannot carry the
    # observer assertion on its own -- which is why heartbeatTick exists as a rule.
    expect(rule("observer-heartbeat-begin").satisfied_by(ESP32_COMPANION) is not None
           and rule("observer-heartbeat-tick").satisfied_by(ESP32_COMPANION) is None,
           "heartbeatTick, not heartbeatBegin, is what distinguishes an observer")
    expect(not rule("observer-heartbeat-tick").applies_to("nrf52", "observer"),
           "the observer heartbeat rules do not apply to nRF52 (no such env exists, "
           "and heartbeatBegin is a no-op there)")


# ---------------------------------------------------------------------------
# check_symbols: the whole-artifact verdict.
# ---------------------------------------------------------------------------

def test_check_symbols_accepts_good_artifacts():
    print("check_symbols: a correct artifact of each family/role passes")
    for family, role, syms in FIXTURES:
        probs, matches = chk.check_symbols(syms, family, role, min_symbols=0)
        expect(not probs, "%s/%s fixture passes cleanly (got %s)"
                          % (family, role, probs))
        applicable = sum(1 for r in chk.RULES if r.applies_to(family, role))
        expect(len(matches) == applicable,
               "%s/%s matched every applicable rule (%d/%d)"
               % (family, role, len(matches), applicable))


def test_check_symbols_catches_each_unlinked_capability():
    print("check_symbols: removing ANY single guarded symbol fails, by rule id")
    covered = set()
    for family, role, syms in FIXTURES:
        for r in [x for x in chk.RULES if x.applies_to(family, role)]:
            hit = r.satisfied_by(syms)
            if hit is None:
                expect(False, "%s/%s fixture is missing %s, so it cannot be tested"
                              % (family, role, r.rule_id))
                continue
            reduced = [s for s in syms if s != hit]
            probs, _ = chk.check_symbols(reduced, family, role, min_symbols=0)
            expect(any(p.startswith(r.rule_id + ":") for p in probs),
                   "%s/%s: unlinking %s fails with its own rule id"
                   % (family, role, r.rule_id))
            covered.add(r.rule_id)
    # Every rule must have been exercised by some fixture. A rule no fixture reaches is
    # a rule nothing proves anything about.
    missing = sorted({r.rule_id for r in chk.RULES} - covered)
    expect(not missing, "every rule is exercised by a fixture (unexercised: %s)"
                        % missing)


def test_check_symbols_catches_wrong_family():
    print("check_symbols: family mix-ups and unclassifiable input")
    probs, _ = chk.check_symbols(NRF52_COMPANION, "esp32", "companion", min_symbols=0)
    expect(any(p.startswith("esp32-") for p in probs),
           "an nRF52 image checked as esp32 fails on the ESP32-only rules")
    probs, _ = chk.check_symbols(ESP32_COMPANION, "nrf52", "companion", min_symbols=0)
    expect(any(p.startswith("nrf52-") for p in probs),
           "an ESP32 image checked as nrf52 fails on the nRF52-only rules")
    probs, _ = chk.check_symbols(ESP32_COMPANION, "sparc", "companion", min_symbols=0)
    expect(bool(probs), "an unknown family is a failure, not an empty pass")
    probs, _ = chk.check_symbols(ESP32_COMPANION, "esp32", "bridge", min_symbols=0)
    expect(bool(probs), "an unknown role is a failure, not an empty pass")
    probs, _ = chk.check_symbols(ESP32_COMPANION, "esp32", "companion", rules=[],
                                 min_symbols=0)
    expect(bool(probs),
           "a family/role with no applicable rules is a failure, not a silent pass")


def test_check_symbols_enforces_the_coverage_floor():
    print("check_symbols: the coverage floor")
    # Every rule matches here, so only the floor can catch it. Without the floor, a
    # stripped or wrong artifact whose few remaining symbols happened to match would
    # report OK.
    probs, matches = chk.check_symbols(ESP32_COMPANION, "esp32", "companion")
    expect(bool(probs) and any("floor" in p for p in probs),
           "a near-empty symbol table fails the floor even when every rule matches")
    expect(len(matches) == sum(1 for r in chk.RULES
                               if r.applies_to("esp32", "companion")),
           "the floor failure does not suppress the rule results")
    padded = ESP32_COMPANION + filler(chk.MIN_SYMBOLS)
    probs, _ = chk.check_symbols(padded, "esp32", "companion")
    expect(not probs, "a realistically sized table passes the floor (got %s)" % probs)


# ---------------------------------------------------------------------------
# The ELF reader. Synthetic files, so this needs no build.
# ---------------------------------------------------------------------------

def build_elf(names, is64=False, big=False, symtab=True, sections=True,
              bad_link=False, truncate=0):
    """A minimal but genuinely well-formed ELF carrying `names` in .symtab."""
    end = ">" if big else "<"
    ei_class = 2 if is64 else 1
    ei_data = 2 if big else 1
    ehsize = 64 if is64 else 52
    shentsize = 64 if is64 else 40
    symsize = 24 if is64 else 16

    strtab = b"\x00"
    offsets = []
    for n in names:
        offsets.append(len(strtab))
        strtab += n.encode("utf-8") + b"\x00"

    syms = b"\x00" * symsize  # index 0 is always the null symbol
    for off in offsets:
        syms += struct.pack(end + "I", off) + b"\x00" * (symsize - 4)

    str_off = ehsize
    sym_off = str_off + len(strtab)
    sh_off = sym_off + len(syms)

    def shdr(sh_type, offset, size, link, entsize):
        if is64:
            return struct.pack(end + "IIQQQQIIQQ", 0, sh_type, 0, 0, offset, size,
                               link, 0, 1, entsize)
        return struct.pack(end + "IIIIIIIIII", 0, sh_type, 0, 0, offset, size,
                           link, 0, 1, entsize)

    headers = [shdr(0, 0, 0, 0, 0), shdr(3, str_off, len(strtab), 0, 0)]
    if symtab:
        headers.append(shdr(chk.SHT_SYMTAB, sym_off, len(syms),
                            99 if bad_link else 1, symsize))

    ident = b"\x7fELF" + bytes([ei_class, ei_data, 1, 0]) + b"\x00" * 8
    shnum = 0 if not sections else len(headers)
    shoff = 0 if not sections else sh_off
    if is64:
        ehdr = ident + struct.pack(end + "HHIQQQIHHHHHH", 2, 40, 1, 0, 0, shoff, 0,
                                   ehsize, 0, 0, shentsize, shnum, 0)
    else:
        ehdr = ident + struct.pack(end + "HHIIIIIHHHHHH", 2, 40, 1, 0, 0, shoff, 0,
                                   ehsize, 0, 0, shentsize, shnum, 0)
    blob = ehdr + strtab + syms + b"".join(headers)
    return blob[:truncate] if truncate else blob


def write_tmp(blob, tag):
    import tempfile
    fd, path = tempfile.mkstemp(prefix="offband-symtest-%s-" % tag, suffix=".elf")
    with os.fdopen(fd, "wb") as fh:
        fh.write(blob)
    return path


def test_elf_reader():
    print("ELF reader: reads names, and refuses anything it cannot read")
    wanted = ["_ZN10ESP32Board13markBootValidEv", "_Z13mesh_log_linehPKcz"]
    paths = []
    try:
        for tag, kw in (("elf32-le", dict(is64=False, big=False)),
                        ("elf64-le", dict(is64=True, big=False)),
                        ("elf32-be", dict(is64=False, big=True)),
                        ("elf64-be", dict(is64=True, big=True))):
            path = write_tmp(build_elf(wanted, **kw), tag)
            paths.append(path)
            got = chk.read_elf_symbols(path)
            expect(sorted(got) == sorted(wanted),
                   "%s: symbol names round-trip (got %s)" % (tag, got))

        for tag, blob, label in (
            ("notelf", b"MZ\x90\x00not an elf at all",
             "a non-ELF file is an error, not an empty symbol list"),
            ("stripped", build_elf(wanted, symtab=False),
             "a stripped ELF is an error -- it must not vacuously pass rules"),
            ("nosections", build_elf(wanted, sections=False),
             "an ELF with no section table is an error"),
            ("badlink", build_elf(wanted, bad_link=True),
             "a symtab pointing at a nonexistent string table is an error"),
            ("truncated", build_elf(wanted, truncate=48),
             "a truncated ELF is an error, not an IndexError"),
        ):
            path = write_tmp(blob, tag)
            paths.append(path)
            expect_raises(chk.ElfError, lambda p=path: chk.read_elf_symbols(p), label)

        expect_raises(chk.ElfError,
                      lambda: chk.read_elf_symbols(os.path.join(
                          os.path.dirname(__file__), "no-such-artifact.elf")),
                      "a missing artifact is an error (an unbuilt env must not pass)")
    finally:
        for p in paths:
            try:
                os.unlink(p)
            except OSError:
                pass


# ---------------------------------------------------------------------------
# env -> (family, role), against the real repo.
# ---------------------------------------------------------------------------

def test_family_resolution():
    print("env -> platform family")
    sections = chk.load_pio_sections()
    expect(len(sections) > 200,
           "platformio.ini plus the variant inis yield many sections (got %d)"
           % len(sections))
    expect(chk.resolve_env_family(sections, "Heltec_v3_companion_radio_ble") == "esp32",
           "an ESP32 env resolves through its variant base to esp32")
    expect(chk.resolve_env_family(sections, "RAK_4631_companion_radio_usb") == "nrf52",
           "an nRF52 env resolves to nrf52")
    expect(chk.resolve_env_family(sections, "env:Heltec_v3_repeater") == "esp32",
           "an already-prefixed env name is accepted")
    # Multi-parent `extends`, written across continuation lines. These three matrix
    # envs are the reason the ini parser joins continuations: read one line at a time
    # they appear to extend nothing, and all three dropped out of coverage.
    expect(chk.resolve_env_family(
               sections, "meshsmith_photon_esp32c6_e22p_30dbm_repeater") == "esp32",
           "an env inheriting from two parents across continuation lines resolves")
    expect(chk.resolve_env_family(
               sections, "meshsmith_photon_nrf52_e22p_30dbm_repeater") == "nrf52",
           "its nRF52 sibling resolves to the other family, not to the first match")

    synthetic = {
        "env:leaf": {"extends": "mid"},
        "mid": {"extends": "base"},
        "base": {"platform": "nordicnrf52"},
        "env:orphan": {"extends": "nowhere"},
        "env:bare": {},
        "env:weird": {"platform": "platformio/avr@1.0.0"},
        "env:loop": {"extends": "loop2"},
        "loop2": {"extends": "env:loop"},
        "env:multi": {"extends": "pa\n   pb"},
        "env:multi_first_only": {"extends": "pa\n   plain"},
        "pa": {"platform": "nordicnrf52"},
        "pb": {"platform": "platformio/espressif32@6.11.0"},
        "plain": {},
    }
    expect(chk.resolve_env_family(synthetic, "leaf") == "nrf52",
           "a multi-level extends chain resolves to the platform at its root")
    expect(chk.parse_extends("pa\n   pb") == ["pa", "pb"],
           "a multi-line extends value parses into an ordered parent list")
    expect(chk.resolve_env_family(synthetic, "multi") == "esp32",
           "with two parents declaring a platform, the LAST one wins (PlatformIO order)")
    expect(chk.resolve_env_family(synthetic, "multi_first_only") == "nrf52",
           "a parent that declares no platform falls back to the earlier one")
    expect_raises(ValueError,
                  lambda: chk.resolve_env_family(synthetic, "does_not_exist"),
                  "an env that does not exist is an error, not a skip")
    expect_raises(ValueError, lambda: chk.resolve_env_family(synthetic, "orphan"),
                  "an extends pointing at a missing section is an error")
    expect_raises(ValueError, lambda: chk.resolve_env_family(synthetic, "bare"),
                  "an env with no platform anywhere in its chain is an error")
    expect_raises(ValueError, lambda: chk.resolve_env_family(synthetic, "weird"),
                  "an unrecognised platform is an error -- it must not be skipped")
    expect_raises(ValueError, lambda: chk.resolve_env_family(synthetic, "loop"),
                  "a circular extends chain is an error, not a hang")
    expect_raises(ValueError, lambda: chk.resolve_env_family(sections, "native"),
                  "a host test env is refused rather than given a made-up verdict")


def test_interpolation():
    print("value resolution: ${section.key} and inheritance")
    sections = {
        "base": {"build_flags": "-D BASE=1"},
        "mid": {"extends": "base", "build_flags": "${base.build_flags} -D MID=1"},
        "env:inherits": {"extends": "mid"},
        "env:appends": {"extends": "mid", "build_flags": "${mid.build_flags} -D LEAF=1"},
        # Replaces rather than appends: no ${mid.build_flags}.
        "env:replaces": {"extends": "mid", "build_flags": "-D ONLY=1"},
        # References a section it does NOT extend -- the GAT562 pattern, 412 of these
        # in the real tree, and invisible to an extends-chain walk.
        "sensor_base": {"build_flags": "-D OFFBAND_OBSERVER"},
        "env:out_of_chain": {"extends": "mid",
                             "build_flags": "${sensor_base.build_flags} -D X=1",
                             "build_src_filter": "+<../examples/companion_radio>"},
        "env:dangling": {"build_flags": "${nosuchsection.build_flags}",
                         "build_src_filter": "+<../examples/companion_radio>"},
        "env:selfref": {"build_flags": "${this.build_flags} -D LOOP=1",
                        "build_src_filter": "+<../examples/simple_repeater>"},
        "env:sysenv": {"build_flags": "${sysenv.NOPE} -D REAL=1",
                       "build_src_filter": "+<../examples/simple_repeater>"},
    }
    expect("-D BASE=1" in chk.effective_value(sections, "inherits", "build_flags"),
           "an env that declares nothing inherits the key through its chain")
    val = chk.effective_value(sections, "appends", "build_flags")
    expect("BASE=1" in val and "MID=1" in val and "LEAF=1" in val,
           "a ${parent.key} reference expands transitively (got %r)" % val)
    val = chk.effective_value(sections, "replaces", "build_flags")
    expect("ONLY=1" in val and "BASE=1" not in val,
           "redeclaring a key without ${parent.key} REPLACES it, not appends (got %r)"
           % val)
    # The one that matters: a determinant reachable only through an out-of-chain
    # reference must be seen, or the env is classified on incomplete information.
    expect(chk.resolve_env_role(sections, "out_of_chain") == "observer",
           "a flag arriving via a ${...} reference outside the extends chain is seen")
    expect_raises(ValueError, lambda: chk.resolve_env_role(sections, "dangling"),
                  "a reference to a section that does not exist is an error, not a "
                  "silently truncated value")
    expect(chk.resolve_env_role(sections, "selfref") == "repeater",
           "a ${this.key} self-reference terminates instead of recursing forever")
    expect(chk.resolve_env_role(sections, "sysenv") == "repeater",
           "a ${sysenv.X} reference is dropped, not treated as a missing section")
    expect(chk.unresolved_interpolations("a ${x.y} b") == [("x", "y")],
           "unresolved_interpolations reports what was left behind")
    expect(chk.unresolved_interpolations("-D CLEAN=1") == [],
           "a fully resolved value reports nothing left behind")


def test_role_resolution():
    print("env -> role")
    sections = chk.load_pio_sections()
    for env, want in (("Heltec_v3_companion_radio_ble", "companion"),
                      ("Heltec_v3_repeater", "repeater"),
                      ("Heltec_v3_room_server", "room_server"),
                      ("Heltec_v3_companion_observer_wifi", "observer"),
                      ("RAK_4631_companion_radio_usb", "companion")):
        got = chk.resolve_env_role(sections, env)
        expect(got == want, "%s resolves to role %s (got %s)" % (env, want, got))

    # The observer is a companion_radio build plus a flag, so the flag must win over
    # the source filter -- and it must be matched word-bounded, or
    # OFFBAND_OBSERVER_BLE_COMPANION alone would class a plain companion as an observer.
    expect(chk.resolve_env_role(
               {"env:x": {"build_flags": "-D OFFBAND_OBSERVER_BLE_COMPANION",
                          "build_src_filter": "+<../examples/companion_radio>"}},
               "x") == "companion",
           "OFFBAND_OBSERVER_BLE_COMPANION alone does not make an env an observer")
    expect(chk.resolve_env_role(
               {"env:x": {"build_flags": "-D OFFBAND_OBSERVER=1",
                          "build_src_filter": "+<../examples/companion_radio>"}},
               "x") == "observer",
           "OFFBAND_OBSERVER with a value still reads as the observer flag")
    # Inherited through extends, which is how every real observer env declares it.
    expect(chk.resolve_env_role(
               {"env:child": {"extends": "env:parent"},
                "env:parent": {"build_flags": "-D OFFBAND_OBSERVER",
                               "build_src_filter": "+<../examples/companion_radio>"}},
               "child") == "observer",
           "the role is resolved through the extends chain, not just the leaf section")

    # A `-<...>` exclusion must be honoured. No env does this today, but if one
    # excluded a role's entry point and this parser still saw it, the env would be
    # checked against that role's rules -- the guard would be asserting the wrong set
    # and reporting OK. (Gemini review, finding 1.)
    expect_raises(ValueError,
                  lambda: chk.resolve_env_role(
                      {"env:x": {"build_src_filter":
                                 "+<../examples/companion_radio> "
                                 "-<../examples/companion_radio>"}}, "x"),
                  "an env that EXCLUDES the only entry point it included has no role, "
                  "rather than keeping the excluded one")
    expect(chk.resolve_env_role(
               {"env:x": {"build_src_filter":
                          "+<../examples/companion_radio> "
                          "+<../examples/simple_repeater> "
                          "-<../examples/companion_radio>"}}, "x") == "repeater",
           "an exclusion removes just that role, leaving the one still included")
    expect(chk.src_filter_paths("+<a> +<b> -<a>") == ["b"],
           "src_filter_paths applies +/- in order")
    expect(chk.src_filter_paths("+<../examples/companion_radio/*.cpp> "
                                "-<../examples/companion_radio>")== [],
           "an exclusion of a directory removes the globs beneath it")

    expect_raises(ValueError,
                  lambda: chk.resolve_env_role(
                      {"env:x": {"build_src_filter": "+<src/*.cpp>"}}, "x"),
                  "an env compiling no known entry point is an error, not a default role")
    expect_raises(ValueError,
                  lambda: chk.resolve_env_role(
                      {"env:x": {"build_src_filter":
                                 "+<../examples/simple_repeater> "
                                 "+<../examples/simple_sensor>"}}, "x"),
                  "an env compiling two entry points is ambiguous, not a guess")
    expect_raises(ValueError, lambda: chk.resolve_env_role(sections, "nope"),
                  "a nonexistent env is an error")


# ---------------------------------------------------------------------------
# Coverage floor against the real tree: the stale-table rule of #1601, restated.
# ---------------------------------------------------------------------------

def ci_matrix_envs():
    import re
    path = os.path.join(chk.REPO_ROOT, ".github", "workflows", "ci.yml")
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    block = re.search(r"^  build:.*?^    steps:", text, re.S | re.M)
    return re.findall(r"^          - (\S+)", block.group(0), re.M) if block else []


def test_every_matrix_env_classifies():
    print("coverage: every env in the CI build matrix must classify")
    envs = ci_matrix_envs()
    expect(len(envs) >= 50,
           "the build matrix is read from ci.yml (got %d envs)" % len(envs))
    sections = chk.load_pio_sections()
    unresolved = []
    pairs = {}
    for env in envs:
        try:
            key = (chk.resolve_env_family(sections, env),
                   chk.resolve_env_role(sections, env))
        except ValueError as exc:
            unresolved.append("%s: %s" % (env, exc))
            continue
        pairs[key] = pairs.get(key, 0) + 1
    # Adding an env to the matrix whose platform or role this cannot classify would
    # otherwise mean one leg checks nothing while still printing OK -- the #1601
    # stale-table failure, in a different file.
    expect(not unresolved,
           "every matrix env resolves to a family and a role (unresolved: %s)"
           % unresolved)
    for key in sorted(pairs):
        expect(any(r.applies_to(*key) for r in chk.RULES),
               "%s/%s (%d matrix envs) has at least one rule" % (key + (pairs[key],)))
    # Both directions. A fixture for a pair the matrix never builds tests a shape that
    # does not exist; a matrix pair with no fixture is a shape nothing above proves
    # anything about, which is how esp32/observer slipped through the first draft.
    fixture_pairs = {(f, r) for f, r, _ in FIXTURES}
    expect(fixture_pairs <= set(pairs),
           "every fixture family/role pair is one the matrix really builds "
           "(fixtures not in the matrix: %s)" % sorted(fixture_pairs - set(pairs)))
    expect(set(pairs) <= fixture_pairs,
           "every family/role pair the matrix builds has a fixture "
           "(unfixtured: %s)" % sorted(set(pairs) - fixture_pairs))

    # Independent cross-check, deliberately NOT using the ini semantics: the env NAME
    # says what role it is. "Every env classifies" would still pass if the resolver
    # classified them all wrongly but consistently; this catches that.
    def implied_role(name):
        n = name.lower()
        for needle, role in (("observer", "observer"), ("room_server", "room_server"),
                             ("repeater", "repeater"), ("companion", "companion"),
                             ("badge", "companion")):
            if needle in n:
                return role
        return None

    disagreements = []
    for env in envs:
        want = implied_role(env)
        if want is None:
            continue
        try:
            got = chk.resolve_env_role(sections, env)
        except ValueError:
            continue  # already reported above
        if got != want:
            disagreements.append("%s: name says %s, config says %s" % (env, want, got))
    expect(not disagreements,
           "the resolved role agrees with the env name on every matrix env "
           "(disagreements: %s)" % disagreements)
    print("        matrix pairs: %s"
          % ", ".join("%s/%s=%d" % (f, r, n) for (f, r), n in sorted(pairs.items())))


def main():
    test_mangling()
    test_matching_positive()
    test_matching_rejects_base_class()
    test_matching_respects_length_prefix()
    test_role_scoping_is_load_bearing_both_ways()
    test_check_symbols_accepts_good_artifacts()
    test_check_symbols_catches_each_unlinked_capability()
    test_check_symbols_catches_wrong_family()
    test_check_symbols_enforces_the_coverage_floor()
    test_elf_reader()
    test_family_resolution()
    test_interpolation()
    test_role_resolution()
    test_every_matrix_env_classifies()

    print()
    if FAILURES:
        print("FAIL: %d case(s) failed:" % len(FAILURES), file=sys.stderr)
        for f in FAILURES:
            print("  - %s" % f, file=sys.stderr)
        return 1
    print("OK: all check_symbol_presence cases pass (negative cases included).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
