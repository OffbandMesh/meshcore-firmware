"""Every low-battery cutoff goes through the one debounced rule (#1667).

The defect this guards was written THREE TIMES, independently, in three UI families:

    if (mv > 0 && mv < AUTO_SHUTDOWN_MILLIVOLTS && !board.isExternalPowered())
        lowBatteryShutdown(mv);

One raw ADC sample, no debounce, powering the board off. ui-new and ui-tiny had it from
the start; ui-gui copied ui-new deliberately and documented the copy -- "as the older
screens run it (ui-new): every 8 s, on the raw reading" -- which was an accurate
description of the defect.

It recurred because nothing checked. #1667 fixed two families and its own commit message
said "both UI families"; there were three, and the third arrived in a base merge after
the grep that found the other two. A green build says nothing: the GUI variant built
successfully in a 4/4 gate run while still carrying it.

WHY THIS MATTERS MORE THAN A STYLE RULE. On the QCC badge, raw reads scatter +/-600 mV on
a cell moving ~1.1 mV/min (#1672). Measured over a 12.4-hour burndown: 1036 of 5421
samples fell below the 3400 mV threshold, minimum 2618 mV, with exactly one reset in the
whole run -- the flash that started it. The single-sample form would have powered the
board off on the first of those 1036, which is precisely the reported bug. 15 variants
define a cutoff.

WHAT THIS IS. A source assertion, not a behavioural test -- it checks that every site
comparing against AUTO_SHUTDOWN_MILLIVOLTS hands the decision to
offband::lowBattShouldShutdown. A refactor that preserves the text while changing
behaviour passes, and a rename false-positives. A tripwire, and the right kind: the thing
it guards is a decision to power hardware off, and the failure mode is silent.

Run: python scripts/test_low_batt_cutoff_call_sites.py
  or python -m pytest scripts/test_low_batt_cutoff_call_sites.py -q
"""
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_qcc_variant_pins import strip_c_comments  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
EXAMPLES = ROOT / "examples"
RULE = "lowBattShouldShutdown"
THRESHOLD = "AUTO_SHUTDOWN_MILLIVOLTS"

# Sites that legitimately mention the threshold without deciding anything: reporting it
# in a boot banner, or handing it to a GUI struct for display.
ALLOWED_NON_DECIDING = (
    re.compile(r"->\s*shutdown_mv\s*="),          # ui-gui: telling the screen the number
    re.compile(r"\(int\)" + THRESHOLD),            # main.cpp: the boot banner
    re.compile(r"#\s*(if|ifdef|ifndef|endif|else)"),
    re.compile(r"#\s*define"),
)


def cpp_sources():
    """Every .cpp under examples/, comment-stripped so prose is never read as code."""
    out = {}
    for p in sorted(EXAMPLES.rglob("*.cpp")):
        out[p.relative_to(ROOT).as_posix()] = strip_c_comments(p.read_text(encoding="utf-8",
                                                                           errors="replace"))
    return out


def _deciding(sources):
    """Files that use the threshold to decide a shutdown, in either form.

    Deliberately NOT keyed on a comparison. Before #1667 the sites compared (`mv <
    AUTO_SHUTDOWN_MILLIVOLTS`); after it they PASS the threshold as an argument to the
    rule. The first version of this check looked only for the comparison, so once the fix
    landed it matched nothing and reported the tree as having no cutoff at all -- caught
    by its own non-vacuity assertion, which is what that assertion is for.

    So: any mention of the threshold that is not a display or report line counts, and such
    a file must hand the decision to the rule.
    """
    out = []
    for path, src in sorted(sources.items()):
        # Strip here rather than trusting the caller. cpp_sources() already does it, but a
        # function that reads code must not depend on having been handed stripped code --
        # the negative cases below pass raw snippets, and so would anyone reusing this.
        for line in strip_c_comments(src).splitlines():
            if THRESHOLD not in line:
                continue
            if any(rx.search(line) for rx in ALLOWED_NON_DECIDING):
                continue
            out.append(path)
            break
    return out


def cutoff_failures(sources):
    """Returns failure messages; empty means every cutoff is debounced.

    Pure over its input so the negatives below can hand it synthetic sources and prove
    each check fires -- a tripwire nobody has watched trip is one nobody knows is wired.
    """
    bad = []
    deciding_files = _deciding(sources)

    for path in deciding_files:
        if RULE not in sources[path]:
            bad.append(
                "%s uses %s to decide a shutdown but never calls offband::%s. That is the "
                "single-raw-sample cutoff this check exists to stop: on the badge, raw "
                "reads scatter +/-600 mV and crossed a 3400 mV threshold 1036 times in "
                "12.4 hours while the board never once browned out (#1672). Hand the "
                "decision to the rule in helpers/ui/LowBattCutoff.h."
                % (path, THRESHOLD, RULE))

    # Not vacuous. If the call sites are renamed or restructured out from under this, it
    # must fail loudly rather than pass over an empty set -- the hole closed in #1638.
    if not deciding_files:
        bad.append(
            "found no file comparing against %s at all. Either the cutoff is gone or this "
            "check has stopped matching it; update the pattern rather than trusting the "
            "silence." % THRESHOLD)
    return bad


def deciding_families(sources):
    """Which UI families contain a cutoff decision, for the count assertion below."""
    fams = set()
    for path in _deciding(sources):
        parts = path.split("/")
        if len(parts) >= 3 and parts[2].startswith("ui-"):
            fams.add(parts[2])
    return fams


# --- positive ---------------------------------------------------------------------------

def test_every_cutoff_is_debounced():
    bad = cutoff_failures(cpp_sources())
    assert not bad, "an undebounced low-battery cutoff is present:\n  - " + "\n  - ".join(bad)


def test_all_three_known_families_are_covered():
    # Named explicitly because the miss was a COUNT error: #1667 said "both UI families"
    # when there were three. If a fourth appears, this fails and someone has to look.
    fams = deciding_families(cpp_sources())
    assert fams == {"ui-new", "ui-tiny", "ui-gui"}, (
        "the set of UI families with a low-battery cutoff changed: %s. If a family was "
        "added, route its cutoff through offband::%s and add it here; if one was removed, "
        "drop it. Do not widen this assertion without looking." % (sorted(fams), RULE))


# --- negative: the check must actually fire ---------------------------------------------

def _sanity():
    assert not cutoff_failures(cpp_sources()), "sanity: the real tree must pass"


def test_negative_a_new_family_with_a_raw_cutoff_is_caught():
    # Exactly the ui-gui shape, in a fourth family that does not call the rule.
    _sanity()
    src = {"examples/companion_radio/ui-future/UITask.cpp":
           "void loop() {\n  const uint16_t mv = getBattMilliVolts();\n"
           "  if (mv > 0 && mv < AUTO_SHUTDOWN_MILLIVOLTS) lowBatteryShutdown(mv);\n}\n"}
    bad = cutoff_failures(src)
    assert any("never calls offband::lowBattShouldShutdown" in b for b in bad), bad


def test_negative_a_cutoff_that_does_call_the_rule_passes():
    src = {"examples/companion_radio/ui-future/UITask.cpp":
           "void loop() {\n  if (offband::lowBattShouldShutdown(_s, now, raw, sm,\n"
           "      (uint16_t)AUTO_SHUTDOWN_MILLIVOLTS, OFFBAND_LOW_BATT_SUSTAIN_MS, ext))\n"
           "    lowBatteryShutdown(raw);\n}\n"}
    assert not cutoff_failures(src), cutoff_failures(src)


def test_negative_a_comment_describing_the_old_form_is_not_a_call_site():
    # LowBattCutoff.h's own header quotes the defective line, and ui-gui's comment used to
    # describe it as the design. Prose must not read as code.
    _sanity()
    src = {"examples/companion_radio/ui-future/UITask.cpp":
           "// if (mv > 0 && mv < AUTO_SHUTDOWN_MILLIVOLTS) shutdown();   <- the old form\n"
           "void loop() { ok(); }\n"}
    bad = cutoff_failures(src)
    assert any("found no file comparing" in b for b in bad), (
        "a commented-out comparison must not count as a decision; got %s" % bad)


def test_negative_reporting_the_threshold_is_not_deciding_on_it():
    # ui-gui hands the number to its screen; main.cpp prints it in the boot banner.
    # Neither decides anything, and neither should be demanded to call the rule.
    src = {"examples/companion_radio/ui-gui/UITask.cpp":
           "void fill(D* d) { d->shutdown_mv = AUTO_SHUTDOWN_MILLIVOLTS; }\n",
           "examples/companion_radio/main.cpp":
           "void banner() { crashLogf(\"shutdown_mv=%d\", (int)AUTO_SHUTDOWN_MILLIVOLTS); }\n"}
    bad = cutoff_failures(src)
    assert any("found no file comparing" in b for b in bad), (
        "a display/report site must not be treated as a cutoff; got %s" % bad)


def test_negative_a_vanished_cutoff_does_not_pass_silently():
    _sanity()
    assert any("found no file comparing" in b for b in cutoff_failures({"x/y.cpp": "int main(){}"}))


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
