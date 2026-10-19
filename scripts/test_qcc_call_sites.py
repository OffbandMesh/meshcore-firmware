"""Call-site guards for the badge .cpp invariants no passing test can reach (#1639).

Four invariants where THE RULE IS TESTED AND THE USE OF THE RULE IS NOT. That gap is the
specific way a green suite coexists with a broken badge: 49 trigger cases, 15 buzzer-
priority cases, 8 store cases and 8 keynav cases all keep passing while the badge stops
working, because every one of them exercises a pure header and none of them exercises the
line that calls it.

    1  UITask::playTriggerTune() must ask for TonePriority::Trigger
       test_buzzer_priority proves Trigger outranks Event. Change one token at the call
       site and the 18.9 s payload is truncated by the next arriving message -- in a busy
       channel, every time -- with 15/15 still green. #1608 exists to prevent exactly this.

    2  the channel path must hand the rule the WIRE text, not the stored sender
       BadgeStore::kSenderLen is 16, so a stored sender truncates at 15 bytes and the
       satellite mark is 4 of them before any variation selector. Pass the stored sender
       and all 49 trigger tests still pass while the trigger silently stops working for
       any sender whose name is long. Nothing on any screen would reveal it.

    3  the badge UI must keep PREPENDING the sigil
       #1624 was fixed by stripping in the store and deliberately keeping the UI's
       prepend -- the sigil is what distinguishes a channel row from a contact row.
       test_badge_store covers the strip half. Remove the prepend and channels render
       bare, contacts lose their '@', and the store tests do not care.

    4  the three screens must let a back-out THROUGH
       #1590: SettingsScreen, BatteryScreen and GpsScreen must not swallow backsOut().
       test_keynav covers the rule; the screens are untested .cpp. Restore
       `return from_button` and a badge with no keyboard FITTED is stranded in Settings
       again -- on the unit kind that ships to patrons.

WHAT THESE ARE, AND THEIR HONEST LIMIT. Source assertions against call sites, the way
test_qcc_variant_pins.py already works. That is WEAKER than a unit test: they check that
text is present, so a refactor that preserves the text while changing behaviour passes,
and a rename false-positives. TRIPWIRES. Do not count them as behavioural coverage.

Where a thing could be lifted into a pure header and genuinely tested instead, it has
been -- #4's actual decision already lives in keynav::tripleClickKey and is covered by
test_keynav. What remains unreachable is whether the screens CALL it, which is what these
check.

Run: python scripts/test_qcc_call_sites.py
  or python -m pytest scripts/test_qcc_call_sites.py -q
"""
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# Imported rather than copied, deliberately. A second definition of "what is a comment"
# is the same copies-not-links problem test_qcc_badge_env.py exists to catch, and this
# file needed the stripper for a concrete reason: MyMesh.cpp NAMES
# messageFiresTriggerOnChannelText() in a comment explaining why it gets the whole text,
# and the first draft of the guard below counted that prose as a second call site.
from test_qcc_variant_pins import strip_c_comments  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
UITASK = ROOT / "examples" / "companion_radio" / "ui-new" / "UITask.cpp"
SCREENS = ROOT / "examples" / "companion_radio" / "ui-new" / "BadgeScreens.cpp"
MYMESH = ROOT / "examples" / "companion_radio" / "MyMesh.cpp"
STORE = ROOT / "src" / "helpers" / "BadgeStore.h"


def function_body(src, qualified):
    """One `Class::method` body, from its opening brace to the next definition at column 0.

    The leading return type is optional so a constructor is matchable too -- requiring one
    is how the #1637 guard first scoped itself away from its own landmark.
    """
    sig = r"^(?:[A-Za-z_][\w:<>,*&\s]*?\s+)?%s\s*\("
    m = re.search(sig % re.escape(qualified), src, re.M)
    if not m:
        return None
    brace = src.index("{", m.end() - 1)
    rest = src[brace + 1:]
    nxt = re.search(sig % r"[\w:]+", rest, re.M)
    return rest[: nxt.start()] if nxt else rest


# --- 1: the trigger tune's priority -----------------------------------------------------

def trigger_priority_failures(uitask_src):
    uitask_src = strip_c_comments(uitask_src)
    bad = []
    body = function_body(uitask_src, "UITask::playTriggerTune")
    if body is None:
        return ["UITask::playTriggerTune() not found -- this guard cannot see its target"]

    if "TonePriority::Trigger" not in body:
        bad.append(
            "playTriggerTune() does not ask for TonePriority::Trigger. The payload is "
            "18.9 s; at any lower priority the next arriving message truncates it, which "
            "in a busy channel means every time. test_buzzer_priority stays 15/15 green "
            "either way -- it tests the ordering, not this call (#1608).")

    others = sorted(set(re.findall(r"TonePriority::(\w+)", body)) - {"Trigger"})
    if others:
        bad.append(
            "playTriggerTune() also names TonePriority::%s. It must ask for Trigger and "
            "nothing else, or which priority wins depends on a branch."
            % ", TonePriority::".join(others))

    if "kTuneTrigger" not in body:
        bad.append(
            "playTriggerTune() no longer plays offband::kTuneTrigger -- it would sound "
            "some other tune at the trigger's priority.")
    return bad


# --- 2: the channel path hands over the wire text ---------------------------------------

CHANNEL_RULE = "messageFiresTriggerOnChannelText"
DM_RULE = "messageFiresTrigger"


def wire_text_failures(mymesh_src):
    mymesh_src = strip_c_comments(mymesh_src)
    bad = []
    calls = re.findall(CHANNEL_RULE + r"\s*\(\s*([^)]*?)\s*\)", mymesh_src)
    if not calls:
        bad.append(
            "%s is never called. The channel path is how the trigger fires in a hall of "
            "strangers -- a DM needs a prior advert exchange." % CHANNEL_RULE)
        return bad
    if len(calls) > 1:
        bad.append("%s is called %d times; only the channel receive path should call it, "
                   "and a second call makes this guard ambiguous." % (CHANNEL_RULE, len(calls)))

    for arg in calls:
        if arg.strip() != "text":
            bad.append(
                "%s is called with %r, not the whole `text`. It MUST get the wire form "
                "\"<name>: <body>\" and do its own split: BadgeStore::kSenderLen is 16, so "
                "a stored sender truncates at 15 bytes and the satellite mark is 4 of them. "
                "Handing it a stored sender works for short names and fails silently for "
                "long ones, and all 49 trigger tests still pass." % (CHANNEL_RULE, arg.strip()))

    # The DM path is the other half, and it is the one that legitimately takes a name --
    # ContactInfo::from.name, which is not the truncated store copy.
    dm = re.findall(r"(?<!OnChannelText)\b" + DM_RULE + r"\s*\(\s*([^)]*?)\s*\)", mymesh_src)
    dm = [a for a in dm if not a.startswith("text")]
    if not dm:
        bad.append("%s(from.name, text) is never called -- the DM path is gone." % DM_RULE)
    else:
        for arg in dm:
            if "from.name" not in arg:
                bad.append(
                    "the DM trigger call passes %r. It must read from.name, which is the "
                    "advert name rather than the store's truncated copy." % arg)
    return bad


# --- 3: the UI prepends, the store strips -----------------------------------------------

def sigil_failures(screens_src, store_src):
    screens_src = strip_c_comments(screens_src)
    store_src = strip_c_comments(store_src)
    bad = []

    # The store strips, so a name is held once without its sigil (#1624).
    #
    # Checked at the WRITE, not by looking for the helper's name anywhere in the file.
    # nameWithoutSigil is DEFINED in this header, so `"nameWithoutSigil(" in store_src`
    # stays true after the call is removed -- the first draft of this check passed its
    # own mutation for that reason. What matters is that the stored name is written
    # THROUGH it.
    writes = re.findall(r"copy\(\s*_convos\[[^\]]*\]\.name\s*,\s*([^,]+?)\s*,", store_src)
    if not writes:
        bad.append(
            "cannot find where BadgeStore writes a convo's name -- this guard is matching "
            "nothing, so it is proving nothing. Update the pattern to the current shape.")
    for src_expr in writes:
        if "nameWithoutSigil" not in src_expr:
            bad.append(
                "BadgeStore writes a convo name from %r, which does not go through "
                "nameWithoutSigil(). #1624 was the badge showing '##queencitycon'; the fix "
                "was to strip here and prepend in the UI, and dropping this half brings "
                "the double sigil straight back." % src_expr)

    # And the UI prepends, which is what tells a channel row from a contact row.
    thread = function_body(screens_src, "ThreadScreen::render")
    if thread is None:
        bad.append("ThreadScreen::render not found -- cannot check the title's sigil")
    elif not re.search(r'"%c%s"', thread) or "'#'" not in thread or "'@'" not in thread:
        bad.append(
            "ThreadScreen::render no longer prepends the sigil. The store holds names "
            "WITHOUT it (#1624), so without the prepend the conversation title renders "
            "bare and a channel is indistinguishable from a contact. test_badge_store's 8 "
            "cases cover the strip and would not notice.")

    inbox = function_body(screens_src, "InboxScreen::drawItem")
    if inbox is None:
        bad.append("InboxScreen::drawItem not found -- cannot check the row sigil")
    elif not re.search(r"sigil\s*=\s*\(.*Channel\s*\)\s*\?\s*'#'\s*:\s*'@'", inbox):
        bad.append(
            "InboxScreen::drawItem no longer chooses the row sigil from the convo kind. "
            "Every inbox row would carry the same symbol, so channels and contacts become "
            "indistinguishable in the list.")
    return bad


# --- 4: the three screens do not swallow a back-out -------------------------------------

BACKOUT_SCREENS = ("SettingsScreen::handleInput",
                   "BatteryScreen::handleInput",
                   "GpsScreen::handleInput")


def backout_failures(screens_src):
    screens_src = strip_c_comments(screens_src)
    bad = []
    for qualified in BACKOUT_SCREENS:
        body = function_body(screens_src, qualified)
        if body is None:
            bad.append("%s not found -- this guard cannot see its target" % qualified)
            continue
        if "backsOut" not in body:
            bad.append(
                "%s does not consult keynav::backsOut(). #1590: these three swallowed a "
                "back-out, and on a badge with no keyboard FITTED that left Settings with "
                "NO exit -- the keyboard is a RUNTIME absence on this board, so every "
                "keyboard path still compiles and only the key that leaves is missing. "
                "test_keynav's 8 cases cover the rule and cannot see this." % qualified)
            continue
        # ORDERING, which is the actual invariant and not what the first draft checked.
        #
        # `return from_button` as the last line is CORRECT and deliberate -- #1590's fix
        # was not removing it, it was adding a backsOut() consultation IN FRONT of it. The
        # first draft of this guard flagged the trailing return itself and so failed the
        # fixed code, which is a guard asserting the bug.
        #
        # What has to hold is that a back-out is recognised BEFORE the handler falls
        # through to its default answer. Reinstating the old behaviour means deleting the
        # backsOut line, which leaves the return without one in front of it.
        back_at = body.index("backsOut")
        for m in re.finditer(r"return\s+from_button\s*;", body):
            if m.start() < back_at:
                bad.append(
                    "%s returns from_button at offset %d, BEFORE it consults "
                    "keynav::backsOut(). A back-out that arrived from the button is "
                    "answered by the default before anything recognises it as an exit -- "
                    "and on a keyboard-less badge that is the only back-out there is "
                    "(#1590)." % (qualified, m.start()))
    return bad


# --- everything, over the real tree -----------------------------------------------------

def _real():
    return (UITASK.read_text(), SCREENS.read_text(), MYMESH.read_text(), STORE.read_text())


def all_failures(uitask, screens, mymesh, store):
    return (trigger_priority_failures(uitask) + wire_text_failures(mymesh)
            + sigil_failures(screens, store) + backout_failures(screens))


def test_the_four_call_sites_are_intact():
    bad = all_failures(*_real())
    assert not bad, "badge call-site invariants broken:\n  - " + "\n  - ".join(bad)


# --- negatives --------------------------------------------------------------------------

def _sanity():
    assert not all_failures(*_real()), "sanity: the real tree must pass"


def _fails_with(phrase, uitask=None, screens=None, mymesh=None, store=None):
    u, s, m, st = _real()
    bad = all_failures(uitask or u, screens or s, mymesh or m, store or st)
    assert any(phrase in b for b in bad), (
        "expected a failure mentioning %r; got %s" % (phrase, bad or "NO FAILURES AT ALL"))


def test_negative_the_trigger_dropped_to_event_priority_is_caught():
    # The one-token edit #1608 exists to prevent.
    _sanity()
    _fails_with("truncates it",
                uitask=_real()[0].replace("offband::TonePriority::Trigger",
                                          "offband::TonePriority::Event", 1))


def test_negative_a_second_priority_in_the_trigger_path_is_caught():
    _sanity()
    _fails_with("must ask for Trigger and nothing else",
                uitask=_real()[0].replace(
                    "  const offband::TonePlay r =\n"
                    "      playTuneById(offband::kTuneTrigger, offband::TonePriority::Trigger);",
                    "  const offband::TonePlay r = playTuneById(offband::kTuneTrigger,\n"
                    "      quiet ? offband::TonePriority::Notify : offband::TonePriority::Trigger);",
                    1))


def test_negative_playing_some_other_tune_at_trigger_priority_is_caught():
    _sanity()
    _fails_with("some other tune",
                uitask=_real()[0].replace("playTuneById(offband::kTuneTrigger, offband::TonePriority::Trigger)",
                                          "playTuneById(offband::kTuneTick, offband::TonePriority::Trigger)", 1))


def test_negative_handing_the_rule_a_stored_sender_is_caught():
    # THE invisible one. Everything still compiles, all 49 trigger tests still pass, and
    # the trigger stops working for any sender with a long name.
    _sanity()
    _fails_with("not the whole `text`",
                mymesh=_real()[2].replace("messageFiresTriggerOnChannelText(text)",
                                          "messageFiresTriggerOnChannelText(stored_sender)", 1))


def test_negative_losing_the_channel_path_entirely_is_caught():
    # Targets the CALL, not the first textual occurrence. The first occurrence in the raw
    # file is the comment explaining why the whole text is passed, so a count-1 replace
    # renamed the prose and left the call running -- and this negative passed over a
    # mutation that had not happened.
    _sanity()
    raw = _real()[2]
    call = "offband::messageFiresTriggerOnChannelText(text)"
    assert raw.count(call) == 1, "sanity: expected exactly one real call site"
    _fails_with("hall of strangers", mymesh=raw.replace(call, "offband::neverCalled(text)"))


def test_negative_the_dm_path_reading_the_store_copy_is_caught():
    _sanity()
    _fails_with("advert name",
                mymesh=_real()[2].replace("messageFiresTrigger(from.name, text)",
                                          "messageFiresTrigger(stored->name, text)", 1))


def test_negative_dropping_the_stores_strip_is_caught():
    _sanity()
    _fails_with("double sigil",
                store=_real()[3].replace("nameWithoutSigil(kind, name)", "name", 1))


def test_negative_dropping_the_title_prepend_is_caught():
    _sanity()
    _fails_with("renders\nbare".replace("\n", " "),
                screens=_real()[1].replace('snprintf(name, sizeof(name), "%c%s", channel ? \'#\' : \'@\', shown);',
                                           'snprintf(name, sizeof(name), "%s", shown);', 1))


def test_negative_a_fixed_row_sigil_is_caught():
    _sanity()
    _fails_with("indistinguishable in the list",
                screens=_real()[1].replace("sigil = (v->kind == Store::Channel) ? '#' : '@';",
                                           "sigil = '#';", 1))


def test_negative_a_screen_swallowing_the_back_out_is_caught():
    _sanity()
    for qualified in BACKOUT_SCREENS:
        s = _real()[1]
        body = function_body(s, qualified)
        assert body is not None and "backsOut" in body, "sanity: %s consults backsOut" % qualified
        # Strip the backsOut consultation from just this one screen.
        mutated = s.replace(body, body.replace("keynav::backsOut", "neverBacksOut"), 1)
        bad = backout_failures(mutated)
        assert any(qualified in b and "NO exit" in b for b in bad), \
            "removing backsOut from %s must be reported; got %s" % (qualified, bad)


def test_negative_the_exact_1590_regression_shape_is_caught():
    # Reinstating the pre-#1590 code: delete the backsOut consultation and the handler is
    # left answering every button event with its default, which is what stranded a
    # keyboard-less badge in Settings. Done for BatteryScreen and GpsScreen, the two that
    # end on `return from_button`.
    _sanity()
    raw = _real()[1]
    for qualified in ("BatteryScreen::handleInput", "GpsScreen::handleInput"):
        body = function_body(strip_c_comments(raw), qualified)
        assert body is not None and "return from_button" in body, \
            "sanity: %s should end on return from_button" % qualified
        broken = raw.replace("  if (keynav::backsOut(key)) return false;\n  return from_button;",
                             "  return from_button;")
        bad = backout_failures(broken)
        assert any(qualified in b for b in bad), \
            "deleting the backsOut line from %s must be reported; got %s" % (qualified, bad)
        # And it must be reported as the ordering/exit problem, not as something else.
        assert any(qualified in b and ("NO exit" in b or "BEFORE it consults" in b)
                   for b in bad), "the message must name the consequence; got %s" % bad


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
