"""LVGL's objects stay in PSRAM on the ESP32 GUI builds (#1738, Epic V #1893).

Through plain malloc, every LVGL object landed in internal RAM: the S3 build serves
each malloc of 4 KB or less from there. That cost the GUI ~70-100 KB of internal RAM
and left no room for Wi-Fi beside BLE (#1891). This checks the wiring that fixes it,
which a later edit could undo without any test failing:
- lv_conf.h routes LVGL to offband_lv_alloc/free/realloc on the ESP32 builds;
- hw/LvAlloc.cpp asks PSRAM first and falls back to internal RAM;
- LvglPort's draw buffers stay DMA-capable internal RAM, never LVGL's allocator;
- every env that builds the GUI hardware layer uses this lv_conf.h.

Run: python scripts/test_lvgl_alloc_wiring.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GUI = "src/helpers/gui"


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def esp32_branch(lv_conf):
    """The text lv_conf.h compiles on the ESP32 builds."""
    code = strip_comments(lv_conf)
    m = re.search(r"#if\s+defined\(ARDUINO_ARCH_ESP32\)\s*&&\s*!defined\(OFFBAND_NATIVE_LVGL\)(.*?)#else",
                  code, re.S)
    return m.group(1) if m else ""


def conf_failures(lv_conf):
    out = []
    branch = esp32_branch(lv_conf)
    if not branch:
        out.append("lv_conf.h has no ESP32 branch for LVGL's allocator")
    for key, fn in (("ALLOC", "offband_lv_alloc"), ("FREE", "offband_lv_free"),
                    ("REALLOC", "offband_lv_realloc")):
        if not re.search(r"#define\s+LV_MEM_CUSTOM_%s\s+%s\b" % (key, fn), branch):
            out.append("lv_conf.h: LV_MEM_CUSTOM_%s on ESP32 is not %s" % (key, fn))
    if not re.search(r'#define\s+LV_MEM_CUSTOM_INCLUDE\s+"LvAlloc\.h"', branch):
        out.append('lv_conf.h: LV_MEM_CUSTOM_INCLUDE on ESP32 is not "LvAlloc.h"')
    return out


def impl_failures(impl):
    out = []
    code = strip_comments(impl)
    prefer = re.search(r"kPsram\s*=\s*([^;]+);", code)
    fallback = re.search(r"kInternal\s*=\s*([^;]+);", code)
    if not prefer or "MALLOC_CAP_SPIRAM" not in prefer.group(1):
        out.append("hw/LvAlloc.cpp: the preferred heap is not PSRAM")
    if not fallback or "MALLOC_CAP_INTERNAL" not in fallback.group(1):
        out.append("hw/LvAlloc.cpp: the fallback heap is not internal RAM")
    for fn in ("offband_lv_alloc", "offband_lv_realloc"):
        body = re.search(fn + r"\s*\([^)]*\)\s*\{(.*?)\n\}", code, re.S)
        if not body or "Preferring(" not in body.group(1) or body.group(1).find("kPsram") > body.group(1).find("kInternal"):
            out.append("hw/LvAlloc.cpp: %s does not ask PSRAM first, then internal RAM" % fn)
    return out


def port_failures(port):
    out = []
    code = strip_comments(port)
    if "lv_mem_alloc" in code:
        out.append("hw/LvglPort.cpp allocates through lv_mem_alloc: draw buffers would leave DMA-capable RAM")
    allocs = re.findall(r"heap_caps_malloc\([^;]*MALLOC_CAP_DMA[^;]*\)", code)
    if len(allocs) < 2:
        out.append("hw/LvglPort.cpp: the two draw buffers are no longer MALLOC_CAP_DMA")
    return out


def env_failures(inis):
    out = []
    for path, text in inis.items():
        for m in re.finditer(r"^\[(env:[^\]]+)\]\s*$(.*?)(?=^\[|\Z)", text, re.M | re.S):
            name, body = m.group(1), strip_comments(re.sub(r";[^\n]*", "", m.group(2)))
            if "helpers/gui/hw/" not in body:
                continue
            if "-I " + GUI not in body or "LV_CONF_INCLUDE_SIMPLE" not in body:
                out.append("%s [%s] builds the GUI hardware layer without %s/lv_conf.h" % (path, name, GUI))
    return out


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def _inis():
    inis = {"platformio.ini": read("platformio.ini")}
    for p in sorted((ROOT / "variants").glob("*/platformio.ini")):
        inis[p.relative_to(ROOT).as_posix()] = p.read_text(encoding="utf-8")
    return inis


def failures(lv_conf, impl, port, inis):
    return conf_failures(lv_conf) + impl_failures(impl) + port_failures(port) + env_failures(inis)


def _real():
    return (read(GUI + "/lv_conf.h"), read(GUI + "/hw/LvAlloc.cpp"), read(GUI + "/hw/LvglPort.cpp"), _inis())


def test_the_real_tree_passes():
    got = failures(*_real())
    assert not got, "\n".join(got)


def test_negative_lv_conf_back_on_plain_malloc_is_caught():
    lv_conf, impl, port, inis = _real()
    bad = lv_conf.replace("#define LV_MEM_CUSTOM_ALLOC offband_lv_alloc", "#define LV_MEM_CUSTOM_ALLOC malloc")
    assert any("LV_MEM_CUSTOM_ALLOC" in f for f in failures(bad, impl, port, inis))


def test_negative_psram_and_internal_swapped_is_caught():
    lv_conf, impl, port, inis = _real()
    bad = impl.replace("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", "TMP").replace(
        "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT", "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT").replace(
        "TMP", "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT")
    got = failures(lv_conf, bad, port, inis)
    assert any("preferred heap is not PSRAM" in f for f in got), got


def test_negative_internal_asked_first_is_caught():
    lv_conf, impl, port, inis = _real()
    bad = impl.replace("heap_caps_malloc(n, kPsram)", "heap_caps_malloc(n, TMP)").replace(
        "heap_caps_malloc(n, kInternal)", "heap_caps_malloc(n, kPsram)").replace(
        "heap_caps_malloc(n, TMP)", "heap_caps_malloc(n, kInternal)")
    got = failures(lv_conf, bad, port, inis)
    assert any("offband_lv_alloc does not ask PSRAM first" in f for f in got), got


def test_negative_draw_buffers_through_lvgl_are_caught():
    lv_conf, impl, port, inis = _real()
    bad = port.replace("heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)", "lv_mem_alloc(bytes)")
    got = failures(lv_conf, impl, bad, inis)
    assert any("lv_mem_alloc" in f for f in got) and any("no longer MALLOC_CAP_DMA" in f for f in got), got


def test_negative_a_gui_env_without_this_lv_conf_is_caught():
    lv_conf, impl, port, inis = _real()
    inis = dict(inis)
    inis["variants/x/platformio.ini"] = "[env:x_gui]\nbuild_src_filter =\n  +<helpers/gui/hw/*.cpp>\n"
    assert any("[env:x_gui]" in f for f in failures(lv_conf, impl, port, inis))


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
