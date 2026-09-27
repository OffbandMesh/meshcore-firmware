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

// #1254: where 100% actually is, learned rather than encoded. A cell's full voltage
// varies by cell, by age and by the board's divider, so a constant is wrong somewhere by
// construction -- on the owner's badge the compiled 4150 sat above what his cell reads
// full, and the bar stopped at 98%.
//
// The badge has two signals and no current sense: VBUS (NRF52Board::isExternalPowered)
// and its own smoothed reading. The rig capture of 2026-09-14..17 says what those are
// worth, and it refutes the voltage gate this file used to carry:
//
//   * A full cell reads anywhere from 4071 to 4129 mV on this badge, depending only on
//     how long ago the charger last ran. A fixed "charged above X" gate therefore tests a
//     number that moves further than the gap it is testing for. At 4100 it rejected every
//     unplug the owner made on 2026-09-17, on a cell he had just charged.
//   * While the charger is working the cell climbs 11 to 27 mV every half hour (133
//     windows across a 96-minute charge, about 0.7 mV a minute). While it sits on USB
//     with the charger done it moves at most 5 mV in a half hour -- 8,119 windows across
//     five stretches, one of them 28 hours long. The two populations do not overlap.
//     Measured as the same statistic this code computes: the reading now minus the
//     reading half an hour earlier, taken at every sample, not a peak-to-peak spread.
//   * On battery the reading only falls from the moment of unplug: in both discharge runs
//     the highest cell reading is the first sample after it.
//
// So the badge does not ask "is this voltage high enough", which it cannot answer. It
// asks "has the charger stopped adding to this cell", which the voltage does answer:
//
//   1. on external power, keep a reading a minute for the last half hour and compare the
//      oldest with the newest -- a ROLLING half hour, so the answer is current at the
//      moment of the unplug rather than at the last window boundary;
//   2. a rise of no more than kFlatRiseMv says the charger is done with this cell;
//   3. on unplug, if that half hour says done, watch the next kWatchMs;
//   4. skip the first kSettleMs and kSettleReadings of it, because the average still
//      carries the reading from USB, then take the HIGHEST reading as this cell's 100%.
//
// The maximum, not a timed sample: a load only ever pushes the reading down -- the rig
// saw the load-side voltage swing 90 mV between a 12 mA and a 150 mA moment in the five
// minutes after an unplug -- so the maximum is the closest thing to a rested value
// obtainable without measuring current.
//
// The maximum is taken WITHIN one window, not across the badge's life: every unplug that
// qualifies replaces the stored value, lower as readily as higher. So a cell whose full
// voltage sags with age is followed down, one charge cycle at a time. The calibrate row
// is for the badge whose charger never settles -- one left on a bench supply, or unplugged
// before a half-hour window ever closes -- not for ageing.
class FullPointLearner {
public:
  // Whether a value is a plausible full point for a single lithium cell. The learner
  // applies this to what it learns; the calibrate action applies it to what it is given,
  // so a mis-timed press cannot pin a number that makes the bar meaningless.
  static bool plausibleFullMv(uint16_t mv);

  // Nothing outside this is a single lithium cell at full charge. A sanity band, not a
  // decision: a full cell reads 4071..4129 mV on the bench badge, and this only has to
  // exclude nonsense.
  static const uint16_t kFloorMv = 3900, kCeilMv = 4250;
  // The span the capture measured the two populations over, and the line between them.
  // Charging: +11 to +27 mV per half hour. Sitting on USB with the charger done: at most
  // +5 mV. Eight is between them, and both ends of the comparison are averaged readings,
  // so noise arrives divided by about the square root of the average's depth.
  //
  // The half hour ROLLS: one sample a minute, oldest against newest. A window that only
  // closed on the half hour would miss a charge that finished five minutes into it -- the
  // owner would unplug a settled badge and be told the charger never settled.
  static const uint8_t kHistorySlots = 31;                    // 31 samples span 30 minutes
  static const uint32_t kSlotMs = 60UL * 1000UL;
  static const int16_t kFlatRiseMv = 8;
  // BatteryAverage closes a sixteenth of the gap per reading, so after kSettleReadings a
  // twentieth of the step is left -- under 2 mV of the 40 mV that unplugging costs. Both
  // the time and the reading count have to pass: the time alone would let a loop that
  // fell behind learn the charger's voltage, and the count alone would let a fast loop
  // learn from a second and a half of readings.
  static const uint32_t kSettleMs = 60UL * 1000UL;
  static const uint8_t kSettleReadings = 48;
  // How long after unplugging to keep watching for the high-water mark.
  static const uint32_t kWatchMs = 5UL * 60UL * 1000UL;

  // What the last feed() decided, so a caller can log it. A learn that never happens
  // must name the step it stopped at -- on the bench one did, with nothing in the log to
  // say whether USB was never seen, the charge was still running, or the window refused.
  enum Event : uint8_t {
    kNone,              // nothing changed
    kPluggedIn,         // external power appeared
    kStillCharging,     // a window closed with the cell still climbing
    kChargerDone,       // a window closed flat: the charger has stopped adding
    kWatching,          // unplugged after a flat window: watching for the rested value
    kNotLearning,       // unplugged without one
    kLearned,           // the window closed on a plausible value (feed returned it)
    kImplausible,       // the window closed outside kFloorMv..kCeilMv
  };
  Event event() const { return _event; }
  uint16_t chargeMv() const { return _charge_mv; }   // last reading while external
  uint16_t bestMv() const { return _best_mv; }       // highest in the current window
  int16_t riseMv() const { return _rise_mv; }        // rise across the rolling half hour
  bool chargerDone() const { return _flat; }         // only ever true on a full half hour

  // Returns a learned full point once, on the reading that completes a window; 0 means
  // nothing to store. `mv` should be the smoothed reading, not a raw ADC sample.
  uint16_t feed(uint32_t now_ms, uint16_t mv, bool external) {
    _event = kNone;
    return external ? onExternal(now_ms, mv) : onBattery(now_ms, mv);
  }

  void reset() {
    _was_external = false; _watching = false; _flat = false; _judged = false;
    _charge_mv = 0; _best_mv = 0;
    _started_ms = 0; _slot_ms = 0;
    _rise_mv = 0; _slots = 0; _write_at = 0; _readings = 0;
    _event = kNone;
  }

private:
  uint16_t onExternal(uint32_t now_ms, uint16_t mv) {
    if (!_was_external) {                  // a fresh plug-in proves nothing yet
      _was_external = true;
      _flat = false; _judged = false; _rise_mv = 0;
      _slots = 1; _write_at = 1;
      _slot_ms = now_ms;
      _history[0] = mv;
      _event = kPluggedIn;
    } else if ((uint32_t)(now_ms - _slot_ms) >= kSlotMs) {
      _slot_ms = now_ms;
      // One write head. Once the ring is full it points at the oldest sample, which is the
      // one this write replaces -- so after the write it points at the new oldest.
      _history[_write_at] = mv;
      _write_at = (uint8_t)((_write_at + 1) % kHistorySlots);
      if (_slots < kHistorySlots) _slots++;
      if (_slots == kHistorySlots) {       // a whole half hour to compare across
        _rise_mv = (int16_t)((int32_t)mv - (int32_t)_history[_write_at]);
        const bool flat = _rise_mv <= kFlatRiseMv;
        if (!_judged || flat != _flat) _event = flat ? kChargerDone : kStillCharging;
        _flat = flat;
        _judged = true;
      }
    }
    _charge_mv = mv;                       // the last thing seen with the charger attached
    _watching = false;
    return 0;
  }

  uint16_t onBattery(uint32_t now_ms, uint16_t mv) {
    if (_was_external) {                   // just unplugged: decide whether this counted
      _was_external = false;
      _watching = _flat;                   // flat means the rolling half hour agrees NOW
      _started_ms = now_ms;
      _best_mv = 0;
      _readings = 0;
      _event = _watching ? kWatching : kNotLearning;
      if (!_watching) return 0;
    }
    if (!_watching) return 0;
    if (_readings < 255) _readings++;
    const uint32_t since = (uint32_t)(now_ms - _started_ms);
    if (since >= kSettleMs && _readings >= kSettleReadings && mv > _best_mv) _best_mv = mv;
    if (since < kWatchMs) return 0;
    _watching = false;
    // _best_mv is 0 when the average never settled inside the window -- a loop running far
    // below a reading a second. That is implausible, and the log says so rather than
    // leaving the badge watching for a window that will never close.
    if (plausibleFullMv(_best_mv)) {
      _event = kLearned;
      return _best_mv;
    }
    _event = kImplausible;
    return 0;
  }

  uint16_t _history[kHistorySlots];        // one reading a minute while on external power
  uint32_t _started_ms = 0, _slot_ms = 0;
  uint16_t _charge_mv = 0, _best_mv = 0;
  int16_t _rise_mv = 0;
  uint8_t _slots = 0, _write_at = 0, _readings = 0;
  bool _was_external = false, _watching = false, _flat = false, _judged = false;
  Event _event = kNone;
};

inline bool FullPointLearner::plausibleFullMv(uint16_t mv) {
  return mv >= kFloorMv && mv <= kCeilMv;
}

// #1254: where the full point in use came from, for the Battery screen to show. `stored`
// is the preference, 0 when nothing has been learned or pinned.
inline const char* fullPointSourceName(uint16_t stored, bool user_set) {
  if (stored == 0) return "default";   // the board's compiled value
  return user_set ? "set" : "learned";
}

}  // namespace offband
