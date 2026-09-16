#pragma once

#include <stdint.h>

// Offband (#1246): the battery as a percentage, and an average to read it through.
//
// The owner, on the bench: the badge bounces between 97% and 100% at full, and never
// shows 100%. Both came out of one piece of arithmetic. The scale ran 3000 to 4200 mV,
// so 100% needed the cell to sit at exactly 4.200 V -- a charger's peak, not a cell,
// since a charged Li-ion settles to about 4.15-4.18 as soon as the charge terminates.
// And the number was taken from whatever the ADC said at that instant, on a divider with
// about 405k of source impedance, while a 22 dBm transmit sags the supply.
//
// So: endpoints a real cell reaches, and an average across time. Free of Arduino, so
// both are unit-tested.
//
// The map is linear in volts, which a lithium cell is not: the middle of its discharge
// is flat and the last stretch falls away. Straightening that needs a measured discharge
// for this cell, the way #833 measured one for another board -- a guessed curve would be
// a worse lie than an honest straight line.
namespace offband {

// 0 at `empty_mv` or below, 100 at `full_mv` or above, rounded in between. A range that
// is not a range reads as empty rather than dividing by zero.
inline int batteryPercent(uint16_t mv, uint16_t empty_mv, uint16_t full_mv) {
  if (full_mv <= empty_mv) return 0;
  if (mv <= empty_mv) return 0;
  if (mv >= full_mv) return 100;
  const uint32_t span = (uint32_t)(full_mv - empty_mv);
  return (int)(((uint32_t)(mv - empty_mv) * 100UL + span / 2) / span);
}

// An exponential moving average of the reading, in millivolts. The accumulator holds
// kWeight times the average, so the integer division cannot eat a slow drift the way
// `avg += (mv - avg) / N` does once the difference falls under N.
//
// At one reading a second, kWeight 16 settles in about a quarter of a minute and cuts
// the spread of what it is fed to roughly a fifth -- enough that a transmit, or a noisy
// sample off a high-impedance divider, cannot move the number on its own. A battery
// that is genuinely draining moves far slower than that, so nothing real is hidden.
class BatteryAverage {
public:
  // The first reading is taken whole: the bar is right at boot rather than climbing to
  // the truth over the first quarter minute.
  //
  // A reading far below the average is a load sagging the supply -- a 22 dBm transmit
  // pulls over a hundred milliamps through a cell -- and not the charge falling that
  // fast. Averaging one in still moved the shown percentage, so a dip is ignored until
  // it keeps happening: a genuine collapse is believed within a few seconds, and the
  // low-battery shutdown reads the raw voltage anyway, so nothing here can delay it.
  void feed(uint16_t mv) {
    if (!_seeded) {
      _acc = (uint32_t)mv * kWeight;
      _seeded = true;
      return;
    }
    const int32_t diff = (int32_t)mv - (int32_t)value();
    if (diff < -(int32_t)kDipMv) {
      if (++_dips < kDipsBelieved) return;
    } else {
      _dips = 0;
      // A large rise is taken whole rather than crawled to. A charger really did arrive;
      // and if the first reading happened to land during a transmit, the average started
      // that far low and would otherwise take a quarter of a minute to climb out of it,
      // showing a false low battery for the whole of it.
      if (diff > (int32_t)kDipMv) {
        _acc = (uint32_t)mv * kWeight;
        return;
      }
    }
    _acc = _acc - (_acc / kWeight) + mv;
  }

  uint16_t value() const {
    if (!_seeded) return 0;
    const uint32_t v = (_acc + kWeight / 2) / kWeight;
    return v > 65535UL ? (uint16_t)65535 : (uint16_t)v;
  }

  bool seeded() const { return _seeded; }
  void reset() { _acc = 0; _seeded = false; _dips = 0; }

  static const uint32_t kWeight = 16;
  // How far below the average a reading has to be to look like a load rather than the
  // cell, and how many in a row before it is believed anyway. 60 mV in one second is
  // far faster than this cell discharges; three in a row is not a transmit.
  static const uint16_t kDipMv = 60;
  static const uint8_t kDipsBelieved = 3;

private:
  uint32_t _acc = 0;
  uint8_t _dips = 0;
  bool _seeded = false;
};

}  // namespace offband
