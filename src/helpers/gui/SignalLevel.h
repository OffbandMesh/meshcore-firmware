#pragma once

// LoRa signal bars for the GUI status bar, node rows and node detail
// (design spec v0.3 §"Signal level", D1 #1323 §1.5).
//
// Bars measure margin, not raw SNR: the same SNR is comfortable at SF7 and
// marginal at SF12, because the demodulator floor drops 2.5 dB per spreading
// factor. Arduino-free so the native tests cover it.

#include <stdint.h>

namespace offband {
namespace gui {

// SX1262 demodulation SNR floor for a spreading factor, in dB.
// Out-of-range SF returns the SF12 floor (the most permissive), so a bad
// value never shows a stronger signal than the radio can actually decode.
float demodFloorDb(uint8_t sf);

// 0..4 bars from the margin over the floor:
// >= 12 dB -> 4, >= 7 -> 3, >= 3 -> 2, >= 0 -> 1, < 0 -> 0.
uint8_t signalBars(float snr_db, uint8_t sf);

// Color role for a bar count: 3-4 ok, 2 warn, 0-1 bad.
enum class SignalRole : uint8_t { Bad, Warn, Ok };
SignalRole signalRole(uint8_t bars);

}  // namespace gui
}  // namespace offband
