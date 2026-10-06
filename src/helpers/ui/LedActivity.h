#pragma once

#include <stdint.h>

// #1367: the seam that lets the packet dispatcher say "a packet decoded, and it occupied
// the air for this long" without knowing that a UI, a badge or an LED exists.
//
// The direction is deliberate and matches SystemChannelCli's post-callback: Dispatcher
// calls DOWN into this file, the UI registers a sink UP into it, and neither includes the
// other. Dispatcher runs on every role and every board; it must not grow a dependency on
// a badge's indicator.
//
// Reached only through -D OFFBAND_LED_ACTIVITY. Without that flag the dispatcher's call
// site compiles out entirely, so the sixty other variants building this code are byte
// for byte unaffected -- they do not even link this translation unit.

namespace offband {

// Milliseconds a decoded packet occupied the air, as the radio driver estimates it
// (Dispatcher already computes this for its own duty-cycle accounting, so the signal
// costs nothing to produce).
typedef void (*LedActivitySink)(uint32_t airtime_ms);

// Register who hears about traffic. Passing nullptr detaches, which is what a UI must do
// before it goes away -- a stale pointer here would be called from the dispatcher.
void setLedActivitySink(LedActivitySink sink);

// Called by the dispatcher when a packet parsed successfully. "Decoded", not "heard":
// noise that fails to parse is not traffic and must not light anything, or the indicator
// stops meaning the mesh is reaching us.
void onPacketDecoded(uint32_t airtime_ms);

}  // namespace offband
