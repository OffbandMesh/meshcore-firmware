#!/usr/bin/env python
"""Offband #1602: the capabilities we added must still be LINKED into the artifact.

Why this exists
---------------
A whole class of Offband regression produces a clean build. Delete the call that arms
the watchdog, let a base-update land a `#define` that selects a no-op crash-log path,
drop a role from the boot-safety wiring -- the compiler is happy, `ci-green` is green,
the native suite is green, and the capability is simply gone from the image. Nobody
finds out until a node needs the thing in the field.

So this asserts on the BUILT ARTIFACT: for every env in the build matrix, the symbols
that implement those capabilities are present in the ELF's symbol table. It runs per
matrix leg right after `pio run`, so a change that silently unlinks a capability fails
the PR on the env where it happened.

Reads the ELF directly, no toolchain
------------------------------------
Symbol names come from parsing `.symtab` in pure Python, not from `nm`. The matrix
spans xtensa, ARM and RISC-V, and each needs a different cross-`nm` that is only on
PATH inside its own PlatformIO toolchain. Parsing the section table ourselves makes the
check identical on every leg and on a developer's machine.

Matching is exact, which is the whole point
-------------------------------------------
Two modes, chosen per rule for a reason:

  `scope`  Build the Itanium-ABI mangled prefix of a FULLY QUALIFIED name and require a
           symbol starting with it -- `ESP32Board::markBootValid` becomes
           `_ZN10ESP32Board13markBootValidE`. The owning class is part of the assertion
           on purpose: `mesh::MainBoard` carries do-nothing defaults for most of these,
           so a rule that matched the bare name `markBootValid` would be satisfied by
           the base-class no-op and would pass on a node with no boot safety at all.

  `parts`  Require one symbol to contain every given name as a LENGTH-PREFIXED token
           (`resetReasonProvider` -> `19resetReasonProvider`). Used where the qualified
           prefix is not stable: `uint32_t` mangles as `j` on xtensa and `m` on ARM
           EABI, so a parameter list cannot be part of the pattern, and an anonymous
           namespace spells itself `_GLOBAL__N_1` at the compiler's discretion. The
           length prefix is what keeps this exact -- a bare substring search for
           `resetReasonProvider` would also be satisfied by `myResetReasonProviderStub`,
           while `19resetReasonProvider` can only be that identifier.

Presence must mean something
----------------------------
A rule is worth having only if the symbol can actually go missing. Several of these
capabilities have a no-op twin behind an `#ifdef` that emits the SAME mangled name, so
asserting the obvious symbol would be vacuous -- it would pass on exactly the build
that lost the feature. Where that is the case the rule names a symbol that only the
live path compiles, and the rule says so. `offband::crashLogStandardInit` exists either
way; `offband::(anonymous)::resetReasonProvider` exists only when the real crash log is
in, and its address is taken by `crashLogSetResetReasonHook`, so the optimizer may not
drop it.

What it does NOT check, deliberately
------------------------------------
Anything whose presence depends on the inliner. `BaseSerialInterface::maxFramePayload`
is a non-virtual inline member: it survives in some envs and is fully inlined away in
others, both correct. A rule over it would fail on correct images, which is worse than
no rule. The BLE frame-cap invariant belongs in a source-level check, not here.
Likewise `ROLLBACK_THRESHOLD` is a macro and leaves no symbol to assert on at all.

The line is virtuality, not inlining, and some rules below do name one-line inline
members (`ESP32Board::isWatchdogArmed` is `{ return _wdt_started; }` in the header).
That is sound because they are virtual `override`s: the vtable takes their address, so
the compiler must emit them whatever it inlines at the call sites. A non-virtual inline
has no such anchor. Do not add a rule for one.

Also not checked: whether a matched function does anything. A symbol proves linkage, not
behaviour. Where a capability has a no-op twin the rule is pointed at a symbol only the
live path compiles and says so; where it cannot be (`crashlog-uptime` on nRF52) the rule
is scoped out rather than left to match a stub.

Known limitation: no rule here would survive `-flto` or identical-code folding, which
may drop or alias a symbol the compiler proved redundant. Neither is enabled anywhere in
this tree (no `-flto`, no `-fipa-icf`, no `--icf` in platformio.ini or any variant), and
all 72 matrix envs build at the default optimization, so the rules hold as written. If
LTO is ever turned on, re-verify every rule against a real artifact before trusting a
green result -- do not assume this still means what it says.

An env this cannot classify is a FAILURE, not a skip -- the pin-guard lesson. A check
that quietly covers half the matrix and prints OK is worse than no check.

Run:  python scripts/check_symbol_presence.py --env <env> [--elf path] [--verbose]
      python scripts/check_symbol_presence.py --list-rules
Exit: 0 clean, 1 on any violation. Paired negative tests in
      scripts/test_check_symbol_presence.py
"""

import glob
import os
import re
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Platform families. An env must resolve to exactly one of these; see
# resolve_env_family(). "native" is not here on purpose -- the host test envs build no
# firmware image and this guard refuses them rather than inventing a result.
FAMILIES = ("esp32", "nrf52", "stm32", "rp2040")
ALL = FAMILIES

# Roles, the SECOND axis, and not an optional one. The crash log does not vary by
# platform, it varies by role: every role calls offband::crashLogStandardInit except the
# observer, which reaches the same machinery through heartbeatBegin()/heartbeatTick()
# instead (CrashLog.h:295, and the "all non-observer companions" note at
# examples/companion_radio/main.cpp:810). A platform-only rule table asserted the
# companion's wiring against the observer and failed a correct image -- which is how
# this axis got found. An env whose role cannot be determined FAILS, same discipline as
# an unclassifiable platform.
ROLES = ("companion", "observer", "repeater", "room_server", "sensor", "kiss_modem",
         "secure_chat")
ANY_ROLE = ROLES

# The example directory a role's entry point lives in, as it appears in
# build_src_filter. The observer is not here: it is a companion_radio build
# distinguished by a build flag, see resolve_env_role().
ROLE_SOURCE_DIR = (
    ("examples/companion_radio", "companion"),
    ("examples/simple_repeater", "repeater"),
    ("examples/simple_room_server", "room_server"),
    ("examples/simple_sensor", "sensor"),
    ("examples/kiss_modem", "kiss_modem"),
    ("examples/simple_secure_chat", "secure_chat"),
)

# platformio `platform =` substring -> family. Matched against the resolved value, so
# a pinned version or a package URL still classifies.
PLATFORM_FAMILY = (
    ("espressif32", "esp32"),
    ("nordicnrf52", "nrf52"),
    ("ststm32", "stm32"),
    ("raspberrypi", "rp2040"),
    ("rp2040", "rp2040"),
)

# A real firmware symbol table runs to thousands of names. Anything near-empty means we
# were handed a stripped image or the wrong file, and every rule below would "pass" by
# matching nothing -- so the floor is itself a rule.
MIN_SYMBOLS = 500


class ElfError(Exception):
    """The file is not an ELF we can read symbol names out of."""


class Rule(object):
    """One linked-capability assertion.

    `scope` is a fully qualified name as a tuple of scope components, innermost last:
    ("mesh", "Packet", "calculatePacketHash"). A single component is a free function.
    `parts` is a set of identifiers that must co-occur in one symbol, as length-prefixed
    tokens. Exactly one of the two is given.
    """

    def __init__(self, rule_id, families, why, scope=None, parts=None, roles=ANY_ROLE):
        assert (scope is None) != (parts is None), "give exactly one of scope/parts"
        self.rule_id = rule_id
        self.families = tuple(families)
        self.roles = tuple(roles)
        self.why = why
        self.scope = tuple(scope) if scope else None
        self.parts = tuple(parts) if parts else None

    def applies_to(self, family, role):
        return family in self.families and role in self.roles

    def describe(self):
        if self.scope:
            return "::".join(self.scope)
        return " + ".join(self.parts) + " (co-occurring)"

    def pattern(self):
        if self.scope:
            return " or ".join(mangled_prefixes(self.scope))
        return " and ".join(length_token(p) for p in self.parts)

    def satisfied_by(self, symbols):
        """Return a matching symbol name, or None."""
        if self.scope:
            prefixes = mangled_prefixes(self.scope)
            for sym in symbols:
                if sym.startswith(prefixes):
                    return sym
            return None
        tokens = tuple(length_token(p) for p in self.parts)
        for sym in symbols:
            if all(tok in sym for tok in tokens):
                return sym
        return None


NON_OBSERVER = tuple(r for r in ROLES if r != "observer")

RULES = [
    # ---- the crash log (#472 / Epic #350), every role ----------------------------
    Rule(
        "crashlog-begin", ALL,
        "Dumps a ring that survived the previous reset. The one line that turns a "
        "crash into a diagnosis. Reached by every role, through crashLogStandardInit "
        "or, on the observer, through heartbeatBegin.",
        scope=("offband", "crashLogBegin"),
    ),
    Rule(
        "crashlog-uptime", ("esp32",),
        "#1074: without it a role's prev_boot_lasted reads 0 by construction. ESP32 "
        "only, and NOT an oversight: on nRF52 and host builds this function IS the "
        "no-op at CrashLog.cpp:1026, because nRF52 retained RAM does not survive a "
        "reset (#378). Asserting it on nRF52 would have matched that stub -- a rule "
        "that can only ever find a no-op guards nothing and claims it does.",
        scope=("offband", "crashLogUptimeTick"),
    ),
    # ---- the crash log, every role BUT the observer ------------------------------
    # The observer predates the standard wrapper and drives the same core from
    # heartbeatBegin()/heartbeatTick(); CrashLog.h:295 records it. Asserting the
    # wrapper against it would fail a correct image.
    Rule(
        "crashlog-init", ALL,
        "Every non-observer role's setup() calls this; without it a node never reports "
        "why it rebooted (#472 / Epic #350).",
        scope=("offband", "crashLogStandardInit"), roles=NON_OBSERVER,
    ),
    Rule(
        "crashlog-tick", ALL,
        "Drains the ring and keeps prev_boot_lasted; #1074 (F1) put it in every role "
        "but the observer, which ticks through heartbeatTick instead. Like "
        "crashlog-init this has a no-op twin emitting the same mangled name, so it "
        "catches DELETION but not an #ifdef switch-off; crashlog-live-path is what "
        "covers that. Audited 2026-10-18: every other rule here has a single "
        "unconditional definition.",
        scope=("offband", "crashLogStandardTick"), roles=NON_OBSERVER,
    ),
    Rule(
        "crashlog-reset-reason-hook", ALL,
        "The reset-reason plumbing. Called from exactly one place: the live "
        "crashLogStandardInit.",
        scope=("offband", "crashLogSetResetReasonHook"), roles=NON_OBSERVER,
    ),
    Rule(
        "crashlog-live-path", ALL,
        "NOT vacuous, unlike the symbols above: crashLogStandardInit is also compiled "
        "as a no-op under OFFBAND_CRASHLOG_DISABLED / OFFBAND_CRASHLOG_HOST, with the "
        "same mangled name. This anonymous-namespace provider is compiled only on the "
        "live path, and its address is taken, so it cannot be optimized away. Its "
        "absence means crash logging was switched off in this image.",
        parts=("offband", "resetReasonProvider"), roles=NON_OBSERVER,
    ),
    # ---- the observer's equivalent -----------------------------------------------
    # heartbeatTick is the discriminating one: heartbeatBegin is linked into every
    # ESP32 image, so on its own it would assert almost nothing.
    Rule(
        "observer-heartbeat-tick", ("esp32",),
        "The observer's crash-log and heap tick, in place of crashLogStandardTick "
        "(CrashLog.h:295). Losing it costs the observer its ring drain and its "
        "prev_boot_lasted, with nothing else wired to do it.",
        scope=("offband", "heartbeatTick"), roles=("observer",),
    ),
    Rule(
        "observer-heartbeat-begin", ("esp32",),
        "Writes the boot record every boot and loads the NVS boot count "
        "(PrefsLayout.h:46). No-op on nRF52, and no observer env is nRF52.",
        scope=("offband", "heartbeatBegin"), roles=("observer",),
    ),
    Rule(
        "meshlog-line", ALL,
        "#1211: every rig-visible diagnostic goes through MeshLog so it reaches the "
        "mirror as well as the port. If this is unlinked, bench output is gone.",
        scope=("mesh_log_line",),
    ),
    Rule(
        "packet-hash", ALL,
        "The packet-hash capability behind FIRMWARE_VER_CODE 22; Offband clients gate "
        "on it. Losing it silently breaks every client that negotiated >= 22.",
        scope=("mesh", "Packet", "calculatePacketHash"),
    ),
    # ---- nRF52 -------------------------------------------------------------------
    Rule(
        "nrf52-watchdog-start", ("nrf52",),
        "The nRF52 hardware WDT implementation (#257). mesh::MainBoard carries a "
        "do-nothing default, so this rule names NRF52Board on purpose -- a bare-name "
        "rule would be satisfied by the base no-op.",
        scope=("NRF52Board", "startWatchdog"),
    ),
    Rule(
        "nrf52-watchdog-feed", ("nrf52",),
        "Fed from the main loop only, so a hung loop trips the WDT (#257).",
        scope=("NRF52Board", "feedWatchdog"),
    ),
    Rule(
        "nrf52-watchdog-armed", ("nrf52",),
        "Lets the CLI report whether the WDT is actually armed, rather than assumed.",
        scope=("NRF52Board", "isWatchdogArmed"),
    ),
    # ---- ESP32 -------------------------------------------------------------------
    Rule(
        "esp32-watchdog-start", ("esp32",),
        "#446: runtime watchdog via the ESP-IDF Task WDT. Named on ESP32Board because "
        "mesh::MainBoard's default does nothing.",
        scope=("ESP32Board", "startWatchdog"),
    ),
    Rule(
        "esp32-watchdog-feed", ("esp32",),
        "#446: fed from the main loop each iteration.",
        scope=("ESP32Board", "feedWatchdog"),
    ),
    Rule(
        "esp32-watchdog-armed", ("esp32",),
        "Reports armed state instead of assuming it (#1083 was diagnosed against an "
        "assumption).",
        scope=("ESP32Board", "isWatchdogArmed"),
    ),
    Rule(
        "esp32-boot-safety-begin", ("esp32",),
        "#63 / D9: app-level boot-rollback safety. On the base class this is a no-op, "
        "so the ESP32Board override is the assertion -- an image that lost it boots "
        "into a loop with no rollback and no counter.",
        scope=("ESP32Board", "beginBootSafety"),
    ),
    Rule(
        "esp32-boot-safety-mark-valid", ("esp32",),
        "#63: the call that ends a probationary boot. Without it every boot stays "
        "unvalidated and the node eventually rolls back a good image.",
        scope=("ESP32Board", "markBootValid"),
    ),
    Rule(
        "esp32-boot-safety-pending", ("esp32",),
        "#63: lets the node know it is on probation.",
        scope=("ESP32Board", "isBootValidationPending"),
    ),
    Rule(
        "esp32-safety-event-append", ("esp32",),
        "#63: the SafeFlash event trail. Base-class default discards events.",
        scope=("ESP32Board", "appendSafetyEvent"),
    ),
    Rule(
        "esp32-safety-log-tail", ("esp32",),
        "#63: how the event trail is read back over the CLI.",
        scope=("ESP32Board", "getSafetyLogTail"),
    ),
    Rule(
        "esp32-safety-state", ("esp32",),
        "#63: current safety state for the CLI.",
        scope=("ESP32Board", "getSafetyState"),
    ),
    Rule(
        "esp32-partitions-info", ("esp32",),
        "#63: which partition is running and which would be rolled back to.",
        scope=("ESP32Board", "getPartitionsInfo"),
    ),
]


def length_token(name):
    """Itanium-ABI source-name token: 'markBootValid' -> '13markBootValid'."""
    return "%d%s" % (len(name), name)


def mangled_prefixes(scope):
    """Mangled-name prefixes for a fully qualified name.

    A single component is a free function (`_Z13mesh_log_line`). More than one is a
    nested name, which may be const-qualified when it is a member function, so both
    `_ZN...E` and `_ZNK...E` are accepted.
    """
    scope = tuple(scope)
    if not scope:
        raise ValueError("empty scope")
    if len(scope) == 1:
        return ("_Z" + length_token(scope[0]),)
    inner = "".join(length_token(p) for p in scope)
    return ("_ZN" + inner + "E", "_ZNK" + inner + "E")


# --------------------------------------------------------------------------------
# ELF symbol table
# --------------------------------------------------------------------------------

SHT_SYMTAB = 2


def _unpack(fmt, data, offset):
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise ElfError("truncated ELF: wanted %d bytes at 0x%x, file is 0x%x"
                       % (size, offset, len(data)))
    return struct.unpack_from(fmt, data, offset)[0]


def read_elf_symbols(path):
    """Every named symbol in every SHT_SYMTAB of an ELF, as a list of strings.

    Raises ElfError for anything that is not a readable ELF with a symbol table --
    including a stripped image, which must fail rather than vacuously satisfy rules.
    """
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except IOError as exc:
        raise ElfError("cannot read %s: %s" % (path, exc))

    if len(data) < 52 or data[:4] != b"\x7fELF":
        raise ElfError("%s is not an ELF file" % path)

    ei_class, ei_data = data[4], data[5]
    if ei_class not in (1, 2):
        raise ElfError("unknown ELF class %r" % ei_class)
    if ei_data not in (1, 2):
        raise ElfError("unknown ELF endianness %r" % ei_data)
    end = "<" if ei_data == 1 else ">"
    is64 = ei_class == 2

    if is64:
        e_shoff = _unpack(end + "Q", data, 0x28)
        e_shentsize = _unpack(end + "H", data, 0x3A)
        e_shnum = _unpack(end + "H", data, 0x3C)
    else:
        e_shoff = _unpack(end + "I", data, 0x20)
        e_shentsize = _unpack(end + "H", data, 0x2E)
        e_shnum = _unpack(end + "H", data, 0x30)

    if e_shoff == 0 or e_shnum == 0:
        raise ElfError("%s has no section table" % path)

    sections = []
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        sh_type = _unpack(end + "I", data, base + 4)
        if is64:
            sh_offset = _unpack(end + "Q", data, base + 0x18)
            sh_size = _unpack(end + "Q", data, base + 0x20)
            sh_link = _unpack(end + "I", data, base + 0x28)
            sh_entsize = _unpack(end + "Q", data, base + 0x38)
        else:
            sh_offset = _unpack(end + "I", data, base + 0x10)
            sh_size = _unpack(end + "I", data, base + 0x14)
            sh_link = _unpack(end + "I", data, base + 0x18)
            sh_entsize = _unpack(end + "I", data, base + 0x24)
        sections.append((sh_type, sh_offset, sh_size, sh_link, sh_entsize))

    names = []
    found_symtab = False
    for sh_type, sh_offset, sh_size, sh_link, sh_entsize in sections:
        if sh_type != SHT_SYMTAB or not sh_entsize:
            continue
        if sh_link >= len(sections):
            raise ElfError("symtab points at section %d, which does not exist" % sh_link)
        found_symtab = True
        str_offset, str_size = sections[sh_link][1], sections[sh_link][2]
        if str_offset + str_size > len(data):
            raise ElfError("truncated ELF: string table runs past end of file")
        for i in range(sh_size // sh_entsize):
            st_name = _unpack(end + "I", data, sh_offset + i * sh_entsize)
            if st_name == 0 or st_name >= str_size:
                continue
            start = str_offset + st_name
            stop = data.find(b"\x00", start, str_offset + str_size)
            if stop < 0:
                raise ElfError("unterminated symbol name at 0x%x" % start)
            names.append(data[start:stop].decode("utf-8", "replace"))

    if not found_symtab:
        raise ElfError("%s has no .symtab (stripped?)" % path)
    return names


# --------------------------------------------------------------------------------
# env -> platform family
# --------------------------------------------------------------------------------

_SECTION_RE = re.compile(r"^\[([^\]]+)\]\s*$")
_KEY_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$")


def load_pio_sections(repo_root=REPO_ROOT):
    """Merge platformio.ini and every variants/*/platformio.ini into {section: {key: value}}.

    Hand-parsed rather than configparser: these files carry `${sysenv...}` values,
    duplicate section names across variant files, and `%` characters, all of which
    configparser either interpolates or rejects.

    Continuation lines are joined, not dropped. `extends` is frequently written as a
    multi-line list -- the meshsmith photon envs inherit from two sections that way --
    and a parser that kept only the first line read those envs as extending nothing.
    """
    paths = [os.path.join(repo_root, "platformio.ini")]
    paths += sorted(glob.glob(os.path.join(repo_root, "variants", "*", "platformio.ini")))
    sections = {}
    for path in paths:
        if not os.path.isfile(path):
            continue
        current = None
        key = None
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for raw in fh:
                line = raw.rstrip("\n")
                if not line.strip() or line.lstrip().startswith((";", "#")):
                    continue
                m = _SECTION_RE.match(line.strip())
                if m:
                    current, key = m.group(1), None
                    sections.setdefault(current, {})
                    continue
                if current is None:
                    continue
                if line[:1] in (" ", "\t"):
                    if key is not None:
                        sections[current][key] = (
                            sections[current][key] + "\n" + line.strip()).strip()
                    continue
                m = _KEY_RE.match(line)
                if m:
                    key = m.group(1)
                    sections[current].setdefault(key, m.group(2).strip())
                else:
                    key = None
    return sections


def parse_extends(value):
    """`extends` as an ordered list of parent sections.

    PlatformIO allows several parents, newline- or comma-separated, with LATER entries
    overriding earlier ones.
    """
    if not value:
        return []
    return [p for p in re.split(r"[,\s]+", value.strip()) if p]


def _find_platform(sections, node, referrer, path, visiting):
    """Depth-first search for the effective `platform`, honouring last-parent-wins."""
    body = sections.get(node)
    if body is None:
        raise ValueError("[%s] extends [%s], which does not exist" % (referrer, node))
    if node in visiting:
        raise ValueError("circular extends chain: %s" % " -> ".join(path + [node]))
    path = path + [node]
    if "platform" in body:
        return body["platform"], path
    visiting = visiting | {node}
    # Reversed: PlatformIO applies parents left to right, so the last one that declares
    # a platform is the one in force.
    for parent in reversed(parse_extends(body.get("extends"))):
        found, found_path = _find_platform(sections, parent, node, path, visiting)
        if found is not None:
            return found, found_path
    return None, path


def resolve_env_family(sections, env):
    """The platform family of `env`, following its `extends` graph.

    Raises ValueError when the env does not exist, declares no platform anywhere in its
    chain, or declares one this does not recognise. Never guesses: an unclassifiable env
    must fail the build, because the alternative is a leg that silently checks nothing.
    """
    name = env if env.startswith("env:") else "env:" + env
    if name not in sections:
        raise ValueError("no [%s] in platformio.ini or any variants/*/platformio.ini"
                         % name)
    platform, chain = _find_platform(sections, name, name, [], set())
    if platform is None:
        raise ValueError("%s declares no platform (chain: %s)"
                         % (name, " -> ".join(chain)))
    if "native" in platform:
        raise ValueError("%s is a host test env (platform = %s); it builds no firmware "
                         "image and has no symbols to guard" % (name, platform))
    for needle, family in PLATFORM_FAMILY:
        if needle in platform:
            return family
    raise ValueError("%s has platform = %s, which maps to no known family. Add it to "
                     "PLATFORM_FAMILY -- an unclassified env must not be skipped."
                     % (name, platform))


_INTERP_RE = re.compile(r"\$\{([^.}]+)\.([^}]+)\}")

# `${sysenv.X}` reads the process environment, not another section; nothing to follow.
#
# The rest are the sections of the per-host secrets ini, pulled in by
# `extra_configs = ${sysenv.PIO_SECRETS_FILE}`. They are listed here so this guard
# NEVER reads that file and can never print one of those values in a diagnostic: they
# are the real admin password, the WiFi PSK, the MQTT credentials and the OTA trigger
# secret. They also cannot carry a role or platform determinant, so dropping them costs
# the classification nothing. A reference to any OTHER unknown section is still an
# error -- that one might have been load-bearing.
_SECRET_SECTIONS = ("node_secrets", "wifi_secrets", "mqtt_secrets", "ota_secrets",
                    "cmdrelay")
_INTERP_IGNORE = ("sysenv",) + _SECRET_SECTIONS


def _section_name(sections, name):
    """Resolve a reference to a real section key, trying the `env:` form second.

    `extends` and `${...}` both name plain sections (`esp32_base`) and env sections
    (`env:Heltec_v3_repeater`), and a bare env name is also accepted from the command
    line. Prepending `env:` unconditionally is what made `${base.build_flags}` expand to
    nothing -- the reference resolved to a section called `env:base` that never existed.
    """
    if name in sections:
        return name
    prefixed = "env:" + name
    return prefixed if prefixed in sections else name


def _key_owner(sections, node, key, seen=None):
    """The section that supplies `key` for `node`: itself, else a parent (last wins)."""
    seen = seen or set()
    if node in seen:
        return None
    seen = seen | {node}
    body = sections.get(node)
    if body is None:
        return None
    if key in body:
        return node
    for parent in reversed(parse_extends(body.get("extends"))):
        owner = _key_owner(sections, parent, key, seen)
        if owner is not None:
            return owner
    return None


def effective_value(sections, env, key, _seen=None):
    """`key` as it ends up applying to `env`, with `${section.key}` expanded.

    This is the one part of PlatformIO's semantics worth reimplementing accurately,
    because an env's role is read out of these strings. Two things a plain extends-chain
    walk gets wrong:

      * A key is INHERITED, not accumulated. An env that redeclares `build_flags`
        without `${parent.build_flags}` REPLACES it; concatenating the chain would
        credit it with flags it does not have.
      * `${...}` references are not confined to the extends chain. 412 of them in this
        tree point at a section the referring env does not extend -- the GAT562 boards
        pull in `${sensor_base.build_flags}` that way. A chain walk cannot see those at
        all, so a determinant arriving through one would be invisible and the env would
        be classified on incomplete information.
    """
    name = _section_name(sections, env)
    _seen = _seen or set()
    owner = _key_owner(sections, name, key)
    if owner is None:
        return ""
    if (owner, key) in _seen:
        return ""  # interpolation cycle; expand no further
    _seen = _seen | {(owner, key)}

    def sub(match):
        target, target_key = match.group(1), match.group(2)
        if target in _INTERP_IGNORE:
            return ""
        if target == "this":
            target = owner
        if target not in sections:
            # Left in place rather than dropped, so unresolved_interpolations() can
            # report it. Classifying an env on a value we only partly understand is the
            # failure this guard exists to avoid.
            return match.group(0)
        return effective_value(sections, target, target_key, _seen)

    return _INTERP_RE.sub(sub, sections[owner][key])


def unresolved_interpolations(value):
    """Any `${...}` left after expansion -- a reference to a section never found."""
    return _INTERP_RE.findall(value or "")


def src_filter_paths(value):
    """The INCLUDED paths of a build_src_filter, with `-<...>` exclusions applied.

    PlatformIO applies `+<glob>` and `-<glob>` in order. Ignoring the minus form would
    let an env that explicitly excludes a role's entry point still be classified as that
    role and then checked against the wrong rules. No env in the tree does this today;
    the parser honours it so the first one to try cannot be misread.
    """
    value = (value or "").replace("\\", "/")
    included = []
    for sign, glob_text in re.findall(r"([+-])<([^>]*)>", value):
        if sign == "+":
            included.append(glob_text)
        else:
            needle = glob_text.rstrip("/*")
            if needle:
                included = [p for p in included if needle not in p]
    return included


def resolve_env_role(sections, env):
    """The role of `env`: which example entry point it compiles.

    Raises ValueError when no known example directory appears in the env's
    build_src_filter. Never defaults to a role -- guessing "companion" for an
    unrecognised env would silently assert the companion's wiring against something
    else, which is exactly the failure that put the observer on this axis.
    """
    name = env if env.startswith("env:") else "env:" + env
    if name not in sections:
        raise ValueError("no [%s] in platformio.ini or any variants/*/platformio.ini"
                         % name)

    flags = effective_value(sections, name, "build_flags")
    # Word-bounded: OFFBAND_OBSERVER_BLE_COMPANION must not read as OFFBAND_OBSERVER.
    if re.search(r"(?<![A-Za-z0-9_])OFFBAND_OBSERVER(?![A-Za-z0-9_])", flags):
        return "observer"

    src = effective_value(sections, name, "build_src_filter")
    # A value we could not fully expand is not evidence. Refusing here is the point:
    # the alternative is deciding the role from a string with a hole in it.
    leftover = unresolved_interpolations(src) + unresolved_interpolations(flags)
    if leftover:
        raise ValueError("%s has unresolved interpolation(s) %s in build_src_filter or "
                         "build_flags, so its role cannot be read from them"
                         % (name, ["${%s.%s}" % r for r in leftover]))

    paths = src_filter_paths(src)
    found = sorted({role for needle, role in ROLE_SOURCE_DIR
                    if any(needle in p for p in paths)})
    if len(found) == 1:
        return found[0]
    if not found:
        raise ValueError("%s compiles no known example entry point; its "
                         "build_src_filter names none of %s. Add it to ROLE_SOURCE_DIR "
                         "-- an unclassified role must not be skipped."
                         % (name, ", ".join(n for n, _ in ROLE_SOURCE_DIR)))
    raise ValueError("%s compiles more than one example entry point (%s); the role is "
                     "ambiguous" % (name, ", ".join(found)))


# --------------------------------------------------------------------------------
# the check
# --------------------------------------------------------------------------------

def check_symbols(symbols, family, role, rules=None, min_symbols=MIN_SYMBOLS):
    """Return (problems, matches). `problems` empty means the artifact is good."""
    rules = RULES if rules is None else rules
    if family not in FAMILIES:
        return (["unknown platform family %r" % family], {})
    if role not in ROLES:
        return (["unknown role %r" % role], {})

    problems = []
    if len(symbols) < min_symbols:
        problems.append(
            "symbol table holds %d names, below the floor of %d -- this looks like a "
            "stripped or wrong artifact, and every rule below would pass by matching "
            "nothing" % (len(symbols), min_symbols))

    applicable = [r for r in rules if r.applies_to(family, role)]
    if not applicable:
        problems.append("no rules apply to %s/%s -- that is a gap in RULES, not a pass"
                        % (family, role))

    matches = {}
    for rule in applicable:
        hit = rule.satisfied_by(symbols)
        if hit is None:
            problems.append(
                "%s: %s is not linked (looked for %s). %s"
                % (rule.rule_id, rule.describe(), rule.pattern(), rule.why))
        else:
            matches[rule.rule_id] = hit
    return (problems, matches)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv

    if "--list-rules" in argv:
        for rule in RULES:
            roles = "any" if rule.roles == ANY_ROLE else ",".join(rule.roles)
            print("%-26s %-12s %-22s %s"
                  % (rule.rule_id, ",".join(rule.families), roles, rule.describe()))
        print("\n%d rules; %d apply to every family and role."
              % (len(RULES),
                 sum(1 for r in RULES if r.families == ALL and r.roles == ANY_ROLE)))
        return 0

    verbose = "--verbose" in argv
    env = elf = None
    for i, arg in enumerate(argv):
        if arg == "--env" and i + 1 < len(argv):
            env = argv[i + 1]
        elif arg.startswith("--env="):
            env = arg.split("=", 1)[1]
        elif arg == "--elf" and i + 1 < len(argv):
            elf = argv[i + 1]
        elif arg.startswith("--elf="):
            elf = arg.split("=", 1)[1]

    if not env:
        print("usage: check_symbol_presence.py --env <env> [--elf <path>] [--verbose]",
              file=sys.stderr)
        return 1
    if elf is None:
        elf = os.path.join(REPO_ROOT, ".pio", "build", env, "firmware.elf")

    sections = load_pio_sections()
    try:
        family = resolve_env_family(sections, env)
        role = resolve_env_role(sections, env)
    except ValueError as exc:
        print("FAIL: cannot classify env %s: %s" % (env, exc), file=sys.stderr)
        return 1

    try:
        symbols = read_elf_symbols(elf)
    except ElfError as exc:
        print("FAIL: %s" % exc, file=sys.stderr)
        return 1

    problems, matches = check_symbols(symbols, family, role)

    if verbose:
        for rule in RULES:
            if not rule.applies_to(family, role):
                continue
            hit = matches.get(rule.rule_id)
            print("  %-32s %s" % (rule.rule_id, hit if hit else "MISSING"))

    if problems:
        print("FAIL: %s (%s/%s): %d unlinked capability/ies:"
              % (env, family, role, len(problems)), file=sys.stderr)
        for p in problems:
            print("  - %s" % p, file=sys.stderr)
        return 1

    # Coverage is part of the result: an OK that asserted nothing would otherwise look
    # exactly like an OK that asserted everything.
    print("OK: %s (%s/%s), %d symbols, %d/%d capability rules linked."
          % (env, family, role, len(symbols), len(matches),
             sum(1 for r in RULES if r.applies_to(family, role))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
