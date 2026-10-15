# GUI fonts (#1444)

LVGL 8 bitmap fonts for the design's compact type scale (spec v0.3, D1 #1323 §1.4). They're generated from the TTFs below; the TTFs themselves aren't committed, following the RC32 font precedent (`scripts/gen-jbm-font.py`). Both faces are SIL OFL 1.1 with no Reserved Font Names. The license and copyright notices are in `OFL.txt`, which must ship with these files.

| File | Role | Source TTF | Size | Glyphs |
|---|---|---|---|---|
| `ob_font_title_18.c` | Screen title | BarlowCondensed-SemiBold.ttf | 18 | 0x20-0x7E |
| `ob_font_body_14.c` | Body / list row | BarlowCondensed-Regular.ttf | 14 | 0x20-0x7E |
| `ob_font_body_12.c` | Secondary body | BarlowCondensed-Regular.ttf | 12 | 0x20-0x7E |
| `ob_font_label_9.c` | Caps label | BarlowCondensed-Regular.ttf | 9 | 0x20-0x7E |
| `ob_font_data_14.c` | Data value | JetBrainsMono-SemiBold.ttf | 14 | 0x20-0x7E |
| `ob_font_meta_10.c` | Status / meta | JetBrainsMono-Regular.ttf | 10 | 0x20-0x7E |
| `ob_font_clock_46.c` | Lock-screen clock | JetBrainsMono-SemiBold.ttf | 46 | 0x30-0x3A (digits, colon) |

Sources: `fonts/ttf/` in github.com/jpt/barlow and github.com/JetBrains/JetBrainsMono, fetched 2026-10-15.

Regenerate one file, for example:

```
npx -y lv_font_conv@1.5.3 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
  --font BarlowCondensed-SemiBold.ttf --range 0x20-0x7E --size 18 \
  -o ob_font_title_18.c --lv-font-name ob_font_title_18
```

Only printable ASCII is included for now. Non-ASCII text (names, messages) needs a wider range, a decision for the screen Epics.
