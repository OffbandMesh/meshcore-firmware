#include "SignalLevel.h"

namespace offband {
namespace gui {

float demodFloorDb(uint8_t sf) {
  if (sf < 5 || sf > 12) sf = 12;
  // SX1262 demodulation SNR limits: -2.5 dB at SF5, 2.5 dB lower per step.
  return -2.5f * float(sf - 4);
}

uint8_t signalBars(float snr_db, uint8_t sf) {
  const float margin = snr_db - demodFloorDb(sf);
  if (margin >= 12.0f) return 4;
  if (margin >= 7.0f) return 3;
  if (margin >= 3.0f) return 2;
  if (margin >= 0.0f) return 1;
  return 0;
}

SignalRole signalRole(uint8_t bars) {
  if (bars >= 3) return SignalRole::Ok;
  if (bars == 2) return SignalRole::Warn;
  return SignalRole::Bad;
}

}  // namespace gui
}  // namespace offband
