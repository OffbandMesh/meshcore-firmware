#include "LedActivity.h"

namespace offband {

// File-static rather than a header inline: one sink per image, and no ODR surprise from
// several translation units each carrying their own copy.
static LedActivitySink _sink = nullptr;

void setLedActivitySink(LedActivitySink sink) {
  _sink = sink;
}

void onPacketDecoded(uint32_t airtime_ms) {
  // Called from the dispatcher's receive path, so it does the least possible work: one
  // null check and one call. Everything about patterns, floors and priorities belongs to
  // the sink, which runs on the UI tick where spending time is safe.
  if (_sink != nullptr) _sink(airtime_ms);
}

}  // namespace offband
