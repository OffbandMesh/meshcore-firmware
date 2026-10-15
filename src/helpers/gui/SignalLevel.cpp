#include "SignalLevel.h"

namespace offband {
namespace gui {

float demodFloorDb(uint8_t sf) {
  (void)sf;
  return 0.0f;
}

uint8_t signalBars(float snr_db, uint8_t sf) {
  (void)snr_db;
  (void)sf;
  return 0;
}

SignalRole signalRole(uint8_t bars) {
  (void)bars;
  return SignalRole::Bad;
}

}  // namespace gui
}  // namespace offband
