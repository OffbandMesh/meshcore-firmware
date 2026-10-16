# GUI fonts (#1444)

LVGL 8 bitmap fonts for the design's compact type scale (spec v0.3, D1 #1323 §1.4). They're generated from the TTFs below; the TTFs themselves aren't committed, following the RC32 font precedent (`scripts/gen-jbm-font.py`). Both faces are SIL OFL 1.1 with no Reserved Font Names. The license and copyright notices are in `OFL.txt`, which must ship with these files.

| File | Role | Source TTF | Size | Glyphs |
|---|---|---|---|---|
| `ob_font_title_18.c` | Screen title | BarlowCondensed-SemiBold.ttf (+ JetBrainsMono-SemiBold for symbols) | 18 | text, symbols |
| `ob_font_body_14.c` | Body / list row | BarlowCondensed-Regular.ttf (+ JetBrainsMono-Regular for symbols) | 14 | text, symbols |
| `ob_font_body_12.c` | Secondary body | BarlowCondensed-Regular.ttf (+ JetBrainsMono-Regular for symbols) | 12 | text, symbols |
| `ob_font_label_9.c` | Caps label | BarlowCondensed-Regular.ttf (+ JetBrainsMono-Regular for symbols) | 9 | text, symbols |
| `ob_font_data_14.c` | Data value | JetBrainsMono-SemiBold.ttf | 14 | text, symbols |
| `ob_font_meta_10.c` | Status / meta | JetBrainsMono-Regular.ttf | 10 | text, symbols |
| `ob_font_clock_46.c` | Lock-screen clock | JetBrainsMono-SemiBold.ttf | 46 | 0x2D, 0x30-0x3A (dash, digits, colon) |

The glyph sets:

- **Text:**
  - 0x20-0x7E is ASCII.
  - 0xA0-0x17F is Latin-1 and Latin Extended-A, so Western and Central European names and messages show. This was the owner's pick on 2026-10-16 (#1428): +57 KB. Greek, Cyrillic and emoji are not included.
  - `…` 0x2026 and `−` 0x2212.
  - A code point the face lacks is simply left out.
- **Symbols:** `✓` 0x2713, `✕` 0x2715, `⇧` 0x21E7, `⌫` 0x232B. Barlow has none of these, so the Barlow files take them from JetBrains Mono in the same weight (#1496).

Sources: `fonts/ttf/` in github.com/jpt/barlow and github.com/JetBrains/JetBrainsMono, fetched 2026-10-15.

Regenerate one file, for example:

```
npx -y lv_font_conv@1.5.3 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
  --font BarlowCondensed-SemiBold.ttf --range 0x20-0x7E,0xA0-0x17F,0x2026,0x2212 \
  --font JetBrainsMono-SemiBold.ttf --range 0x2713,0x2715,0x21E7,0x232B --size 18 \
  -o ob_font_title_18.c --lv-font-name ob_font_title_18
```
