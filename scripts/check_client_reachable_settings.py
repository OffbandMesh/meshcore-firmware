#!/usr/bin/env python3
"""Guard: every setting the Offband screen set can write is reachable from a client (#1792).

WHY THIS EXISTS
    A board with no keyboard, or no screen at all, is configured from the companion
    app. Epic #1788 found three settings a badge's own screens could change and no client
    could reach -- GPS mode, a channel's tone, the custom tune slots -- and found them by
    hand, late, because someone held a pyramid badge with no keyboard. Nothing in CI knew
    that a setting a screen writes must also be writable over the companion protocol, so
    the next one would have gone missing the same way.

WHAT "REACHABLE" MEANS HERE -- and what it does not
    #1792 was first written to pair screen-written prefs against their CLI bindings
    (`def("...", _parent->field)` in NodePrefs.h / CommonCLI.h). That premise was wrong:
    a CLI binding is NOT a client path. `gps.mode` had a binding and was still unreachable,
    because the companion protocol does not expose that namespace (#1788, correction
    comment). A guard built on bindings would have passed while GPS mode was missing,
    which is the false assurance this guard exists to remove.

    So this checks the thing that matters: for each setting, an EVIDENCE list of source
    lines that together form the client's write path -- the companion handler that
    receives the command AND the code that performs the write. Every line must be present.
    A refactor that silently deletes a client path fails here even though the screen
    still writes the setting.

WHAT IT SCANS
    Writers: the screen set, examples/companion_radio/ui-new/*.cpp and *.h. Two shapes:
      - a direct write to a NodePrefs member: `prefs->x = ...`, `_node_prefs->x++`, ...
        (member names are parsed from NodePrefs.h, so a new pref is seen automatically);
      - a call to a writer that stores a setting OUTSIDE NodePrefs -- a channel's tone
        lives on the channel record, the custom slots are files, GPS mode goes through
        MyMesh. #1792's draft said this class could not be caught; it can, by name, and
        WRITER_CALLS below is that list.

    A setting found by the scan must appear in REACHABLE (with evidence) or EXEMPT (with
    a reason). Neither: fail. Evidence missing: fail.

    And every setter-shaped call the screens make through the_mesh, the UI task or
    offband (`set...(`, `save...(`, `clear...(`, `toggle...(` ...) must be in WRITER_CALLS
    or in NOT_A_SETTING with a reason -- so a NEW writer of a setting stored outside
    NodePrefs fails here until someone decides what it is, rather than slipping past.

    Direct writes are found through a pointer or a reference (`->x`, `.x`), across line
    breaks, as in-place updates (`++`, `+=`, `<<=` ...), and as the destination of a memory
    or string writer (`memcpy(&p->x, ...)`, `strcpy(p->name, ...)`). A write through a
    pointer-to-member (`p->*m = id`) names no setting, so it is reported as unattributed
    and fails until the writer holding it is listed in WRITER_CALLS.

WHAT IT DOES NOT COVER
    - Writes hidden behind a macro. This reads source text and does not preprocess, apart
      from dropping `#if 0` blocks.
    - A writer called UNQUALIFIED, or through an object other than the_mesh, the UI task
      or offband. The registry above sees only those three routes.
    - A whole-struct copy written back (`*prefs = copy;`). The member writes to the copy
      are still seen, so the setting is still checked.
    - Control flow. Evidence proves the handler's call, the hook and the write line are
      all present -- not that one reaches the other. Behavior is the bench's to prove.
    - The GUI (ui-gui) and the other UI families. Their own settings screens, where they
      exist, are not in scope yet.
    - Whether the client APP exposes the setting. This proves the firmware accepts it;
      the app is OffbandMesh/meshcore-client's to check.

Run: python scripts/check_client_reachable_settings.py
Exit 0 = every screen-written setting is reachable or exempt, 1 = a gap (or unparsable).
"""
from __future__ import annotations
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NODEPREFS_REL = Path("examples") / "companion_radio" / "NodePrefs.h"
SCREEN_GLOBS = (
    "examples/companion_radio/ui-new/*.cpp",
    "examples/companion_radio/ui-new/*.h",
)

MYMESH = "examples/companion_radio/MyMesh.cpp"
UI_NEW = "examples/companion_radio/ui-new/UITask.cpp"
ENGINE = "examples/companion_radio/ToneEngine.h"

# Settings stored outside NodePrefs, found by the call that writes them.
WRITER_CALLS = {
    "setStoredTone": "event tones (ui_tone_*)",
    "setChannelTune": "channel tone",
    "saveSlotRtttl": "custom tune slots",
    "clearSlot": "custom tune slots",
    "setGpsMode": "gps mode",
}

# Both text sizes are written by one statement through a reference, so the evidence is
# the whole of it -- the binding, the change check and the write, in order. A lone
# `field = size;` would match any unrelated line that happened to use those names.
_TEXT_SIZE_WRITE = (
    r"uint8_t&\s*field\s*=\s*message\s*\?\s*_node_prefs->ui_msg_text_size\s*:\s*"
    r"_node_prefs->ui_text_size\s*;\s*if\s*\(\s*field\s*!=\s*size\s*\)\s*\{\s*field\s*=\s*size\s*;"
)

# setting -> the lines that make up its client write path. Every one must match.
REACHABLE: dict[str, list[tuple[str, str]]] = {
    "notify_scope": [
        (MYMESH, r"sub\s*==\s*offband::OFFBAND_UI_SCOPE_SET"),
        (MYMESH, r"_prefs\.notify_scope\s*=\s*want\b"),
    ],
    "buzzer_quiet": [
        (MYMESH, r"sub\s*==\s*offband::OFFBAND_UI_SCOPE_SET"),
        (MYMESH, r"_prefs\.buzzer_quiet\s*=\s*\(want\s*==\s*NOTIFY_SCOPE_NONE\)"),
    ],
    "gps_enabled": [
        (MYMESH, r'strcmp\(sp,\s*"gps"\)\s*==\s*0'),
        (MYMESH, r"_prefs\.gps_enabled\s*=\s*\(np\[0\]\s*==\s*'1'\)"),
    ],
    "gps mode": [
        (MYMESH, r"sub\s*==\s*offband::OFFBAND_UI_GPS_SET"),
        (MYMESH, r"setGpsMode\(\(offband::GpsMode\)cmd_frame\[2\]\)"),
    ],
    # The screen settings go MyMesh -> a UI client* hook -> the write. Each entry cites the
    # handler's call, the hook, and the line in the hook's path that writes the field, so
    # emptying a hook's body fails here as surely as deleting the handler. What a regex
    # cannot prove is control flow between them; the behavior is the bench's to prove.
    "ui_text_size": [
        (MYMESH, r"_ui->clientSetTextSize\("),
        (UI_NEW, r"bool UITask::clientSetTextSize\("),
        (UI_NEW, _TEXT_SIZE_WRITE),
    ],
    "ui_msg_text_size": [
        (MYMESH, r"_ui->clientSetTextSize\(cmd_frame\[2\]\s*==\s*1"),
        (UI_NEW, _TEXT_SIZE_WRITE),
    ],
    "ui_screen_secs": [
        (MYMESH, r"_ui->clientSetScreenOff\("),
        (UI_NEW, r"bool UITask::clientSetScreenOff\("),
        (UI_NEW, r"\bsetAutoOffSecs\(secs\)\s*;"),
        (UI_NEW, r"_node_prefs->ui_screen_secs\s*=\s*secs\s*;"),
    ],
    "ui_tz": [
        (MYMESH, r"_ui->clientSetTimeZone\("),
        (UI_NEW, r"bool UITask::clientSetTimeZone\("),
        (UI_NEW, r"_node_prefs->ui_tz\s*=\s*tz\s*;"),
    ],
    "batt_full_mv": [
        (MYMESH, r"_ui->clientPinBattFull\(\)"),
        (MYMESH, r"_ui->clientClearBattFull\(\)"),
        (UI_NEW, r"uint8_t UITask::clientPinBattFull\("),
        (UI_NEW, r"if\s*\(setBattFullMilliVolts\(mv\)\)\s*return 0"),
        (UI_NEW, r"_node_prefs->batt_full_mv\s*=\s*mv\s*;"),
        (UI_NEW, r"_node_prefs->batt_full_mv\s*=\s*0\s*;"),
    ],
    "batt_full_user": [
        (MYMESH, r"_ui->clientPinBattFull\(\)"),
        (MYMESH, r"_ui->clientClearBattFull\(\)"),
        (UI_NEW, r"bool UITask::clientClearBattFull\("),
        (UI_NEW, r"_node_prefs->batt_full_user\s*=\s*1\s*;"),
        (UI_NEW, r"_node_prefs->batt_full_user\s*=\s*0\s*;"),
    ],
    "event tones (ui_tone_*)": [
        (MYMESH, r"tones->setEventTone\(cmd_frame\[2\],\s*cmd_frame\[3\]\)"),
        (ENGINE, r"return setStoredTone\(_prefs,"),
    ],
    "channel tone": [
        (MYMESH, r"sub\s*==\s*offband::OFFBAND_UI_CHTUNE_SET"),
        (MYMESH, r"setChannelTune\(cmd_frame\[2\],\s*cmd_frame\[3\]\)"),
    ],
    "custom tune slots": [
        (MYMESH, r"tones->feedSlot\("),
        (ENGINE, r"saveSlotRtttl\(_store,\s*slot,"),
    ],
}

# setting -> why a client has no business writing it. A reason, never a bare name.
EXEMPT: dict[str, str] = {}

# Every setter-shaped call the screens make through the_mesh, the UI task or offband --
# `set...(`, `save...(`, `clear...(`, `toggle...(` and the like -- must be either a writer
# in WRITER_CALLS or listed here with the reason it needs no client path of its own. This
# is what keeps WRITER_CALLS from being a silent bottleneck: a new writer the screens call
# fails until someone decides which list it belongs on.
NOT_A_SETTING: dict[str, str] = {
    "savePrefs": "persists NodePrefs; the fields it saves are scanned directly",
    "storedTone": "a reader -- returns the stored tune id, writes nothing",
    "setLedActivitySink": "registers a callback for the activity LED, stores no setting",
    "applyBuzzerMute": "pushes buzzer_quiet to the driver; buzzer_quiet itself is scanned",
    "toggleGPS": "its body writes gps_enabled, which the field scan sees and checks",
    "setAutoOffSecs": "its body writes ui_screen_secs, which the field scan sees and checks",
    "setBattFullMilliVolts": "its body writes batt_full_mv and batt_full_user, which are scanned",
    "clearBattFullMilliVolts": "its body writes batt_full_mv and batt_full_user, which are scanned",
    "enableBluetooth": "runtime radio state, not stored -- it does not survive a reboot, and "
                       "a client connected over BLE has no use for switching BLE off",
    "disableBluetooth": "runtime radio state, not stored -- it does not survive a reboot, and "
                        "a client connected over BLE has no use for switching BLE off",
}
SETTER_CALL_RE = re.compile(
    r"\b(?:the_mesh\s*\.|_task\s*->|offband\s*::)\s*"
    r"((?:set|save|clear|store|write|toggle|enable|disable|apply|reset)[A-Za-z0-9_]*)\s*\("
)

# A NodePrefs member is any `type name ...;` line inside the struct. Arrays count.
MEMBER_RE = re.compile(
    r"^\s*(?:u?int(?:8|16|32|64)_t|float|double|bool|char)\s+([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;",
    re.MULTILINE,
)
# Comments, and string, raw-string or character literals, in one pass -- so `//` inside
# a string ("http://...", R"(http://...)") is left alone, and a `"` inside a comment does
# not open a string. Raw strings come first: `R"(` would otherwise read as a plain string.
_TOKEN_RE = re.compile(
    r'R"([^(\s"\\]{0,16})\(.*?\)\1"'
    # A `//` comment ending in a backslash continues onto the next line (a line splice),
    # so the compiler treats both lines as comment, and so does this.
    r'|//(?:[^\n\\]|\\\r?\n|\\.)*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'',
    re.DOTALL,
)
# Code switched off with `#if 0`. Evidence inside one proves nothing, and a write inside
# one is not a write. Nested conditionals inside the block are not tracked.
_IF0_RE = re.compile(r"^[ \t]*#[ \t]*if[ \t]+0\b.*?^[ \t]*#[ \t]*(?:endif|else)\b", re.DOTALL | re.MULTILINE)


def strip_comments(text: str) -> str:
    """Comments and `#if 0` blocks removed, literals kept, newlines preserved so line
    numbers still point at the source."""
    def repl(m: re.Match) -> str:
        tok = m.group(0)
        if tok.startswith("//"):
            return "\n" * tok.count("\n")   # a spliced comment keeps its line breaks
        if tok.startswith("/*"):
            return "\n" * tok.count("\n")
        return tok
    text = _TOKEN_RE.sub(repl, text)
    return _IF0_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)


def nodeprefs_members(root: Path = ROOT) -> set[str]:
    text = strip_comments((root / NODEPREFS_REL).read_text(encoding="utf-8", errors="replace"))
    return set(MEMBER_RE.findall(text))


#: The setting name reported for a write the scanner sees but cannot attribute.
UNATTRIBUTED = "pointer-to-member write (cannot tell which setting)"

# Words that can sit before a call without making it a definition.
_NOT_A_TYPE = {"return", "else", "do", "case", "throw", "co_return", "and", "or", "not"}


def is_definition(text: str, start: int) -> bool:
    """Is the identifier at `start` being declared or defined, rather than called?

    A definition has a TYPE immediately before the name (`bool f(`, `uint8_t f(`,
    `static void\\nf(`); a call has an operator, a scope, a member access, a keyword or
    nothing (`x = f(`, `the_mesh.f(`, `offband::f(`, `return f(`, `f(` alone)."""
    before = text[:start].rstrip()
    # A qualified name -- `MyMesh::f(`, `offband::f(` -- is decided by what comes before the
    # qualifier: `bool MyMesh::f(` is a definition, `return offband::f(` is a call.
    while True:
        q = re.search(r"([A-Za-z_]\w*)\s*::\s*$", before)
        if not q:
            break
        before = before[:q.start()].rstrip()
    if not before or before[-1] in "=(,!&|?:;{}+-<>.":
        return False                       # an operator, a scope or member access, a statement start
    if before.endswith("->"):
        return False
    m = re.search(r"([A-Za-z_]\w*)\s*[*&]*$", before)
    return bool(m) and m.group(1) not in _NOT_A_TYPE


def screen_writes(root: Path = ROOT) -> dict[str, list[str]]:
    """setting -> ['file:line', ...] for every write the screen set makes."""
    members = nodeprefs_members(root)
    if not members:
        raise RuntimeError(f"no members parsed from {NODEPREFS_REL}")
    alt = "|".join(sorted(members, key=len, reverse=True))
    # `->member` or `.member` -- a pointer or a reference/value -- then an assignment or
    # an in-place update. `\s` spans newlines, so a write split across lines is one match.
    # `=(?!=)` excludes `==`; `>=`, `<=` and `!=` never match because the character before
    # their `=` is not whitespace or a member name.
    write_re = re.compile(
        r"(?:->|\.)\s*(" + alt + r")\b\s*(?:\[[^\]]*\])?\s*"
        r"(?:=(?!=)|\+\+|--|<<=|>>=|[-+*/%|&^]=)"
    )
    # A member handed to a memory or string writer: memcpy(&p->x, ...), strcpy(p->name, ...).
    memwrite_re = re.compile(
        r"\b(?:memcpy|memmove|memset|strcpy|strncpy|strcat|strncat|snprintf|sprintf)\s*\(\s*&?\s*"
        r"[A-Za-z_][\w\[\]]*\s*(?:->|\.)\s*(" + alt + r")\b"
    )
    # A member's ADDRESS taken anywhere -- set_value(&p->x, 1) -- is a write the scanner
    # cannot see the end of, so it is counted as one. Conservative on purpose: a false
    # alarm costs an EXEMPT line; a miss costs a setting nobody can reach. `&&` is logical
    # and, never address-of, and is excluded on both sides.
    addr_re = re.compile(
        r"(?<![&\w)\]])&(?!&)\s*[A-Za-z_][\w\[\]]*\s*(?:->|\.)\s*(" + alt + r")\b(?!\s*\()"
    )
    # A write through a pointer-to-member names no setting at all, so it is reported as
    # one the guard cannot attribute -- the writer that holds it belongs in WRITER_CALLS.
    ptm_re = re.compile(r"(?:->|\.)\*\s*[A-Za-z_]\w*\s*(?:=(?!=)|\+\+|--|[-+*/%|&^]=)")
    call_re = re.compile(r"\b(" + "|".join(WRITER_CALLS) + r")\s*\(")
    found: dict[str, list[str]] = {}

    def add(setting: str, rel: str, text: str, pos: int) -> None:
        found.setdefault(setting, []).append(f"{rel}:{text.count(chr(10), 0, pos) + 1}")

    for pattern in SCREEN_GLOBS:
        for path in sorted(root.glob(pattern)):
            rel = path.relative_to(root).as_posix()
            text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
            for m in write_re.finditer(text):
                add(m.group(1), rel, text, m.start())
            for m in memwrite_re.finditer(text):
                add(m.group(1), rel, text, m.start())
            for m in addr_re.finditer(text):
                add(m.group(1), rel, text, m.start())
            for m in ptm_re.finditer(text):
                add(UNATTRIBUTED, rel, text, m.start())
            for m in call_re.finditer(text):
                if not is_definition(text, m.start()):
                    add(WRITER_CALLS[m.group(1)], rel, text, m.start())
    return found


def unregistered_setters(root: Path = ROOT) -> list[str]:
    """Setter-shaped calls from the screens that are on neither list, as 'name at file:line'."""
    out = []
    for pattern in SCREEN_GLOBS:
        for path in sorted(root.glob(pattern)):
            rel = path.relative_to(root).as_posix()
            text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
            for m in SETTER_CALL_RE.finditer(text):
                name = m.group(1)
                if name in WRITER_CALLS or name in NOT_A_SETTING:
                    continue
                out.append(f"{name} at {rel}:{text.count(chr(10), 0, m.start()) + 1}")
    return out


def missing_evidence(setting: str, root: Path = ROOT) -> list[str]:
    misses = []
    for rel, pattern in REACHABLE.get(setting, []):
        path = root / rel
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        if not re.search(pattern, strip_comments(text)):
            misses.append(f"{rel}: /{pattern}/")
    return misses


def check(root: Path = ROOT) -> list[str]:
    """Problems found, as human-readable lines. Empty = clean."""
    problems = []
    for setting, where in sorted(screen_writes(root).items()):
        if setting in EXEMPT:
            continue
        if setting not in REACHABLE:
            problems.append(
                f"{setting}: written by the screens ({', '.join(where[:3])}) with no client "
                f"path. Add a client write path and list it in REACHABLE, or add it to "
                f"EXEMPT with the reason a client must not set it.")
            continue
        for miss in missing_evidence(setting, root):
            problems.append(f"{setting}: client path evidence gone -- {miss}")
    for call in unregistered_setters(root):
        problems.append(
            f"unregistered writer: {call}. If it stores a setting, add it to WRITER_CALLS "
            f"(and give the setting a client path); if not, add it to NOT_A_SETTING with why.")
    return problems


def main(root: Path = ROOT) -> int:
    try:
        problems = check(root)
        writes = screen_writes(root)
    except (OSError, RuntimeError) as e:
        print(f"check_client_reachable_settings: cannot run: {e}")
        return 1
    if problems:
        print("Settings the screens write that a client cannot reach (#1792):")
        for p in problems:
            print(f"  {p}")
        return 1
    print(f"OK: {len(writes)} screen-written settings, all reachable from a client "
          f"({len(EXEMPT)} exempt).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
