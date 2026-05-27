// src/helpers/wifi_observer/CliPassthrough.cpp
//
// Plan 3 Task 4 (Strycher/LoRa#272). See CliPassthrough.h for the
// allowlist semantics + denied-message contract.
//
// Note on include surface: we forward-declare dispatchObserverCli()
// and wifiObserverPool() rather than including ObserverCli.h /
// WifiObserver.h. Their transitive include of WifiObserverConfig.h
// + MqttBroker.h pulls in Arduino + esp-idf headers, which would
// gate the host-runnable allowlist test (Plan 3 Task 4 Step 3)
// behind a heavyweight stub. The forward declarations stay
// linkage-equivalent to the real symbols and the host test only
// has to provide stub bodies for the linker.

#include "CliPassthrough.h"

#include <cstdio>
#include <cstring>

namespace crosswire {

// Forward declarations of the two symbols this module dispatches to.
// Definitions live in ObserverCli.cpp + WifiObserver.cpp respectively.
class MqttBrokerPool;  // opaque here; reference passed through.
bool dispatchObserverCli(const char* cmd, char* reply, size_t reply_size,
                         MqttBrokerPool& pool);
MqttBrokerPool& wifiObserverPool();

// Explicit deny patterns. Each is checked as a literal prefix
// (after the "get "/"set " head is stripped + leading whitespace
// trimmed). Order does not matter -- first match denies.
//
// Per spec the list is intentionally minimal; expand only when a
// user surfaces a real need.
static const char* kDenyPrefixes[] = {
    "!",            // shell escape
    "$(",           // command substitution
    "`",            // backticks
    "reboot",       // tier-2 device state change
    "format",       // tier-2 NVS / FS wipe
    "erase",        // tier-2 flash erase
    "factory",      // factory reset (UI has its own gated button)
    "fs.",          // raw filesystem
    "flash.",       // raw flash
    "ota.",         // raw OTA (Plan 4 has its own button)
    "exit",         // CLI lifecycle
    "quit",
    "rm ",          // unix tease
    "cat ",
    nullptr,
};

static const char* trimLeading(const char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    return s;
}

bool cliPassthroughIsAllowed(const char* line) {
    if (line == nullptr) return false;
    const char* p = trimLeading(line);
    // Allowlist: must start with "get " or "set ".
    if (strncmp(p, "get ", 4) != 0 && strncmp(p, "set ", 4) != 0) return false;
    // Deny scan against the tail (after "get "/"set ").
    const char* tail = p + 4;
    tail = trimLeading(tail);
    for (size_t i = 0; kDenyPrefixes[i] != nullptr; ++i) {
        if (strncmp(tail, kDenyPrefixes[i], strlen(kDenyPrefixes[i])) == 0) {
            return false;
        }
    }
    return true;
}

CliResult cliPassthroughExecute(const char* line, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        // Caller bug. Nothing safe to write into; just classify.
        return cliPassthroughIsAllowed(line) ? CliResult::Unknown
                                             : CliResult::Denied;
    }
    if (!cliPassthroughIsAllowed(line)) {
        snprintf(out, out_len, "denied: not in allowlist");
        return CliResult::Denied;
    }
    // Try ObserverCli (Plan 2) first -- it handles mqtt.* keys + web.*
    // (Task 3 addition). Returns true if the command was recognized.
    if (dispatchObserverCli(line, out, out_len, wifiObserverPool())) {
        return CliResult::Ok;
    }
    // Fall through to wider CommonCLI dispatch deferred until Plan 3
    // Task 11 wire-up. For now, unmatched -> Unknown.
    snprintf(out, out_len, "unknown: %s", line);
    return CliResult::Unknown;
}

}  // namespace crosswire
