#pragma once

// #1586: see Stream.h in this directory for why this shim exists.
#include "../mocks/Arduino.h"

// rweather/Crypto's RNG.cpp includes <Arduino.h> unconditionally and calls
// micros(), which test/mocks/Arduino.h does not provide. PlatformIO compiles every
// source in the library whether or not we use it, so RNG.cpp has to build even
// though MACThenDecrypt only needs AES128 and SHA256. Defined here rather than in
// the shared mock to keep this to the one env that links the real library.
//
// If test/mocks/Arduino.h ever grows its own micros(), this becomes a duplicate
// definition and only [env:native_mac_align] breaks -- delete these lines then.
inline uint32_t micros() {
  return g_mock_millis * 1000;
}
