"""cmdSetChannel consults the occupied-slot guard before it writes (#1865).

ChannelSlotGuard.h decides; test/test_channel_slot_guard tests the decision. This
checks the wiring the native test cannot reach: that MyMesh::cmdSetChannel asks
slotWriteDecision() before setChannel(), returns CmdResult::Occupied on a refusal,
and that CMD_SET_CHANNEL answers it with ERR_CODE_BAD_STATE rather than reporting
success or a bad index. Without it, the guard compiles and does nothing.

Run: python scripts/test_channel_slot_guard_wiring.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MYMESH = ROOT / "examples" / "companion_radio" / "MyMesh.cpp"


def function_body(code, signature):
    start = code.find(signature)
    if start < 0:
        return None
    open_at = code.find("{", start)
    depth, i = 0, open_at
    while i < len(code):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return code[open_at:i + 1]
        i += 1
    return None


def strip_comments(text):
    """Comments out, string and char literals kept intact (a '//' or a brace inside
    a literal must neither cut a line nor skew the brace count)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


# The guard as it must read: asked before the write, with Public's key so a clear of
# Public is refused (#1896), any refusal answered with Occupied, and only for a slot
# that exists (got), so an out-of-range index still reaches NotFound.
GUARD = re.compile(
    r"verdict\s*=\s*got\s*\?\s*offband::slotWriteDecision\(\s*idx\s*,\s*before\.name\s*,\s*before\.channel\.secret\s*,"
    r"\s*name\s*,\s*secret\s*,\s*offband::kPublicChannelKey\s*\)\s*:\s*offband::SlotWrite::Write\s*;"
    r"\s*if\s*\(\s*verdict\s*!=\s*offband::SlotWrite::Write\s*\)\s*\{[^{}]*?return\s+CmdResult::Occupied\s*;",
    re.S)
SCREENS = ROOT / "examples" / "companion_radio" / "ui-gui" / "Screens.cpp"

# #1896: the channel menu offers Leave on every held channel except Public.
LEAVE = re.compile(
    r"is_public\s*=\s*held\s*&&\s*memcmp\(\s*key\s*,\s*offband::kPublicChannelKey\s*,\s*kChannelKeyLen\s*\)\s*==\s*0\s*;"
    r"\s*if\s*\(\s*held\s*&&\s*!is_public\s*\)\s*_menu_rows\[_sheet_count\+\+\]\s*=\s*MenuLeave\s*;",
    re.S)


CHANNELS = ROOT / "examples" / "companion_radio" / "ui-gui" / "ChannelScreens.cpp"


def public_failures(mymesh, channels):
    """#1896's other sites: one Public, in its slot, restored at boot, and "public" typed is Public."""
    out = []
    code = strip_comments(mymesh)
    body = function_body(code, "CmdResult MyMesh::cmdSetChannel(") or ""
    if not re.search(r"if\s*\(\s*writing_public\s*&&\s*idx\s*==\s*offband::kPublicSlot\s*\)\s*\{"
                     r"[^}]*?slotHoldsKey\(\s*other\.channel\.secret\s*,\s*offband::kPublicChannelKey\s*\)", body, re.S):
        out.append("cmdSetChannel() no longer clears an older copy of Public when Public goes to its slot")
    load = code.find("_store->loadChannels(this);")
    repair = code.find("offband::publicRepairSlot(")
    if load < 0 or repair < 0 or repair < load:
        out.append("begin() no longer restores a lost Public after loading the stored channels")
    ch = strip_comments(channels)
    join = function_body(ch, "int GuiShell::chanJoin(") or ""
    if not re.search(r"memcmp\(\s*key\s*,\s*offband::kPublicChannelKey\s*,\s*kChannelKeyLen\s*\)\s*==\s*0\s*\)"
                     r"\s*slot\s*=\s*offband::kPublicSlot\s*;", join):
        out.append("ChannelScreens.cpp: chanJoin() no longer puts Public in its slot")
    submit = function_body(ch, "void GuiShell::chanSubmit(") or ""
    np, ht = submit.find("namesPublic("), submit.find("hashtagName(")
    if np < 0 or ht < 0 or np > ht:
        out.append("ChannelScreens.cpp: chanSubmit() no longer checks for Public before making a hashtag")
    return out


def _public_real():
    return MYMESH.read_text(encoding="utf-8"), CHANNELS.read_text(encoding="utf-8")


def gui_failures(text):
    code = strip_comments(text)
    # Assignments only: "== MenuLeave" is a comparison, not a row being offered.
    if not LEAVE.search(code) or len(re.findall(r"(?<![=!<>])=\s*MenuLeave\s*;", code)) != 1:
        return ["Screens.cpp: the channel menu can offer Leave on Public"]
    return []


def failures(text):
    code = strip_comments(text)
    out = []
    body = function_body(code, "CmdResult MyMesh::cmdSetChannel(")
    if body is None:
        return ["MyMesh.cpp: cmdSetChannel() not found; this check no longer looks at it"]
    m = GUARD.search(body)
    write = body.find("setChannel(idx")
    if not m:
        out.append("cmdSetChannel() no longer asks slotWriteDecision() and returns Occupied on Refuse: "
                   "a channel can be written over another")
    elif write >= 0 and write < m.start():
        out.append("cmdSetChannel() writes the slot before asking slotWriteDecision()")
    if not re.search(r"if\s*\(\s*name\[0\]\s*!=\s*0\s*\)\s*memcpy\(\s*channel\.channel\.secret", body):
        out.append("cmdSetChannel() no longer drops the key on a clear: a nameless slot would keep "
                   "receiving its old channel")
    handler = re.search(r"CMD_SET_CHANNEL && len >= 2 \+ 32 \+ 16\)(.*?)\} else if \(cmd_frame\[0\]", code, re.S)
    if not handler:
        out.append("MyMesh.cpp: the 128-bit CMD_SET_CHANNEL handler not found")
    elif not re.search(r"CmdResult::Occupied\)\s*\{\s*writeErrFrame\(ERR_CODE_BAD_STATE\)", handler.group(1)):
        out.append("CMD_SET_CHANNEL no longer answers a refused overwrite with ERR_CODE_BAD_STATE")
    return out


def test_the_real_tree_passes():
    got = failures(MYMESH.read_text(encoding="utf-8"))
    assert not got, "\n".join(got)


def test_negative_a_dropped_guard_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace("slotWriteDecision(", "slotWriteDecisionX(")
    assert any("no longer asks slotWriteDecision" in f for f in failures(text))


def test_negative_an_inverted_guard_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "if (verdict != offband::SlotWrite::Write) {", "if (verdict == offband::SlotWrite::Write) {", 1)
    assert any("no longer asks slotWriteDecision" in f for f in failures(text))


def test_negative_a_guard_without_the_got_check_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace("got ? offband::slotWriteDecision(",
                                                      "true ? offband::slotWriteDecision(", 1)
    assert any("no longer asks slotWriteDecision" in f for f in failures(text))


def test_negative_a_guard_without_publics_key_is_caught():
    # #1896: without it, the guard lets a client clear Public.
    text = re.sub(r"(slotWriteDecision\([^;]*?)offband::kPublicChannelKey", r"\1nullptr",
                  MYMESH.read_text(encoding="utf-8"), count=1, flags=re.S)
    assert any("no longer asks slotWriteDecision" in f for f in failures(text))


def test_negative_a_guard_told_the_wrong_slot_is_caught():
    # #1896: the slot decides whether Public may land, so it must be the slot written.
    text = MYMESH.read_text(encoding="utf-8").replace("slotWriteDecision(idx,", "slotWriteDecision(0,", 1)
    assert any("no longer asks slotWriteDecision" in f for f in failures(text))


def test_negative_a_commented_out_guard_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace("const offband::SlotWrite verdict",
                                                      "/* const offband::SlotWrite verdict", 1)
    text = text.replace("return CmdResult::Occupied;\n  }", "return CmdResult::Occupied;\n  } */", 1)
    assert any("no longer asks slotWriteDecision" in f for f in failures(text))


def test_the_public_sites_are_wired():
    got = public_failures(*_public_real())
    assert not got, "\n".join(got)


def test_negative_no_clearing_of_older_public_copies_is_caught():
    mymesh, channels = _public_real()
    bad = mymesh.replace("if (writing_public && idx == offband::kPublicSlot) {", "if (false) {", 1)
    assert any("older copy of Public" in f for f in public_failures(bad, channels))


def test_negative_no_boot_repair_is_caught():
    mymesh, channels = _public_real()
    bad = mymesh.replace("offband::publicRepairSlot(", "offband::publicRepairSlotX(", 1)
    assert any("restores a lost Public" in f for f in public_failures(bad, channels))


def test_negative_chanjoin_without_publics_slot_is_caught():
    mymesh, channels = _public_real()
    bad = channels.replace("slot = offband::kPublicSlot;", "slot = -1;", 1)
    assert any("chanJoin()" in f for f in public_failures(mymesh, bad))


def test_negative_hashtag_before_public_check_is_caught():
    mymesh, channels = _public_real()
    bad = channels.replace("if (namesPublic(_chan_buf.text())) {", "if (false) {", 1)
    assert any("chanSubmit()" in f for f in public_failures(mymesh, bad))


def test_the_gui_never_offers_leave_on_public():
    got = gui_failures(SCREENS.read_text(encoding="utf-8"))
    assert not got, "\n".join(got)


def test_negative_leave_offered_on_public_is_caught():
    text = SCREENS.read_text(encoding="utf-8").replace(
        "if (held && !is_public) _menu_rows[_sheet_count++] = MenuLeave;",
        "if (held) _menu_rows[_sheet_count++] = MenuLeave;", 1)
    assert gui_failures(text)


def test_negative_a_second_unguarded_leave_row_is_caught():
    text = SCREENS.read_text(encoding="utf-8").replace(
        "if (held && !is_public) _menu_rows[_sheet_count++] = MenuLeave;",
        "if (held && !is_public) _menu_rows[_sheet_count++] = MenuLeave;\n    "
        "if (held) _menu_rows[_sheet_count++] = MenuLeave;", 1)
    assert gui_failures(text)


def test_negative_a_clear_that_keeps_its_key_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "if (name[0] != 0) memcpy(channel.channel.secret", "memcpy(channel.channel.secret", 1)
    assert any("drops the key on a clear" in f for f in failures(text))


def test_literals_do_not_confuse_the_parser():
    assert strip_comments('a("http://x{") // b\nc') == 'a("http://x{") \nc'


def test_negative_a_refusal_reported_as_not_found_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "writeErrFrame(ERR_CODE_BAD_STATE); // #1865", "writeErrFrame(ERR_CODE_NOT_FOUND); // #1865")
    assert any("ERR_CODE_BAD_STATE" in f for f in failures(text))


def test_negative_writing_before_the_guard_is_caught():
    text = ("CmdResult MyMesh::cmdSetChannel(uint8_t idx, const char* name, const uint8_t secret[16]) {\n"
            "  setChannel(idx, channel);\n"
            "  const offband::SlotWrite verdict = got ? offband::slotWriteDecision(idx, before.name,\n"
            "      before.channel.secret, name, secret, offband::kPublicChannelKey) : offband::SlotWrite::Write;\n"
            "  if (verdict != offband::SlotWrite::Write) {\n    return CmdResult::Occupied;\n  }\n"
            "  if (name[0] != 0) memcpy(channel.channel.secret, secret, 16);\n}\n")
    assert any("before asking" in f for f in failures(text))


if __name__ == "__main__":
    n_fail = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("PASS %s" % name)
            except AssertionError as e:
                n_fail += 1
                print("FAIL %s: %s" % (name, e))
    print("\n%d failure(s)" % n_fail)
    sys.exit(1 if n_fail else 0)
