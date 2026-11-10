# GUI fonts for e-paper (#2009)

The same seven fonts as `../fonts/`, with the same faces, sizes, glyph sets and LVGL names, converted at **1 bit per pixel with strong autohinting**. Only the e-paper image (`LilyGo_TDeck_Pro_companion_radio_ble_gui_diag`) builds this folder, in place of `../fonts/`; the color screens keep the anti-aliased set.

Why: a black-and-white panel has no grey. The 4-bit fonts draw thin strokes as grey edges, so any cutoff drops some of them. On `tdeck-pro-1` the 9 px labels lost strokes, MAP read as "NAP" and the I in COMPANION vanished, while the 14 px values came out heavy (#2037). At 1 bit the hinter snaps each stroke to whole pixels, so a stroke is either drawn or not, and every glyph pixel is drawn in full.

Sources, licenses and glyph ranges are as in `../fonts/README.md`; the notices are in `OFL.txt`, which must ship with these files. Converter: `lv_font_conv@1.5.3`. The emoji face is `NotoEmoji[wght].ttf`, renamed `NotoEmoji.ttf` because the shell skips path conversion on a name with brackets.

Regenerate, from a folder holding the TTFs in `ttf/`:

```
TXT="0x20-0x7E,0xA0-0x17F,0x2014,0x2026,0x203A,0x2212"
SYM="0x2713,0x2715,0x21E7,0x232B"
EMO="0x1F300-0x1F5FF,0x1F600-0x1F64F,0x1F680-0x1F6FF,0x1F900-0x1F9FF,0x2600-0x2712,0x2716-0x27BF"
C="npx -y lv_font_conv@1.5.3 --bpp 1 --format lvgl --no-compress --lv-include lvgl.h"

# Barlow files: text face, JetBrains Mono symbols, Noto emoji
$C --font ttf/BarlowCondensed-SemiBold.ttf --autohint-strong --range $TXT \
   --font ttf/JetBrainsMono-SemiBold.ttf --autohint-strong --range $SYM \
   --font ttf/NotoEmoji.ttf --autohint-strong --range $EMO \
   --size 18 -o ob_font_title_18.c --lv-font-name ob_font_title_18
# ob_font_body_14 / body_12 / label_9: BarlowCondensed-Regular + JetBrainsMono-Regular, sizes 14 / 12 / 9

# JetBrains Mono files
$C --font ttf/JetBrainsMono-SemiBold.ttf --autohint-strong --range $TXT,$SYM --size 14 -o ob_font_data_14.c --lv-font-name ob_font_data_14
$C --font ttf/JetBrainsMono-Regular.ttf --autohint-strong --range $TXT,$SYM --size 10 -o ob_font_meta_10.c --lv-font-name ob_font_meta_10
$C --font ttf/JetBrainsMono-SemiBold.ttf --autohint-strong --range 0x2D,0x30-0x3A --size 46 -o ob_font_clock_46.c --lv-font-name ob_font_clock_46
```
