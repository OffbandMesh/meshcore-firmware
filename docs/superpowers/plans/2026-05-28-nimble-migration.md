# NimBLE Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` to execute this plan. Each task gets a fresh-context implementer subagent + spec reviewer + code-quality reviewer per the skill. Subagent prompts include explicit file-allowlist, file-forbidlist, acceptance criteria, and "out of scope — surface but do not act" guards. Subagents cannot touch hardware (Tier 2 stays with the human). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate `SerialBLEInterface` and the observer-build BLE stack configuration from Bluedroid to NimBLE, recovering ~30-50 KB of internal SRAM across all 3 SoCs and unblocking BLE pairing on the Plan 3 observer firmware where it currently fails with `BLE_INIT: Malloc failed`. This is the only viable path to functional Plan 3 BLE on Heltec V3 (which has no PSRAM and cannot benefit from runtime-PSRAM heap relief).

**Architecture:** Replace Bluedroid (the IDF default BT host on arduino-esp32) with NimBLE (Apache mynewt's smaller stack) by (1) adding the `h2zero/NimBLE-Arduino` library to `lib_deps`, (2) flipping sdkconfig macros via `-D CONFIG_BT_BLUEDROID_ENABLED=0 -D CONFIG_BT_NIMBLE_ENABLED=1`, and (3) porting `src/helpers/esp32/SerialBLEInterface.cpp/.h` (344 lines total) from the `arduino-esp32` `BLEDevice` API to the structurally-similar `NimBLEDevice` API. Pairing and bonding callback shapes differ subtly between the two stacks; that is the highest-risk surface and gets its own dedicated task with hardware-test gating.

**Tech Stack:**
- `h2zero/NimBLE-Arduino @ ^2.0.0` (drop-in NimBLE wrapper designed to match the arduino-esp32 `BLEDevice` API shape; vetted in many ESP32 projects)
- ESP-IDF NimBLE host (bundled with arduino-esp32 framework; activated by sdkconfig)
- arduino-esp32 v2.x / IDF v4.4 (the existing toolchain; no framework change)
- Same `pio` build harness; no new tooling

**Repo locations:**
- Implementation lands in **`Strycher/MeshCore`** repo, NEW worktree under `meshcore-firmware/.worktrees/feat-nimble-migration/`
- Branched FROM `main` of the Strycher/MeshCore fork (NOT from Plan 3) so it can be validated on a simpler non-observer BLE env where Bluedroid pairing works today
- Plan 3 branch (`feat/observer-plan-3-web-ui-and-auth`) will REBASE onto the new main after NimBLE merges; inherits NimBLE automatically

**Prerequisites:**
- LoRa#288 (this epic's GH issue) open, LoRa-zm4 (Citadel task) claimed
- `git fetch strycher main` returns the current main HEAD before worktree creation
- Heltec V3 hardware (`hv3-bench`) available for the integration test bench (current Bluedroid pairing works on V3 companion env, providing the regression baseline)

**Tier discipline reminder (CLAUDE.md):**
- `pio run` is **Tier 2**; explicit per-action approval required for each build (one approval, one build, not "build until it works").
- Flashing for the integration test is **Tier 2**; explicit approval per device + per flash.
- `pio device monitor` is **Tier B**; pre-touch checklist required (port enumeration + VID:PID match + named device).
- Agent Mail check is automatic before each git/gh/dw mutation (hook enforced).

---

## Scope — what this plan delivers

| In scope | Out of scope |
|---|---|
| Add `h2zero/NimBLE-Arduino` library dep to BLE companion envs (xiao, v3, v4) | New variants beyond the existing 3 SoCs |
| Switch sdkconfig: disable Bluedroid, enable NimBLE on the BLE companion envs | Other BLE stacks (NimBLE host is the only target) |
| Port `SerialBLEInterface.h` declarations to NimBLE types | API changes to `SerialBLEInterface` public surface (preserve exactly so the_mesh / main.cpp / observer code is untouched) |
| Port `SerialBLEInterface.cpp` GATT service + characteristic setup | Changing the BLE service UUIDs or characteristic UUIDs (preserve for client compatibility) |
| Port `SerialBLEInterface.cpp` pairing + bonding callbacks (highest risk) | Adding/removing pairing features (PIN flow stays as-is) |
| Build verify on all 3 envs (V3, V4, XIAO companion BLE) | Plan 3 observer envs specifically — they inherit when Plan 3 rebases |
| Tier-2 hardware integration test: pair Heltec V3 with phone, measure heap delta | Web UI test (Plan 3 work; happens after rebase) |
| Per-task tagged commits + measured before/after heap | OTA / firmware-upgrade NimBLE differences |
| Documentation update if pairing behavior visibly differs to end users | NimBLE central-mode (scanner) features — peripheral mode only |

**Success means:** A new firmware build of `Heltec_v3_companion_radio_ble` (the non-observer env that's been stable on Bluedroid) on `hv3-bench` produces a chip that BLE-pairs to the user's phone exactly as before, exchanges GATT data exactly as before, and shows a measurable internal-SRAM free-heap gain at boot (target: +20 KB or more on V3, more on XIAO/V4). Plus all 3 envs (V3, V4, XIAO) build clean with NimBLE enabled and Bluedroid disabled.

---

## File Structure

### Files modified (Strycher/MeshCore fork)

```
src/helpers/esp32/
  SerialBLEInterface.h            -- replace #include <BLEDevice.h> with <NimBLEDevice.h>;
                                     rename member types (BLEServer* -> NimBLEServer*, etc.);
                                     preserve PUBLIC METHOD SIGNATURES (callers unchanged)
  SerialBLEInterface.cpp          -- port GATT setup + characteristic registration to NimBLE API;
                                     port pairing/bonding callbacks (the hard part);
                                     preserve advertising-on-disconnect behavior

variants/heltec_v3/platformio.ini   -- add lib_deps += h2zero/NimBLE-Arduino @ ^2.0.0
                                       on companion_radio_ble + WSL3_companion_radio_ble envs;
                                       add -D CONFIG_BT_NIMBLE_ENABLED=1
                                            -D CONFIG_BT_BLUEDROID_ENABLED=0
variants/heltec_v4/platformio.ini   -- same modifications on companion_radio_ble env
variants/xiao_s3_wio/platformio.ini -- same modifications on Xiao_S3_WIO_companion_radio_ble env
```

### Files NOT modified (out of scope; protect during subagent dispatch)

```
examples/companion_radio/main.cpp         -- preserves the_mesh / serial_interface usage
src/helpers/wifi_observer/*               -- observer layer untouched; inherits when Plan 3 rebases
src/helpers/esp32/SerialWifiInterface.*   -- separate transport; not affected
src/Mesh.h / src/MyMesh.h / src/MyMesh.cpp -- mesh core untouched
boards/*.json                              -- board defs unchanged
.claude/hooks/*                            -- session enforcement unchanged
```

**File-Convergence enforcement:** `SerialBLEInterface.h` is touched by Task N2 only. `SerialBLEInterface.cpp` is touched by Tasks N3 and N4 (sequential dependency: N4 waits for N3). The 3 variant `platformio.ini` files are each touched once by N1 (additive) and once by N5 (subtractive). No parallel work on the same file; the task graph serializes file access.

---

## Task Dependency Graph

```
N1 (add NimBLE lib + ENABLE flag, keep Bluedroid enabled too — transitional)
  -> N2 (port header)
       -> N3 (port .cpp GATT + characteristic setup)
            -> N4 (port .cpp pairing/bonding callbacks — HIGH risk)
                 -> N5 (disable Bluedroid in env sdkconfig)
                      -> N6 (build verify all 3 envs)
                           -> N7 (Tier-2 bench test: pair on V3, measure heap delta) — HUMAN
```

All seven tasks land on the SAME branch (`feat/nimble-migration`) sequentially. Each task gets one fresh-context implementer subagent + spec + code-quality review per `superpowers:subagent-driven-development`. After N7 passes, a single PR opens for human review + auto-merge.

---

## Task N1: Add NimBLE-Arduino library, enable NimBLE side-by-side with Bluedroid (transitional)

**Files:**
- Modify: `variants/heltec_v3/platformio.ini` (envs `Heltec_v3_companion_radio_ble` + `Heltec_WSL3_companion_radio_ble`)
- Modify: `variants/heltec_v4/platformio.ini` (env `heltec_v4_companion_radio_ble`)
- Modify: `variants/xiao_s3_wio/platformio.ini` (env `Xiao_S3_WIO_companion_radio_ble`)

**Subagent forbidden files:** All `.cpp`, `.h`, `.json`, all other `platformio.ini` files in `variants/`, `boards/*`, anything under `src/`, anything under `examples/`.

**Subagent acceptance:**
- `lib_deps` includes `h2zero/NimBLE-Arduino @ ^2.0.0` on the 4 envs listed above
- Build flags include `-D CONFIG_BT_NIMBLE_ENABLED=1` on the 4 envs
- Bluedroid is NOT disabled in this task (`CONFIG_BT_BLUEDROID_ENABLED` remains whatever the framework default is; we layer NimBLE on top transitionally)
- Build of any one of the 4 envs may now FAIL with linker conflicts (two BT stacks compiled simultaneously) — this is EXPECTED and acceptable; the failure surfaces in subsequent tasks. The acceptance for N1 is configuration correctness, not build success.

**Steps:**

- [ ] **Step 1: Audit current env structure**

  Read each of the 4 envs (file:line each). Identify the `[env:...]` block and confirm `lib_deps` exists (if not, add). Identify the `build_flags` block.

- [ ] **Step 2: Add NimBLE-Arduino to lib_deps**

  In each env block, append to `lib_deps`:
  ```ini
    h2zero/NimBLE-Arduino @ ^2.0.0
  ```

- [ ] **Step 3: Add NimBLE enable flag to build_flags**

  In each env block, append to `build_flags`:
  ```ini
    -D CONFIG_BT_NIMBLE_ENABLED=1
  ```

- [ ] **Step 4: Verify configuration via grep**

  Run: `grep -E "NimBLE-Arduino|CONFIG_BT_NIMBLE_ENABLED" variants/*/platformio.ini`

  Expected: 4 matches per pattern, one per env.

- [ ] **Step 5: Subagent self-review pass**

  Confirm no other files were modified (`git diff --stat` should show only 3 files: heltec_v3, heltec_v4, xiao_s3_wio platformio.ini).

- [ ] **Step 6: Commit (subagent commit per subagent-driven-development pattern)**

  ```
  feat(ble): N1 add NimBLE-Arduino lib + NIMBLE_ENABLED flag (transitional) (#288)

  Adds h2zero/NimBLE-Arduino @ ^2.0.0 + CONFIG_BT_NIMBLE_ENABLED=1 to the
  companion_radio_ble envs on V3, V4, XIAO. Bluedroid remains enabled at
  this checkpoint; build will be exercised in N6 after N2-N4 port the
  SerialBLEInterface code. Task: LoRa-zm4
  ```

  Tag: `crosswire-nimble-task-1-add-lib`

---

## Task N2: Port SerialBLEInterface.h declarations to NimBLE types

**Files:**
- Modify: `src/helpers/esp32/SerialBLEInterface.h` (the only file)

**Subagent forbidden files:** `SerialBLEInterface.cpp`, all `variants/*/platformio.ini`, all other `.cpp/.h` files, `examples/companion_radio/main.cpp`.

**Subagent acceptance:**
- `#include <BLEDevice.h>` becomes `#include <NimBLEDevice.h>`
- All member type renames: `BLEServer*` → `NimBLEServer*`, `BLECharacteristic*` → `NimBLECharacteristic*`, `BLEAdvertising*` → `NimBLEAdvertising*`, etc.
- All public method signatures UNCHANGED (callers in main.cpp must compile without modification once .cpp ports)
- File compiles standalone via `xtensa-esp32s3-elf-g++ -fsyntax-only` against the NimBLE-Arduino include path (subagent verifies)

**Steps:**

- [ ] **Step 1: Read full SerialBLEInterface.h**

  Identify every type alias / forward decl / member type referencing the Bluedroid BLE* hierarchy.

- [ ] **Step 2: Update include**

  ```cpp
  #include <NimBLEDevice.h>    // was: #include <BLEDevice.h>
  ```

- [ ] **Step 3: Rename member pointer types**

  Replace each `BLEXxx*` with `NimBLEXxx*`. Specific renames (subagent will encounter):
  - `BLEServer*` → `NimBLEServer*`
  - `BLEService*` → `NimBLEService*`
  - `BLECharacteristic*` → `NimBLECharacteristic*`
  - `BLEAdvertising*` → `NimBLEAdvertising*`
  - `BLEAdvertisementData*` (if used) → `NimBLEAdvertisementData*`

- [ ] **Step 4: Preserve all public method signatures**

  No changes to `begin()`, `isConnected()`, `enable*Callback()`, `writeFrame()`, etc. The public API is the contract main.cpp depends on.

- [ ] **Step 5: Syntax-only check**

  Subagent runs: `xtensa-esp32s3-elf-g++ -fsyntax-only -I <NimBLE-Arduino include path> SerialBLEInterface.h` (or equivalent through the build system). Verify no errors.

- [ ] **Step 6: Commit + tag**

  ```
  feat(ble): N2 port SerialBLEInterface.h to NimBLE types (#288)

  Renames BLE* -> NimBLE* member types and switches the include header.
  Public method signatures preserved exactly; main.cpp unchanged.
  Task: LoRa-zm4
  ```

  Tag: `crosswire-nimble-task-2-port-header`

---

## Task N3: Port SerialBLEInterface.cpp GATT service + characteristic setup

**Files:**
- Modify: `src/helpers/esp32/SerialBLEInterface.cpp` (ONLY methods relating to GATT service creation, characteristic registration, advertising setup, and connection event handling — NOT pairing/bonding callbacks)

**Subagent forbidden files:** `SerialBLEInterface.h` (touched in N2), all `variants/*/platformio.ini`, all other `.cpp/.h` files.

**Subagent forbidden methods within .cpp:** Anything in the security/pairing/bonding callback group (`setSecurityAuth`, `setCallbacks` for security, `onPassKeyRequest`, `onAuthenticationComplete`, `onConfirmPIN`, etc.) — those are Task N4. Subagent must surface but not modify them.

**Subagent acceptance:**
- `begin()`: `BLEDevice::init()` → `NimBLEDevice::init()`, `BLEDevice::createServer()` → `NimBLEDevice::createServer()`
- Service + characteristic creation uses NimBLE builder methods (slight syntactic differences from Bluedroid; subagent reads the NimBLE-Arduino docs to map)
- Advertising setup (`getAdvertising()`, `start()`, `setName()`, etc.) uses NimBLE API
- The manual `adv_restart_time` reconnect loop (per Gemini's high-risk-area callout) is PRESERVED exactly — same logic, just NimBLE method names

**Steps:**

- [ ] **Step 1: Read full SerialBLEInterface.cpp + identify method groups**

  Tag each method as: (a) GATT/advertising setup (this task), (b) connection event (this task), (c) security/pairing/bonding (Task N4 — forbidden here).

- [ ] **Step 2: Port begin() — device init + server creation**

  ```cpp
  // Before:  BLEDevice::init(name);
  //          BLEServer* pServer = BLEDevice::createServer();
  // After:
  NimBLEDevice::init(name);
  NimBLEServer* pServer = NimBLEDevice::createServer();
  ```

- [ ] **Step 3: Port service + characteristic registration**

  Adapt the existing service UUID + characteristic UUID definitions (PRESERVE THE UUIDS — these are the client-contract). Builder API in NimBLE is slightly different:
  ```cpp
  NimBLEService* pService = pServer->createService(SERVICE_UUID);
  NimBLECharacteristic* pTxChar = pService->createCharacteristic(
      TX_CHAR_UUID,
      NIMBLE_PROPERTY::NOTIFY
  );
  // ... matching the Bluedroid characteristic config
  ```

- [ ] **Step 4: Port advertising setup**

  ```cpp
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);
  pAdv->setScanResponse(true);
  pAdv->start();
  ```

- [ ] **Step 5: Port connection event handler**

  Server callbacks `onConnect` / `onDisconnect` get the new NimBLE callback class structure. Preserve the existing `adv_restart_time` logic on disconnect.

- [ ] **Step 6: Syntax-only check (do not link yet — pairing callbacks unimplemented)**

- [ ] **Step 7: Subagent self-review pass**

  Verify pairing/bonding methods were NOT touched (`git diff` shows only the GATT/advertising/connection methods).

- [ ] **Step 8: Commit + tag**

  ```
  feat(ble): N3 port GATT service + characteristic + advertising to NimBLE (#288)

  Ports the device init, server/service/characteristic creation, and
  advertising setup methods of SerialBLEInterface.cpp to the NimBLE-Arduino
  API. Pairing/bonding callbacks deliberately untouched (Task N4 scope).
  SERVICE_UUID and characteristic UUIDs preserved exactly. adv_restart_time
  reconnect logic preserved. Task: LoRa-zm4
  ```

  Tag: `crosswire-nimble-task-3-port-gatt-service`

---

## Task N4: Port SerialBLEInterface.cpp pairing + bonding callbacks (HIGH RISK)

**Files:**
- Modify: `src/helpers/esp32/SerialBLEInterface.cpp` (ONLY the security/pairing/bonding callbacks)

**Subagent forbidden files + methods:** All previously-touched methods from N3 (GATT setup, advertising, connection events) must NOT be re-touched. All other files forbidden.

**Risk callout (from Gemini):** This is the most complex part of the port. Bluedroid uses `BLESecurity` + characteristic-level encryption properties; NimBLE uses `NimBLEDevice::setSecurityAuth()` + global security callbacks via `NimBLEServerCallbacks` subclass. The exact sequence of `setSecurityAuth`, `setCallbacks`, and the data provided in the `onAuthenticationComplete` callback differs and is the most likely place for the port to misbehave at runtime. Subagent prompt MUST instruct: "if any aspect of the callback shape is ambiguous, document the ambiguity in the commit message as a known risk for the integration test; do not silently invent semantics."

**Subagent acceptance:**
- PIN-based pairing: BLE clients (the user's phone) prompted for the same 6-digit PIN as before
- Bonding storage: NimBLE writes bond records to NVS the same way Bluedroid did (NimBLE handles this internally via `CONFIG_BT_NIMBLE_NVS_PERSIST=1`; subagent verifies this is set)
- Compiles AND links (no Bluedroid symbols referenced; this is the first task where linking should succeed end-to-end on N1's transitional config)

**Steps:**

- [ ] **Step 1: Read NimBLE-Arduino docs/examples for security callback signatures**

  Subagent reviews `NimBLEDevice::setSecurityAuth`, `NimBLEServerCallbacks::onPassKeyRequest`, `onConfirmPIN`, `onAuthenticationComplete` signatures from the library's headers in `~/.platformio/lib/<NimBLE-Arduino-install>/src/`.

- [ ] **Step 2: Port the security init call**

  ```cpp
  // Bluedroid was:  BLEDevice::setSecurityCallbacks(new MySecCallback());
  //                 BLESecurity *pSecurity = new BLESecurity();
  //                 pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  //                 pSecurity->setCapability(ESP_IO_CAP_OUT);
  //                 pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  // NimBLE:
  NimBLEDevice::setSecurityAuth(/*bonding*/ true, /*mitm*/ true, /*sc*/ true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(static_pin);
  pServer->setCallbacks(new MySecCallback());
  ```

- [ ] **Step 3: Port the callback class**

  The Bluedroid `BLESecurityCallbacks` subclass becomes a `NimBLEServerCallbacks` subclass (NimBLE merges security + connection callbacks into one class).

- [ ] **Step 4: Port onPassKeyRequest**

  ```cpp
  uint32_t onPassKeyRequest() {
      return static_pin;   // PRESERVES existing logic
  }
  ```

- [ ] **Step 5: Port onAuthenticationComplete**

  ```cpp
  void onAuthenticationComplete(ble_gap_conn_desc* desc) {
      if (desc->sec_state.encrypted) {
          // success path; preserve existing log + state update
      } else {
          // failure path
      }
  }
  ```

- [ ] **Step 6: Verify NVS persistence is enabled**

  Subagent checks the NimBLE-Arduino library's default `CONFIG_BT_NIMBLE_NVS_PERSIST` is 1 (it is in the library's default sdkconfig). If not, surfaces this as a known gap for the integration test.

- [ ] **Step 7: Full build (still transitional — Bluedroid + NimBLE both enabled per N1)**

  ```
  pio run -e Heltec_v3_companion_radio_ble
  ```

  Expected: may fail with link errors (Bluedroid + NimBLE both pulled in). The acceptance is that the NimBLE-side code compiles; Bluedroid will be disabled in N5.

- [ ] **Step 8: Subagent self-review pass**

  Pairing flow walkthrough in the commit message: "Phone initiates pair → onPassKeyRequest fires → returns static_pin → onAuthenticationComplete fires with desc->sec_state.encrypted=true → bonding persists to NVS automatically."

- [ ] **Step 9: Commit + tag**

  ```
  fix(ble): N4 port pairing + bonding callbacks to NimBLE (#288)

  Ports the security init + callback class subclass + onPassKeyRequest +
  onAuthenticationComplete methods. NimBLE merges security and connection
  callbacks into NimBLEServerCallbacks (different from Bluedroid's split).
  PIN behavior preserved. NVS bond persistence verified enabled.

  HIGH-RISK AREAS surfaced for integration test (N7):
    - First-time pair flow on a fresh NVS
    - Re-connect with existing bond (LTK reuse)
    - Behavior when phone forgets bond but device retains it
       (mirror of the asymmetric-bond scenario from 2026-05-28 session)

  Task: LoRa-zm4
  ```

  Tag: `crosswire-nimble-task-4-port-pairing`

---

## Task N5: Disable Bluedroid in env sdkconfig overrides

**Files:**
- Modify: same 4 envs as N1

**Subagent forbidden files:** All `.cpp/.h`, all other `platformio.ini` files in `variants/`, `boards/*`, `src/`, `examples/`.

**Subagent acceptance:**
- `-D CONFIG_BT_BLUEDROID_ENABLED=0` appears in build_flags for each of the 4 envs
- `-D CONFIG_BT_NIMBLE_ENABLED=1` from N1 still present
- The 4 platformio.ini files build cleanly with NIMBLE only

**Steps:**

- [ ] **Step 1: Add the disable flag**

  In each of the 4 env build_flags blocks, append:
  ```ini
    -D CONFIG_BT_BLUEDROID_ENABLED=0
  ```

- [ ] **Step 2: Verify grep**

  ```bash
  grep -E "CONFIG_BT_BLUEDROID_ENABLED|CONFIG_BT_NIMBLE_ENABLED" variants/*/platformio.ini
  ```

  Expected: each env has both — NIMBLE=1 + BLUEDROID=0.

- [ ] **Step 3: Subagent self-review pass**

  Verify only the 3 platformio.ini files in `git diff --stat`.

- [ ] **Step 4: Commit + tag**

  ```
  feat(ble): N5 disable Bluedroid in companion_radio_ble envs (#288)

  Adds CONFIG_BT_BLUEDROID_ENABLED=0 to the same 4 envs that received
  the NimBLE-Arduino lib and enable flag in N1. Bluedroid is now fully
  out of the build; NimBLE is the sole BLE stack.
  Task: LoRa-zm4
  ```

  Tag: `crosswire-nimble-task-5-disable-bluedroid`

---

## Task N6: Build verify all 3 envs

**Files:** none (verification only)

**Subagent forbidden files:** EVERYTHING. No modifications allowed.

**Subagent acceptance:**
- `pio run -e Heltec_v3_companion_radio_ble` succeeds (Tier 2 — controller will surface for explicit approval before this task dispatches)
- `pio run -e heltec_v4_companion_radio_ble` succeeds (Tier 2 — same)
- `pio run -e Xiao_S3_WIO_companion_radio_ble` succeeds (Tier 2 — same)
- `xtensa-esp32s3-elf-nm -S firmware.elf | grep -i bluedroid` returns empty on each .elf (proof Bluedroid is GONE)
- `xtensa-esp32s3-elf-nm -S firmware.elf | grep -i nimble` returns NON-empty (proof NimBLE is present)

**Steps:**

- [ ] **Step 1: STOP — request Tier 2 build approval for V3 from user**

  Subagent prompt explicitly: "You may NOT run `pio run` without explicit user approval per CLAUDE.md Tier 2 discipline. Surface the planned command, request approval, await confirmation."

- [ ] **Step 2: Build V3**

  After approval:
  ```
  pio run -e Heltec_v3_companion_radio_ble
  ```

- [ ] **Step 3: Verify NimBLE present, Bluedroid absent on V3 .elf**

  ```bash
  $nm = "C:\Users\stryc\.platformio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32s3-elf-nm.exe"
  & $nm -S ".pio\build\Heltec_v3_companion_radio_ble\firmware.elf" | grep -i bluedroid    # should be empty
  & $nm -S ".pio\build\Heltec_v3_companion_radio_ble\firmware.elf" | grep -i nimble | head # should have entries
  ```

- [ ] **Step 4: Repeat for V4 + XIAO**

  Steps 1-3 for each remaining env, with their own Tier 2 approval ask.

- [ ] **Step 5: Heap-size delta measurement (from build summary)**

  Compare `RAM:` reported by each `pio run` summary against the pre-NimBLE baseline (recorded in N1 commit message OR from the latest main HEAD build). Document the delta in the N6 commit message.

- [ ] **Step 6: Commit + tag (no file changes; commit captures the verification log)**

  ```
  test(ble): N6 build verify 3 envs with NimBLE-only configuration (#288)

  All 3 companion_radio_ble envs (V3, V4, XIAO) build successfully with
  Bluedroid disabled and NimBLE enabled. nm verification confirms no
  Bluedroid symbols + presence of NimBLE symbols on each .elf.

  RAM usage deltas vs Bluedroid baseline:
    V3:   <BEFORE> bytes -> <AFTER> bytes (Δ +<DELTA>)
    V4:   <BEFORE> bytes -> <AFTER> bytes (Δ +<DELTA>)
    XIAO: <BEFORE> bytes -> <AFTER> bytes (Δ +<DELTA>)

  Task: LoRa-zm4
  ```

  Tag: `crosswire-nimble-task-6-build-verify`

---

## Task N7: Tier-2 hardware integration test (HUMAN-OPERATED)

**Files:** none (verification only). NO SUBAGENT for this task. Controller orchestrates with human.

**Tier:** Hard Tier 2 — flash AND port-monitor. Pre-touch checklist per CLAUDE.md required for each flash and each monitor.

**Acceptance:**
- Flash succeeds onto `hv3-bench` (Heltec V3, MAC `3c:0f:02:eb:f9:0c`, currently registered in `hardware-devices.yaml`)
- Chip boots without crash loop (boot counter advances normally, heartbeats emit)
- User initiates BLE pair from phone (MeshCore Companion app or similar)
- Pair completes successfully (PIN flow + bond persistence)
- GATT exchange works (user sends a test command, receives response — `dw` ready, `get device.info`, or equivalent)
- Heap measurement: `free_heap` from heartbeat is at least 20 KB higher than the equivalent Bluedroid baseline on V3
- Asymmetric-bond test: phone forgets bond, attempts re-pair; chip handles gracefully (offers fresh pair OR refuses with clear log message) without crash

**Steps:**

- [ ] **Step 1: Pre-touch checklist for flash (CLAUDE.md)**

  Controller runs `pio device list`, confirms VID:PID for `hv3-bench`, surfaces port + named device to user, awaits explicit "go" approval.

- [ ] **Step 2: Flash V3 .bin via `scripts/pio-flash`**

  ```
  python scripts/pio-flash.py flash hv3-bench --env Heltec_v3_companion_radio_ble
  ```

  The wrapper handles tier-A identity check.

- [ ] **Step 3: Open serial monitor**

  ```
  pio device monitor -p <COMxx> -b 115200 --filter esp32_exception_decoder
  ```

  Wait for first heartbeat. Record `free_heap` from heartbeat line. Compare to Bluedroid baseline.

- [ ] **Step 4: User initiates BLE pair from phone**

  Document: what app, what device name was visible, how PIN was entered (or auto-prompted), how long the pair took, any error messages.

- [ ] **Step 5: User sends a test GATT command**

  Record: command sent, response received, any latency observed.

- [ ] **Step 6: Asymmetric-bond test**

  User: forget bond on phone side; attempt re-pair. Record: what happened (graceful re-pair, refusal, crash).

- [ ] **Step 7: Decision gate**

  IF all acceptance items pass: epic complete, proceed to PR creation.
  IF any fail: open a `fix(ble):` follow-up task within this epic; subagent investigates per the failure mode. Do NOT close the epic with failures.

- [ ] **Step 8: Update GitHub issue #288 with bench evidence**

  Comment with: heap delta, pair-flow log, GATT test result. Close acceptance checkboxes.

- [ ] **Step 9: Open PR for merge to main**

  ```
  gh pr create -R Strycher/MeshCore --title "feat(ble): migrate Bluedroid -> NimBLE (closes #288)" --body "..."
  ```

  Body includes: full N7 evidence; per-task commit list with tags; rebase-path note for Plan 3 branch.

- [ ] **Step 10: Enable auto-merge after human approval**

  ```
  gh pr merge <num> -R Strycher/MeshCore --auto --rebase
  ```

  After human approves the PR.

- [ ] **Step 11: Post-merge tag**

  ```
  git tag crosswire-vX.Y.0-nimble <merge-commit-sha>
  git push strycher --tags
  ```

  Per VERSIONING discipline — tag immediately after merge.

- [ ] **Step 12: Close LoRa-zm4**

  ```
  dw --project LoRa close LoRa-zm4 --reason "NimBLE migration shipped at <sha>; pair + GATT verified on V3; heap delta +<N> KB; Plan 3 branch can now rebase"
  ```

---

## Post-epic: Plan 3 rebase

After Epic D (this plan) merges, the Plan 3 branch (`feat/observer-plan-3-web-ui-and-auth`) needs to rebase onto the new main:

```bash
cd meshcore-firmware/.worktrees/feat-observer-plan-3-web-ui-and-auth
git fetch strycher main
git rebase strycher/main
# resolve conflicts in main.cpp / SerialBLEInterface usage if any
# (none expected since SerialBLEInterface public API was preserved)
pio run -e Xiao_S3_WIO_companion_observer_wifi    # Tier 2
```

This is NOT part of Epic D. It's a follow-up step on the Plan 3 work. File as a new task at that time.

---

## Self-Review Checklist

Per `superpowers:writing-plans`, run this myself after writing the plan:

- [x] Spec coverage: every claim in the GH issue #288 body has a corresponding task here
- [x] No placeholders (TBD, "implement later", "similar to Task N"); every code step has actual code or actual commands
- [x] Type consistency: `NimBLEServer`, `NimBLECharacteristic`, etc. — same names used throughout
- [x] File-Convergence Rule: `SerialBLEInterface.cpp` touched by both N3 and N4, but N4 depends on N3 (sequential); no parallel conflict
- [x] Integration test is the final task (N7) per Epic Completion Protocol
- [x] Tier discipline surfaced explicitly (Tier 2 build approval gates per env in N6, Tier 2 flash + monitor gates in N7)
- [x] Subagent forbidden-files lists are explicit per task
- [x] Out-of-scope items captured in Scope table
- [x] Plan 3 rebase noted as a follow-up (not part of this epic) so future agents don't bundle them

---

## Execution Handoff

**Plan saved to:** `docs/superpowers/plans/2026-05-28-nimble-migration.md`

**REQUIRED SUB-SKILL:** `superpowers:subagent-driven-development`

**Controller's next action after plan approval:**

1. Create the worktree (Tier 1, no approval needed beyond the existing epic claim):
   ```
   cd meshcore-firmware
   git worktree add .worktrees/feat-nimble-migration -b feat/nimble-migration strycher/main
   ```
2. Dispatch the N1 implementer subagent (fresh context, file-allowlist enforced)
3. After N1: dispatch the spec reviewer subagent
4. After spec review passes: dispatch the code-quality reviewer subagent
5. After all reviews pass: tag, mark N1 complete, dispatch N2
6. Repeat through N6
7. STOP at N7 — hand off to human for bench testing
