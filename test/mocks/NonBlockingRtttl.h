#pragma once

// Host stand-in for the NonBlockingRTTTL library, so buzzer.cpp can compile natively
// (#1922). It records what the driver asked for; it makes no sound and plays no notes.

namespace rtttl {

inline bool g_mock_playing = false;
inline int g_mock_begin_calls = 0;
inline int g_mock_stop_calls = 0;
inline const char* g_mock_melody = nullptr;

inline void begin(unsigned char, const char* melody) {
  g_mock_playing = true;
  g_mock_melody = melody;
  ++g_mock_begin_calls;
}
inline bool isPlaying() { return g_mock_playing; }
inline bool done() { return !g_mock_playing; }
inline void play() {}
inline void stop() {
  g_mock_playing = false;
  ++g_mock_stop_calls;
}

}  // namespace rtttl
