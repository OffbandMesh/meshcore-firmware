#!/usr/bin/env python3
"""Estimate RC32 battery runtime from a captured [pwr] telemetry log (#766 / Q01).

WHY THIS EXISTS
---------------
A 2500 mAh cell on this board plausibly runs 30-60 hours. Waiting for the board to
die is the most accurate measurement and also the least useful one, because it can
outlast the window in which the answer is needed. This projects the endpoint from
the part of the curve we already have.

WHAT IT WILL NOT DO
-------------------
Extrapolate a straight line from early data. A freshly charged LiPo sheds its
surface charge quickly under first load -- the RC32 dropped 19 mV in the first 90
seconds, which linearly extrapolated predicts death within the hour. It then
settles onto a long, nearly flat plateau where the true slope is an order of
magnitude smaller. Any estimate that ignores this is not conservative, it is
simply wrong, so the script discards a leading warm-up window by default and says
so in its output.

METHOD
------
Two independent estimates, deliberately, because they fail in different ways:

  1. SLOPE PROJECTION -- least-squares fit of mV vs time over a recent window,
     extrapolated to the cutoff. Honest about the near term; degrades in the
     plateau where dV/dt is small and noise dominates.

  2. CHARGE ACCOUNTING -- map voltage to state-of-charge through a nominal LiPo
     curve, measure how much SoC was consumed over the observed interval, and
     scale by the rated capacity to get average current. Runtime then follows
     from the charge remaining above the cutoff. Insensitive to plateau
     flatness, but only as good as the SoC table.

Agreement between the two is the confidence signal. Divergence means the cell is
somewhere the nominal table describes badly, and the answer should be reported as
a range rather than a number.

The SoC table is NOMINAL for a generic single-cell LiPo under light load. It is
not calibrated against this cell, and it is the largest error term here -- treat
charge-accounting output as an estimate with maybe +/-20% on it, not a
measurement. The only calibrated number in the whole pipeline is elapsed time.
"""

import argparse
import re
import sys
from datetime import datetime, timezone

# Nominal open-circuit-ish LiPo discharge curve, light load: (volts, charge level 0..1).
# NOTE: "SoC" is avoided in this file. In a firmware repo it reads as System-on-Chip
# (the ESP32-S3 is one), and this is State-of-Charge -- an unhelpful collision.
# Monotonic in both columns; interpolated linearly between rows. The steep regions at
# each end and the flat middle are the whole point -- a single linear ratio would
# misestimate both.
SOC_TABLE = [
    (4.20, 1.00), (4.15, 0.95), (4.11, 0.90), (4.08, 0.85), (4.02, 0.80),
    (3.98, 0.75), (3.95, 0.70), (3.91, 0.65), (3.87, 0.60), (3.85, 0.55),
    (3.84, 0.50), (3.82, 0.45), (3.80, 0.40), (3.79, 0.35), (3.77, 0.30),
    (3.75, 0.25), (3.73, 0.20), (3.71, 0.15), (3.69, 0.10), (3.61, 0.05),
    (3.40, 0.00),
]

LINE_RE = re.compile(r"^\[(?P<ts>[0-9T:.\-]+)Z\]\s+\[(?P<ms>\d+)\]\s+\[pwr\]\s+mv=(?P<mv>\d+)\s+up=(?P<up>\d+)s")


def raw_soc(volts):
    """Table charge level (0..1) for a cell voltage, by linear interpolation."""
    if volts >= SOC_TABLE[0][0]:
        return 1.0
    if volts <= SOC_TABLE[-1][0]:
        return 0.0
    for (v_hi, s_hi), (v_lo, s_lo) in zip(SOC_TABLE, SOC_TABLE[1:]):
        if v_lo <= volts <= v_hi:
            span = v_hi - v_lo
            if span <= 0:
                return s_lo
            return s_lo + (volts - v_lo) / span * (s_hi - s_lo)
    return 0.0


def make_soc(full_mv):
    """Charge level anchored so that `full_mv` reads as 100%.

    WHY THIS EXISTS. The raw table tops out at 4.20 V, but a real pack rarely
    presents that to the ADC at the start of a run. Chargers terminate at 4.20 V
    and the cell then relaxes; the reading is taken under load, so it sags further;
    and the divider itself carries the tolerance of R36/R38 plus the ADC.

    On this run the owner confirmed the pack was **fully charged** at 4130 mV. The
    unanchored table called that 85%, i.e. it opened the accounting already claiming
    ~375 mAh had been consumed before the board drew anything. That error propagates
    straight into the current estimate -- it inflates mAh-consumed, and therefore
    inflates average draw, and therefore shortens the projected runtime. It was a
    large part of why the two methods disagreed.

    Anchoring renormalises the curve so the observed full-charge voltage is 100% and
    the cutoff stays 0%, preserving the table's SHAPE (which is the part worth having)
    while discarding its assumption about where full sits. Since the table defines
    3.40 V as 0, that reduces to dividing through by the table value at `full_mv`.

    This is still a nominal curve with a measured anchor, not a calibrated cell.
    """
    denom = raw_soc(full_mv)
    if denom <= 0:
        return raw_soc
    return lambda v: min(1.0, raw_soc(v) / denom)


def parse(path):
    """Yield (datetime, uptime_seconds, millivolts) for each [pwr] line."""
    out = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = LINE_RE.match(line.strip())
            if not m:
                continue
            ts = datetime.fromisoformat(m.group("ts")).replace(tzinfo=timezone.utc)
            out.append((ts, int(m.group("up")), int(m.group("mv"))))
    return out


def fit(points):
    """Least-squares slope/intercept of mv vs hours. Returns (slope, intercept)."""
    n = len(points)
    if n < 2:
        return 0.0, points[0][1] if points else 0.0
    sx = sum(p[0] for p in points)
    sy = sum(p[1] for p in points)
    sxx = sum(p[0] * p[0] for p in points)
    sxy = sum(p[0] * p[1] for p in points)
    denom = n * sxx - sx * sx
    if abs(denom) < 1e-12:
        return 0.0, sy / n
    slope = (n * sxy - sx * sy) / denom
    return slope, (sy - slope * sx) / n


def hms(hours):
    if hours != hours or hours in (float("inf"), float("-inf")) or hours < 0:
        return "n/a"
    h = int(hours)
    return f"{h}h {int(round((hours - h) * 60)):02d}m"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--capacity-mah", type=float, default=2500.0)
    ap.add_argument("--cutoff-mv", type=int, default=3400,
                    help="AUTO_SHUTDOWN_MILLIVOLTS -- the firmware's own end of run")
    ap.add_argument("--warmup-min", type=float, default=30.0,
                    help="leading minutes discarded as surface-charge decay")
    ap.add_argument("--window-h", type=float, default=3.0,
                    help="trailing hours used for the slope fit")
    ap.add_argument("--full-mv", type=int, default=None,
                    help="observed voltage at 100%% charge; anchors the curve. "
                         "Defaults to the first sample of the discharge, which is "
                         "correct when the pack was started fully charged.")
    args = ap.parse_args()

    rows = parse(args.log)
    if not rows:
        print("no [pwr] lines found -- is this the right log?")
        return 1

    # The run is the TRAILING CONTIGUOUS segment of plausible cell readings -- not
    # every sample that happens to fall in range.
    #
    # This log contains samples from before the battery was wired correctly, when
    # reversed leads left VBAT unpowered and the ADC read a clean 0 mV (#766). Those
    # zeros are inside any naive "<= 4350" filter, and including them anchors the fit
    # at 0 mV and reports the battery CHARGING at +6482 mV/h. A discharge estimator
    # that can output a positive slope is reporting a parsing bug as physics.
    #
    # So: valid means a real single cell (2500..4350 mV). Anything outside that is a
    # host supply above or an unpowered/absent cell below, and either one ENDS the
    # segment -- we keep only what follows the last such break.
    VALID_LO, VALID_HI = 2500, 4350
    start_idx = 0
    for i, r in enumerate(rows):
        if not (VALID_LO <= r[2] <= VALID_HI):
            start_idx = i + 1
    batt = rows[start_idx:]
    if not batt:
        last = rows[-1][2]
        if last > VALID_HI:
            print(f"{len(rows)} [pwr] lines, latest {last} mV -- still on USB, no discharge yet.")
        else:
            print(f"{len(rows)} [pwr] lines, latest {last} mV -- no valid cell reading "
                  f"(battery absent or miswired?).")
        return 1
    if start_idx:
        print(f"note             : ignoring {start_idx} sample(s) before the current "
              f"discharge began (USB rail or unpowered VBAT)")

    t0 = batt[0][0]
    elapsed_h = (batt[-1][0] - t0).total_seconds() / 3600.0
    now_mv = batt[-1][2]

    print(f"samples          : {len(batt)} on battery ({len(rows)} total)")
    print(f"start            : {t0.isoformat()}  {batt[0][2]} mV")
    print(f"latest           : {batt[-1][0].isoformat()}  {now_mv} mV")
    print(f"elapsed          : {hms(elapsed_h)}")
    print(f"cutoff           : {args.cutoff_mv} mV (AUTO_SHUTDOWN_MILLIVOLTS)")

    if now_mv <= args.cutoff_mv:
        print(f"\nRUN COMPLETE -- reached cutoff. Runtime = {hms(elapsed_h)}")
        return 0

    usable = [(( ts - t0).total_seconds() / 3600.0, mv)
              for ts, _up, mv in batt
              if (ts - t0).total_seconds() >= args.warmup_min * 60]
    if len(usable) < 4:
        print(f"\ntoo early: need >{args.warmup_min:.0f} min past start before the surface-charge")
        print("decay settles. Re-run later; nothing before then is worth extrapolating.")
        return 0

    # 1. slope projection over the trailing window
    t_end = usable[-1][0]
    window = [p for p in usable if p[0] >= t_end - args.window_h] or usable
    slope, _ = fit(window)
    print(f"\nslope (last {hms(min(args.window_h, t_end - window[0][0]))}) : {slope:+.1f} mV/h")

    if slope < -0.5:
        remain_slope = (now_mv - args.cutoff_mv) / (-slope)
        print(f"  -> projection  : {hms(remain_slope)} remaining, "
              f"total {hms(elapsed_h + remain_slope)}")
    else:
        remain_slope = None
        print("  -> projection  : slope too flat to extrapolate (plateau); "
              "use charge accounting")

    # 2. charge accounting
    full_mv = args.full_mv if args.full_mv else batt[0][2]
    soc = make_soc(full_mv / 1000.0)
    print(f"anchor           : {full_mv} mV treated as 100% charge"
          f"{'' if args.full_mv else ' (first sample of the discharge)'}")
    soc_start, soc_now = soc(usable[0][1] / 1000.0), soc(now_mv / 1000.0)
    used_frac = soc_start - soc_now
    span_h = t_end - usable[0][0]
    if used_frac > 0.005 and span_h > 0:
        mah_used = used_frac * args.capacity_mah
        avg_ma = mah_used / span_h
        mah_left = soc_now * args.capacity_mah
        remain_charge = mah_left / avg_ma if avg_ma > 0 else float("inf")
        print(f"\ncharge accounting:")
        print(f"  charge level   : {soc_start*100:.0f}% -> {soc_now*100:.0f}%"
              f"  ({mah_used:.0f} mAh over {hms(span_h)})")
        print(f"  avg draw       : {avg_ma:.1f} mA")
        print(f"  -> projection  : {hms(remain_charge)} remaining, "
              f"total {hms(elapsed_h + remain_charge)}")

        if remain_slope is not None:
            lo, hi = sorted((remain_slope, remain_charge))
            spread = hi / lo if lo > 0 else float("inf")
            verdict = ("methods agree" if spread < 1.5 else
                       "methods diverge -- report a RANGE, not a number")
            print(f"\n  {verdict}: total runtime {hms(elapsed_h + lo)} .. "
                  f"{hms(elapsed_h + hi)}")
    else:
        print("\ncharge accounting: SoC has not moved measurably yet -- still on the")
        print("plateau. Elapsed time is real; the projection is not, yet.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
