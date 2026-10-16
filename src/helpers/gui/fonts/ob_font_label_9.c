/*******************************************************************************
 * Size: 9 px
 * Bpp: 4
 * Opts: --bpp 4 --format lvgl --no-compress --lv-include lvgl.h --font BarlowCondensed-Regular.ttf --range 0x20-0x7E,0xB7,0xD7,0x2026,0x2212 --font JetBrainsMono-Regular.ttf --range 0x2713,0x2715,0x21E7,0x232B --size 9 -o C:/Dev/.worktrees/meshcore-firmware-1428/src/helpers/gui/fonts/ob_font_label_9.c --lv-font-name ob_font_label_9
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef OB_FONT_LABEL_9
#define OB_FONT_LABEL_9 1
#endif

#if OB_FONT_LABEL_9

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x72, 0x72, 0x72, 0x61, 0x41, 0x0, 0x83,

    /* U+0022 "\"" */
    0x99, 0x8, 0x80, 0x0, 0x0,

    /* U+0023 "#" */
    0x4, 0x4, 0x0, 0xa0, 0xa0, 0x4d, 0x9d, 0x40,
    0xa0, 0xa0, 0x8d, 0xad, 0x14, 0x63, 0x60, 0x54,
    0x54, 0x0,

    /* U+0024 "$" */
    0x4, 0x10, 0x5a, 0xa0, 0xa0, 0x73, 0x66, 0x0,
    0x6, 0x80, 0x40, 0x73, 0x93, 0xa2, 0x9, 0x50,
    0x1, 0x0,

    /* U+0025 "%" */
    0x56, 0x50, 0x70, 0x70, 0x63, 0x40, 0x36, 0x27,
    0x0, 0x0, 0x25, 0x0, 0x0, 0x71, 0x64, 0x2,
    0x56, 0x6, 0x7, 0x3, 0x55,

    /* U+0026 "&" */
    0x2b, 0xa0, 0x6, 0x48, 0x20, 0x29, 0xb0, 0x0,
    0xd4, 0x0, 0x94, 0x94, 0xa, 0x5, 0xb0, 0x69,
    0x9a, 0x10,

    /* U+0027 "'" */
    0x98, 0x0,

    /* U+0028 "(" */
    0x4, 0x37, 0x74, 0x82, 0xa1, 0x91, 0x83, 0x55,
    0x19,

    /* U+0029 ")" */
    0x50, 0x73, 0x46, 0x28, 0x1a, 0x29, 0x37, 0x55,
    0x91,

    /* U+002A "*" */
    0x2, 0x0, 0x49, 0x70, 0x2d, 0x80, 0x16, 0x10,

    /* U+002B "+" */
    0x4, 0x30, 0x6a, 0xa5, 0x4, 0x30, 0x3, 0x20,

    /* U+002C "," */
    0x20, 0x80, 0x70,

    /* U+002D "-" */
    0x69, 0x50,

    /* U+002E "." */
    0x0, 0x83,

    /* U+002F "/" */
    0x4, 0x50, 0x81, 0xa, 0x0, 0x90, 0x36, 0x7,
    0x20, 0x90, 0x0,

    /* U+0030 "0" */
    0x2a, 0xa1, 0x82, 0x37, 0x90, 0x27, 0x90, 0x27,
    0x90, 0x27, 0x81, 0x37, 0x2a, 0xa1,

    /* U+0031 "1" */
    0x8c, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa,

    /* U+0032 "2" */
    0x5a, 0x80, 0xa0, 0xa0, 0x10, 0xa0, 0x2, 0x90,
    0xa, 0x10, 0x48, 0x0, 0xba, 0xa2,

    /* U+0033 "3" */
    0x6a, 0xe1, 0x2, 0x90, 0xa, 0x30, 0x1, 0x90,
    0x10, 0x71, 0xa0, 0x81, 0x4b, 0x90,

    /* U+0034 "4" */
    0x9, 0x0, 0xa, 0x0, 0x46, 0x10, 0x91, 0x91,
    0xaa, 0xd5, 0x0, 0x91, 0x0, 0x81,

    /* U+0035 "5" */
    0x99, 0x90, 0xa0, 0x0, 0xa8, 0x80, 0x61, 0x81,
    0x0, 0x63, 0x81, 0x82, 0x3b, 0xa0,

    /* U+0036 "6" */
    0x3b, 0xa0, 0x90, 0x51, 0xa9, 0x80, 0xa2, 0x81,
    0x90, 0x63, 0x82, 0x73, 0x3b, 0xa0,

    /* U+0037 "7" */
    0xca, 0xe0, 0x10, 0xa0, 0x1, 0x90, 0x5, 0x50,
    0x9, 0x10, 0xb, 0x0, 0x19, 0x0,

    /* U+0038 "8" */
    0x3a, 0x90, 0x90, 0x81, 0x81, 0x81, 0x2c, 0xa0,
    0x81, 0x81, 0x90, 0x72, 0x3a, 0x90,

    /* U+0039 "9" */
    0x7a, 0x7a, 0xa, 0xa0, 0xaa, 0xc, 0x4a, 0xc6,
    0xa, 0x7b, 0x70,

    /* U+003A ":" */
    0x65, 0x0, 0x0, 0x65,

    /* U+003B ";" */
    0x83, 0x0, 0x10, 0x80, 0x70,

    /* U+003C "<" */
    0x0, 0x36, 0x29, 0x70, 0x96, 0x0, 0x4, 0xa4,
    0x0, 0x2,

    /* U+003D "=" */
    0x68, 0x85, 0x0, 0x0, 0x68, 0x85,

    /* U+003E ">" */
    0x63, 0x0, 0x7, 0x92, 0x0, 0x78, 0x4a, 0x40,
    0x20, 0x0,

    /* U+003F "?" */
    0x5a, 0x8a, 0xa, 0x10, 0xb0, 0x84, 0xa, 0x0,
    0x0, 0xc, 0x0,

    /* U+0040 "@" */
    0x0, 0x67, 0x82, 0x0, 0x85, 0x74, 0x91, 0x52,
    0x73, 0x33, 0x47, 0x7, 0x52, 0x62, 0x70, 0x67,
    0x85, 0x4, 0x60, 0x1, 0x0, 0x5, 0x87, 0x20,
    0x0,

    /* U+0041 "A" */
    0x9, 0x50, 0x9, 0x80, 0x9, 0x90, 0x27, 0x90,
    0x54, 0x72, 0x9a, 0xb5, 0xa0, 0x18,

    /* U+0042 "B" */
    0x7a, 0xa2, 0x72, 0x28, 0x72, 0x37, 0x79, 0xc1,
    0x72, 0x26, 0x72, 0x18, 0x7b, 0xb2,

    /* U+0043 "C" */
    0x3a, 0xa1, 0x81, 0x36, 0x90, 0x0, 0x90, 0x0,
    0x90, 0x0, 0x81, 0x36, 0x3a, 0xa1,

    /* U+0044 "D" */
    0x7a, 0xa1, 0x72, 0x37, 0x72, 0x28, 0x72, 0x28,
    0x72, 0x28, 0x72, 0x37, 0x7a, 0xb1,

    /* U+0045 "E" */
    0x7a, 0x93, 0x72, 0x0, 0x72, 0x0, 0x7a, 0x50,
    0x72, 0x0, 0x72, 0x0, 0x7a, 0xa3,

    /* U+0046 "F" */
    0x7a, 0x93, 0x72, 0x0, 0x72, 0x0, 0x7a, 0x50,
    0x72, 0x0, 0x72, 0x0, 0x72, 0x0,

    /* U+0047 "G" */
    0x3a, 0xb2, 0x91, 0x37, 0x90, 0x0, 0x91, 0xa6,
    0x90, 0x27, 0x91, 0x46, 0x3a, 0xa1,

    /* U+0048 "H" */
    0x72, 0x27, 0x72, 0x27, 0x72, 0x27, 0x7b, 0xb7,
    0x72, 0x27, 0x72, 0x27, 0x72, 0x27,

    /* U+0049 "I" */
    0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72,

    /* U+004A "J" */
    0x0, 0x63, 0x0, 0x63, 0x0, 0x63, 0x0, 0x63,
    0x50, 0x63, 0xa0, 0x72, 0x5a, 0xa0,

    /* U+004B "K" */
    0x72, 0x46, 0x72, 0xa0, 0x78, 0x60, 0x7d, 0x70,
    0x73, 0xa0, 0x72, 0x73, 0x72, 0x19,

    /* U+004C "L" */
    0x72, 0x0, 0x72, 0x0, 0x72, 0x0, 0x72, 0x0,
    0x72, 0x0, 0x72, 0x0, 0x7a, 0xa3,

    /* U+004D "M" */
    0x75, 0xd, 0x7b, 0x5d, 0x78, 0xca, 0x72, 0x4a,
    0x72, 0xa, 0x72, 0xa, 0x72, 0xa,

    /* U+004E "N" */
    0x75, 0x9, 0x7b, 0xa, 0x7a, 0x1a, 0x75, 0x6a,
    0x72, 0x9a, 0x72, 0x8a, 0x72, 0x3a,

    /* U+004F "O" */
    0x2a, 0xa2, 0x81, 0x27, 0x90, 0x28, 0x90, 0x28,
    0x90, 0x28, 0x81, 0x27, 0x2a, 0xa2,

    /* U+0050 "P" */
    0x7a, 0xa2, 0x72, 0x28, 0x72, 0x37, 0x7a, 0x91,
    0x72, 0x0, 0x72, 0x0, 0x72, 0x0,

    /* U+0051 "Q" */
    0x2a, 0xa1, 0x81, 0x45, 0x90, 0x36, 0x90, 0x36,
    0x90, 0x36, 0x81, 0x55, 0x2b, 0xa1, 0x6, 0x30,

    /* U+0052 "R" */
    0x7a, 0xb2, 0x72, 0x38, 0x72, 0x36, 0x7a, 0xc1,
    0x72, 0x90, 0x72, 0x72, 0x72, 0x27,

    /* U+0053 "S" */
    0x4a, 0xa0, 0xa0, 0x73, 0x83, 0x0, 0xa, 0x50,
    0x0, 0xa1, 0xa0, 0x73, 0x4a, 0xa0,

    /* U+0054 "T" */
    0x8d, 0x93, 0xa, 0x0, 0xa, 0x0, 0xa, 0x0,
    0xa, 0x0, 0xa, 0x0, 0x9, 0x0,

    /* U+0055 "U" */
    0x81, 0x9, 0x82, 0x19, 0x82, 0x19, 0x82, 0x19,
    0x82, 0x19, 0x73, 0x28, 0x1a, 0xa2,

    /* U+0056 "V" */
    0xa0, 0x46, 0x91, 0x63, 0x64, 0x90, 0x36, 0x90,
    0x9, 0x90, 0xa, 0x70, 0x9, 0x30,

    /* U+0057 "W" */
    0xa0, 0xa1, 0x72, 0x90, 0xb3, 0x90, 0x73, 0xa5,
    0x90, 0x46, 0x77, 0x90, 0x1b, 0x49, 0x80, 0xe,
    0x2a, 0x50, 0xb, 0x9, 0x20,

    /* U+0058 "X" */
    0x90, 0x63, 0x54, 0x90, 0xb, 0x70, 0xa, 0x30,
    0xb, 0x70, 0x54, 0x90, 0x90, 0x63,

    /* U+0059 "Y" */
    0xa0, 0x73, 0x54, 0xa0, 0x9, 0x80, 0xa, 0x30,
    0x9, 0x10, 0x9, 0x10, 0x8, 0x10,

    /* U+005A "Z" */
    0x7a, 0xd1, 0x0, 0xa0, 0x4, 0x60, 0xa, 0x0,
    0x19, 0x0, 0x64, 0x0, 0xba, 0xa1,

    /* U+005B "[" */
    0x6a, 0x7, 0x20, 0x72, 0x7, 0x20, 0x72, 0x7,
    0x20, 0x72, 0x7, 0x20, 0x6a, 0x0,

    /* U+005C "\\" */
    0x54, 0x1, 0x80, 0xa, 0x0, 0x90, 0x6, 0x40,
    0x27, 0x0, 0x90,

    /* U+005D "]" */
    0x3b, 0x20, 0x63, 0x6, 0x30, 0x63, 0x6, 0x30,
    0x63, 0x6, 0x30, 0x63, 0x3b, 0x20,

    /* U+005E "^" */
    0x7, 0x60, 0x8, 0x80, 0x17, 0x70, 0x62, 0x34,

    /* U+005F "_" */
    0x69, 0x91,

    /* U+0060 "`" */
    0x40, 0x43,

    /* U+0061 "a" */
    0x3b, 0xa0, 0x40, 0x82, 0x39, 0xc2, 0xa0, 0x82,
    0x79, 0xc2,

    /* U+0062 "b" */
    0x81, 0x0, 0x81, 0x0, 0x8a, 0xb0, 0x82, 0x63,
    0x81, 0x64, 0x82, 0x63, 0x8a, 0xb0,

    /* U+0063 "c" */
    0x4b, 0xa0, 0xa0, 0x41, 0xa0, 0x0, 0xa0, 0x41,
    0x4b, 0xa0,

    /* U+0064 "d" */
    0x0, 0x45, 0x0, 0x45, 0x2b, 0xb5, 0x73, 0x55,
    0x72, 0x45, 0x73, 0x55, 0x2b, 0xb5,

    /* U+0065 "e" */
    0x4a, 0xa0, 0xa0, 0x82, 0xa9, 0xb2, 0xa0, 0x20,
    0x4b, 0xa0,

    /* U+0066 "f" */
    0x2b, 0x5, 0x40, 0xaa, 0x6, 0x40, 0x64, 0x6,
    0x40, 0x54, 0x0,

    /* U+0067 "g" */
    0x59, 0xc2, 0xa0, 0x82, 0xa0, 0x72, 0xa0, 0x82,
    0x5b, 0xc2, 0x0, 0x91, 0x8, 0x80,

    /* U+0068 "h" */
    0x81, 0x0, 0x81, 0x0, 0x89, 0xb0, 0x81, 0x72,
    0x81, 0x73, 0x81, 0x73, 0x81, 0x63,

    /* U+0069 "i" */
    0x92, 0x0, 0x81, 0x81, 0x81, 0x81, 0x81,

    /* U+006A "j" */
    0x9, 0x20, 0x0, 0x8, 0x10, 0x81, 0x8, 0x10,
    0x81, 0x8, 0x10, 0x91, 0x3b, 0x0,

    /* U+006B "k" */
    0x81, 0x0, 0x81, 0x0, 0x81, 0xa0, 0x88, 0x50,
    0x89, 0x80, 0x81, 0x90, 0x81, 0x72,

    /* U+006C "l" */
    0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,

    /* U+006D "m" */
    0x88, 0xb8, 0xb0, 0x82, 0x83, 0x73, 0x81, 0x72,
    0x63, 0x81, 0x72, 0x63, 0x81, 0x72, 0x63,

    /* U+006E "n" */
    0x89, 0xa0, 0x81, 0x72, 0x81, 0x73, 0x81, 0x73,
    0x81, 0x63,

    /* U+006F "o" */
    0x4b, 0xa0, 0xa0, 0x72, 0xa0, 0x73, 0xa0, 0x72,
    0x4b, 0xa0,

    /* U+0070 "p" */
    0x7a, 0xb1, 0x82, 0x54, 0x82, 0x55, 0x82, 0x64,
    0x8a, 0xc1, 0x82, 0x0, 0x72, 0x0,

    /* U+0071 "q" */
    0x2a, 0xb5, 0x73, 0x55, 0x72, 0x45, 0x73, 0x55,
    0x2b, 0xb5, 0x0, 0x45, 0x0, 0x45,

    /* U+0072 "r" */
    0x8a, 0x48, 0x30, 0x81, 0x8, 0x10, 0x81, 0x0,

    /* U+0073 "s" */
    0x6a, 0x89, 0x4, 0x2b, 0x44, 0xa, 0x69, 0x90,

    /* U+0074 "t" */
    0x63, 0xa, 0xa0, 0x63, 0x6, 0x30, 0x63, 0x3,
    0xb0,

    /* U+0075 "u" */
    0x81, 0x63, 0x81, 0x63, 0x81, 0x63, 0x81, 0x73,
    0x4a, 0xb3,

    /* U+0076 "v" */
    0xa0, 0xa1, 0x72, 0xa0, 0x46, 0x90, 0xc, 0x50,
    0xc, 0x20,

    /* U+0077 "w" */
    0x90, 0xb0, 0x98, 0x2d, 0x27, 0x57, 0x87, 0x42,
    0xd2, 0xd1, 0xc, 0xc, 0x0,

    /* U+0078 "x" */
    0xa1, 0xa0, 0x3a, 0x70, 0xd, 0x10, 0x3a, 0x70,
    0xa1, 0xa0,

    /* U+0079 "y" */
    0xa0, 0x90, 0x72, 0xa0, 0x45, 0x80, 0xb, 0x50,
    0xc, 0x10, 0xa, 0x0, 0x67, 0x0,

    /* U+007A "z" */
    0x7a, 0xd0, 0x56, 0xb, 0x5, 0x60, 0xca, 0x80,

    /* U+007B "{" */
    0x1a, 0x5, 0x50, 0x55, 0x6, 0x40, 0xb1, 0x5,
    0x40, 0x55, 0x5, 0x50, 0x1b, 0x0,

    /* U+007C "|" */
    0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72,

    /* U+007D "}" */
    0x67, 0x0, 0xa0, 0xa, 0x0, 0xa0, 0x9, 0x30,
    0xa0, 0xa, 0x0, 0xa0, 0x67, 0x0,

    /* U+007E "~" */
    0x9, 0x75, 0x10, 0x5, 0xa0,

    /* U+00B7 "·" */
    0x74, 0x0,

    /* U+00D7 "×" */
    0x30, 0x33, 0xa6, 0x39, 0x73, 0x3,

    /* U+2026 "…" */
    0x0, 0x0, 0x0, 0x83, 0x83, 0x92,

    /* U+21E7 "⇧" */
    0x0, 0x0, 0x0, 0x0, 0x83, 0x0, 0x7, 0x4,
    0x30, 0x58, 0x5, 0x70, 0x6, 0x6, 0x0, 0x6,
    0x6, 0x0, 0x6, 0x6, 0x0, 0x5, 0x67, 0x0,

    /* U+2212 "−" */
    0x68, 0x84,

    /* U+232B "⌫" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x4e, 0xff, 0xeb,
    0x4, 0xf6, 0x6b, 0x2d, 0x3f, 0xff, 0x40, 0xee,
    0xb, 0xfb, 0x24, 0x6e, 0x0, 0xb9, 0xdf, 0x8d,
    0x0, 0x6, 0x77, 0x73,

    /* U+2713 "✓" */
    0x0, 0x0, 0x50, 0x0, 0x4, 0x90, 0x31, 0x1c,
    0x0, 0x4c, 0xa4, 0x0, 0x3, 0x80, 0x0,

    /* U+2715 "✕" */
    0x0, 0x0, 0x2, 0xa0, 0x58, 0x5, 0xca, 0x0,
    0x5c, 0xa0, 0x2a, 0x5, 0x80, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 29, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 27, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 7, .adv_w = 38, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 12, .adv_w = 75, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 30, .adv_w = 57, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 48, .adv_w = 99, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 69, .adv_w = 71, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 87, .adv_w = 21, .box_w = 1, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 89, .adv_w = 31, .box_w = 2, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 98, .adv_w = 32, .box_w = 2, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 107, .adv_w = 56, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 115, .adv_w = 63, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 123, .adv_w = 22, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 126, .adv_w = 46, .box_w = 3, .box_h = 1, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 128, .adv_w = 27, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 130, .adv_w = 42, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 141, .adv_w = 62, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 155, .adv_w = 38, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 162, .adv_w = 55, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 176, .adv_w = 57, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 59, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 204, .adv_w = 57, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 218, .adv_w = 57, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 232, .adv_w = 51, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 246, .adv_w = 56, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 260, .adv_w = 51, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 271, .adv_w = 30, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 275, .adv_w = 24, .box_w = 2, .box_h = 5, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 280, .adv_w = 64, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 290, .adv_w = 64, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 296, .adv_w = 63, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 306, .adv_w = 51, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 317, .adv_w = 107, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 342, .adv_w = 60, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 356, .adv_w = 62, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 370, .adv_w = 60, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 384, .adv_w = 63, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 398, .adv_w = 57, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 412, .adv_w = 56, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 426, .adv_w = 62, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 440, .adv_w = 64, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 454, .adv_w = 27, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 461, .adv_w = 59, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 475, .adv_w = 61, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 489, .adv_w = 56, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 503, .adv_w = 71, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 517, .adv_w = 67, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 531, .adv_w = 63, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 545, .adv_w = 60, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 559, .adv_w = 61, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 575, .adv_w = 61, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 589, .adv_w = 57, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 603, .adv_w = 55, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 617, .adv_w = 65, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 631, .adv_w = 58, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 645, .adv_w = 87, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 666, .adv_w = 56, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 680, .adv_w = 56, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 694, .adv_w = 54, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 708, .adv_w = 36, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 722, .adv_w = 48, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 733, .adv_w = 44, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 747, .adv_w = 63, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 755, .adv_w = 55, .box_w = 4, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 757, .adv_w = 26, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 759, .adv_w = 57, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 769, .adv_w = 58, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 783, .adv_w = 55, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 793, .adv_w = 61, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 807, .adv_w = 56, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 817, .adv_w = 35, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 828, .adv_w = 58, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 842, .adv_w = 58, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 856, .adv_w = 25, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 863, .adv_w = 25, .box_w = 3, .box_h = 9, .ofs_x = -1, .ofs_y = -2},
    {.bitmap_index = 877, .adv_w = 54, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 891, .adv_w = 25, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 898, .adv_w = 91, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 913, .adv_w = 58, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 923, .adv_w = 56, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 933, .adv_w = 62, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 947, .adv_w = 62, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 961, .adv_w = 40, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 969, .adv_w = 51, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 977, .adv_w = 34, .box_w = 3, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 986, .adv_w = 60, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 996, .adv_w = 54, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1006, .adv_w = 79, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1019, .adv_w = 52, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1029, .adv_w = 53, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1043, .adv_w = 49, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1051, .adv_w = 36, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1065, .adv_w = 26, .box_w = 2, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1073, .adv_w = 39, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1087, .adv_w = 80, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1092, .adv_w = 28, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 1094, .adv_w = 53, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 1100, .adv_w = 90, .box_w = 6, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1106, .adv_w = 86, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1130, .adv_w = 63, .box_w = 4, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1132, .adv_w = 86, .box_w = 8, .box_h = 7, .ofs_x = -2, .ofs_y = 0},
    {.bitmap_index = 1160, .adv_w = 86, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 1175, .adv_w = 86, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_1[] = {
    0x0, 0x20, 0x1f6f, 0x2130, 0x215b, 0x2274, 0x265c, 0x265e
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 183, .range_length = 9823, .glyph_id_start = 96,
        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = 8, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Pair left and right glyphs for kerning*/
static const uint8_t kern_pair_glyph_ids[] =
{
    1, 10,
    1, 16,
    1, 32,
    1, 33,
    1, 61,
    1, 62,
    1, 94,
    3, 10,
    3, 21,
    3, 23,
    3, 24,
    3, 27,
    3, 28,
    3, 62,
    3, 92,
    3, 94,
    7, 3,
    7, 8,
    7, 11,
    7, 13,
    7, 15,
    7, 28,
    7, 62,
    7, 92,
    7, 94,
    7, 98,
    8, 10,
    8, 21,
    8, 23,
    8, 24,
    8, 27,
    8, 28,
    8, 62,
    8, 92,
    8, 94,
    9, 1,
    9, 3,
    9, 7,
    9, 8,
    9, 9,
    9, 13,
    9, 15,
    9, 17,
    9, 20,
    9, 21,
    9, 23,
    9, 25,
    9, 26,
    9, 27,
    9, 28,
    9, 92,
    9, 98,
    10, 10,
    10, 13,
    10, 15,
    10, 62,
    10, 94,
    10, 98,
    11, 7,
    11, 11,
    11, 13,
    11, 15,
    11, 18,
    11, 20,
    11, 21,
    11, 23,
    11, 24,
    11, 26,
    11, 27,
    11, 28,
    11, 32,
    11, 92,
    11, 98,
    13, 2,
    13, 7,
    13, 9,
    13, 10,
    13, 11,
    13, 18,
    13, 19,
    13, 20,
    13, 21,
    13, 22,
    13, 23,
    13, 24,
    13, 25,
    13, 26,
    13, 32,
    13, 33,
    13, 61,
    13, 64,
    13, 92,
    13, 96,
    15, 2,
    15, 7,
    15, 9,
    15, 10,
    15, 11,
    15, 18,
    15, 19,
    15, 20,
    15, 21,
    15, 24,
    15, 26,
    15, 32,
    15, 61,
    15, 64,
    15, 92,
    15, 96,
    16, 1,
    16, 13,
    16, 15,
    16, 16,
    16, 17,
    16, 21,
    16, 23,
    16, 25,
    16, 27,
    16, 28,
    16, 98,
    17, 10,
    17, 16,
    17, 24,
    17, 34,
    17, 53,
    17, 55,
    17, 56,
    17, 57,
    17, 58,
    17, 61,
    17, 62,
    17, 94,
    18, 75,
    19, 13,
    19, 14,
    19, 15,
    19, 21,
    19, 24,
    19, 53,
    19, 55,
    19, 56,
    19, 57,
    19, 58,
    19, 61,
    19, 75,
    19, 89,
    19, 96,
    19, 98,
    20, 10,
    20, 24,
    20, 34,
    20, 53,
    20, 55,
    20, 56,
    20, 57,
    20, 58,
    20, 61,
    20, 62,
    20, 94,
    21, 6,
    21, 10,
    21, 11,
    21, 18,
    21, 19,
    21, 24,
    21, 26,
    21, 34,
    21, 53,
    21, 55,
    21, 56,
    21, 57,
    21, 58,
    21, 59,
    21, 61,
    21, 62,
    21, 71,
    21, 87,
    21, 88,
    21, 89,
    21, 90,
    21, 94,
    22, 10,
    22, 18,
    22, 19,
    22, 24,
    22, 34,
    22, 53,
    22, 55,
    22, 56,
    22, 58,
    22, 87,
    22, 88,
    22, 89,
    22, 90,
    23, 3,
    23, 6,
    23, 8,
    23, 10,
    23, 11,
    23, 18,
    23, 19,
    23, 24,
    23, 26,
    23, 34,
    23, 53,
    23, 55,
    23, 56,
    23, 57,
    23, 58,
    23, 61,
    23, 62,
    23, 71,
    23, 87,
    23, 88,
    23, 89,
    23, 90,
    23, 94,
    24, 3,
    24, 4,
    24, 8,
    24, 11,
    24, 13,
    24, 14,
    24, 15,
    24, 16,
    24, 21,
    24, 23,
    24, 34,
    24, 43,
    24, 56,
    24, 57,
    24, 66,
    24, 68,
    24, 69,
    24, 70,
    24, 72,
    24, 78,
    24, 79,
    24, 80,
    24, 81,
    24, 82,
    24, 83,
    24, 84,
    24, 86,
    24, 87,
    24, 88,
    24, 89,
    24, 90,
    24, 91,
    24, 96,
    24, 98,
    25, 10,
    25, 24,
    25, 34,
    25, 53,
    25, 55,
    25, 56,
    25, 57,
    25, 58,
    25, 61,
    25, 62,
    25, 94,
    26, 10,
    26, 11,
    26, 13,
    26, 15,
    26, 16,
    26, 21,
    26, 23,
    26, 34,
    26, 43,
    26, 55,
    26, 56,
    26, 57,
    26, 58,
    26, 62,
    26, 85,
    26, 89,
    26, 94,
    26, 96,
    26, 98,
    27, 10,
    27, 11,
    27, 32,
    27, 61,
    27, 62,
    27, 94,
    27, 96,
    28, 10,
    28, 11,
    28, 32,
    28, 61,
    28, 62,
    28, 94,
    28, 96,
    32, 10,
    32, 11,
    32, 32,
    32, 62,
    32, 94,
    33, 1,
    33, 13,
    33, 15,
    33, 98,
    60, 1,
    60, 3,
    60, 7,
    60, 8,
    60, 9,
    60, 13,
    60, 17,
    60, 18,
    60, 20,
    60, 21,
    60, 23,
    60, 25,
    60, 26,
    60, 27,
    60, 28,
    60, 92,
    61, 1,
    61, 13,
    61, 17,
    61, 18,
    61, 21,
    61, 22,
    61, 24,
    61, 26,
    61, 61,
    64, 13,
    64, 15,
    64, 98,
    92, 1,
    92, 3,
    92, 7,
    92, 8,
    92, 9,
    92, 13,
    92, 17,
    92, 18,
    92, 20,
    92, 21,
    92, 23,
    92, 25,
    92, 26,
    92, 27,
    92, 28,
    92, 92,
    94, 3,
    94, 8,
    94, 10,
    94, 11,
    94, 13,
    94, 15,
    94, 62,
    94, 94,
    94, 98,
    96, 13,
    96, 15,
    96, 18,
    96, 19,
    96, 20,
    96, 22,
    96, 24,
    96, 26,
    96, 27,
    96, 28,
    96, 98,
    98, 2,
    98, 7,
    98, 9,
    98, 10,
    98, 11,
    98, 18,
    98, 19,
    98, 20,
    98, 21,
    98, 24,
    98, 26,
    98, 32,
    98, 61,
    98, 64,
    98, 92,
    98, 96
};

/* Kerning between the respective left and right glyphs
 * 4.4 format which needs to scaled with `kern_scale`*/
static const int8_t kern_pair_values[] =
{
    -1, -3, -3, -3, -3, -1, -1, 1,
    -7, -5, 0, -5, -5, 0, -1, 0,
    -7, -7, -8, 1, 0, 0, 0, 0,
    0, 0, 1, -7, -5, 0, -5, -5,
    0, -1, 0, -1, 1, -1, 1, -1,
    1, -1, -1, -1, -2, -1, -1, -1,
    -2, -1, -1, -1, -1, -1, -1, -2,
    -1, -1, -1, 0, -11, -13, 0, 0,
    -6, -4, 1, 0, -2, -2, 0, -1,
    -13, 0, 1, -1, -1, -11, -6, 0,
    1, -2, 0, 1, -4, 0, -5, -4,
    1, -8, 0, -2, -8, 0, 0, -1,
    -1, -13, -6, 0, 0, -2, -4, -5,
    -4, -8, 0, -2, -8, -3, -8, -8,
    -15, -1, -4, -4, -1, -4, -4, -8,
    -1, -1, -1, -1, -2, -1, -1, -1,
    -2, -1, -3, -3, 1, 0, -4, 1,
    -3, -1, -2, -1, 0, 0, -2, -1,
    2, 0, -3, 1, -1, -1, 0, -2,
    -1, -1, -1, -2, -1, -3, -3, -1,
    -1, -1, -2, -1, -2, -1, 0, -2,
    -2, -2, -1, -3, -1, -1, -2, 0,
    -1, -1, -1, -1, -2, -1, -2, -1,
    0, 0, 0, -1, 0, -1, -1, 0,
    0, -1, -5, -3, -5, -1, -5, -5,
    -1, -4, -1, 0, -4, -4, -3, -1,
    -5, -3, -3, 0, -1, -1, -1, -1,
    -3, 0, -2, 0, 0, -8, -5, -8,
    -6, -5, -4, -6, -6, 0, 0, -4,
    -4, -4, -4, -4, -3, -3, -4, -3,
    -4, -3, -3, -2, -1, -1, -1, -1,
    -2, -5, -8, -1, -1, -1, -2, -1,
    -1, -1, -2, -1, -3, -3, -1, 0,
    -7, -7, -4, -2, -1, -4, -4, 0,
    0, -1, -1, -3, 0, 0, -2, -2,
    -7, -2, -2, -3, -4, -1, -1, -2,
    -2, -2, -3, -4, -1, -1, -2, -1,
    0, 0, -2, -1, -1, -4, -4, -4,
    -1, 0, -3, 0, -2, 0, -3, -2,
    -2, -5, -4, -3, -2, -1, -1, -1,
    -3, 1, -1, -5, -1, -1, -2, -3,
    -15, 0, 0, 0, -1, 0, -2, 0,
    -1, 0, -3, -2, -2, -4, -3, -2,
    -2, -1, -1, -1, -1, -1, -1, -1,
    -2, -2, -1, -1, -2, -8, -8, -7,
    -4, -1, -1, -7, -2, -2, -2, -8,
    0, 0, -1, -1, -13, -6, 0, 0,
    -2, -4, -5, -4, -8, 0, -2, -8
};

/*Collect the kern pair's data in one place*/
static const lv_font_fmt_txt_kern_pair_t kern_pairs =
{
    .glyph_ids = kern_pair_glyph_ids,
    .values = kern_pair_values,
    .pair_cnt = 384,
    .glyph_ids_size = 0
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_pairs,
    .kern_scale = 16,
    .cmap_num = 2,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ob_font_label_9 = {
#else
lv_font_t ob_font_label_9 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 10,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if OB_FONT_LABEL_9*/

