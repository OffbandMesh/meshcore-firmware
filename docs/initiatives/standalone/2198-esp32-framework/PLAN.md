# Feature PLAN: one current ESP32 framework, with an isolated package store

| | |
|---|---|
| **Feature** | [#2198](https://github.com/OffbandMesh/meshcore-firmware/issues/2198) · Citadel `Crosswire-uyd` |
| **Serves** | Standalone. The yardstick is the owner's: performance, quality, efficiency, battery life, recovery, stability and capability. The only hard constraints are MeshCore protocol interoperability and stock-client interoperability. |
| **Status** | For the owner's approval by merge. §9 decision 2 is ruled; the other §9 choices are open. |
| **Owner** | @Strycher |
| **Last updated** | 2026-11-21 |

## At a glance

- **What:** every ESP32 build moves onto one current Arduino-ESP32 framework, pioarduino 55.x (Arduino 3.3, ESP-IDF 5.5). This project also gets its own PlatformIO package store on every host.
- **Why now:**
  - Today there are two frameworks, and both are behind. 342 envs, every S3, C3 and classic ESP32 one, build on Arduino 2.0.17 / IDF 4.4. The 29 C6 envs build on Arduino 3.1.3 / IDF 5.3.
  - On IDF 5.3, a blind I2C walk on a Photon-1W C6 with no battery hangs the main loop until the task watchdog reboots it. IDF 5.5 bounds the bus recovery that hangs.
  - Another project's build on the same machine can swap our framework libraries out mid-build.
- **How:**
  - First, give this project its own store, and pin the build tools.
  - Then port the code so it builds on both frameworks, move every shipped 4 MB build (C3 and C6) to one partition table that fits them and a full contact list, and switch every ESP32 build to one platform in one change.
  - Then check every bench board family on it, including OTA from today's release and stock-client use.
  - Last, re-test the Photon I2C hang on the new framework.
- **Done when:** every ESP32 env builds on one pinned platform, CI enforces that, every bench board family has passed its hardware check, an OTA from the current release keeps each unit's identity and contacts, and `OUTCOMES.md` is approved.

## 1. Diagnosis

Tags: [verified] means observed in a build, on a unit, or by a search. [read] means read in source or docs but not run.

| # | Finding | Evidence | Effect |
|---|---|---|---|
| 1 | There are 371 ESP32 envs: 273 S3, 47 classic ESP32, 22 C3 and 29 C6. 70 of them ship: 48 S3, 14 C6, 4 C3 and 4 classic (Heltec V2). By role, the shipped ones are 37 companion, 16 repeater, 9 room server and 8 observer. All but the C6 envs build on `platformio/espressif32@6.11.0` (Arduino 2.0.17, IDF 4.4). The C6 envs build on pioarduino `53.03.13-1` (Arduino 3.1.3, IDF 5.3). | Inventory of every `[env:*]` against `.github/release-envs.txt` and `ci.yml`, 2026-11-18 [verified]. | Scope of Epics B, C and D. |
| 2 | Every pioarduino platform installs its precompiled IDF libraries into one folder, `framework-arduinoespressif32-libs`. When two pioarduino versions build on one host, they replace that folder under each other, and the build fails with false compile errors. The cores themselves can live side by side (`@src-<hash>`); the libraries can't. | Package store on this host, `C:\pio\packages` [verified]. | Epic A. |
| 3 | `platformio.ini` can move the store: `packages_dir = ${platformio.core_dir}/offband-<tag>/packages` resolves to `C:\pio\offband-<tag>\packages` here, and to `~/.platformio/offband-<tag>/...` on a CI runner. Only the `PLATFORMIO_PACKAGES_DIR` / `PLATFORMIO_PLATFORMS_DIR` variables override it, and the global `PLATFORMIO_CORE_DIR` does not. A real 55.03.311 build in an isolated store left the shared store untouched. | `pio project config` on a probe project, and a full build in the isolated store, 2026-11-18 [verified]. | A1's design: one store per platform version per host, shared by every worktree of this project and by no other project. CI's `~/.platformio` cache already covers that path. |
| 4 | Store isolation has to be keyed by the platform version, not only by the project. During this migration, a worktree on the old platform and one on the new would otherwise collide inside this project. | Follows from row 2 [read]. | A1 keys the store by a tag, and config-lint checks that the tag matches the platform pin. |
| 5 | CI installs whatever PlatformIO is newest (`pip install --upgrade platformio`, in `ci.yml:71`, `ci.yml:589` and `.github/actions/setup-build-environment/action.yml`). This host runs 6.1.19. pioarduino 55.03.313 needs PlatformIO 6.2.0 or newer; 55.03.311 needs 6.1.19 or newer. | Workflow files; pioarduino release notes [verified]. | A2 pins one PlatformIO version on CI and on the host. Today, a PlatformIO release can change every build without any change in this repo. |
| 6 | pioarduino's IDF tool installer refuses to run under MSYS / Git Bash (`idf_tools.py`, `if 'MSYSTEM' in os.environ: fatal`). It refuses even with the variable unset in that shell. The same build from PowerShell works. | Build attempts on 55.03.311, 2026-11-18 [verified]. | A3: our Windows build entry points start PlatformIO without `MSYSTEM`. |
| 7 | **Code port.** On 55.03.311, the only compile failure in the S3 companion and Heltec V2 companion builds is `src/helpers/esp32/ESPNOWRadio.cpp`. Two ESP-NOW callback signatures changed (`:39`, `:40`), and `esp_efuse_mac_get_default` is no longer declared without `<esp_mac.h>` (`:68`). Every other file in those builds compiled. `src/helpers/bridges/ESPNowBridge.cpp:12` and `:18` use the same old callback signatures. With a throwaway version-guarded patch of `ESPNOWRadio.cpp`, the builds link. | Build logs `heltec_v4_companion_radio_ble`, `Heltec_v2_companion_radio_usb`, 2026-11-18 [verified]. Bridge: by search, then by build (row 8) [verified]. | Epic B. |
| 8 | **More API changes found by search**, not all reached by a build yet: `esp_efuse_mac_get_default` also in `SerialBLEInterface.cpp:227`; unguarded LEDC calls in `EinkPanel.cpp:51-52` and `GuiPanel.cpp:16-17`; direct mbedtls use in `JwtHelper.cpp`. `ESP32Board.h`, `MqttAuth.cpp` and `MqttBroker.cpp` already carry IDF version guards. | `git grep` on `firmware-base` [read]. **Wave 2, 38 envs on 55.03.311 with only the `ESPNOWRadio.cpp` patch [verified]:** 35 built. Every role (companion BLE, USB and observer, repeater, room server) on every chip family compiled, so `SerialBLEInterface.cpp` and `JwtHelper.cpp` build as they are. Three failed. `GuiPanel.cpp:16-17` failed the same way in both GUI builds: `ledcSetup` and `ledcAttachPin` are gone, replaced by `ledcAttach`. `SdCard.cpp:162` failed in the T-Deck GUI build because `f_mkfs` now takes an `MKFS_PARM*` (FatFs R0.14). The Xiao C3 room server hit the size limit, as predicted in row 9. `EinkPanel.cpp` and `ESPNowBridge.cpp` only build in envs outside that set, so a third run built them [verified]. `Heltec_v3_repeater_bridge_espnow` fails at `ESPNowBridge.cpp:48-49`, where the old-signature callbacks declared at `:12` and `:18` are registered. `LilyGo_TDeck_Pro_companion_radio_ble_gui_diag` fails at `EinkPanel.cpp:51-52` (LEDC, as in `GuiPanel.cpp`). Every API change row 8 predicted from search is now confirmed by a build. | Epic B. |
| 9 | **Size.** Every figure in this row is PlatformIO's, with pioarduino's default of no library archiving. Row 17 shows archiving takes most of the C6 growth back, and row 18 shows PlatformIO's figure leaves out `.eh_frame`. D4 re-measures every env on `firmware.bin` with archiving on. Images grow 5.6% to 22.9% on 55.x. The smallest growth is in the large observer images; the largest is in small C3 and C6 images, where the framework's fixed cost dominates. The Xiao C3 repeater goes from 1,188,218 to 1,416,055 bytes (+19.2%), against a 1,310,720-byte app slot: 108%, so it doesn't build. The C3 room server is 1,182,474 bytes today, with the same libraries, so it overflows too. The two C3 companion builds have room (61.9% and 44.8% today). The Photon C6 repeater grows from 610,293 to 732,232 bytes (+20%) and stays at 37%. | Build logs on 55.03.311 against the v1.7.1 release sizes, 2026-11-18 [verified]. **Wave 2 [verified]:** the C3 room server measures 1,411,417 bytes (107.7%), so it overflows as predicted. The C3 companion BLE build grows 22.9% to 996,913 bytes (76.1%). Every S3, C6 and classic build measured is at or below 58.1% of its slot. The tightest is the Photon C6 companion at 58.1%, and the highest classic build is the Heltec V2 repeater at 39.9%. Static RAM ranges from 1.6 KB smaller (the C3 repeater) to 29.5 KB larger. The largest increase is the E290 companion BLE build (+29.5 KB, to 221,340 bytes). The C6 repeaters grow 8.4 KB (+17%). | Epic C. Every other shipped env is checked against its limit in D. |
| 10 | **The C3 builds ship but aren't built by CI.** All four Xiao C3 envs are in `.github/release-envs.txt`, and none is in `ci.yml`. Of the classic ESP32 builds, only `Heltec_v2_companion_radio_usb` is in CI. So a C3 that stopped building would first show up as a red release. | Search of both files on `firmware-base`, 2026-11-18 [verified]. | §9 choice 5. Adding envs to the matrix is the owner's decision. |
| 11 | **Release images.** `build.sh` runs a custom `mergebin` target (`merge-bin.py`), which calls the flasher tool through `$OBJCOPY` with the `merge_bin` subcommand. The C6 releases already use it on pioarduino 53. Whether it still works with the flasher that ships with 55.x isn't tested yet. | `merge-bin.py:20-36`, `build.sh:169` [read]. **Tested on 55.03.311, one env per chip (S3, C3, classic ESP32, C6) [verified]:** `mergebin` works on all four. The bootloader is at 0x0 (0x1000 on classic), the partition table at 0x8000 and the app at 0x10000. The chip IDs are right, and every appended SHA-256 validates. **One defect:** on S3, C3 and C6 the merged bootloader's header now says QIO, where today's release image says DIO. `merge-bin.py` passes the board file's raw `flash_mode` (`qio` in all 19 repo board files under `boards/` that set one), while both platforms' own builders map `qio` to `dio` (`_get_board_flash_mode`). The old flasher left the hashed bootloader's mode alone; the 55.x flasher rewrites it and re-hashes it. OTA is unaffected; only USB-flashed merged images change. | D2 runs `mergebin` for one env per chip, and makes `merge-bin.py` use `${__get_board_flash_mode(__env__)}`, which both platforms provide. The merged images then keep today's DIO. |
| 12 | **The bootloader rollback layer is disarmed today, on both frameworks.** Both cores' `initArduino()` mark a newly OTA'd image valid before `setup()` runs, unless the app overrides `verifyRollbackLater()`. Offband doesn't override it. So the `PENDING_VERIFY` state that `ESP32Board.cpp:333` checks has already been cleared, and the bootloader's crash rollback, which `ESP32Board.cpp:163-180` counts on, never arms. Only the app-level counter (60 s, more than 3 boots) is protecting a bad image. Both frameworks enable rollback in the bootloader and the app. | Arduino 2.0.17 `esp32-hal-misc.c:225-238`; 3.3.11 `:315-328`; both `sdkconfig`s; search of `src/`, `examples/` and `variants/` for `verifyRollbackLater` [read]. Not bench-tested. | §9 choice 4. B3 fixes it if ruled in, and E checks it either way. |
| 13 | **The ESP32 filesystem is SPIFFS, and boot formats it on a failed mount:** `SPIFFS.begin(true)`. Identity and contacts live there. If the new framework failed to mount an old filesystem, the first boot after an OTA would wipe the unit silently. Nothing found says it does. NVS also has to read old data. | `examples/companion_radio/main.cpp:601` [read]. | E checks, on every bench family, that an OTA from the current release keeps the identity and contacts. It's a §6 stop. |
| 14 | **Bootloaders.** An OTA replaces the app, never the bootloader. So a deployed unit keeps its IDF 4.4 bootloader and must boot an IDF 5.5 app. A merged USB image brings the new bootloader with it. ESP-IDF documents old bootloaders booting newer apps. Whether a new bootloader boots an older Offband app, which is what a downgrade by OTA would need, isn't confirmed. | ESP-IDF bootloader compatibility notes [read]. Not bench-tested. | E checks both directions on one S3 unit. Downgrades are §9 choice 3. |
| 15 | **Photon C6 I2C hang.** On the Photon-1W with no battery, the `i2cscan` walk hung the main loop. The task watchdog fired at 30 s (`E (62070) task_wdt ... loopTask`), and the board came back on `reset_reason=6`. It's on IDF 5.3. On 2026-10-08, MeshSmith moved their Photon repeater to Arduino 3.3.8 / IDF 5.5.4, whose bus-clear recovery is bounded to 50 ms, citing this hang on a missing fuel gauge. | `photon-c6` capture, recorded on [#2196](https://github.com/OffbandMesh/meshcore-firmware/issues/2196) and [#1050](https://github.com/OffbandMesh/meshcore-firmware/issues/1050) [verified]. MeshSmith's commit `4feee571` [read]. | Epic F re-runs the same test on 55.x. |
| 16 | **Libraries mostly float** on caret ranges (NimBLE resolves to 2.5.2). The same commit has given different memory footprints across CI runs. | `lib_deps` across `variants/`; [#686](https://github.com/OffbandMesh/meshcore-firmware/issues/686) [verified]. | Carried in (§10). A pinned platform with floating libraries is still not a reproducible build. |
| 17 | **pioarduino turns library archiving off on every env, and that is most of the C6 growth.** `builder/main.py:950-954` sets `lib_archive = False` unless the project sets `lib_archive` anywhere ("This makes weak defs in framework and libs possible"). The 6.11.0 platform and pioarduino 53 leave libraries archived; the linker maps show `libRadioLib.a` today and loose objects on 55.x. A loose object is always linked. So in the Xiao C6 repeater, `AsyncTCP.cpp.o` and `WiFi/STA.cpp.o` come in although nothing calls them, and they pull lwIP (53.5 KB), the supplicant, `net80211` and even the Wi-Fi mesh library after them. Today that repeater has 0 bytes of any of those. With `lib_archive = yes`, the repeater's binary goes from 702,560 to 621,048 bytes: growth over today drops from +20.9% to +6.9%. The C3 and S3 repeaters don't change (−242 and +660 bytes), because they really use Wi-Fi for OTA. No library override of a weak framework function is lost: for every symbol bound to a weak definition, a search of the 207 library objects and archives in each build found no strong definition, on the C3, C6 and S3 repeaters. | Platform source [read]. Linker maps and `nm`, `Xiao_C6_repeater_`, `Xiao_C3_repeater`, `Xiao_S3_repeater`, 2026-11-18 [verified]. Weak-override check: 3 envs, not yet all [verified for those 3]. | D1 sets `lib_archive = yes` in `esp32_base`. D3's lint requires it. D1 runs the weak-override check on every ESP32 env, and any hit is a §6 stop. |
| 18 | **PlatformIO's size check doesn't count `.eh_frame`, and the binary does.** `.eh_frame` is the C++ exception-unwinding table. Every framework here is precompiled with `CONFIG_COMPILER_CXX_EXCEPTIONS=y`. The C3 repeater's real `firmware.bin` today is 1,276,720 bytes, 97.4% of its slot, while PlatformIO reports 1,188,218 (90.7%). The C3 carries 87 KB of `.eh_frame` today and 80 KB on 55.x. On 55.x the S3 gains a 36 KB `.eh_frame` it didn't have: its real binary grows +186.9 KB, where PlatformIO reported +150 KB. Section sums match the `.bin` files to within a few hundred bytes. | `firmware.bin` sizes and map section totals, both platforms, 2026-11-18 [verified]. | Every size figure in C and D4 is taken from `firmware.bin` against the partition, not from PlatformIO's report. C1 measures what dropping exception tables from our own code saves; the framework's own tables are precompiled and stay. |
| 19 | **SPIFFS holds every per-node file, and 128 KB doesn't hold a full companion.** On ESP32, SPIFFS holds the identity, `/contacts3` (152 B × contacts), one `/bl/<key>` advert file per contact (≤166 B each), `/channels2` (68 B × channels), `/blocks`, the prefs files, and, on repeaters and room servers, `/s_contacts`, `/regions2` and an uncapped `/packet_log`. NVS (SafeBoot counters, `cw_boot`, the observer's Wi-Fi credentials, BLE bonds) sits at 0x9000 in every 4 MB table, so a repartition leaves it alone. Packed with `mkspiffs` at ESP32 Arduino's geometry (4 KB blocks, 256 B pages), the companion worst case at the C3 and C6 limits (350 contacts, 40 channels) needs 0x60000 (384 KB), keeping room to rewrite `/contacts3`. **128 KB (`min_spiffs.csv`) holds 107 contacts with that room, and 137 with none.** The repeater worst case fits 128 KB with about 70 KB to spare. | Record sizes from the write code (`DataStore.cpp`, `ClientACL.cpp`, `RegionMap.cpp`, `IdentityStore.cpp`, `MyMesh.cpp`) [read]; capacity with `mkspiffs` 0.2.3 (arduino-esp32 build) on a fresh image [verified]. A fresh image is the best case: fragmentation and garbage collection on a live unit need more. Prefs files sized at a 4 KB allowance each, not computed. | §9 decision 2 and Epic C: the shipped 4 MB builds, C3 and C6, move to a table with 512 KB of SPIFFS. That also fixes the shipped C6 companions, which run 128 KB with a 350-contact limit today. |
| 20 | **Bench coverage by chip:** S3 has the Heltec V4 fleet, the V4 TFT, RC32, XIAO S3 and T-Deck Pro; C6 has the Photon-1W and RCC6; P4 has the EtherMesh-1W (on loan) and the desk unit. There is **no C3 unit and no classic-ESP32 LoRa unit** (only radio-less WROOM DevKits). | `HARDWARE.local.md` device list [verified]. | §9 choice 6: what "checked" means for C3 and Heltec V2. |

## 2. Scope

**In:**
- Every ESP32 env, all four chips, every role: companion, repeater, room server, observer, KISS modem, bridges and terminal chat.
- This project's package store on every host and in CI, and the PlatformIO version.
- The code port, the C3 size fix and the one-platform switch.
- Hardware checks on every bench family, including OTA from the current release, identity and contacts surviving it, and stock-client use.
- The Photon C6 I2C re-test, and what `i2cscan` becomes.
- The bootloader rollback fix, if §9 choice 4 rules it in.
- CI with positive and negative tests, the `docs/changes/` records and user docs for each PR, and `OUTCOMES.md`.

**Out:**
- nRF52, RP2040 and STM32 builds.
- ESP32-P4 bring-up. The P4 needs pioarduino, so this Feature removes the platform blocker for [#491](https://github.com/OffbandMesh/meshcore-firmware/issues/491) and [#1438](https://github.com/OffbandMesh/meshcore-firmware/issues/1438), but the bring-up stays there.
- Pinning every library ([#686](https://github.com/OffbandMesh/meshcore-firmware/issues/686)), except where a library must change for the new framework.
- Arduino 4.0 / IDF 6.1. It's a release candidate today.
- A release. This Feature ends with the build line on the new framework. The release that carries it goes through the normal release gate.

## 3. Epics

Tasks are 4 points or smaller. Each Epic ends with a verification task and gets one PR. Gemini 2.5 Pro reviews each task before it closes.

### Epic P: PLAN OF RECORD ([#2199](https://github.com/OffbandMesh/meshcore-firmware/issues/2199))

| Task | Pts |
|---|---|
| **P1 · Write this plan** ([#2200](https://github.com/OffbandMesh/meshcore-firmware/issues/2200)): diagnosed, Gemini-reviewed, committed. | 3 |
| **P2 · Push, PR, merge** ([#2201](https://github.com/OffbandMesh/meshcore-firmware/issues/2201)): merging is the owner's approval. | 1 |

### Epic A: BUILD FOUNDATION ([#2203](https://github.com/OffbandMesh/meshcore-firmware/issues/2203))

| Task | Pts |
|---|---|
| **A1 · Isolated store:** in `platformio.ini`, set `[platformio] packages_dir` and `platforms_dir` to `${platformio.core_dir}/offband-${offband.pio_store_tag}/...` (row 3). The tag is the pioarduino platform version.<br>• **Config-lint:** the tag equals the version in every pioarduino `platform =` URL. Negative case: a pin that changes without the tag.<br>• **Checked by:** a build from this worktree and one from another project on the same host, run one after the other, leave each other's stores untouched. | 3 |
| **A2 · Pinned PlatformIO:** pin one version, chosen in §9 choice 1, in `ci.yml` (both sites), the setup action and the host setup notes. A config-lint check fails on any unpinned `pip install ... platformio`, with a negative case. | 2 |
| **A3 · MSYS-safe builds:** `scripts/pio-flash.py` and any other script of ours that starts PlatformIO drops `MSYSTEM` for the child (row 6). A host test covers it: the child environment has no `MSYSTEM`, and the rest of the environment is passed through. | 2 |
| **A4 · C3 and classic in CI** (only if §9 choice 5 rules it in): add the four Xiao C3 envs and one Heltec V2 repeater to the CI matrix. | 1 |
| **A5 · Docs:** the developer setup note for the store and the pinned PlatformIO; the `docs/changes/` record. | 1 |
| **A6 · Epic verification:** the full CI matrix is green on the PR, and the two-project build from A1 runs again from a clean store. | 1 |
| **A7 · Push, PR, merge.** | 1 |

### Epic B: CODE PORT ([#2204](https://github.com/OffbandMesh/meshcore-firmware/issues/2204))

Every change builds on **both** frameworks behind `ESP_IDF_VERSION` guards, so B merges without moving any board, and D is only a platform change.

| Task | Pts |
|---|---|
| **B1 · ESP-NOW:** `ESPNOWRadio.cpp` and `ESPNowBridge.cpp` take the IDF 5.x callback signatures: `wifi_tx_info_t` on send from IDF 5.5, and `esp_now_recv_info_t` on receive from IDF 5.0. They include `<esp_mac.h>` on IDF 5 (row 7). Both build on both platforms. | 2 |
| **B2 · The rest of the port:** every other failure wave 2 classified (row 8), guarded the same way. Known so far: LEDC in `GuiPanel.cpp:16-17` and `EinkPanel.cpp:51-52` (`ledcAttach` on Arduino 3), and `f_mkfs` in `SdCard.cpp:162` (the `MKFS_PARM` form on FatFs R0.14). A format that changes between the two frameworks is a §6 stop: the SD card format has to stay readable on both. | 3 |
| **B3 · Bootloader rollback** (only if §9 choice 4 rules it in): override `verifyRollbackLater()` to return true, so an OTA'd image stays `PENDING_VERIFY` until `markBootValid()` confirms it after 60 s (row 12). Host-test the decision. Bench-test it in E5. | 2 |
| **B4 · Dual-build check:** a scratch run builds every shipped and CI ESP32 env on both platforms in isolated stores. The result table goes on the Epic issue. | 2 |
| **B5 · Docs:** the `docs/changes/` record (no user-visible change). | 1 |
| **B6 · Epic verification:** the CI matrix is green on the old platform, and B4's table shows zero compile failures on the new one. | 1 |
| **B7 · Push, PR, merge.** | 1 |

### Epic C: 4 MB BOARDS, C3 AND C6 ([#2205](https://github.com/OffbandMesh/meshcore-firmware/issues/2205))

Owner's ruling, §9 decision 2: every shipped 4 MB ESP32 build moves to one partition table. Its sizes come from [`SIZE-ANALYSIS.md`](SIZE-ANALYSIS.md): two 0x1B0000 (1,728 KB) app slots and 0x80000 (512 KB) of SPIFFS, with nvs, otadata and coredump at today's offsets. The move needs one USB flash, which wipes SPIFFS. Users are warned, and nothing is backed up.

| Task | Pts |
|---|---|
| **C1 · The table:** add `offband_4MB.csv` with the layout above, and point every shipped 4 MB ESP32 env at it. The set is decided from the inventory, not from memory: today it is the four Xiao C3 envs, the two Xiao C6 envs (`Xiao_C6_companion_radio_ble_`, `Xiao_C6_repeater_`) and the four Photon C6 envs. Each env's own `MAX_CONTACTS` and `MAX_GROUP_CHANNELS` stay as they are. | 2 |
| **C2 · Fit:** every env from C1 builds with its real `firmware.bin` under the 1,728 KB slot, and its worst-case SPIFFS data set fits 512 KB with room to rewrite `/contacts3`. Both are measured: the size by build, the SPIFFS by packing the worst case with `mkspiffs` at that env's limits, as in the analysis. Results go on the Epic issue. | 2 |
| **C3 · A unit on the old table says so:** an OTA writes only the app, so a 4 MB unit updated over the air keeps its old partition table. At boot the firmware reads the running table. If SPIFFS is smaller than this build expects, it logs a warning and the CLI shows "old partition table: a USB flash is needed, which erases this node's identity and contacts". It never repartitions or formats by itself. Host tests cover the old-table and new-table cases. | 3 |
| **C4 · CI:** config-lint fails if a shipped 4 MB ESP32 env uses any other partition table, with a negative case. | 1 |
| **C5 · The warning:** the release notes open with it, each affected board's guide page carries it, and the `docs/changes/` record names it. It says which boards are affected, that the update needs a USB flash, that the flash erases the node's identity, contacts, channels, access list and settings, and what to write down first. Copy is the owner's to approve. | 1 |
| **C6 · Epic verification and PR.** | 1 |

### Epic D: ONE PLATFORM ([#2206](https://github.com/OffbandMesh/meshcore-firmware/issues/2206))

| Task | Pts |
|---|---|
| **D1 · Switch:** `esp32_base` and `esp32c6_base` take the one pioarduino version chosen in §9 choice 1. `platformio/espressif32@6.11.0` and pioarduino 53 leave the tree. The store tag moves with the pin. `esp32_base` sets `lib_archive = yes` (row 17). The weak-override check runs on every ESP32 env: for each symbol bound to a weak definition, no library object holds a strong one. | 2 |
| **D2 · Release images:** make `merge-bin.py` take its flash mode from the platform's `_get_board_flash_mode`, so a merged bootloader keeps today's DIO (row 11). Its test fails on a merged image whose bootloader header says QIO. Then run `mergebin` on one env per chip, and check that each merged image holds a bootloader, partition table and app at the offsets the chip expects (row 11). Compare the app image header from both frameworks for each chip: chip ID, minimum chip revision, and flash mode, frequency and size. Any difference is reported before E flashes anything. | 2 |
| **D3 · One-platform invariant:** config-lint fails if any ESP32 env resolves to a different `platform`. A negative case uses an env that sets its own. It also fails if an ESP32 env doesn't resolve `lib_archive` to `yes`, with its own negative case. | 2 |
| **D4 · Size table:** flash and RAM, before and after, for every shipped ESP32 env. Flash is `firmware.bin` against the app slot, because PlatformIO's figure leaves out `.eh_frame` (row 18). Any env over 90% of its slot gets a row in §10 or a fix. | 2 |
| **D5 · Docs:** the release-facing change, written for users per §9 choice 3 (downgrade); the `docs/changes/` record. Copy is the owner's to approve. | 1 |
| **D6 · Epic verification:** the full CI matrix is green, plus D2 and D4. | 1 |
| **D7 · Push, PR.** It merges after E1 to E6 pass (§4). | 1 |

### Epic E: HARDWARE CHECKS ([#2207](https://github.com/OffbandMesh/meshcore-firmware/issues/2207))

Runs on D's branch before D merges. Each flash gets the owner's go naming the unit, through `scripts/pio-flash`, with the image identity checked and the capture held. The order is cheapest to most valuable, so a broken framework shows up on the bench unit first.

| Task | Pts |
|---|---|
| **E1 · S3 bench unit (`hv4-bench-1`):**<br>• Start on the current release, and take a full flash backup through `pio-flash` before the first OTA. If the identity check below fails, the unit can be restored byte for byte.<br>• OTA to the new build. The identity, contacts and settings survive (row 13).<br>• A phone already bonded on the old build reconnects without re-pairing. The BLE bond keys live in NVS, so this is the NVS check.<br>• Boot, radio, BLE pairing with the **stock MeshCore client** and the Offband client, and messages both ways with a unit still on the old build.<br>• `SafeBoot` boot count clean.<br>• Then the downgrade check from row 14, per §9 choice 3. | 4 |
| **E2 · S3 families:** V4 TFT GUI, RC32, XIAO S3 and T-Deck Pro. Each gets boot, radio, the UI or client path it ships with, and the OTA-keeps-identity check. | 4 |
| **E3 · C6 families:** the RCC6 companion (16 MB, so OTA as in E1) gets the same checks. The Photon-1W is a 4 MB board (Epic C):<br>• First, OTA the new build onto its old table: it boots, and C3's old-table warning appears.<br>• Then a USB flash with `offband_4MB.csv`, which wipes its SPIFFS. That flash needs the owner's wipe go naming the unit, separate from the flash go.<br>• After it: a clean first boot, then a stored contact list filled past the old 107-contact ceiling that survives a reboot. | 4 |
| **E4 · Roles:** one repeater and one room server: remote admin from the stock client; an observer: after the OTA, it rejoins its configured WiFi and MQTT broker with nothing re-entered (the credentials live in NVS). | 3 |
| **E5 · Rollback:** if choice 4 ruled B3 in, a deliberately broken OTA image on the bench unit rolls back through the bootloader. Either way, the app-level counter still rolls back an image that runs but fails to validate. | 2 |
| **E6 · C3 and classic:** per §9 choice 6. | 1 |
| **E7 · Results and docs:** per-unit results on the Epic issue; facts that identify a unit go in `HARDWARE.local.md`. Docs fixes from what E found. | 1 |
| **E8 · Push, PR, merge**, after D's PR has merged (§4). | 1 |

### Epic F: PHOTON C6 I2C ([#2208](https://github.com/OffbandMesh/meshcore-firmware/issues/2208))

| Task | Pts |
|---|---|
| **F1 · Re-run the hang test** on the Photon-1W with no battery, on a 55.x build, with the same `i2cscan` walk that hung on IDF 5.3 (row 15). One flash, on the owner's go. | 2 |
| **F2 · Rule on `i2cscan`:** per §9 choice 7, on F1's result. If the walk completes, the verb ships as planned in [#1055](https://github.com/OffbandMesh/meshcore-firmware/issues/1055). If it still hangs, the verb refuses on boards whose sensor scan is skipped, and the hang goes to its own issue. | 2 |
| **F3 · Epic verification and PR.** | 1 |

### Epic G: INTEGRATION ([#2209](https://github.com/OffbandMesh/meshcore-firmware/issues/2209))

| Task | Pts |
|---|---|
| **G1 · Integrated run:** with everything merged, a clean store builds the full matrix, and the bench unit cold-boots and passes E1's checks again. | 2 |
| **G2 · `OUTCOMES.md`** beside this plan: plain summary first, then what was delivered, the evidence, and what's left. | 2 |
| **G3 · Push, PR, merge.** | 1 |

## 4. Order and dependencies

P → A → B → C → D → E → F → G.

- **A first.** Without its own store, the dual builds in B and C on this host can fail for reasons that aren't in our code (row 2).
- **B before D.** B makes the code build on both platforms, so D is a one-line platform change with a green matrix behind it.
- **C before D.** D can't go green while the C3 envs overflow.
- **E before D merges.** `firmware-base` never carries an untested framework into a release. The order, in full:
  1. D's PR is opened.
  2. E flashes images built from D's PR branch. E commits nothing to that branch.
  3. D's PR merges only after E1 to E6 pass and their results are recorded on #2207.
  4. E's own PR (E7's results and docs) is opened from `firmware-base` after D merges, and merges second.
- **F after D.** It tests the hang on the framework that ships.
- **Shared files:** A, C and D all touch `platformio.ini` and `variants/*/platformio.ini`; B touches `src/helpers/esp32/` and `src/helpers/bridges/`. They run in that order, so no two Epics change the same lines at once.
- **Outside this Feature:** the [#1285](https://github.com/OffbandMesh/meshcore-firmware/issues/1285) re-land (the declared RTC and `i2cscan`) is on its own branch. F needs the verb on its test image. F builds that image from the re-land branch, not by bundling the change into this Feature.

## 5. Grants and authorization

| Term | Proposed | Why |
|---|---|---|
| **What merging this plan authorizes** | Epics A through G as written, end to end, with merges held to each Epic's PR. A4, B3 and the C2 option run only as §9 rules. | The plan is the master layout. |
| **Merges** | 7 | One PR each for A, B, C, D, E, F and G. This plan's own merge is the approval, not a grant use. |
| **Epic verification** | The owner approves each Epic's PR. | D, E and F end on bench results only the owner can see, and A changes every developer's build setup. |
| **Owner steps during the run** | A go for each flash in E and F, naming the unit. | One flash, one explicit go. |
| **Stops** | Listed in §6. | Each one means the next step would act on a wrong premise. |
| **Flash** | None in the grant. | Owner directive: one flash, one explicit go. |
| **CI matrix** | Changes only as §9 choice 5 rules. | Matrix changes are the owner's. |

```grant-terms
{
  "epic_chain": [2203, 2204, 2205, 2206, 2207, 2208, 2209],
  "chain": true,
  "verification": { "mode": "owner" },
  "merge": { "budget": 7, "basis": "One PR each for Epic A (#2203, build foundation), B (#2204, code port), C (#2205, 4 MB boards), D (#2206, one platform), E (#2207, hardware checks, merged after D per §4), F (#2208, Photon C6 I2C) and G (#2209, integration and OUTCOMES.md). This plan's own PR merges as the approval and uses no budget." },
  "flash": null,
  "reset": null,
  "expires_after_hours": 336
}
```

## 6. Can't-tell branches

| Where | Can't tell what | Does |
|---|---|---|
| A1 | Another project's build still changes our store, or ours changes theirs | **stop**: report the store paths on the Epic issue. Nothing else in A merges. |
| B2 / B4 | A failure that a version guard can't fix (a library with no build for both frameworks) | **stop**: report it with the library and version. Whether to upgrade, replace or patch the library is the owner's. |
| B2 | The SD card format call can't produce a card that the current release also reads (`SdCard.cpp:162`) | **stop**: report it. A card formatted by one build must mount on the other. |
| D1 | The weak-override check finds a library's strong definition that archiving leaves out | **stop**: report the symbol and library. Whether to keep archiving for that env, or force the object in, is the owner's. |
| C2 | A 4 MB env doesn't fit the 1,728 KB slot, or its worst-case SPIFFS set doesn't fit 512 KB | **stop**: report the env and both figures. The table isn't changed without the owner. |
| C3 | The firmware can't read the running partition table at boot | **stop** the check and log that it couldn't tell. It never formats, repartitions or assumes either table. |
| D2 | A merged image doesn't hold the parts at the expected offsets, or `mergebin` fails | **stop**. No release image ships from D until it's fixed. |
| D4 | A shipped non-C3 env goes over its slot | **stop**: it gets the same treatment as C, ruled by the owner. |
| E (any) | After an OTA, the identity, contacts or settings aren't the ones from before | **stop all OTA tests**: report it. No further unit is updated. This is the wipe the plan exists to prevent (row 13). The USB move to `offband_4MB.csv` wipes by design and is not this case. |
| E3 | The Photon's USB flash to the new table would run without a wipe go from the owner naming the unit | **stop**. A flash go is not a wipe go. |
| E (any) | The unit doesn't boot the new image, or SafeBoot rolls it back | **stop**: hold the capture, report it. No other unit of that family is flashed. |
| E1 | The new bootloader doesn't boot the previous Offband release | **stop**: report it. §9 choice 3 is settled on that result before D5's copy is written. |
| E (any) | The stock client can't pair, list contacts or exchange messages, where it could on the current release | **stop**. Stock-client interoperability is a hard constraint. |
| E / F flash | The connected unit doesn't match the registered identity | **stop**: `pio-flash` refuses. Never pass `--approve-class-match` without the owner's word. |
| Any boot | SafeBoot's boot count reaches 3 | **stop** all port-opens until the unit has run 60 s. |
| F1 | The walk hangs on 55.x too | **stop**: report it with the capture. §9 choice 7 decides the verb on that result. |

No branch proceeds without its condition, so no fail-open grant is needed.

## 7. Verification

- **Named command:** the full CI matrix (`ci-green`: config-lint, the native suite and every build env), plus each Epic's own check: A1's two-project build, B4's dual-build table, C2's size and SPIFFS results, and D2's and D4's results.
- **Acceptance bar:**
  - Every ESP32 env resolves to one platform, and config-lint enforces it.
  - Every shipped ESP32 env builds under its slot.
  - On every bench family, an OTA from the current release keeps the unit's identity, contacts and settings. The stock client pairs and messages as before, and the unit exchanges messages with one on the old build.
  - The Photon-1W walk result on 55.x is recorded on #2196.

## 8. Done when

- Every ESP32 env builds on one pinned pioarduino platform, with a pinned PlatformIO, in this project's own store, and CI enforces it.
- Every bench family has passed E's checks, recorded per unit.
- F's result is recorded and `i2cscan` is ruled.
- `OUTCOMES.md` is approved.

## 9. Your choices

### Decided

| # | Decision | By |
|---|---|---|
| 2 | Every shipped 4 MB ESP32 build, C3 and C6, moves to `offband_4MB.csv` (Epic C). Moving needs a USB flash that wipes SPIFFS; users get a prominent warning, and nothing is backed up. Staying on old platforms and tables only postpones this, and 4 MB boards will reach the end of their room eventually. | Owner, 2026-11-19 (C3 wipe) and 2026-11-21 (C3 and C6, the table) |

### Open

Each needs a ruling before the task it names starts.

| # | Question | Options | Recommendation |
|---|---|---|---|
| 1 | Which framework version (A2, D1). | (a) 55.03.313: Arduino 3.3.13, IDF 5.5.6, released 2026-11-17, needs PlatformIO 6.2. (b) 55.03.311: Arduino 3.3.11, IDF 5.5.5, needs PlatformIO 6.1.19, built here. (c) 55.03.38-1, MeshSmith's choice for the Photon. | (b) 55.03.311. Every build, size and failure in §1 was measured on it, and it runs on the PlatformIO this host has. 55.03.313 came out the day before this plan and is untested here; it also needs PlatformIO 6.2, which A2 would have to pin first. Moving from 311 to 313 later is a patch-level step that changes one line, and the D3 lint and the CI matrix cover it. |
| 3 | Downgrades after the switch (D5, E1). | (a) Supported: E1 must show the new bootloader boots the previous release, and D5 says so. (b) Not supported by OTA: D5 tells users that going back needs a USB flash of the older merged image. | (a) if E1 shows it works, and (b) otherwise. E1 settles it on the unit, not on documentation. |
| 4 | Fix the disarmed bootloader rollback (row 12) here (B3, E5). | (a) In this Feature. (b) Its own issue, after this Feature. | (a). E has to test OTA recovery on the new framework anyway. With the layer disarmed, E5 would be testing a path that never runs. The fix is small, and it adds recovery, which is part of the yardstick. |
| 5 | Put the C3 and one classic build in the CI matrix (A4). | (a) Yes: four Xiao C3 envs and `Heltec_v2_repeater`. (b) No: they're checked at release time only, as today. | (a). The C3s are the builds closest to their limit, and today their first build after a change is a release build (row 10). That's five more matrix jobs. |
| 6 | What "checked" means for C3 and classic ESP32, with no unit on the bench (E6). | (a) Build and size only, and say so in the release notes. (b) Buy one Xiao C3 with an SX1262 add-on and run E's checks on it. (c) Ask a user with one to test a diag build. | Yours. (a) ships them untested on hardware for the first time on this framework. (b) and (c) cost money or a person's time. |
| 7 | What `i2cscan` becomes (F2). | (a) As planned in #1055, if F1's walk completes on 55.x. (b) It refuses on boards whose sensor scan is skipped, whatever F1 shows. | (a) on a clean F1, (b) if it still hangs. |

## 10. Carried-in work

| Item | Goes to |
|---|---|
| [#686](https://github.com/OffbandMesh/meshcore-firmware/issues/686): floating `lib_deps` | Stays open. Libraries that change for the new framework are pinned in B. The rest stays with #686. |
| [#742](https://github.com/OffbandMesh/meshcore-firmware/issues/742): the verbose bootloader, blocked on toolchain selection | Re-checked after D. pioarduino 55 may unblock it. |
| [#491](https://github.com/OffbandMesh/meshcore-firmware/issues/491), [#1438](https://github.com/OffbandMesh/meshcore-firmware/issues/1438): ESP32-P4 | Unblocked by D on the platform side. The bring-up stays in those issues. |
| [#1050](https://github.com/OffbandMesh/meshcore-firmware/issues/1050), [#2196](https://github.com/OffbandMesh/meshcore-firmware/issues/2196): the Photon I2C hang | F1's result is recorded on both. |
| [#294](https://github.com/OffbandMesh/meshcore-firmware/issues/294): Photon-1W USB companion handshake | E3 runs the Photon USB companion and records whether the new framework changes it. The fix stays in #294. |
| Envs over 90% of their slot after D4 | One row each, here or in their own issue. |
| The repeater's `/packet_log` has no size cap. | Its own issue, outside this Feature: it exists on today's framework. Not filed yet. (The C6 companions' 128 KB SPIFFS defect, row 19, is now fixed inside this Feature by Epic C.) |
| `Meshimi_*` envs use `max_app_4MB.csv`, which has no SPIFFS partition at all. Not shipped. | Its own issue if Meshimi is ever released. Epic C covers shipped envs only. |

When this plan merges, each Task gets a GitHub issue and a Citadel task, per canon.

## 11. Review

Gemini 2.5 Pro reviewed this plan adversarially on 2026-11-18, cost $0.025. Audit log: `docs/llm-consultations/2026-11-18-esp32-framework-plan-gemini-gemini-2.5-pro.log`. It raised eight findings. Each one was checked against the frameworks' own headers and sdkconfigs before it was accepted or rejected.

| # | Finding | Ruling | Evidence |
|---|---|---|---|
| 1 | B3 is unsafe: the OTA data partition format changed between IDF 4.4 and 5.x, so an old bootloader misreads what a new app writes. | **Rejected.** `esp_ota_select_entry_t` (sequence number, label, state, CRC) is the same struct in both frameworks. B3 stays; E5 tests it on the unit. | `bootloader_support/include/esp_flash_partitions.h`, both frameworks [read]. |
| 2 | Secure boot or flash encryption could break an OTA across IDF versions. | **Not applicable.** Neither is enabled: `CONFIG_SECURE_BOOT`, `CONFIG_SECURE_FLASH_ENC_ENABLED` and `CONFIG_FLASH_ENCRYPTION_ENABLED` are not set in either framework's sdkconfig. | Both `esp32s3/sdkconfig` [read]. |
| 3 | IDF 5 adds an MD5 to the partition table, which an IDF 4.4 bootloader can't read. | **Rejected.** Both frameworks define `ESP_PARTITION_MAGIC_MD5`. An OTA also writes only the app partition; the partition table is never rewritten. | `esp_flash_partitions.h:17`, both [read]. |
| 4 | NVS (BLE bonds, WiFi credentials) isn't explicitly checked. | **Accepted.** E1 checks that a bonded phone reconnects without re-pairing, and E4 checks that an observer rejoins WiFi and MQTT with nothing re-entered. | — |
| 5 | App image header fields (chip revision, flash mode, frequency, size) may differ. | **Accepted.** D2 compares the headers from both frameworks per chip before E flashes anything. | — |
| 6 | The first unit's SPIFFS is wiped before the §6 stop can fire. | **Accepted.** E1 takes a full flash backup before the first OTA, so a wipe is recoverable byte for byte. | `pio-flash backup` [read]. |
| 7 | The D and E merge order reads two ways. | **Accepted.** §4 now states the order, and D7, E8 and the grant basis follow it. | — |
| 8 | Row 12 confuses two rollback systems; `ESP32Board.cpp` doesn't count on the bootloader layer. | **Rejected.** The comment at `ESP32Board.cpp:163-180` names the bootloader layer as layer 1 of its defense, and the app counter as layer 2. | `ESP32Board.cpp:163-180` [read]. |