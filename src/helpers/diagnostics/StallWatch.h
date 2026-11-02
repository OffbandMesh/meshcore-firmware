#pragma once

// #1966: report where the main loop is stuck, before the watchdog resets the board.
// nRF52 only, and compiled only with OFFBAND_STALL_WATCH (a diagnostic env).
//
// Output is three `[stall]` lines at 15, 20 and 25 s without a feed, each with the stuck
// task's PC, LR and the next stack words. Map them with:
//   arm-none-eabi-addr2line -f -C -e .pio/build/<env>/firmware.elf <pc> <lr>

namespace offband {

#if defined(OFFBAND_STALL_WATCH) && defined(NRF52_PLATFORM)
void stallWatchBegin();   // from setup(), on the loop task, after the watchdog starts
void stallWatchFeed();    // from loop(), wherever the watchdog is fed
#else
inline void stallWatchBegin() {}
inline void stallWatchFeed() {}
#endif

}  // namespace offband
