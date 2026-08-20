// src/helpers/prefs/PrefsRead.h
//
// #899: read an OPTIONAL string pref without the ERROR-log storm.
//
// Arduino's Preferences::getString(key, default) takes a default parameter
// precisely so that a missing key is a normal outcome -- and then logs at
// ERROR when it uses that default:
//
//     // framework-arduinoespressif32/libraries/Preferences/src/Preferences.cpp:483
//     esp_err_t err = nvs_get_str(_handle, key, value, &len);
//     if(err){
//         log_e("nvs_get_str len fail: %s %s", key, nvs_error(err));
//         return String(defaultValue);
//     }
//
// Any caller using that API as designed emits ERROR spam by construction. Our
// broker config makes it continuous: writeBrokerConfig deliberately REMOVES
// empty fields rather than storing blanks (#182), so absent keys are the
// DESIGNED steady state, and the pool worker re-reads every slot on a 500 ms
// cycle. Measured on ST-P with brokers unconfigured: 29.3 ERROR lines/sec,
// 97.1% of all serial output, splicing CLI replies mid-word.
//
// Only getString needs this. The numeric getters log at log_v (VERBOSE):
//
//     // Preferences.cpp, getUChar
//     log_v("nvs_get_u8 fail: %s %s", key, nvs_error(err));
//
// which is why exactly the string keys appeared in the capture while `enabled`,
// `port` and `transport` did not.
//
// COST, because it is not free: isKey() -> getType() probes up to TEN
// nvs_get_* calls to identify the type, and for a MISSING key it runs all ten
// before returning PT_INVALID. We trade one failing probe plus a ~60-byte UART
// write for ten flash lookups. Worth it -- flash reads are microseconds, while
// 60 bytes at 115200 baud is ~5 ms AND corrupts any reply sharing the UART --
// but it is a real cost on a hot path, not a pure win.
//
// Behaviour is otherwise IDENTICAL to getString: a missing key still yields the
// default. That is what makes this safe for every caller, including the CLI
// display path (`get mqtt.broker.<N>.<key>`), which must keep showing a
// disabled-but-configured broker's stored values.

#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>

namespace offband {

// The ONE place `getString` may be called bare. scripts/check_nvs_guarded_reads.py
// refuses to accept this as the exempt helper unless the type check below is
// still present.
//
// getType(key) == PT_STR, NOT isKey(key). isKey() is literally
// `getType(key) != PT_INVALID`, so this costs exactly the same -- but isKey
// returns true for a key that exists holding a NON-string type, and getString
// would then fail with ESP_ERR_NVS_TYPE_MISMATCH and log at ERROR anyway,
// reintroducing the defect. Requiring PT_STR closes that. Found by review.
//
// KNOWN AND ACCEPTED -- a TOCTOU window: readBrokerConfig runs on the MQTT
// worker task while the CLI can write config from another task, so a key can be
// erased between the type check and the read, producing ONE stray ERROR line.
// That is a single line in a rare race, against 29.3 lines/sec continuously
// before this change. The Arduino API exposes no atomic "read if present", so
// closing it entirely would mean reaching past Preferences to the raw nvs
// handle -- a much larger change for a far smaller problem.
inline String prefStr(Preferences& p, const char* key, const char* dflt = "") {
    return p.getType(key) == PT_STR ? p.getString(key, dflt) : String(dflt);
}

}  // namespace offband

#endif  // ARDUINO
