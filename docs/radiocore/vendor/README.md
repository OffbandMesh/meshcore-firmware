# `docs/radiocore/vendor/` — third-party assets, deliberately not tracked

**This directory is gitignored except for this file.** If you cloned this repo, the
contents below are **not present on your machine**. That is intentional, not a mistake or
a broken checkout.

## What belongs here

| File | Source | Why it is not committed |
|---|---|---|
| `RC32_ESP32/RC32.jpg` | Heltec — official RC32 pinout diagram | vendor artwork, not ours to redistribute; ~3.8 MB |
| `RCC6/RCC6.jpg` | Heltec — official RCC6 pinout diagram | vendor artwork; ~4.2 MB |
| `RC52_nRF52/image.png` | Heltec — official RC52 pinout diagram | vendor artwork; ~7.9 MB |
| `Heltec_RadioCore_Beta_Feedback.md` | Heltec — beta-programme brief, verbatim | vendor text, reproduced in full |
| `DUBS_LCD_image1.png` | community member, via Discord | third party's photo; ~3.0 MB |
| `n30nex_stats_image.png` | community member, via Discord | third party's screenshot |

## Why

1. **Licensing.** This repo is MIT (see `LICENSE.txt`). Vendor pinout artwork, a verbatim
   vendor announcement, and other people's photographs are not ours to relicense or
   redistribute under it.
2. **History weight.** ~19 MB of binaries is permanent once committed — git history cannot
   shed it without a full rewrite, and every future clone pays for it forever.

## Where the data went instead

Every pin value, electrical fact and hazard needed for bring-up is **transcribed as text**
into [`../README.md`](../README.md), which also lists explicitly what could *not* be
transcribed (connector mechanicals, voltage tolerances, component placement).

If you need the originals, ask the owner — they are kept locally alongside this file.
