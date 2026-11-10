/*******************************************************************************
 * Size: 10 px
 * Bpp: 1
 * Opts: --bpp 1 --format lvgl --no-compress --lv-include lvgl.h --font C:/Users/stryc/AppData/Local/Temp/claude/514d5dbaf833c194/35e6cb28-4b71-4fdc-9a62-cff25467859e/scratchpad/fontconv/ttf/JetBrainsMono-Regular.ttf --autohint-strong --range 0x20-0x7E,0xA0-0x17F,0x2014,0x2026,0x203A,0x2212,0x2713,0x2715,0x21E7,0x232B --size 10 -o C:/Users/stryc/AppData/Local/Temp/claude/514d5dbaf833c194/35e6cb28-4b71-4fdc-9a62-cff25467859e/scratchpad/fontconv/out/ob_font_meta_10.c --lv-font-name ob_font_meta_10
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef OB_FONT_META_10
#define OB_FONT_META_10 1
#endif

#if OB_FONT_META_10

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0x55, 0x43,

    /* U+0022 "\"" */
    0xb6, 0x80,

    /* U+0023 "#" */
    0x28, 0xa7, 0xd2, 0x53, 0xe5, 0x14,

    /* U+0024 "$" */
    0x22, 0xfb, 0xa7, 0x3b, 0xb2, 0x20,

    /* U+0025 "%" */
    0xe6, 0xab, 0x3c, 0x3c, 0xd5, 0x67,

    /* U+0026 "&" */
    0x71, 0x4, 0x18, 0x96, 0x69, 0x99,

    /* U+0027 "'" */
    0xe0,

    /* U+0028 "(" */
    0x2a, 0x49, 0x24, 0x4c,

    /* U+0029 ")" */
    0x88, 0x92, 0x49, 0x58,

    /* U+002A "*" */
    0x21, 0x3e, 0xc5, 0x0,

    /* U+002B "+" */
    0x21, 0x3e, 0x42, 0x0,

    /* U+002C "," */
    0x68,

    /* U+002D "-" */
    0xf0,

    /* U+002E "." */
    0x80,

    /* U+002F "/" */
    0x11, 0x22, 0x24, 0x44, 0x88,

    /* U+0030 "0" */
    0x69, 0x9f, 0x99, 0x96,

    /* U+0031 "1" */
    0x65, 0x8, 0x42, 0x10, 0x9f,

    /* U+0032 "2" */
    0x69, 0x11, 0x24, 0xcf,

    /* U+0033 "3" */
    0xf3, 0x26, 0x11, 0x96,

    /* U+0034 "4" */
    0x22, 0x44, 0x9f, 0x11,

    /* U+0035 "5" */
    0xf8, 0x8e, 0x11, 0x96,

    /* U+0036 "6" */
    0x21, 0x10, 0xe8, 0xc6, 0x2e,

    /* U+0037 "7" */
    0xfc, 0x84, 0x22, 0x10, 0x88,

    /* U+0038 "8" */
    0x74, 0x62, 0xe8, 0xc6, 0x2e,

    /* U+0039 "9" */
    0x74, 0x63, 0x17, 0x8, 0x84,

    /* U+003A ":" */
    0x84,

    /* U+003B ";" */
    0x87,

    /* U+003C "<" */
    0x1b, 0x20, 0xc1, 0x80,

    /* U+003D "=" */
    0xf0, 0xf0,

    /* U+003E ">" */
    0xc1, 0x82, 0x6c, 0x0,

    /* U+003F "?" */
    0xe1, 0x11, 0x64, 0x4,

    /* U+0040 "@" */
    0x76, 0x63, 0x7a, 0xd6, 0xb7, 0xc3, 0x80,

    /* U+0041 "A" */
    0x30, 0xc3, 0x14, 0x49, 0xe4, 0x92,

    /* U+0042 "B" */
    0xe9, 0x9e, 0x99, 0x9e,

    /* U+0043 "C" */
    0x69, 0x88, 0x88, 0x96,

    /* U+0044 "D" */
    0xe9, 0x99, 0x99, 0x9e,

    /* U+0045 "E" */
    0xf8, 0x8f, 0x88, 0x8f,

    /* U+0046 "F" */
    0xf8, 0x88, 0xf8, 0x88,

    /* U+0047 "G" */
    0x69, 0x88, 0xb9, 0x96,

    /* U+0048 "H" */
    0x99, 0x9f, 0x99, 0x99,

    /* U+0049 "I" */
    0xf9, 0x8, 0x42, 0x10, 0x9f,

    /* U+004A "J" */
    0x8, 0x42, 0x10, 0x86, 0x2e,

    /* U+004B "K" */
    0x94, 0xa9, 0xca, 0x4a, 0x51,

    /* U+004C "L" */
    0x88, 0x88, 0x88, 0x8f,

    /* U+004D "M" */
    0xde, 0xfb, 0x58, 0xc6, 0x31,

    /* U+004E "N" */
    0x9d, 0xdd, 0xdb, 0xb9,

    /* U+004F "O" */
    0x69, 0x99, 0x99, 0x96,

    /* U+0050 "P" */
    0xf4, 0x63, 0x1f, 0x42, 0x10,

    /* U+0051 "Q" */
    0x69, 0x99, 0x99, 0x96, 0x21,

    /* U+0052 "R" */
    0xe9, 0x99, 0xea, 0xa9,

    /* U+0053 "S" */
    0x74, 0x60, 0xc1, 0x86, 0x2e,

    /* U+0054 "T" */
    0xf9, 0x8, 0x42, 0x10, 0x84,

    /* U+0055 "U" */
    0x99, 0x99, 0x99, 0x96,

    /* U+0056 "V" */
    0x49, 0x24, 0x92, 0x50, 0xc3, 0xc,

    /* U+0057 "W" */
    0xad, 0x6b, 0xdf, 0x7b, 0x5a,

    /* U+0058 "X" */
    0x49, 0x23, 0xc, 0x30, 0xc4, 0x92,

    /* U+0059 "Y" */
    0x8c, 0x54, 0xa2, 0x10, 0x84,

    /* U+005A "Z" */
    0xf1, 0x22, 0x44, 0x8f,

    /* U+005B "[" */
    0xf2, 0x49, 0x24, 0x9c,

    /* U+005C "\\" */
    0x88, 0x44, 0x42, 0x22, 0x11,

    /* U+005D "]" */
    0xe4, 0x92, 0x49, 0x3c,

    /* U+005E "^" */
    0x66, 0x99,

    /* U+005F "_" */
    0xf0,

    /* U+0060 "`" */
    0xa0,

    /* U+0061 "a" */
    0x71, 0xf9, 0x9f,

    /* U+0062 "b" */
    0x88, 0xe9, 0x99, 0x9e,

    /* U+0063 "c" */
    0x74, 0xa1, 0x9, 0x38,

    /* U+0064 "d" */
    0x11, 0x79, 0x99, 0x97,

    /* U+0065 "e" */
    0x69, 0xf8, 0x96,

    /* U+0066 "f" */
    0x39, 0x9, 0xf2, 0x10, 0x84,

    /* U+0067 "g" */
    0x79, 0x99, 0x97, 0x17,

    /* U+0068 "h" */
    0x88, 0xf9, 0x99, 0x99,

    /* U+0069 "i" */
    0x21, 0x0, 0xc2, 0x10, 0x84, 0xf8,

    /* U+006A "j" */
    0x24, 0x72, 0x49, 0x27, 0x0,

    /* U+006B "k" */
    0x84, 0x25, 0x6e, 0x52, 0x52,

    /* U+006C "l" */
    0xe1, 0x8, 0x42, 0x10, 0x87,

    /* U+006D "m" */
    0xfd, 0x6b, 0x5a, 0xd4,

    /* U+006E "n" */
    0xf9, 0x99, 0x99,

    /* U+006F "o" */
    0x69, 0x99, 0x96,

    /* U+0070 "p" */
    0xe9, 0x99, 0x9e, 0x88,

    /* U+0071 "q" */
    0x79, 0x99, 0x9f, 0x11,

    /* U+0072 "r" */
    0xf9, 0x98, 0x88,

    /* U+0073 "s" */
    0xf9, 0xc3, 0x9f,

    /* U+0074 "t" */
    0x21, 0x3e, 0x42, 0x10, 0x87,

    /* U+0075 "u" */
    0x99, 0x99, 0x96,

    /* U+0076 "v" */
    0x99, 0x96, 0x66,

    /* U+0077 "w" */
    0xb6, 0xeb, 0x9e, 0x49, 0x20,

    /* U+0078 "x" */
    0x48, 0xc3, 0xc, 0x49, 0x20,

    /* U+0079 "y" */
    0x99, 0x96, 0x62, 0x44,

    /* U+007A "z" */
    0xf1, 0x24, 0x8f,

    /* U+007B "{" */
    0x32, 0x22, 0xc2, 0x22, 0x23,

    /* U+007C "|" */
    0xff, 0xc0,

    /* U+007D "}" */
    0xc4, 0x44, 0x34, 0x44, 0x4c,

    /* U+007E "~" */
    0xed, 0xc0,

    /* U+00A0 " " */
    0x0,

    /* U+00A1 "¡" */
    0xc1, 0x55,

    /* U+00A2 "¢" */
    0x21, 0x3d, 0x4a, 0x53, 0xc4, 0x20,

    /* U+00A3 "£" */
    0x74, 0xa1, 0xf, 0x23, 0x1f,

    /* U+00A4 "¤" */
    0x1, 0xe4, 0x92, 0x49, 0xe0, 0x0,

    /* U+00A5 "¥" */
    0x8c, 0x54, 0xaf, 0x93, 0xe4,

    /* U+00A6 "¦" */
    0xf3, 0xc0,

    /* U+00A7 "§" */
    0xe9, 0x86, 0x99, 0x61, 0x9f,

    /* U+00A8 "¨" */
    0xa0,

    /* U+00A9 "©" */
    0x77, 0x73, 0x9c, 0xf5, 0xc0,

    /* U+00AA "ª" */
    0xdf, 0x80,

    /* U+00AB "«" */
    0x29, 0x49, 0x14, 0x28,

    /* U+00AC "¬" */
    0xf1, 0x10,

    /* U+00AE "®" */
    0x74, 0x7b, 0xde, 0xc5, 0xc0,

    /* U+00AF "¯" */
    0xf0,

    /* U+00B0 "°" */
    0xf7, 0x80,

    /* U+00B1 "±" */
    0x21, 0x3e, 0x42, 0x7c,

    /* U+00B2 "²" */
    0xf5, 0x70,

    /* U+00B3 "³" */
    0x72, 0x17,

    /* U+00B4 "´" */
    0x50,

    /* U+00B5 "µ" */
    0x99, 0x99, 0x9f, 0x88,

    /* U+00B6 "¶" */
    0x6f, 0x7b, 0xd2, 0x94, 0xa5, 0x29, 0x40,

    /* U+00B7 "·" */
    0xc0,

    /* U+00B8 "¸" */
    0xb0,

    /* U+00B9 "¹" */
    0xc9, 0x70,

    /* U+00BA "º" */
    0xf7, 0x80,

    /* U+00BB "»" */
    0x50, 0xa2, 0x5a, 0x50,

    /* U+00BC "¼" */
    0xc9, 0x65, 0x38, 0x21, 0x4b, 0xa2,

    /* U+00BD "½" */
    0xca, 0x95, 0xc3, 0xa6, 0x53,

    /* U+00BE "¾" */
    0x65, 0x22, 0x9c, 0x20, 0xe5, 0xe1,

    /* U+00BF "¿" */
    0x20, 0x26, 0x88, 0x87,

    /* U+00C0 "À" */
    0x20, 0x43, 0xc, 0x31, 0x44, 0x9e, 0x49, 0x20,

    /* U+00C1 "Á" */
    0x10, 0x83, 0xc, 0x31, 0x44, 0x9e, 0x49, 0x20,

    /* U+00C2 "Â" */
    0x31, 0x23, 0xc, 0x31, 0x44, 0x9e, 0x49, 0x20,

    /* U+00C3 "Ã" */
    0x70, 0x8, 0xc5, 0x29, 0x4e, 0x8c, 0x40,

    /* U+00C4 "Ä" */
    0x50, 0x8, 0xc5, 0x29, 0x4e, 0x94, 0x40,

    /* U+00C5 "Å" */
    0x64, 0x98, 0xc6, 0x31, 0x52, 0xf4, 0xa2,

    /* U+00C6 "Æ" */
    0x3d, 0x45, 0x17, 0x51, 0xc9, 0x27,

    /* U+00C7 "Ç" */
    0x69, 0x88, 0x88, 0x96, 0x66,

    /* U+00C8 "È" */
    0x42, 0xf8, 0x8f, 0x88, 0x8f,

    /* U+00C9 "É" */
    0x22, 0xf8, 0x8f, 0x88, 0x8f,

    /* U+00CA "Ê" */
    0x69, 0xf8, 0x8f, 0x88, 0x8f,

    /* U+00CB "Ë" */
    0x90, 0xf8, 0x8f, 0x88, 0x8f,

    /* U+00CC "Ì" */
    0x41, 0x3e, 0x42, 0x10, 0x84, 0x27, 0xc0,

    /* U+00CD "Í" */
    0x11, 0x3e, 0x42, 0x10, 0x84, 0x27, 0xc0,

    /* U+00CE "Î" */
    0x22, 0xbe, 0x42, 0x10, 0x84, 0x27, 0xc0,

    /* U+00CF "Ï" */
    0xa3, 0xa4, 0x92, 0x5c,

    /* U+00D0 "Ð" */
    0x72, 0x52, 0x9e, 0xa5, 0x2e,

    /* U+00D1 "Ñ" */
    0x60, 0x9d, 0xdd, 0xdb, 0xb9,

    /* U+00D2 "Ò" */
    0x44, 0x6, 0x99, 0x99, 0x99, 0x60,

    /* U+00D3 "Ó" */
    0x22, 0x6, 0x99, 0x99, 0x99, 0x60,

    /* U+00D4 "Ô" */
    0x6, 0x6, 0x99, 0x99, 0x99, 0x60,

    /* U+00D5 "Õ" */
    0x60, 0x69, 0x99, 0x99, 0x96,

    /* U+00D6 "Ö" */
    0x50, 0x69, 0x99, 0x99, 0x96,

    /* U+00D7 "×" */
    0x96, 0x6e, 0x0,

    /* U+00D8 "Ø" */
    0x39, 0x24, 0x96, 0x69, 0x24, 0x9c, 0x80,

    /* U+00D9 "Ù" */
    0x44, 0x9, 0x99, 0x99, 0x99, 0x60,

    /* U+00DA "Ú" */
    0x22, 0x9, 0x99, 0x99, 0x99, 0x60,

    /* U+00DB "Û" */
    0x6, 0x9, 0x99, 0x99, 0x99, 0x60,

    /* U+00DC "Ü" */
    0x90, 0x99, 0x99, 0x99, 0x96,

    /* U+00DD "Ý" */
    0x11, 0x23, 0x15, 0x28, 0x84, 0x21, 0x0,

    /* U+00DE "Þ" */
    0x84, 0x3d, 0x18, 0xfa, 0x10,

    /* U+00DF "ß" */
    0x69, 0x9a, 0x99, 0x9a,

    /* U+00E0 "à" */
    0x44, 0x7, 0x9f, 0x99, 0xf0,

    /* U+00E1 "á" */
    0x22, 0x7, 0x1f, 0x99, 0xf0,

    /* U+00E2 "â" */
    0x6, 0x7, 0x1f, 0x99, 0xf0,

    /* U+00E3 "ã" */
    0x16, 0x7, 0x1f, 0x99, 0xf0,

    /* U+00E4 "ä" */
    0x55, 0x7, 0x1f, 0x99, 0xf0,

    /* U+00E5 "å" */
    0x6a, 0x67, 0x1f, 0x99, 0xf0,

    /* U+00E6 "æ" */
    0xdd, 0x7f, 0x4a, 0xec,

    /* U+00E7 "ç" */
    0x74, 0xa1, 0x9, 0x38, 0x8c,

    /* U+00E8 "è" */
    0x44, 0x6, 0x9f, 0x89, 0x60,

    /* U+00E9 "é" */
    0x22, 0x6, 0x9f, 0x89, 0x60,

    /* U+00EA "ê" */
    0x6, 0x6, 0x9f, 0x89, 0x60,

    /* U+00EB "ë" */
    0x55, 0x6, 0x9f, 0x89, 0x60,

    /* U+00EC "ì" */
    0x41, 0x1, 0xc2, 0x10, 0x84, 0xf8,

    /* U+00ED "í" */
    0x11, 0x1, 0xc2, 0x10, 0x84, 0xf8,

    /* U+00EE "î" */
    0x22, 0x81, 0xc2, 0x10, 0x84, 0xf8,

    /* U+00EF "ï" */
    0x52, 0x80, 0xc2, 0x10, 0x84, 0xf8,

    /* U+00F0 "ð" */
    0x73, 0x4, 0xd9, 0xc6, 0x2e,

    /* U+00F1 "ñ" */
    0x16, 0xf, 0x99, 0x99, 0x90,

    /* U+00F2 "ò" */
    0x44, 0x6, 0x99, 0x99, 0x60,

    /* U+00F3 "ó" */
    0x22, 0x6, 0x99, 0x99, 0x60,

    /* U+00F4 "ô" */
    0x6, 0x6, 0x99, 0x99, 0x60,

    /* U+00F5 "õ" */
    0x6, 0x6, 0x99, 0x99, 0x60,

    /* U+00F6 "ö" */
    0x55, 0x6, 0x99, 0x99, 0x60,

    /* U+00F7 "÷" */
    0x40, 0x70, 0x80,

    /* U+00F8 "ø" */
    0x4, 0xe5, 0x96, 0x69, 0x27, 0x0,

    /* U+00F9 "ù" */
    0x44, 0x9, 0x99, 0x99, 0x60,

    /* U+00FA "ú" */
    0x22, 0x9, 0x99, 0x99, 0x60,

    /* U+00FB "û" */
    0x6, 0x9, 0x99, 0x99, 0x60,

    /* U+00FC "ü" */
    0x99, 0x9, 0x99, 0x99, 0x60,

    /* U+00FD "ý" */
    0x22, 0x9, 0x99, 0x66, 0x24, 0x40,

    /* U+00FE "þ" */
    0x88, 0xe9, 0x99, 0x9e, 0x88,

    /* U+00FF "ÿ" */
    0x52, 0x81, 0x15, 0x29, 0x44, 0x21, 0x10,

    /* U+0100 "Ā" */
    0x78, 0x3, 0xc, 0x31, 0x44, 0x9e, 0x49, 0x20,

    /* U+0101 "ā" */
    0x70, 0x71, 0xf9, 0x9f,

    /* U+0102 "Ă" */
    0x53, 0x88, 0xc5, 0x29, 0x4e, 0x8c, 0x40,

    /* U+0103 "ă" */
    0x56, 0x7, 0x1f, 0x99, 0xf0,

    /* U+0104 "Ą" */
    0x63, 0x18, 0xa9, 0x7a, 0x51, 0x10, 0xc0,

    /* U+0105 "ą" */
    0x70, 0xbd, 0x29, 0x78, 0x83,

    /* U+0106 "Ć" */
    0x22, 0x6, 0x98, 0x88, 0x89, 0x60,

    /* U+0107 "ć" */
    0x11, 0x0, 0xe9, 0x42, 0x12, 0x70,

    /* U+0108 "Ĉ" */
    0x6, 0x6, 0x98, 0x88, 0x89, 0x60,

    /* U+0109 "ĉ" */
    0x22, 0x80, 0xe9, 0x42, 0x12, 0x70,

    /* U+010A "Ċ" */
    0x60, 0x69, 0x88, 0x88, 0x96,

    /* U+010B "ċ" */
    0x21, 0x0, 0xe9, 0x42, 0x12, 0x70,

    /* U+010C "Č" */
    0x60, 0x69, 0x88, 0x88, 0x96,

    /* U+010D "č" */
    0x51, 0x1d, 0x28, 0x42, 0x4e,

    /* U+010E "Ď" */
    0x60, 0xe9, 0x99, 0x99, 0x9e,

    /* U+010F "ď" */
    0x18, 0x67, 0x24, 0x92, 0x49, 0x1c,

    /* U+0110 "Đ" */
    0x72, 0x52, 0x9e, 0xa5, 0x2e,

    /* U+0111 "đ" */
    0x11, 0xdd, 0x29, 0x4a, 0x4e,

    /* U+0112 "Ē" */
    0x70, 0xf8, 0x8f, 0x88, 0x8f,

    /* U+0113 "ē" */
    0x60, 0x69, 0xf8, 0x96,

    /* U+0114 "Ĕ" */
    0xa6, 0xf8, 0x8f, 0x88, 0x8f,

    /* U+0115 "ĕ" */
    0x6, 0x6, 0x9f, 0x89, 0x60,

    /* U+0116 "Ė" */
    0x60, 0xf8, 0x8f, 0x88, 0x8f,

    /* U+0117 "ė" */
    0x22, 0x6, 0x9f, 0x89, 0x60,

    /* U+0118 "Ę" */
    0xf4, 0x21, 0xe8, 0x42, 0x1e, 0x20, 0xc0,

    /* U+0119 "ę" */
    0x69, 0xf8, 0x96, 0x42,

    /* U+011A "Ě" */
    0x52, 0xf8, 0x8f, 0x88, 0x8f,

    /* U+011B "ě" */
    0x96, 0x6, 0x9f, 0x89, 0x60,

    /* U+011C "Ĝ" */
    0x6, 0x6, 0x98, 0x8b, 0x99, 0x60,

    /* U+011D "ĝ" */
    0x6, 0x7, 0x99, 0x99, 0x71, 0x70,

    /* U+011E "Ğ" */
    0x60, 0x69, 0x88, 0xb9, 0x96,

    /* U+011F "ğ" */
    0x6, 0x7, 0x99, 0x99, 0x71, 0x70,

    /* U+0120 "Ġ" */
    0x60, 0x69, 0x88, 0xb9, 0x96,

    /* U+0121 "ġ" */
    0x22, 0x7, 0x99, 0x99, 0x71, 0x70,

    /* U+0122 "Ģ" */
    0x69, 0x88, 0xb9, 0x96, 0x4, 0x40,

    /* U+0123 "ģ" */
    0x22, 0x40, 0x79, 0x99, 0x97, 0x17,

    /* U+0124 "Ĥ" */
    0x69, 0x99, 0x9f, 0x99, 0x99,

    /* U+0125 "ĥ" */
    0x45, 0x0, 0x84, 0x3d, 0x29, 0x4a, 0x52,

    /* U+0126 "Ħ" */
    0x4b, 0xf4, 0x9e, 0x49, 0x24, 0x92,

    /* U+0127 "ħ" */
    0x47, 0x10, 0xf4, 0xa5, 0x29,

    /* U+0128 "Ĩ" */
    0xe3, 0xa4, 0x92, 0x5c,

    /* U+0129 "ĩ" */
    0x13, 0x81, 0xc2, 0x10, 0x84, 0xf8,

    /* U+012A "Ī" */
    0x70, 0x3e, 0x42, 0x10, 0x84, 0x27, 0xc0,

    /* U+012B "ī" */
    0x70, 0x38, 0x42, 0x10, 0x9f,

    /* U+012C "Ĭ" */
    0xbf, 0xa4, 0x92, 0x5c,

    /* U+012D "ĭ" */
    0x53, 0x81, 0xc2, 0x10, 0x84, 0xf8,

    /* U+012E "Į" */
    0xf4, 0x44, 0x44, 0x4f, 0x84,

    /* U+012F "į" */
    0x21, 0x0, 0xc2, 0x10, 0x84, 0xfa, 0x8,

    /* U+0130 "İ" */
    0x60, 0x3e, 0x42, 0x10, 0x84, 0x27, 0xc0,

    /* U+0131 "ı" */
    0xe1, 0x8, 0x42, 0x7c,

    /* U+0132 "Ĳ" */
    0xea, 0x52, 0x9e, 0x85, 0x26,

    /* U+0133 "ĳ" */
    0x99, 0x9, 0x99, 0x99, 0x91, 0xe0,

    /* U+0134 "Ĵ" */
    0x8, 0x50, 0x2, 0x8, 0x20, 0x82, 0xa, 0x27,
    0x0,

    /* U+0135 "ĵ" */
    0x11, 0x41, 0xe1, 0x8, 0x42, 0x10, 0xb8,

    /* U+0136 "Ķ" */
    0x94, 0xa9, 0xca, 0x4a, 0x51, 0x1, 0x10,

    /* U+0137 "ķ" */
    0x84, 0x25, 0x6e, 0x52, 0x52, 0x1, 0x10,

    /* U+0138 "ĸ" */
    0x95, 0xb9, 0x49, 0x48,

    /* U+0139 "Ĺ" */
    0x48, 0x88, 0x88, 0x88, 0x8f,

    /* U+013A "ĺ" */
    0x21, 0x38, 0x42, 0x10, 0x84, 0x21, 0xc0,

    /* U+013B "Ļ" */
    0x88, 0x88, 0x88, 0x8f, 0x2, 0x40,

    /* U+013C "ļ" */
    0xe1, 0x8, 0x42, 0x10, 0x87, 0x0, 0x88,

    /* U+013D "Ľ" */
    0xaa, 0x88, 0x88, 0x8f,

    /* U+013E "ľ" */
    0xe9, 0x48, 0x42, 0x10, 0x87,

    /* U+013F "Ŀ" */
    0x88, 0x88, 0x98, 0x8f,

    /* U+0140 "ŀ" */
    0xe0, 0x82, 0x8, 0x24, 0x82, 0xe,

    /* U+0141 "Ł" */
    0x42, 0x14, 0xcc, 0x21, 0xf,

    /* U+0142 "ł" */
    0xe1, 0xa, 0x66, 0x10, 0x87,

    /* U+0143 "Ń" */
    0x20, 0x9d, 0xdd, 0xdb, 0xb9,

    /* U+0144 "ń" */
    0x22, 0xf, 0x99, 0x99, 0x90,

    /* U+0145 "Ņ" */
    0x9d, 0xdd, 0xdb, 0xb9, 0x4, 0x40,

    /* U+0146 "ņ" */
    0xf9, 0x99, 0x99, 0x4, 0x40,

    /* U+0147 "Ň" */
    0x60, 0x9d, 0xdd, 0xdb, 0xb9,

    /* U+0148 "ň" */
    0x60, 0xf9, 0x99, 0x99,

    /* U+0149 "ŉ" */
    0x44, 0x0, 0xf4, 0xa5, 0x29, 0x48,

    /* U+014A "Ŋ" */
    0x9d, 0xdd, 0xbb, 0x99, 0x12,

    /* U+014B "ŋ" */
    0xe9, 0x99, 0x99, 0x16,

    /* U+014C "Ō" */
    0x60, 0x69, 0x99, 0x99, 0x96,

    /* U+014D "ō" */
    0x60, 0x69, 0x99, 0x96,

    /* U+014E "Ŏ" */
    0x6, 0x6, 0x99, 0x99, 0x99, 0x60,

    /* U+014F "ŏ" */
    0x6, 0x6, 0x99, 0x99, 0x60,

    /* U+0150 "Ő" */
    0x50, 0x69, 0x99, 0x99, 0x96,

    /* U+0151 "ő" */
    0x50, 0x69, 0x99, 0x96,

    /* U+0152 "Œ" */
    0x7e, 0x49, 0x27, 0x92, 0x49, 0x1f,

    /* U+0153 "œ" */
    0xdd, 0x6f, 0x4a, 0xec,

    /* U+0154 "Ŕ" */
    0x24, 0xe9, 0x99, 0xea, 0xa9,

    /* U+0155 "ŕ" */
    0x22, 0xf, 0x99, 0x88, 0x80,

    /* U+0156 "Ŗ" */
    0xe9, 0x99, 0xea, 0xa9, 0x4, 0x40,

    /* U+0157 "ŗ" */
    0x7a, 0x52, 0x84, 0x20, 0x8, 0x80,

    /* U+0158 "Ř" */
    0x60, 0xe9, 0x99, 0xea, 0xa9,

    /* U+0159 "ř" */
    0x96, 0xf, 0x99, 0x88, 0x80,

    /* U+015A "Ś" */
    0x11, 0x1d, 0x18, 0x30, 0x61, 0x8b, 0x80,

    /* U+015B "ś" */
    0x22, 0xf, 0x9c, 0x39, 0xf0,

    /* U+015C "Ŝ" */
    0x22, 0x80, 0xe8, 0xc1, 0x83, 0xc, 0x5c,

    /* U+015D "ŝ" */
    0x6, 0xf, 0x9c, 0x39, 0xf0,

    /* U+015E "Ş" */
    0x74, 0xe0, 0xc1, 0x86, 0x2e, 0x23, 0x80,

    /* U+015F "ş" */
    0xf9, 0xc3, 0x9f, 0x66,

    /* U+0160 "Š" */
    0x51, 0x1d, 0x18, 0x30, 0x61, 0x8b, 0x80,

    /* U+0161 "š" */
    0x66, 0xf, 0x8c, 0x39, 0xf0,

    /* U+0162 "Ţ" */
    0xf9, 0x8, 0x42, 0x10, 0x84, 0x31, 0x80,

    /* U+0163 "ţ" */
    0x21, 0x3e, 0x42, 0x10, 0x87, 0x10, 0x80,

    /* U+0164 "Ť" */
    0x51, 0x3e, 0x42, 0x10, 0x84, 0x21, 0x0,

    /* U+0165 "ť" */
    0x9, 0x49, 0xf2, 0x10, 0x84, 0x38,

    /* U+0166 "Ŧ" */
    0xf9, 0x8, 0x47, 0x10, 0x84,

    /* U+0167 "ŧ" */
    0x21, 0x3e, 0x42, 0x3c, 0x87,

    /* U+0168 "Ũ" */
    0x60, 0x99, 0x99, 0x99, 0x96,

    /* U+0169 "ũ" */
    0x6, 0x9, 0x99, 0x99, 0x60,

    /* U+016A "Ū" */
    0x60, 0x99, 0x99, 0x99, 0x96,

    /* U+016B "ū" */
    0x60, 0x99, 0x99, 0x96,

    /* U+016C "Ŭ" */
    0x6, 0x9, 0x99, 0x99, 0x99, 0x60,

    /* U+016D "ŭ" */
    0x6, 0x9, 0x99, 0x99, 0x60,

    /* U+016E "Ů" */
    0x6a, 0x69, 0x99, 0x99, 0x99, 0x60,

    /* U+016F "ů" */
    0x6a, 0x69, 0x99, 0x99, 0x60,

    /* U+0170 "Ű" */
    0x50, 0x99, 0x99, 0x99, 0x96,

    /* U+0171 "ű" */
    0x50, 0x99, 0x99, 0x96,

    /* U+0172 "Ų" */
    0x99, 0x99, 0x99, 0x96, 0x42,

    /* U+0173 "ų" */
    0x99, 0x99, 0x96, 0x42,

    /* U+0174 "Ŵ" */
    0x62, 0xab, 0x5a, 0xf7, 0xde, 0xd6, 0x80,

    /* U+0175 "ŵ" */
    0x0, 0xc0, 0x2d, 0xba, 0xe7, 0x92, 0x48,

    /* U+0176 "Ŷ" */
    0x22, 0xa3, 0x15, 0x28, 0x84, 0x21, 0x0,

    /* U+0177 "ŷ" */
    0x6, 0x9, 0x99, 0x66, 0x24, 0x40,

    /* U+0178 "Ÿ" */
    0x50, 0x22, 0xa5, 0x28, 0x84, 0x21, 0x0,

    /* U+0179 "Ź" */
    0x24, 0xf1, 0x22, 0x44, 0x8f,

    /* U+017A "ź" */
    0x2, 0xf, 0x12, 0x48, 0xf0,

    /* U+017B "Ż" */
    0x60, 0xf1, 0x22, 0x44, 0x8f,

    /* U+017C "ż" */
    0x21, 0x0, 0xf1, 0x11, 0x88, 0xf8,

    /* U+017D "Ž" */
    0x60, 0xf1, 0x22, 0x44, 0x8f,

    /* U+017E "ž" */
    0x96, 0xf, 0x12, 0x48, 0xf0,

    /* U+017F "ſ" */
    0xf8, 0x88, 0x88, 0x88,

    /* U+2014 "—" */
    0xfc,

    /* U+2026 "…" */
    0xad, 0x40,

    /* U+203A "›" */
    0x42, 0x12, 0x40,

    /* U+21E7 "⇧" */
    0x10, 0x61, 0x23, 0x62, 0x85, 0xa, 0x14, 0x38,

    /* U+2212 "−" */
    0xe0,

    /* U+232B "⌫" */
    0x1f, 0x12, 0x5c, 0x6e, 0x32, 0x48, 0xf8,

    /* U+2713 "✓" */
    0x8, 0x21, 0x18, 0x20,

    /* U+2715 "✕" */
    0x9, 0x66, 0x90
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 96, .box_w = 2, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 3, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 5, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 11, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 17, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 23, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 29, .adv_w = 96, .box_w = 1, .box_h = 3, .ofs_x = 3, .ofs_y = 5},
    {.bitmap_index = 30, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 34, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 38, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 42, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 46, .adv_w = 96, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 47, .adv_w = 96, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 48, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 49, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 54, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 58, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 63, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 67, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 71, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 75, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 79, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 89, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 94, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 99, .adv_w = 96, .box_w = 1, .box_h = 6, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 100, .adv_w = 96, .box_w = 1, .box_h = 8, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 101, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 105, .adv_w = 96, .box_w = 4, .box_h = 3, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 107, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 111, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 115, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 122, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 128, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 132, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 136, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 140, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 144, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 148, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 152, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 156, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 161, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 166, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 171, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 175, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 180, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 184, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 188, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 193, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 198, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 202, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 207, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 212, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 216, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 222, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 227, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 233, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 238, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 242, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 246, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 251, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 255, .adv_w = 96, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 257, .adv_w = 96, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 258, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 7},
    {.bitmap_index = 259, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 262, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 266, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 270, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 274, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 277, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 282, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 286, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 290, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 296, .adv_w = 96, .box_w = 3, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 301, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 306, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 311, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 315, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 318, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 321, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 325, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 329, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 332, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 335, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 340, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 343, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 346, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 351, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 356, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 360, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 363, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 368, .adv_w = 96, .box_w = 1, .box_h = 10, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 370, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 375, .adv_w = 96, .box_w = 5, .box_h = 2, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 377, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 378, .adv_w = 96, .box_w = 2, .box_h = 8, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 380, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 386, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 391, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 397, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 402, .adv_w = 96, .box_w = 1, .box_h = 10, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 404, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 409, .adv_w = 96, .box_w = 3, .box_h = 1, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 410, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 415, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 417, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 421, .adv_w = 96, .box_w = 4, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 423, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 428, .adv_w = 96, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 429, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 431, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 435, .adv_w = 96, .box_w = 3, .box_h = 4, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 437, .adv_w = 96, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 439, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 7},
    {.bitmap_index = 440, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 444, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 451, .adv_w = 96, .box_w = 1, .box_h = 2, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 452, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 453, .adv_w = 96, .box_w = 3, .box_h = 4, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 455, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 457, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 461, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 467, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 472, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 478, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 482, .adv_w = 96, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 490, .adv_w = 96, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 498, .adv_w = 96, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 506, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 513, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 520, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 527, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 533, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 538, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 543, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 548, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 553, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 558, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 565, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 572, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 579, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 583, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 588, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 593, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 599, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 605, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 611, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 616, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 621, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 624, .adv_w = 96, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 631, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 637, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 643, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 649, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 654, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 661, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 666, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 670, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 675, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 680, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 685, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 690, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 695, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 700, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 704, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 709, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 714, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 719, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 724, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 729, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 735, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 741, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 747, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 753, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 758, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 763, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 768, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 773, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 778, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 783, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 788, .adv_w = 96, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 791, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 797, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 802, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 807, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 812, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 817, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 823, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 828, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 835, .adv_w = 96, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 843, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 847, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 854, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 859, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 866, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 871, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 877, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 883, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 889, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 895, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 900, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 906, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 911, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 916, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 921, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 927, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 932, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 937, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 942, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 946, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 951, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 956, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 961, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 966, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 973, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 977, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 982, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 987, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 993, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 999, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1004, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1010, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1015, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1021, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1027, .adv_w = 96, .box_w = 4, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1033, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1038, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1045, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1051, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1056, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1060, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1066, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1073, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1078, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1082, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1088, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1093, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1100, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1107, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1111, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1116, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1122, .adv_w = 96, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1131, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1138, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1145, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1152, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1156, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1161, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1168, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1174, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1181, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1185, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1190, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1194, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1200, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1205, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1210, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1215, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1220, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1226, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1231, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1236, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1240, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1246, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1251, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1255, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1260, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1264, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1270, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1275, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1280, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1284, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1290, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1294, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1299, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1304, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1310, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1316, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1321, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1326, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1333, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1338, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1345, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1350, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1357, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1361, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1368, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1373, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1380, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1387, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1394, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1400, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1405, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1410, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1415, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1420, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1425, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1429, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1435, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1440, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1446, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1451, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1456, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1460, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1465, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1469, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1476, .adv_w = 96, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1483, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1490, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1496, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1503, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1508, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1513, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1518, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1524, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1529, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1534, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 1538, .adv_w = 96, .box_w = 6, .box_h = 1, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1539, .adv_w = 96, .box_w = 5, .box_h = 2, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1541, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1544, .adv_w = 96, .box_w = 7, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 1552, .adv_w = 96, .box_w = 3, .box_h = 1, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 1553, .adv_w = 96, .box_w = 9, .box_h = 6, .ofs_x = -2, .ofs_y = 1},
    {.bitmap_index = 1560, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 1564, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_3[] = {
    0x0, 0x12, 0x26, 0x1d3, 0x1fe, 0x317, 0x6ff, 0x701
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 160, .range_length = 13, .glyph_id_start = 96,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 174, .range_length = 210, .glyph_id_start = 109,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 8212, .range_length = 1794, .glyph_id_start = 319,
        .unicode_list = unicode_list_3, .glyph_id_ofs_list = NULL, .list_length = 8, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
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
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 4,
    .bpp = 1,
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
const lv_font_t ob_font_meta_10 = {
#else
lv_font_t ob_font_meta_10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 14,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if OB_FONT_META_10*/

