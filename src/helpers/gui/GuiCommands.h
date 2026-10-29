#pragma once

// The command API's results and argument checks (#1453, PLAN.md §3 B2).
// MyMesh's cmd* methods return these; the phone frame handlers turn them
// into their existing reply codes, and the on-device GUI shows them.
// Inline so every companion build has them, GUI or not.

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

enum class CmdResult : uint8_t {
  Ok,
  BadArgs,   // out of range or malformed; nothing changed
  NotFound,  // no such contact or channel
  Busy,      // the send queue or the request slot is full
  Failed,    // the radio or the store refused
  Occupied,  // a different channel holds that slot; nothing changed (#1865)
};

// Radio settings as the phone sends them: frequency in kHz, bandwidth in Hz,
// spreading factor, coding rate (4/5..4/8 as 5..8). The ranges are the ones
// CMD_SET_RADIO_PARAMS has always enforced.
inline CmdResult checkRadioParams(uint32_t freq_khz, uint32_t bw_hz, uint8_t sf, uint8_t cr) {
  const bool ok = freq_khz >= 150000 && freq_khz <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 &&
                  cr <= 8 && bw_hz >= 7000 && bw_hz <= 500000;
  return ok ? CmdResult::Ok : CmdResult::BadArgs;
}

// TX power in dBm, -9 up to the board's maximum.
inline CmdResult checkTxPower(int8_t dbm, int8_t max_dbm) {
  return (dbm >= -9 && dbm <= max_dbm) ? CmdResult::Ok : CmdResult::BadArgs;
}

// Message text: 1..max_len bytes (160 for the GUI, D5 decision 11). The GUI
// checks this before sending; the phone path keeps its own framing limits.
inline CmdResult checkMessageText(size_t len, size_t max_len) {
  return (len >= 1 && len <= max_len) ? CmdResult::Ok : CmdResult::BadArgs;
}

}  // namespace gui
}  // namespace offband
