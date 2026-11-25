#!/usr/bin/env python
"""Self-test for size_matrix.py (#2278): map parsing, attribution, the pulled-by trace,
the grid, and the SPIFFS data set. Synthetic map text shaped like GNU ld's output."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import size_matrix as sm  # noqa: E402

FAILURES = []

MAP = """Archive member included to satisfy reference by file (symbol)

C:/pio/x/liblwip.a(tcp.o)
                              .pio/build/E/lib8b7/AsyncTCP/AsyncTCP.cpp.o (tcp_write)
C:/pio/x/libnet80211.a(ieee.o)
                              C:/pio/x/libwpa_supplicant.a(main.o) (esp_wifi_internal_tx)

Discarded input sections

 .text.unused   0x00000000       0x40 .pio/build/E/src/helpers/Dead.cpp.o

Memory Configuration

Linker script and memory map

.flash.text     0x42000020      0x300
 .text.a        0x42000020      0x100 .pio/build/E/src/helpers/A.cpp.o
 .text.a_function_with_a_very_long_name_that_ld_wraps_onto_the_next_line
                0x42000120       0x80 C:/pio/x/liblwip.a(tcp.o)
 .text.b        0x420001a0      0x180 .pio/build/E/lib71f/libRadioLib.a(Module.cpp.o)
.flash.rodata   0x3c000000      0x200
 .rodata.x.str1.4
                0x3c000000      0x150 .pio/build/E/lib688/SPI/SPI.cpp.o
 .rodata.y      0x3c000150       0xb0 .pio/build/E/src/main.cpp.o
.eh_frame       0x3c000200       0x40
.dram0.bss      0x3fc00000     0x1000
"""


def expect(condition, label):
    print("  %s  %s" % ("pass" if condition else "FAIL", label))
    if not condition:
        FAILURES.append(label)


def main():
    print("sections and image size")
    t = sm.section_totals(MAP)
    expect(t.get(".flash.text") == 0x300 and t.get(".dram0.bss") == 0x1000, "output sections are read (got %r)" % t)
    expect(sm.loaded_bytes(MAP) == 0x300 + 0x200 + 0x40, "the image sum counts loaded sections, not .bss")
    nrf = ".text           0x00026000    0x5a000\n.data           0x20006000      0x400\n"
    expect(sm.loaded_bytes(nrf) is None, "a map with no ESP32 sections is no measurement, not 0")

    print("attribution")
    code = sm.attribute(MAP, sm.CODE)
    expect(code == {"[our src]": 0x100, "liblwip.a": 0x80, "[lib] RadioLib": 0x180},
           "code by library: our src, a framework archive, an archived PlatformIO lib (got %r)" % code)
    expect(".text.unused" not in MAP.split("Linker script")[1] and code.get("[our src]") == 0x100,
           "discarded input sections are not counted")
    ro = sm.attribute(MAP, {".flash.rodata"})
    expect(ro == {"[lib] SPI": 0x150, "[our src]": 0xb0}, "a loose library object and a wrapped line (got %r)" % ro)
    ro_s = sm.attribute(MAP, {".flash.rodata"}, skip_strings=True)
    expect(ro_s == {"[our src]": 0xb0}, "--skip-strings leaves out the merged string pool (got %r)" % ro_s)

    print("owner()")
    for path, want in [("C:/pio/packages/fw/libpp.a(wdev.o)", "libpp.a"),
                       (".pio/build/E/lib71f/libRadioLib.a(Module.cpp.o)", "[lib] RadioLib"),
                       (".pio/build/E/lib688/SPI/SPI.cpp.o", "[lib] SPI"),
                       (".pio/build/E/src/helpers/A.cpp.o", "[our src]"),
                       (".pio/build/E/examples/simple_repeater/main.cpp.o", "[lib] simple_repeater")]:
        expect(sm.owner(path) == want, "%s -> %s (got %r)" % (path, want, sm.owner(path)))

    print("pulled-by trace")
    p = sm.pulled_by(MAP, r"liblwip\.a")
    expect(p == [("C:/pio/x/liblwip.a(tcp.o)", ".pio/build/E/lib8b7/AsyncTCP/AsyncTCP.cpp.o", "tcp_write")],
           "names the object and symbol that pulled a member in (got %r)" % p)
    expect(sm.pulled_by(MAP, r"libnothere\.a") == [], "a pattern with no match returns nothing")

    print("grid")
    old = {"E1": {"image": 1000 * 1024, "slot": 1280 * 1024, "ram": 70 * 1024, "image_estimated": False},
           "E2": {"image": 500 * 1024, "slot": 1920 * 1024, "ram": 40 * 1024, "image_estimated": False}}
    new = {"E1": {"image": 1100 * 1024, "slot": 1280 * 1024, "ram": 72 * 1024, "image_estimated": True}}
    g = sm.grid(old, new)
    expect("| `E1` | 1,000 | ~1,100 | +100 (+10.0%) | 1,280 | 86% | 180 | 70 -> 72 |" in g,
           "a row with growth, fill, left and the ~ estimate mark (got %r)" % g.splitlines()[2])
    expect("| `E2` | 500 | - |" in g, "an env missing from the new run is shown, not dropped")

    print("SPIFFS data set")
    f = sm.companion_files(350, 40, True)
    expect(f["contacts3"] == 152 * 350 and f["spare"] == 152 * 350 and f["channels2"] == 68 * 40,
           "record sizes from the write code, with rewrite room")
    expect(sum(1 for k in f if k.startswith("bl/")) == 350, "one advert file per contact")
    expect("spare" not in sm.companion_files(10, 1, False), "no rewrite room when not asked for")

    if FAILURES:
        print("\n%d failure(s)." % len(FAILURES))
        return 1
    print("\nall cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
