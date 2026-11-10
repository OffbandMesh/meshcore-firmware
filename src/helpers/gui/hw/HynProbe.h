#pragma once

// Which Hynitron touch chip answers at 0x1A (#2108, plan task B1). Our own
// code: the register sequences are the chips' (see ../HynInfo.h).

#include <stdint.h>

#include <Wire.h>

#include "../HynInfo.h"

struct HynProbeResult {
  bool acked = false;      // the address answered before the info read
  bool read_ok = false;    // the info read returned every byte
  bool valid = false;      // the block parsed as this family's
  offband::gui::HynInfo info;
  uint8_t raw[32] = {};    // the block's first bytes, for the log (cst3xx's type is at 18-19)
};

// After the probes: does another chip on the same bus still answer? A probe
// that left the touch chip holding the bus would show here.
bool hynBusAnswers(TwoWire& wire, uint8_t other_addr);

// Resets the chip on `rst`, then reads `family`'s info block. Each call
// resets first, so one family's commands never leave the chip in a mode the
// next probe doesn't expect. Ends with the chip reset again.
void hynProbe(TwoWire& wire, int rst, offband::gui::HynFamily family, HynProbeResult* out);
