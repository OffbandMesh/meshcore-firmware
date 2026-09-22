"""Drift guard for the WROOM copy of the UART sniffer sketch (#1199).
Run: python scripts/test_sniffer_wroom_copy.py
  or python -m pytest scripts/test_sniffer_wroom_copy.py -q

tools/diag/sniffer/rc_uart_sniffer_v3_wroom/ is a copy of the Feather sketch in
tools/diag/sniffer/rc_uart_sniffer_v3/. Arduino builds a sketch from its own folder
only, so the two cannot share a source file. They may differ ONLY inside blocks marked

    // ==== VARIANT BEGIN: <name> ====
    // ==== VARIANT END: <name> ====

and both must carry the same named blocks in the same order. Everything else must
match line for line, so a fix to one sketch cannot silently skip the other.
"""
import difflib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FEATHER = ROOT / "tools" / "diag" / "sniffer" / "rc_uart_sniffer_v3" / "rc_uart_sniffer_v3.ino"
WROOM = ROOT / "tools" / "diag" / "sniffer" / "rc_uart_sniffer_v3_wroom" / "rc_uart_sniffer_v3_wroom.ino"

BEGIN = re.compile(r"^\s*// ==== VARIANT BEGIN: ([\w-]+) ====\s*$")
END = re.compile(r"^\s*// ==== VARIANT END: ([\w-]+) ====\s*$")
PIN_DEFINE = re.compile(r"^\s*#define\s+(PIN_\w+)\s+(\S+)")


def split_variants(text):
    """Return (shared lines, {block name: block lines}); a block collapses to its marker."""
    shared, blocks, open_name = [], {}, None
    for n, line in enumerate(text.splitlines(), 1):
        begin, end = BEGIN.match(line), END.match(line)
        if begin:
            if open_name is not None:
                raise ValueError(f"line {n}: block '{begin.group(1)}' opens inside '{open_name}'")
            open_name = begin.group(1)
            if open_name in blocks:
                raise ValueError(f"line {n}: block '{open_name}' appears twice")
            blocks[open_name] = []
            shared.append(line.strip())
        elif end:
            if end.group(1) != open_name:
                raise ValueError(f"line {n}: END '{end.group(1)}' does not close '{open_name}'")
            open_name = None
        elif open_name is not None:
            blocks[open_name].append(line)
        else:
            shared.append(line)
    if open_name is not None:
        raise ValueError(f"block '{open_name}' is never closed")
    return shared, blocks


def read(path):
    return path.read_text(encoding="utf-8")


def test_both_sketches_exist():
    assert FEATHER.is_file(), f"missing {FEATHER}"
    assert WROOM.is_file(), f"missing {WROOM}"


def test_both_carry_the_same_blocks_in_the_same_order():
    _, feather = split_variants(read(FEATHER))
    _, wroom = split_variants(read(WROOM))
    assert list(feather) == list(wroom), f"Feather {list(feather)} vs WROOM {list(wroom)}"
    assert {"board-pins", "gauge-pins"} <= set(feather), "the pin blocks must stay marked"


def test_everything_outside_the_blocks_is_identical():
    feather, _ = split_variants(read(FEATHER))
    wroom, _ = split_variants(read(WROOM))
    diff = list(difflib.unified_diff(feather, wroom, "rc_uart_sniffer_v3", "rc_uart_sniffer_v3_wroom",
                                     lineterm="", n=1))
    assert not diff, "the sketches drifted outside their VARIANT blocks:\n" + "\n".join(diff[:40])


def test_wroom_pins_are_plain_gpio_numbers():
    # Why the copy exists: the Feather's pin names (RX, TX, A0..A3) are board-variant
    # aliases. On a classic ESP32 RX is UART0 on the USB bridge and A2 does not exist.
    _, blocks = split_variants(read(WROOM))
    defines = [PIN_DEFINE.match(l) for name in ("board-pins", "gauge-pins") for l in blocks[name]]
    defines = [m for m in defines if m]
    assert defines, "no PIN_ defines found in the WROOM pin blocks"
    for m in defines:
        assert m.group(2).isdigit(), f"{m.group(1)} is '{m.group(2)}', not a GPIO number"


def test_an_unclosed_block_is_an_error():
    try:
        split_variants("// ==== VARIANT BEGIN: x ====\nint a;\n")
    except ValueError:
        return
    raise AssertionError("an unclosed block was accepted")


def test_a_mismatched_end_is_an_error():
    try:
        split_variants("// ==== VARIANT BEGIN: x ====\n// ==== VARIANT END: y ====\n")
    except ValueError:
        return
    raise AssertionError("a mismatched END was accepted")


def test_block_contents_are_ignored_and_markers_are_kept():
    shared, blocks = split_variants("a\n// ==== VARIANT BEGIN: x ====\nb\n// ==== VARIANT END: x ====\nc\n")
    assert shared == ["a", "// ==== VARIANT BEGIN: x ====", "c"]
    assert blocks == {"x": ["b"]}


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except (AssertionError, ValueError, OSError) as e:
                failures += 1
                print(f"FAIL {name}: {e}")
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
