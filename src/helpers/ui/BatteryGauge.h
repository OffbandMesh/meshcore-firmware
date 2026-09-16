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
// #1252: the map follows a measured lithium discharge, not a straight line in volts. A
// cell is not linear -- most of its charge sits in a narrow band in the middle -- and a
// straight line under-read the middle by about ten points, so the badge outlasted its own
// bar the whole way down.
//
// The shape is measured, not guessed: the RC52 burn from #1004, 24,216 paired samples at
// 30 s over 202 h, with charge integrated off an INA228 current sense to genuine cell
// exhaustion at 3563 mV -- 2483 mAh. Charge is measured rather than inferred from
// elapsed time, which is what makes it a state-of-charge curve and not a runtime plot.
//
// It is the RC52 run and not the RC32 one (#833) for two reasons: RC32 logged voltage
// against time only, so using it assumes a constant current; and the shape moves with
// load. Under RC32's 116 mA the terminal voltage sags, so at 3700 mV it still held 56%
// where RC52 at 12 mA held 41%. The badge is a light-drain nRF52 like RC52.
//
// Capacity does not enter into it: a 2000 mAh cell and a 3000 mAh cell of the same
// chemistry sit at the same voltage at the same fraction full, and capacity only sets how
// long each percent lasts. So one curve covers every cell and nothing needs measuring per
// device -- which is the point, because testing every cell against every board never ends.
//
// What DOES move the curve, stated so nobody reads more into it than is there: the load,
// strongly (see the RC32 comparison above), and with it the cell's internal resistance,
// its age and its temperature. This is one cell, at one age, at one temperature, at a
// drain close to the badge's. The shape is what transfers; the exact millivolts drift.
// That is still far better than the straight line it replaces, which was out by ten
// points in the middle -- more than any of those effects.
//
// One property worth knowing: a curve is steep at the knee, so a few millivolts of wobble
// move the number more there than they do up top -- about 2.8 mV per point around
// 3600-3650 against 6.5 mV on the old line. The averaging in BatteryAverage holds the
// residual to a couple of millivolts, so it stays under a point, but the bar is livelier
// near empty than it is near full. That is the cell's behavior, not an artefact.
namespace offband {

// Cell millivolts against charge remaining in tenths of a percent, from the #1004 burn.
// Must stay descending in both columns: the interpolation below relies on it. Spacing
// tightens to 25 mV from 3700 down, where the curve turns hardest and a 50 mV step was
// straightening away most of the knee.
struct CurvePoint { uint16_t mv; uint16_t permille; };
static const CurvePoint kLithiumCurve[] = {
  {4150, 993}, {4125, 976}, {4100, 953}, {4050, 901}, {4000, 846}, {3950, 783},
  {3900, 721}, {3850, 652}, {3800, 567}, {3750, 482}, {3700, 414}, {3675, 382},
  {3650, 329}, {3625, 253}, {3600, 148}, {3575, 49},  {3550, 6},
};
constexpr int kLithiumCurvePoints = 17;

// Charge remaining at `mv`, in tenths of a percent, off the measured curve. Beyond either
// end it holds at that end -- the curve says nothing about voltages it never saw.
inline uint16_t chargeAt(uint16_t mv) {
  if (mv >= kLithiumCurve[0].mv) return kLithiumCurve[0].permille;
  const CurvePoint& last = kLithiumCurve[kLithiumCurvePoints - 1];
  if (mv <= last.mv) return last.permille;
  for (int i = 1; i < kLithiumCurvePoints; i++) {
    const CurvePoint& lo = kLithiumCurve[i];
    if (mv >= lo.mv) {
      const CurvePoint& hi = kLithiumCurve[i - 1];
      const uint32_t span_mv = (uint32_t)(hi.mv - lo.mv);
      const uint32_t up = (uint32_t)(hi.permille - lo.permille);
      return (uint16_t)(lo.permille + ((uint32_t)(mv - lo.mv) * up + span_mv / 2) / span_mv);
    }
  }
  return last.permille;   // unreachable: the clamp above covers everything at or below
                          // the last point, so the loop always returns. Here for the
                          // compiler, not for a case.
}

// The straight line the curve replaces. Still the answer where the curve has nothing to
// say about a board's particular ends.
inline int linearPercent(uint16_t mv, uint16_t empty_mv, uint16_t full_mv) {
  const uint32_t span = (uint32_t)(full_mv - empty_mv);
  return (int)(((uint32_t)(mv - empty_mv) * 100UL + span / 2) / span);
}

// 0 at `empty_mv` or below, 100 at `full_mv` or above, and the measured curve in between,
// stretched so that each board's own ends still read exactly 0 and 100. The curve gives
// the shape; the board gives where the shape starts and stops.
inline int batteryPercent(uint16_t mv, uint16_t empty_mv, uint16_t full_mv) {
  if (full_mv <= empty_mv) return 0;
  if (mv <= empty_mv) return 0;
  if (mv >= full_mv) return 100;
  const uint16_t at_empty = chargeAt(empty_mv);
  const uint16_t at_full = chargeAt(full_mv);
  // Both ends landed on the same clamp, so the curve cannot tell them apart -- a board
  // whose whole range sits outside what the burn covered. Fall back to the straight line
  // rather than drawing an empty bar across everything that board can do.
  if (at_full <= at_empty) return linearPercent(mv, empty_mv, full_mv);
  const uint32_t here = (uint32_t)chargeAt(mv);
  if (here <= at_empty) return 0;
  const uint32_t span = (uint32_t)(at_full - at_empty);
  const uint32_t pct = ((here - at_empty) * 100UL + span / 2) / span;
  return pct > 100UL ? 100 : (int)pct;   // monotonic, so unreachable; kept as a floor
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
