// src/helpers/config/DisplayConfigProvider.cpp
//
// #370: the shared `display.*` config provider. See DisplayConfigProvider.h.
//
// The NVS accessors + handlers are relocated VERBATIM from ConfigSchema.cpp and
// ObserverCli.cpp (byte-identical reply strings + identical NVS keys/namespace,
// so no wire-contract or on-flash change). The private write-failure diagnostic
// and the `offband_ui` constants are carried here too, so this file does NOT
// depend on ConfigSchema.

#include "DisplayConfigProvider.h"
#include "ConfigDispatch.h"
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Preferences.h>
  #ifdef ESP_PLATFORM
    #include <nvs.h>                          // nvs_get_stats() -- write-failure diagnostic
    #include <helpers/diagnostics/CrashLog.h> // crashLogf() -- shared role-neutral diagnostic (relocated here by #350)
  #endif
#endif

namespace offband {

// offband_ui NVS keys (relocated from ConfigSchema -- fork-branded namespace so a
// future upstream NVS namespace can never clash).
namespace {
constexpr const char* kNvsOffbandUi       = "offband_ui";
constexpr const char* kKeyDisplayAlwaysOn = "always_on";   // bool; default false (#141)
constexpr const char* kKeyDisplayRotation = "rotation";    // uint8 0/180; default 0 (#148)

// #181: surface an NVS write failure to the persistent crash log + Serial. Real
// device only; a no-op on host/non-ESP builds. Copied from ConfigSchema so the
// display accessors carry their own diagnostic and stay decoupled.
#if defined(ARDUINO) && defined(ESP_PLATFORM)
void logCfgWriteFailure(const char* where, const char* ns) {
    nvs_stats_t st;
    if (nvs_get_stats(NULL, &st) == ESP_OK) {
        crashLogf("[cfg] %s WRITE FAILED ns=%s nvs[used=%u free=%u total=%u]", where, ns,
                  (unsigned)st.used_entries, (unsigned)st.free_entries, (unsigned)st.total_entries);
    } else {
        crashLogf("[cfg] %s WRITE FAILED ns=%s (nvs_get_stats unavailable)", where, ns);
    }
}
#else
void logCfgWriteFailure(const char*, const char*) {}
#endif
}  // namespace

// ---------------------------------------------------------------------------
// Display-preference persistence (relocated from ConfigSchema.cpp -- unchanged)
// ---------------------------------------------------------------------------
bool getDisplayAlwaysOn() {
#ifdef ARDUINO
    Preferences p;
    p.begin(kNvsOffbandUi, /*readOnly=*/true);
    bool v = p.getBool(kKeyDisplayAlwaysOn, false);
    p.end();
    return v;
#else
    return false;
#endif
}

bool setDisplayAlwaysOn(bool on) {
#ifdef ARDUINO
    Preferences p;
    if (!p.begin(kNvsOffbandUi, /*readOnly=*/false)) {
        logCfgWriteFailure("setDisplayAlwaysOn.begin", kNvsOffbandUi);
        return false;
    }
    size_t wrote = p.putBool(kKeyDisplayAlwaysOn, on);
    p.end();
    if (wrote != sizeof(uint8_t)) {     // putBool stores 1 byte; 0 on failure
        logCfgWriteFailure("setDisplayAlwaysOn.putBool", kNvsOffbandUi);
        return false;
    }
    return true;
#else
    (void)on;
    return true;
#endif
}

uint8_t getDisplayRotation() {
#ifdef ARDUINO
    Preferences p;
    p.begin(kNvsOffbandUi, /*readOnly=*/true);
    uint8_t v = p.getUChar(kKeyDisplayRotation, 0);
    p.end();
    return (v == 180) ? 180 : 0;
#else
    return 0;
#endif
}

bool setDisplayRotation(uint8_t deg) {
#ifdef ARDUINO
    Preferences p;
    if (!p.begin(kNvsOffbandUi, /*readOnly=*/false)) {
        logCfgWriteFailure("setDisplayRotation.begin", kNvsOffbandUi);
        return false;
    }
    size_t wrote = p.putUChar(kKeyDisplayRotation, (deg == 180) ? 180 : 0);
    p.end();
    if (wrote != sizeof(uint8_t)) {     // putUChar stores 1 byte; 0 on failure
        logCfgWriteFailure("setDisplayRotation.putUChar", kNvsOffbandUi);
        return false;
    }
    return true;
#else
    (void)deg;
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Appliers + handlers (relocated from ObserverCli.cpp -- bodies unchanged)
// ---------------------------------------------------------------------------
static void (*s_display_always_on_applier)(bool) = nullptr;

void setDisplayAlwaysOnApplier(void (*fn)(bool)) {
    s_display_always_on_applier = fn;
}

bool handleDisplayAlwaysOn(char* reply, size_t reply_size, bool on) {
    // #181: if persistence fails, surface it and do NOT apply to the live display
    // -- applying a setting that won't survive a reboot would mislead the user
    // about what's actually stored (SAFELANE 6: state must match the ACK).
    if (!setDisplayAlwaysOn(on)) {                                     // persist (offband_ui NVS)
        snprintf(reply, reply_size, "ERROR: failed to save display setting (NVS write failed)\n");
        return true;
    }
    if (s_display_always_on_applier) s_display_always_on_applier(on);  // apply to the live display
    snprintf(reply, reply_size,
             on ? "display: always on (screen stays lit)\n"
                : "display: normal (blanks after 15 s)\n");
    return true;
}

static void (*s_display_rotation_applier)(uint8_t) = nullptr;

void setDisplayRotationApplier(void (*fn)(uint8_t)) {
    s_display_rotation_applier = fn;
}

static bool (*s_display_rotation_supported)() = nullptr;

void setDisplayRotationSupportedQuery(bool (*fn)()) {
    s_display_rotation_supported = fn;
}

// In-session cache of the current rotation so `display flip` toggles reliably
// from RAM instead of a write-then-read NVS round-trip (a fresh read-only
// handle may not observe a just-committed write). Lazily seeded from NVS;
// updated on every rotate/flip. NVS stays the persistence layer (#148).
static int s_rotation_cache = -1;   // -1 = not yet loaded

bool handleDisplayRotate(char* reply, size_t reply_size, uint8_t deg) {
    // #148: gate to drivers with a verified runtime-rotation override (SSD1306
    // OLED). Others report unsupported rather than silently no-op'ing; the TFT
    // (ST7789) override is not yet hardware-verified and is tracked separately.
    // Deny-by-default: if the capability query was never registered, treat the
    // display as unsupported (don't fall through to a silent no-op) -- per Gemini review.
    if (!s_display_rotation_supported || !s_display_rotation_supported()) {
        snprintf(reply, reply_size, "display: rotation not supported on this display\n");
        return true;
    }
    // #181: persist first; on NVS failure surface it and leave the cache + live
    // display untouched, so RAM state, NVS, and the ACK all stay consistent.
    if (!setDisplayRotation(deg)) {                                           // persist (offband_ui NVS)
        snprintf(reply, reply_size, "ERROR: failed to save display rotation (NVS write failed)\n");
        return true;
    }
    s_rotation_cache = deg;                                                   // keep the in-session cache current
    if (s_display_rotation_applier) s_display_rotation_applier(deg);          // apply to the live display
    snprintf(reply, reply_size,
             deg == 180 ? "display: rotation 180 (flipped)\n"
                        : "display: rotation 0 (default)\n");
    return true;
}

bool handleDisplayFlip(char* reply, size_t reply_size) {
    // Toggle from the in-session cache (seeded from NVS on first use), so flip
    // always inverts 0<->180 without depending on a read-after-write.
    if (s_rotation_cache < 0) s_rotation_cache = getDisplayRotation();
    uint8_t other = (s_rotation_cache == 180) ? 0 : 180;
    return handleDisplayRotate(reply, reply_size, other);
}

// ---------------------------------------------------------------------------
// Provider set/get -- relocated verbatim from observerConfigSet/Get's display
// branches.
// ---------------------------------------------------------------------------
namespace {

bool displayConfigSet(const char* key, const char* value, char* reply, size_t reply_size) {
    if (key == nullptr || value == nullptr || reply == nullptr || reply_size == 0) return false;

    if (config::strEq(key, "display.always_on")) {
        bool on;
        if (!config::parseBool(value, on)) { snprintf(reply, reply_size, "ERROR: display.always_on expects 0|1\n"); return true; }
        return handleDisplayAlwaysOn(reply, reply_size, on);
    }
    if (config::strEq(key, "display.rotation")) {
        if (config::strEq(value, "0"))   return handleDisplayRotate(reply, reply_size, 0);
        if (config::strEq(value, "180")) return handleDisplayRotate(reply, reply_size, 180);
        snprintf(reply, reply_size, "ERROR: display.rotation expects 0|180\n");
        return true;
    }
    return false;  // not a display key
}

bool displayConfigGet(const char* key, char* reply, size_t reply_size) {
    if (key == nullptr || reply == nullptr || reply_size == 0) return false;

    if (config::strEq(key, "display.always_on")) {
        snprintf(reply, reply_size, "display.always_on = %d\n", getDisplayAlwaysOn() ? 1 : 0);
        return true;
    }
    if (config::strEq(key, "display.rotation")) {
        snprintf(reply, reply_size, "display.rotation = %u\n", (unsigned)getDisplayRotation());
        return true;
    }
    return false;  // not a display key
}

// #366: this provider owns exactly the `display.` prefix.
const char* const kDisplayPrefixes[] = { "display." };

__attribute__((used)) config::ProviderRegistrar _display_config_provider(
    &displayConfigSet, &displayConfigGet, "display",
    kDisplayPrefixes, (int)(sizeof(kDisplayPrefixes) / sizeof(kDisplayPrefixes[0])));

}  // namespace

}  // namespace offband
