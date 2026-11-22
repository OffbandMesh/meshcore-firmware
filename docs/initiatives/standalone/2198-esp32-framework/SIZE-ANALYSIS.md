# ESP32 image size on pioarduino 55.x

Part of the ESP32 framework Feature ([#2198](https://github.com/OffbandMesh/meshcore-firmware/issues/2198)), plan [`PLAN.md`](PLAN.md). Measured 2026-11-19 on 55.03.311.

## The short version

- **Most of the growth was ours, not the framework's.** Three defects in how we build added 32–134 KB to each companion image and 40–78 KB to each repeater on 55.x. With them fixed, companion builds grow 0–5% on every chip, and the C6 builds shrink slightly.
- **What's left is the Wi-Fi stack.** Repeaters, room servers and the observer grow 126–140 KB, because IDF 5.5's Wi-Fi, TLS and TCP/IP code is bigger. Those roles really use Wi-Fi.
- **That only hurts on 4 MB boards.** On the S3 and the classic ESP32 every build stays under 52% of its slot. On the C3 the repeater and room server don't fit (108%).
- **One 4 MB partition table fixes the C3 and the C6 together.** It makes the C3 fit at 80%, and gives the C6 the SPIFFS a full contact list needs. It costs those owners one USB flash that wipes SPIFFS.

## How it was measured

- One board per chip, every shipped role: S3 Heltec V3, C3 Xiao, C6 Photon, classic ESP32 Heltec V2. nRF52 (RAK4631) is shown for headroom only; this Feature doesn't touch it.
- Each env built on today's platform and on 55.03.311 with `lib_archive = yes`, with a linker map.
- **Size is `firmware.bin`**, the image an OTA writes, against the app slot. PlatformIO's own size figure leaves out `.eh_frame` (row 18 of the plan), so it under-states every image by 0–87 KB.
- Where a build stops at the size check and writes no `.bin`, the figure is the sum of the loaded sections in its map, marked `~`. That sum matched the `.bin` to within a few hundred bytes on every build that had both.
- RAM is static RAM from the build, against the 320 KB DRAM limit PlatformIO reports. **Heap at runtime isn't measured**; that needs units (Epic E).

## The grid

Sizes in KB. "55.x" is with the three fixes below applied. "Left" is what remains in the app slot. **Figures marked `~` are estimates**: those builds overflow, so no `firmware.bin` exists, and the figure is the sum of the loaded sections in the linker map.

| Chip | Role | Today | 55.x | Growth | Slot | Fill | Left | Static RAM, today → 55.x |
|---|---|---|---|---|---|---|---|---|
| S3 | companion USB | 698 | 700 | +3 (0.4%) | 3,264 | 21% | 2,564 | 123 → 126 |
| S3 | companion BLE | 916 | 934 | +18 (2.0%) | 3,264 | 29% | 2,330 | 178 → 182 |
| S3 | repeater | 1,193 | 1,329 | +136 (11.4%) | 3,264 | 41% | 1,935 | 79 → 81 |
| S3 | room server | 1,188 | 1,324 | +136 (11.5%) | 3,264 | 41% | 1,940 | 83 → 85 |
| S3 | observer | 1,503 | 1,643 | +140 (9.3%) | 3,264 | 50% | 1,621 | 120 → 122 |
| C3 | companion USB | 628 | 643 | +15 (2.4%) | 1,280 | 50% | 637 | 158 → 156 |
| C3 | companion BLE | 854 | 900 | +46 (5.4%) | 1,280 | 70% | 380 | 171 → 169 |
| C3 | repeater | 1,247 | ~1,383 | +137 (11.0%) | 1,280 | **108%** | **−103** | 71 → 69 |
| C3 | room server | 1,240 | ~1,378 | +138 (11.1%) | 1,280 | **108%** | **−98** | 74 → 73 |
| C6 | companion USB | 666 | 657 | −9 (−1.4%) | 1,920 | 34% | 1,263 | 157 → 160 |
| C6 | companion BLE | 1,059 | 1,046 | −13 (−1.2%) | 1,920 | 54% | 874 | 170 → 172 |
| C6 | repeater | 652 | 646 | −6 (−0.9%) | 1,920 | 34% | 1,274 | 48 → 51 |
| C6 | room server | 645 | 640 | −5 (−0.7%) | 1,920 | 33% | 1,280 | 51 → 54 |
| ESP32 | companion USB | 636 | 625 | −11 (−1.7%) | 3,264 | 19% | 2,639 | 79 → 79 |
| ESP32 | companion BLE | 922 | 923 | +1 (0.1%) | 3,264 | 28% | 2,341 | 115 → 116 |
| ESP32 | repeater | 1,164 | 1,290 | +126 (10.8%) | 3,264 | 40% | 1,974 | 69 → 72 |
| ESP32 | room server | 1,158 | 1,284 | +126 (10.8%) | 3,264 | 39% | 1,980 | 73 → 75 |
| nRF52 | companion BLE | 522 | — | — | 696 | 75% | 174 | 163 of 230 |
| nRF52 | companion USB | 514 | — | — | 696 | 74% | 182 | 117 of 230 |
| nRF52 | repeater | 489 | — | — | 796 | 61% | 307 | 49 of 230 |
| nRF52 | room server | 483 | — | — | 796 | 61% | 313 | 53 of 230 |

The C3's real binary today is already 97.4% of its slot (repeater), not the 90.7% PlatformIO reports.

## Why it grows

### Three build defects of ours (fixed in the grid above)

| Defect | What it does | Measured cost on 55.x |
|---|---|---|
| **pioarduino turns library archiving off** unless the project sets `lib_archive` (`builder/main.py:950-954`) | Every library object is linked whether called or not. The C6 repeater linked AsyncTCP and the Wi-Fi client, and through them lwIP, the supplicant and the Wi-Fi mesh library | C6 repeater: 81.5 KB (+20.9% → +6.9%). No effect where Wi-Fi is used anyway |
| **C++ exception tables on code that throws nothing.** The framework compiles our code with `-fexceptions`; nothing in `src/`, `examples/` or `arch/` throws or catches | `.eh_frame` unwind tables for all our code and the libraries we build | Repeaters: S3 −48, C3 −78, C6 −48, ESP32 −40. Companion BLE: S3 −37, C3 −54, C6 −59, ESP32 −28 |
| **Companion builds compile every file in `src/helpers/esp32/`** through a wildcard, including the ESP-NOW radio and the Wi-Fi transport | `ESPNOWRadio.cpp` calls `WiFi.mode()` and `SerialWifiInterface.cpp` uses the network client and server, so a BLE or USB companion links the whole Wi-Fi and TCP/IP stack it never starts. Same pattern as the BLE file #199 fixed | Companion BLE, both files: S3 −67, C3 −80, ESP32 −75, C6 −1 (its variant already excluded ESP-NOW). Companion USB, the ESP-NOW file alone: C3 −79, ESP32 −74 (the Wi-Fi transport was measured together with the exception fix there) |

The exception-table fix shrinks today's builds too; the C3 repeater carries 87 KB of `.eh_frame` on today's platform. The wildcard costs almost nothing today: on Arduino 2.0.17 those two files didn't pull in lwIP.

### What's left: the Wi-Fi stack

On the C3 repeater, about 159 KB of the remaining code growth is Wi-Fi, TLS and TCP/IP:

| Library | Today | 55.x | Growth |
|---|---|---|---|
| `wpa_supplicant` (Wi-Fi security) | 47.5 | 87.6 | +40.1 |
| `mbedcrypto` (crypto) | 35.0 | 67.5 | +32.5 |
| lwIP (TCP/IP) | 88.6 | 119.2 | +30.6 |
| `net80211` (Wi-Fi MAC) | 119.9 | 147.7 | +27.8 |
| `pp` (Wi-Fi driver) | 46.6 | 61.1 | +14.5 |
| Arduino Network library | 0 | 8.7 | +8.7 |
| `esp_netif` | 6.3 | 10.6 | +4.3 |

Code in KB, from the linker maps. Every repeater, room server and observer carries it, because they all use Wi-Fi for OTA. The S3 and classic ESP32 grow by the same amount.

The rest moves both ways and nets out: IDF 5's split peripheral drivers (`esp_driver_*`, +37 KB) replace `libdriver` (−25 KB), the Arduino 3 core grows 24 KB, `libc` shrinks 13 KB, and the web server shrinks 30 KB.

### Why the C6 barely moves

The C6 builds were already on IDF 5.3 (pioarduino 53), so they had already paid for the newer Wi-Fi stack. Moving to 5.5 adds little, and the three fixes take back slightly more than that.

## SPIFFS that actually works

SPIFFS holds each node's identity, contacts, channels, adverts and settings (plan row 19). A full companion, at the 350-contact limit these boards set, needs **384 KB**, with room to rewrite the contact file. That figure comes from packing real images with `mkspiffs`.

| Board | Flash | Table | App slot | SPIFFS | Full contact list fits? |
|---|---|---|---|---|---|
| Heltec V3 (S3) | 8 MB | `default_8MB` | 3,264 KB | 1.5 MB | yes |
| Heltec V4 (S3) | 16 MB | `default_16MB` | 6,400 KB | 3.4 MB | yes |
| Heltec V2 (ESP32) | 8 MB | `default_8MB` | 3,264 KB | 1.5 MB | yes (its BLE build allows 160 contacts) |
| Xiao C3 | 4 MB | `default` | 1,280 KB | 1.4 MB | yes, but the 55.x repeater doesn't fit the app slot |
| Photon C6, Xiao C6 | 4 MB | `min_spiffs` | 1,920 KB | 128 KB | **no: 107 of 350** (today's defect, plan §10) |
| RAK4631 (nRF52) | 1 MB | LittleFS | — | — | not measured: different filesystem |

### One 4 MB table for the C3 and the C6

| Partition | Offset | Size |
|---|---|---|
| nvs | 0x9000 | 0x5000 (unchanged) |
| otadata | 0xE000 | 0x2000 (unchanged) |
| app0 | 0x10000 | 0x1B0000 (1,728 KB) |
| app1 | 0x1C0000 | 0x1B0000 (1,728 KB) |
| spiffs | 0x370000 | 0x80000 (512 KB) |
| coredump | 0x3F0000 | 0x10000 (unchanged) |

- **C3 on 55.x:** repeater 80%, room server 80%, companion BLE 52%, companion USB 37%.
- **C6 on 55.x:** companion BLE 61%, repeater 37%.
- **SPIFFS:** 512 KB, a third more than the 384 KB a full contact list measures at.
- **NVS is untouched,** so SafeBoot counters, BLE bonds and Wi-Fi credentials survive.
- **Cost:** a partition change can't be delivered by OTA. Each C3 and C6 owner does one USB flash, which wipes SPIFFS. The owner ruled on 2026-11-19 that C3 users are warned and nothing is backed up; the C6 is the same question.

## Options to minimize bloat and stay current

| # | Option | Effect | Measured? | Cost |
|---|---|---|---|---|
| 1 | Fix our three build defects | The whole difference between "55.x as is" and the grid: 32–134 KB per companion, 40–78 KB per repeater | Yes | Config only: `lib_archive = yes`, drop `-fexceptions`, list each companion's ESP32 helper files instead of the wildcard |
| 2 | One 4 MB partition table for C3 and C6 | C3 fits at 80%; C6 holds the full contact list | Yes, from the builds and `mkspiffs` | One USB flash and a SPIFFS wipe for 4 MB owners |
| 3 | Rebuild the framework from our own config (`custom_sdkconfig`) | Removes framework features we don't use: exceptions in the framework itself, WPA2-Enterprise, the HTTPS server, OWE | Yes, on the C3 repeater: **about 1 KB** (1,382 KB against 1,383 KB with option 1 alone). Not worth it | Not a drop-in (below) |
| 4 | Gate size on the real `.bin` in CI, with a budget per chip | Stops silent creep; today's gate misses `.eh_frame` | — | One check |
| 5 | Pin the platform and re-run this matrix on every bump | Staying current becomes a measured, routine step | The tooling exists from this analysis | One matrix run per bump |
| 6 | Last resort: drop Wi-Fi OTA on the C3 | About 500 KB: the Wi-Fi, TLS and TCP/IP code plus the web server, by the C3 repeater's map | Estimated from the map, not built | C3 repeaters update by USB only; not needed if option 2 is taken |

### What option 3 costs

Found by trying it on the C3 repeater [verified, build log and `builder/frameworks/arduino.py:404-476`]:

- **It doesn't build our project as it is.** The platform's first pass compiles the framework libraries and then our sources as a plain ESP-IDF project. That pass ignores `build_src_filter` ("the 'src_filter' option cannot be used with ESP-IDF") and fails on our include paths.
- **It replaces the framework libraries in the shared package store.** An env without the same `custom_sdkconfig` triggers a reinstall of the stock libraries, and the next custom env rebuilds them again. So every ESP32 env would have to share one configuration per framework version, or builds keep swapping the framework.
- **The first compile downloads ESP-IDF 5.5.5** and takes about 10 minutes per chip.
- **The clean way to do it** would be to build our own framework-library package once per version, publish it, and pin it beside the platform. That's real work, and worth it only if the size saving is large.
- **It isn't.** Built from a minimal project with the same config, then linked into the repeater, it saved about 1 KB. Exceptions, WPA2-Enterprise, the HTTPS server and OWE are barely linked into the image anyway. Once our own code drops its exception tables (option 1), 11.5 KB of `.eh_frame` is left, almost all of it the framework's own. Other framework features could be tried the same way, but the Wi-Fi stack that is the real growth can't be turned off on a role that uses Wi-Fi.

Options 1, 4 and 5 apply to every chip. Option 2 is for the 4 MB boards only. On the S3 and the classic ESP32, the 55.x Wi-Fi stack fits with 1.6 MB or more to spare.
