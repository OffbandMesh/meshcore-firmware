#pragma once

// The SD card for the companion GUI, one loop tick at a time (Epic I #1433:
// I7 #1703 hot-swap, I8 #1704 copy and resync). Owner, 2026-10-21, option B:
// the live store and its flash files carry on as before; the card's archive
// catches up with them whenever a card is working.
//
//   SdMonitor  when to probe or mount (no card-detect line on the R8 kit)
//   SdCard     the card on the panel's SPI bus
//   SdArchive  the history on the card
//
// A card that errors mid-write is treated as pulled: unmounted, shown red,
// and caught up again when it next mounts, with nothing lost from the live
// store.

#include <stdint.h>

#include <helpers/gui/MsgStore.h>
#include <helpers/gui/SdArchive.h>
#include <helpers/gui/SdMonitor.h>

#include "SdCard.h"

class GuiSd {
public:
  // `owner` names this radio (its public key's first bytes), so a card from
  // another radio is set aside rather than mixed into this one's history.
  void begin(uint32_t owner) {
    _owner = owner;
    _begun = true;
  }
  // A card has worked in this radio before (GuiPrefs::sd_used): an empty
  // slot then shows red from boot. Call before the first loop.
  void remember(bool card_used) { _mon.remember(card_used); }

  void loop(uint32_t now_ms, offband::gui::MsgStore& live);

  // A message's delivery state or echo count changed: rewrite it on the card.
  void noteChanged(uint32_t seq);

  offband::gui::SdState state() const { return _mon.state(); }
  offband::gui::SdIcon icon() const { return _mon.icon(); }
  // Bumped whenever anything a screen shows changes.
  uint32_t revision() const { return _mon.revision() + _rev; }

  // The first copy, or a catch-up after the card was out.
  bool copying() const { return _copy_total > 0; }
  uint16_t copyDone() const { return _copy_done; }
  uint16_t copyTotal() const { return _copy_total; }
  uint32_t copyStartedMs() const { return _copy_started; }

  bool usage(uint64_t* total, uint64_t* free_bytes) { return _card.usage(total, free_bytes); }
  bool describe(uint64_t* capacity, const char** fs_type) { return _card.describe(capacity, fs_type); }
  uint32_t messagesOnCard() const;
  // Card out: live messages that will be copied when it's back ("Waiting to
  // copy"). 0 if no card has been in since boot.
  uint16_t waiting(const offband::gui::MsgStore& live) const;

  // Settings -> SD card -> Format card. Erases the card and starts the
  // archive again from the live store; flash is untouched.
  bool format(uint32_t now_ms, offband::gui::MsgStore& live);

private:
  bool openArchive(offband::gui::MsgStore& live);
  void closeArchive();
  void cardFailed(uint32_t now_ms);

  offband::gui::SdMonitor _mon;
  SdCard _card;
  offband::gui::SdArchive _arch;
  SdFile _rec, _thr;
  uint32_t _owner = 0;
  bool _begun = false;
  uint32_t _rev = 0;
  uint32_t _last_through = 0;   // the card's cursor when it was last open
  bool _had_card = false;
  uint16_t _copy_total = 0;
  uint16_t _copy_done = 0;
  uint32_t _copy_started = 0;
  // Bench timing (owner, 2026-10-21): the first ask, slow ones, the slowest slice.
  static constexpr uint32_t kSlowMs = 20;
  bool _logged_ask = false;
  uint32_t _slice_max = 0;
  // Changed seqs waiting to be rewritten on the card; a full ring drops the
  // oldest, which only costs a stale delivery mark in the archive.
  static constexpr uint8_t kChangedMax = 16;
  uint32_t _changed[kChangedMax] = {};
  uint8_t _changed_n = 0;
};
