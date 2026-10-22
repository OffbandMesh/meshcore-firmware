"""Every channel the radio holds is in the GUI's Chats, messages or not (#1741).

The store makes a channel's thread on first use. Before #1741 the only uses were a
message arriving or being sent, so a fresh radio's Public, or any channel nobody had
posted on, never appeared in Chats and could not be written to. Two call sites fix it,
and this checks both stay:

    MyMesh::begin() walks every channel slot and calls guiThreadForChannel() for each
    named one, after the first-boot preflash seeds, inside #ifdef OFFBAND_GUI
    MyMesh::cmdSetChannel() calls guiThreadForChannel() for a channel the app adds

Source assertions, not behavior: the bench (#1730) is what shows Public in Chats.

Run: python scripts/test_gui_channel_threads.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MYMESH = ROOT / "examples" / "companion_radio" / "MyMesh.cpp"


def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def body_of(code, signature):
    """The brace-matched body of the first function whose header matches `signature`."""
    m = re.search(signature, code)
    if not m:
        return None
    i = code.index("{", m.end())
    depth = 0
    for j in range(i, len(code)):
        if code[j] == "{":
            depth += 1
        elif code[j] == "}":
            depth -= 1
            if depth == 0:
                return code[i:j + 1]
    return None


BOOT_LOOP = re.compile(
    r"#ifdef\s+OFFBAND_GUI\s*\n\s*for\s*\(\s*int\s+(\w+)\s*=\s*0\s*;\s*\1\s*<\s*MAX_GROUP_CHANNELS\s*;[^)]*\)\s*\{"
    r"[^}]*getChannel\(\s*\1\s*,\s*(\w+)\s*\)\s*&&\s*\2\.name\[0\]\s*!=\s*0\s*\)\s*guiThreadForChannel\(")


def failures(mymesh):
    out = []
    code = strip_comments(mymesh)
    begin = body_of(code, r"void\s+MyMesh::begin\s*\(\s*bool\s+\w+\s*\)")
    if begin is None:
        return ["MyMesh::begin() not found: this check cannot see what it protects"]
    loop = BOOT_LOOP.search(begin)
    if not loop:
        out.append("MyMesh::begin() must give every named channel a Chats thread (guiThreadForChannel), "
                   "inside #ifdef OFFBAND_GUI")
    else:
        seeds = begin.find("OFFBAND_PREFLASH_CHANNELS")
        if seeds >= 0 and loop.start() < seeds:
            out.append("the channel threads must be made after the preflash seeds, or a seeded channel is missed")
    setch = body_of(code, r"CmdResult\s+MyMesh::cmdSetChannel\s*\(")
    if setch is None or "guiThreadForChannel(" not in setch:
        out.append("MyMesh::cmdSetChannel() must give a channel the app adds its Chats thread")
    return out


UITASK = ROOT / "examples" / "companion_radio" / "ui-gui" / "UITask.cpp"
CHANSCREENS = ROOT / "examples" / "companion_radio" / "ui-gui" / "ChannelScreens.cpp"


def add_failures(uitask, screens):
    """Adding a channel on the device (Epic R #1743, R2 #1749).

    UITask::addChannel goes through cmdSetChannel, the app's own path, so a channel
    added on the radio gets the same notification default, tune and Chats thread as
    one added from the phone. A key the radio already holds is found and opened,
    never written again: the write would rename the user's channel.
    """
    out = []
    add = body_of(strip_comments(uitask), r"int\s+UITask::addChannel\s*\(")
    if add is None:
        return ["UITask::addChannel() not found: this check cannot see what it protects"]
    held = add.find("memcmp(")
    write = add.find("cmdSetChannel(")
    if write < 0:
        out.append("UITask::addChannel() must add through cmdSetChannel, the app's own path")
    elif held < 0 or held > write:
        out.append("UITask::addChannel() must check for the key already being held before it writes, "
                   "or rejoining a channel renames it")
    join = body_of(strip_comments(screens), r"int\s+GuiShell::chanJoin\s*\(")
    if join is None or join.find("findChannelByKey(") < 0 or \
            join.find("findChannelByKey(") > join.find("firstFreeChannelSlot("):
        out.append("chanJoin() must look for the key before taking a free slot, or a channel is doubled")
    return out


def test_the_real_tree_passes():
    got = failures(MYMESH.read_text(encoding="utf-8"))
    got += add_failures(UITASK.read_text(encoding="utf-8"), CHANSCREENS.read_text(encoding="utf-8"))
    assert not got, "\n".join(got)


def test_negative_adding_around_the_apps_path_is_caught():
    ui = UITASK.read_text(encoding="utf-8").replace("the_mesh.cmdSetChannel(", "the_mesh.setChannelRaw(")
    got = add_failures(ui, CHANSCREENS.read_text(encoding="utf-8"))
    assert any("through cmdSetChannel" in f for f in got), got


def test_negative_rewriting_a_held_channel_is_caught():
    real = UITASK.read_text(encoding="utf-8")
    broken = real.replace("memcmp(held.channel.secret, key, kChannelKeyLen) == 0", "false")
    assert broken != real, "sanity: the held-key check must be in the tree"
    got = add_failures(broken, CHANSCREENS.read_text(encoding="utf-8"))
    assert any("renames it" in f for f in got), got


def test_negative_a_doubled_channel_is_caught():
    real = CHANSCREENS.read_text(encoding="utf-8")
    broken = real.replace("int slot = findChannelByKey(slots, n, key);\n  if (slot < 0) slot = firstFreeChannelSlot(slots, n);",
                          "int slot = firstFreeChannelSlot(slots, n);")
    assert broken != real, "sanity: chanJoin's lookup must be in the tree"
    got = add_failures(UITASK.read_text(encoding="utf-8"), broken)
    assert any("doubled" in f for f in got), got


def _fails_with(fragment, text):
    got = failures(text)
    assert any(fragment in f for f in got), "expected a failure naming %r, got %s" % (fragment, got)


def test_negative_no_threads_at_boot_is_caught():
    real = MYMESH.read_text(encoding="utf-8")
    broken = real.replace("if (getChannel(i, ch) && ch.name[0] != 0) guiThreadForChannel(uint8_t(i));", "")
    assert broken != real, "sanity: the boot loop's call must be in the tree"
    _fails_with("every named channel a Chats thread", broken)


def test_negative_an_app_added_channel_without_a_thread_is_caught():
    real = MYMESH.read_text(encoding="utf-8")
    broken = real.replace("if (name[0] != 0) guiThreadForChannel(idx);", "")
    assert broken != real, "sanity: cmdSetChannel's call must be in the tree"
    _fails_with("cmdSetChannel() must give", broken)


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
