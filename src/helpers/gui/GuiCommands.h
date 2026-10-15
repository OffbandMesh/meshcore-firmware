#pragma once

// The GUI's command API results and argument checks (#1453, PLAN.md §3 B2).
// The phone frame handlers and the GUI call the same MyMesh methods; the
// checks live here, Arduino-free, so both paths reject the same input.

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
};

// Radio settings as the phone sends them: frequency in kHz, bandwidth in Hz,
// spreading factor, coding rate (4/5..4/8 as 5..8). The ranges are the ones
// CMD_SET_RADIO_PARAMS has always enforced.
CmdResult checkRadioParams(uint32_t freq_khz, uint32_t bw_hz, uint8_t sf, uint8_t cr);

// TX power in dBm, -9 up to the board's maximum.
CmdResult checkTxPower(int8_t dbm, int8_t max_dbm);

// Message text: 1..max_len bytes (160 for the GUI, D5 decision 11).
CmdResult checkMessageText(size_t len, size_t max_len);

}  // namespace gui
}  // namespace offband
