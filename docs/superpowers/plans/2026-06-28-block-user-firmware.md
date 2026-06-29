# Block User — Firmware Half Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the Offband-only firmware half of user blocking — a portable, node-resident pubkey block list that drops blocked DMs at the device and syncs to/from the app over the companion link.

**Architecture:** A pure in-memory `BlockStore` (raw 32-byte pubkeys, no crypto deps → fully native-unit-testable) owned by `MyMesh`, persisted to a file (mirroring the `savePrefs` pattern), wired into (a) the `CMD_DEVICE_QUERY` capability byte, (b) a new `0xC2 CMD_OFFBAND_BLOCK` companion command (ADD/REMOVE/LIST/CLEAR), and (c) a one-line drop check at the top of `queueMessage`. **Receive-side only** — no change to forwarding, relaying, adverts, channels, or any RF path (contract §11). Adverts and channel blocking stay app-side, so the firmware needs no advert/channel changes.

**Tech Stack:** C++17, PlatformIO (`[env:native]` googletest 1.17.0 for unit tests; ESP32/nRF52 for device builds), the companion-API frame protocol (`0xC0+` namespace).

**Contract of record:** `docs/architecture/2026-06-28-block-user-firmware-app-contract.md` (Feature #241, Epic #242).

**Locked design decisions** (resolving the contract's §10 open questions):
- `MAX_BLOCKED_KEYS = 32` (32 × 32 B = 1 KB).
- `0xC2` sub-codes: `ADD=0x01`, `REMOVE=0x02`, `LIST=0x03`, `CLEAR=0x04`.
- `OFFBAND_CAP_BLOCK = 0x02`; `FIRMWARE_VER_CODE` 14 → 15.
- Persistence: a dedicated file `/blocks` (flat `[count][key0..keyN]`), loaded at boot, saved on change.
- Adverts: **no firmware change** (suppression is app-side per contract §5/§6).

---

## File Structure

- **Create** `src/helpers/BlockStore.h` — pure, header-only block list (raw pubkeys). Under `-I src`, so both the companion build and the native test see it. One responsibility: hold/add/remove/test/iterate blocked keys.
- **Create** `test/block/test_block_store.cpp` — native googletest for `BlockStore`.
- **Modify** `examples/companion_radio/OffbandConfigProtocol.h` — add `OFFBAND_CAP_BLOCK`, `CMD_OFFBAND_BLOCK` + sub-codes.
- **Modify** `examples/companion_radio/MyMesh.h` — add a `BlockStore _blocks;` member + `loadBlocks()/saveBlocks()` decls.
- **Modify** `examples/companion_radio/MyMesh.cpp` — bump `FIRMWARE_VER_CODE`, set the cap bit, the `0xC2` handler, the `queueMessage` drop, persistence, boot load.

---

## Task 1: BlockStore core (pure, native-unit-tested)

**Files:**
- Create: `src/helpers/BlockStore.h`
- Test: `test/block/test_block_store.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// test/block/test_block_store.cpp
#include <gtest/gtest.h>
#include <cstring>
#include "helpers/BlockStore.h"

static void mkKey(uint8_t* out, uint8_t seed) { memset(out, seed, BLOCK_KEY_SIZE); }

TEST(BlockStore, AddContainsRemove) {
    BlockStore bs;
    uint8_t a[BLOCK_KEY_SIZE], b[BLOCK_KEY_SIZE];
    mkKey(a, 0xAA); mkKey(b, 0xBB);

    EXPECT_EQ(bs.count(), 0);
    EXPECT_FALSE(bs.contains(a));

    EXPECT_TRUE(bs.add(a));
    EXPECT_TRUE(bs.contains(a));
    EXPECT_FALSE(bs.contains(b));
    EXPECT_EQ(bs.count(), 1);

    EXPECT_TRUE(bs.remove(a));
    EXPECT_FALSE(bs.contains(a));
    EXPECT_EQ(bs.count(), 0);
    EXPECT_FALSE(bs.remove(a));   // not present
}

TEST(BlockStore, DedupAndCapacity) {
    BlockStore bs;
    uint8_t k[BLOCK_KEY_SIZE];
    mkKey(k, 0x01);
    EXPECT_TRUE(bs.add(k));
    EXPECT_TRUE(bs.add(k));        // dedup -> still true, count stays 1
    EXPECT_EQ(bs.count(), 1);

    for (int i = 2; i <= MAX_BLOCKED_KEYS; i++) { uint8_t kk[BLOCK_KEY_SIZE]; mkKey(kk, (uint8_t)i); EXPECT_TRUE(bs.add(kk)); }
    EXPECT_EQ(bs.count(), MAX_BLOCKED_KEYS);

    uint8_t overflow[BLOCK_KEY_SIZE]; mkKey(overflow, 0xFF);
    EXPECT_FALSE(bs.add(overflow)); // full
    EXPECT_EQ(bs.count(), MAX_BLOCKED_KEYS);
}

TEST(BlockStore, RemoveMiddleKeepsOthers) {
    BlockStore bs;
    uint8_t a[BLOCK_KEY_SIZE], b[BLOCK_KEY_SIZE], c[BLOCK_KEY_SIZE];
    mkKey(a, 1); mkKey(b, 2); mkKey(c, 3);
    bs.add(a); bs.add(b); bs.add(c);
    EXPECT_TRUE(bs.remove(b));
    EXPECT_EQ(bs.count(), 2);
    EXPECT_TRUE(bs.contains(a));
    EXPECT_TRUE(bs.contains(c));
    EXPECT_FALSE(bs.contains(b));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f block`
Expected: FAIL — `helpers/BlockStore.h` not found / `BlockStore` undefined.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/helpers/BlockStore.h
#pragma once
#include <stdint.h>
#include <string.h>

#ifndef MAX_BLOCKED_KEYS
  #define MAX_BLOCKED_KEYS 32
#endif
#define BLOCK_KEY_SIZE 32   // == PUB_KEY_SIZE

// Pure in-memory list of blocked public keys (raw 32-byte prefixes of pub_key).
// No crypto/hardware deps so it is fully native-unit-testable. Persistence and
// Identity<->pubkey conversion live in the caller (MyMesh).
class BlockStore {
    uint8_t _keys[MAX_BLOCKED_KEYS][BLOCK_KEY_SIZE];
    uint8_t _count = 0;
public:
    uint8_t count() const { return _count; }
    const uint8_t* keyAt(uint8_t i) const { return _keys[i]; }

    bool contains(const uint8_t* pubkey) const {
        for (uint8_t i = 0; i < _count; i++)
            if (memcmp(_keys[i], pubkey, BLOCK_KEY_SIZE) == 0) return true;
        return false;
    }
    // true if present after the call (added or already there); false if full.
    bool add(const uint8_t* pubkey) {
        if (contains(pubkey)) return true;
        if (_count >= MAX_BLOCKED_KEYS) return false;
        memcpy(_keys[_count++], pubkey, BLOCK_KEY_SIZE);
        return true;
    }
    // true if a key was removed.
    bool remove(const uint8_t* pubkey) {
        for (uint8_t i = 0; i < _count; i++) {
            if (memcmp(_keys[i], pubkey, BLOCK_KEY_SIZE) == 0) {
                _count--;
                if (i != _count) memcpy(_keys[i], _keys[_count], BLOCK_KEY_SIZE); // swap-remove
                return true;
            }
        }
        return false;
    }
    void clear() { _count = 0; }
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f block`
Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add src/helpers/BlockStore.h test/block/test_block_store.cpp
git commit -m "feat(#241): BlockStore — pure native-tested pubkey block list"
```

---

## Task 2: Capability bit + version gate

**Files:**
- Modify: `examples/companion_radio/OffbandConfigProtocol.h` (near line 113, beside `OFFBAND_CAP_WIFI_OBSERVER`)
- Modify: `examples/companion_radio/MyMesh.cpp` (`FIRMWARE_VER_CODE` define; cap byte at ~line 1374)

- [ ] **Step 1: Add the cap constant + command codes**

In `OffbandConfigProtocol.h`, immediately after the `OFFBAND_CAP_WIFI_OBSERVER` line:

```cpp
constexpr uint8_t OFFBAND_CAP_BLOCK = 0x02;  // bit 1: user-block list (BlockStore) compiled in

// Block list sync (fork command 0xC2; companion-API only, never on the mesh).
constexpr uint8_t CMD_OFFBAND_BLOCK       = 0xC2;  // request:  cmd_frame[0]
constexpr uint8_t RESP_CODE_OFFBAND_BLOCK = 0xC2;  // response: out_frame[0]
constexpr uint8_t OFFBAND_BLOCK_ADD    = 0x01;
constexpr uint8_t OFFBAND_BLOCK_REMOVE = 0x02;
constexpr uint8_t OFFBAND_BLOCK_LIST   = 0x03;
constexpr uint8_t OFFBAND_BLOCK_CLEAR  = 0x04;
```

- [ ] **Step 2: Bump the version gate**

Find `#define FIRMWARE_VER_CODE` (used at `MyMesh.cpp:1354`; defined in `MyMesh.h`/`MyMesh.cpp`). Change its value `14` → `15`.

- [ ] **Step 3: Set the cap bit in the DEVICE_QUERY reply**

In `MyMesh.cpp`, in the `offband_caps` block (~line 1372), add after the `#ifdef OFFBAND_OBSERVER` block:

```cpp
    offband_caps |= offband::OFFBAND_CAP_BLOCK;   // block list always present on the companion
```

- [ ] **Step 4: Build-verify the companion compiles**

Run: `pio run -e heltec_v4_companion_radio_ble`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add examples/companion_radio/OffbandConfigProtocol.h examples/companion_radio/MyMesh.cpp examples/companion_radio/MyMesh.h
git commit -m "feat(#241): block capability bit 0x02 + ver-gate 15 + 0xC2 command codes"
```

---

## Task 3: `BlockStore` member + DM-drop hook + boot persistence

**Files:**
- Modify: `examples/companion_radio/MyMesh.h` (add member + load/save decls)
- Modify: `examples/companion_radio/MyMesh.cpp` (`queueMessage` drop; `loadBlocks`/`saveBlocks`; call `loadBlocks()` at startup)

- [ ] **Step 1: Add the member + includes (MyMesh.h)**

In `MyMesh.h`, add the include near the other helper includes:

```cpp
#include <helpers/BlockStore.h>
```

In the `MyMesh` class private section (beside other members):

```cpp
  BlockStore _blocks;
  void loadBlocks();
  void saveBlocks();
```

- [ ] **Step 2: Add the DM-drop check (the only enforcement)**

In `MyMesh.cpp`, at the **top** of `queueMessage` (line 494), as the first statement:

```cpp
void MyMesh::queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt,
                          uint32_t sender_timestamp, const uint8_t *extra, uint8_t extra_len, const char *text) {
  // #241: receive-side block. Drop DMs from a blocked pubkey before they reach the
  // app queue/tickle. Forwarding/relaying is untouched (contract §11) — this is the
  // app-push layer, after routing decisions.
  if (_blocks.contains(from.id.pub_key)) return;
  // ... existing body unchanged ...
```

- [ ] **Step 3: Implement persistence (mirror the savePrefs file pattern)**

Add to `MyMesh.cpp` (use the same `_fs`/FILESYSTEM the prefs/contacts use — confirm the exact handle by reading `CommonCLI::savePrefs` at `CommonCLI.cpp:214` and the contacts save in this file; replicate its `open(..., FILE_O_WRITE/READ)` + `write`/`read` calls):

```cpp
void MyMesh::saveBlocks() {
  auto f = _fs->open("/blocks", FILE_O_WRITE);
  if (!f) return;
  uint8_t n = _blocks.count();
  f.write(&n, 1);
  for (uint8_t i = 0; i < n; i++) f.write(_blocks.keyAt(i), BLOCK_KEY_SIZE);
  f.close();
}

void MyMesh::loadBlocks() {
  if (!_fs->exists("/blocks")) return;
  auto f = _fs->open("/blocks", FILE_O_READ);
  if (!f) return;
  uint8_t n = 0; f.read(&n, 1);
  uint8_t key[BLOCK_KEY_SIZE];
  for (uint8_t i = 0; i < n && i < MAX_BLOCKED_KEYS; i++) {
    if (f.read(key, BLOCK_KEY_SIZE) == BLOCK_KEY_SIZE) _blocks.add(key);
  }
  f.close();
}
```

> NOTE: `_fs` is the filesystem handle this class already uses for prefs/contacts. If the member/API differs, mirror exactly what `saveContacts`/`savePrefs` do in this file — do not invent a new filesystem path.

- [ ] **Step 4: Load at boot**

In `MyMesh`'s startup (where contacts/prefs are loaded — find the existing `loadContacts()`/prefs-load call site in `begin()`/`startInterface()`), add:

```cpp
  loadBlocks();
```

- [ ] **Step 5: Build-verify**

Run: `pio run -e heltec_v4_companion_radio_ble`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add examples/companion_radio/MyMesh.h examples/companion_radio/MyMesh.cpp
git commit -m "feat(#241): BlockStore member + DM-drop in queueMessage + file persistence"
```

---

## Task 4: `0xC2` command handler (ADD / REMOVE / LIST / CLEAR)

**Files:**
- Modify: `examples/companion_radio/MyMesh.cpp` (in `handleCmdFrame`, beside the other `0xC0/0xC1` fork handlers ~line 1352)

- [ ] **Step 1: Add the handler**

In `handleCmdFrame`, beside the `CMD_OFFBAND_GPS` (0xC1) handler, add:

```cpp
  // #241: block-list sync (companion-API only; never on the mesh). 0xC2.
  if (cmd_frame[0] == offband::CMD_OFFBAND_BLOCK && len >= 2) {
    uint8_t sub = cmd_frame[1];
    if (sub == offband::OFFBAND_BLOCK_ADD && len >= 2 + PUB_KEY_SIZE) {
      bool ok = _blocks.add(&cmd_frame[2]);
      if (ok) saveBlocks();
      out_frame[0] = RESP_CODE_OFFBAND_BLOCK; out_frame[1] = sub; out_frame[2] = ok ? 1 : 0;
      _serial->writeFrame(out_frame, 3);
    } else if (sub == offband::OFFBAND_BLOCK_REMOVE && len >= 2 + PUB_KEY_SIZE) {
      bool ok = _blocks.remove(&cmd_frame[2]);
      if (ok) saveBlocks();
      out_frame[0] = RESP_CODE_OFFBAND_BLOCK; out_frame[1] = sub; out_frame[2] = ok ? 1 : 0;
      _serial->writeFrame(out_frame, 3);
    } else if (sub == offband::OFFBAND_BLOCK_CLEAR) {
      _blocks.clear(); saveBlocks();
      out_frame[0] = RESP_CODE_OFFBAND_BLOCK; out_frame[1] = sub; out_frame[2] = 1;
      _serial->writeFrame(out_frame, 3);
    } else if (sub == offband::OFFBAND_BLOCK_LIST) {
      // dump: one frame START, then one frame per key, then END (mirrors the broker-pool dump)
      out_frame[0] = RESP_CODE_OFFBAND_BLOCK; out_frame[1] = sub; out_frame[2] = 0xFF; // START
      out_frame[3] = _blocks.count();
      _serial->writeFrame(out_frame, 4);
      for (uint8_t i = 0; i < _blocks.count(); i++) {
        out_frame[0] = RESP_CODE_OFFBAND_BLOCK; out_frame[1] = sub; out_frame[2] = i;
        memcpy(&out_frame[3], _blocks.keyAt(i), PUB_KEY_SIZE);
        _serial->writeFrame(out_frame, 3 + PUB_KEY_SIZE);
      }
      out_frame[0] = RESP_CODE_OFFBAND_BLOCK; out_frame[1] = sub; out_frame[2] = 0xFE; // END
      _serial->writeFrame(out_frame, 3);
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
    return;
  }
```

- [ ] **Step 2: Build-verify**

Run: `pio run -e heltec_v4_companion_radio_ble`
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add examples/companion_radio/MyMesh.cpp
git commit -m "feat(#241): 0xC2 block sync handler (ADD/REMOVE/LIST/CLEAR)"
```

---

## Task 5: Full matrix build-verify + native test gate

**Files:** none (verification only)

- [ ] **Step 1: Native unit tests pass**

Run: `pio test -e native`
Expected: PASS (incl. the 3 BlockStore tests).

- [ ] **Step 2: Representative device builds (ESP32 + nRF52 + observer)**

Run: `pio run -e heltec_v4_companion_radio_ble -e RAK_4631_companion_radio_usb -e Heltec_v3_companion_observer_wifi`
Expected: all SUCCESS (the V3 observer guards the GPS `setTxTimeoutMs` already; block adds no HWCDC dep).

- [ ] **Step 3: Confirm the interop invariant by inspection**

Grep the diff for any change to forwarding/relay/advert/channel paths — there should be **none**. The only behavioral change is the `queueMessage` early-return.

Run: `git diff origin/firmware-base -- src examples | grep -nE 'sendFlood|routeDirect|onAdvertRecv|allowPacketForward|onGroupData|forward' || echo "no forwarding-path changes (invariant holds)"`
Expected: `no forwarding-path changes (invariant holds)`.

- [ ] **Step 4: Commit (if any verification-driven fixups)** — otherwise skip.

---

## On-device verification (after merge, with the app or a crafted 0xC2)
- Flash a companion; from the app (or a crafted `0xC2 ADD <pubkey>`) block a test contact's pubkey; confirm their DMs no longer surface, the OLED/relay behaviour is unchanged, and `0xC2 LIST` round-trips the key. Power-cycle → list persists (file). These are integration checks; the logic core is covered by the native tests.

---

## Self-Review

**Spec coverage** (contract → task):
- §2 capability/ver-gate → Task 2. §3 store → Task 1 + Task 3 persistence. §4 `0xC2` sync → Task 4. §5 DM-drop / adverts-untouched → Task 3 (drop) + (no advert code = compliant). §6/§7 channel/self-heal → **app-side, out of firmware scope** (correctly no firmware task). §11 interop invariant → Task 5 Step 3 inspection.
- Gaps: none for the firmware half. Channel/name-index/age-out are app responsibilities (contract §6/§8), explicitly out of scope here.

**Placeholder scan:** the one soft reference is the exact `_fs` filesystem API in Task 3 — flagged with a NOTE to mirror `savePrefs`/`saveContacts` in the same file rather than invent calls. All logic + tests are concrete.

**Type consistency:** `BlockStore::add/remove/contains(const uint8_t*)`, `keyAt`, `count`, `BLOCK_KEY_SIZE`, `MAX_BLOCKED_KEYS`, `OFFBAND_CAP_BLOCK`, `CMD_OFFBAND_BLOCK`, `OFFBAND_BLOCK_*` are used consistently across Tasks 1–4.
