#pragma once

// #1586: shim include dir for [env:native_mac_align].
//
// test/mocks stubs out AES.h and SHA256.h (encryptBlock/decryptBlock and
// finalizeHMAC are no-ops that write nothing), which is fine for the tests that
// only need Utils.cpp to LINK -- but it makes a crypto round-trip untestable and,
// worse, makes a "forged MAC is rejected" assertion pass for the wrong reason.
//
// So this env puts THIS directory on the include path instead of test/mocks.
// It carries only the Arduino-ish headers Utils.cpp needs, and deliberately has
// no AES.h or SHA256.h, so those resolve to the real rweather/Crypto library.
#include "../mocks/Stream.h"
