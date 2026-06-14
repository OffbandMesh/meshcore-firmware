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

namespace offband {

// Forward declarations. These MUST stay byte-identical to:
//   - dispatchObserverCli: declared in ObserverCli.h
//   - wifiObserverPool: declared in WifiObserver.h
//   - MqttBrokerPool: defined in MqttBrokerPool.h
//
// Forward-declared instead of #included to keep the host test
// (scripts/test_observer_cli_allowlist.py) able to compile this
// file standalone with linkable stubs. If those signatures change
// upstream, update them here too -- the firmware link will catch
// mismatches but the host test will silently link against its
// own stubs.
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

// Normalize the first 2 whitespace-separated tokens (verb + field-name) to
// lowercase. Everything after the second token is case-preserved because
// values (URLs, passwords, hostnames, IATA codes) can legitimately contain
// mixed case.
//
// Motivation: phone keyboards auto-capitalize the first letter of every
// message by default, which broke the strcmp-based verb matching in
// dispatchObserverCli. Users typing `set mqtt.broker.0.url ...` would have
// `Set mqtt.broker.0.url ...` actually sent, and dispatch would reject it
// silently. Same for `Mqtt status`. Strycher/LoRa#313.
//
// Bounded copy with NUL-termination; safe against unterminated `src`.
static void normalizeVerbAndFieldToLower(char* buf, size_t buf_size,
                                         const char* src) {
    if (buf == nullptr || buf_size == 0) return;
    if (src == nullptr) { buf[0] = '\0'; return; }
    strncpy(buf, src, buf_size - 1);
    buf[buf_size - 1] = '\0';
    int tokens_completed = 0;
    bool in_token = false;
    for (char* c = buf; *c; ++c) {
        if (*c == ' ' || *c == '\t') {
            if (in_token) {
                tokens_completed++;
                in_token = false;
                if (tokens_completed >= 2) break;
            }
        } else {
            in_token = true;
            if (*c >= 'A' && *c <= 'Z') *c += ('a' - 'A');
        }
    }
}

bool cliPassthroughIsAllowed(const char* line) {
    if (line == nullptr) return false;
    const char* p = trimLeading(line);
    // Allowlist: must start with "get ", "set ", "mqtt ", or "wifi ". Verb
    // is already-lowercased by the caller via normalizeVerbAndFieldToLower
    // (Strycher/LoRa#313 phone auto-capitalize fix). "mqtt " covers the
    // status/enable/disable subcommands documented in ObserverCli.h; "wifi "
    // covers the aligned "wifi status/enable/disable" grammar
    // (#45) -- without it, "wifi status" was denied here
    // before ever reaching dispatchObserverCli.
    size_t skip = 0;
    if      (strncmp(p, "get ", 4) == 0)  skip = 4;
    else if (strncmp(p, "set ", 4) == 0)  skip = 4;
    else if (strncmp(p, "mqtt ", 5) == 0) skip = 5;
    else if (strncmp(p, "wifi ", 5) == 0) skip = 5;
    else return false;
    // Deny scan against the tail (after the verb + its trailing space).
    const char* tail = p + skip;
    tail = trimLeading(tail);
    for (size_t i = 0; kDenyPrefixes[i] != nullptr; ++i) {
        if (strncmp(tail, kDenyPrefixes[i], strlen(kDenyPrefixes[i])) == 0) {
            return false;
        }
    }
    return true;
}

CliResult cliPassthroughExecute(const char* line, char* out_response,
                                size_t out_len) {
    if (out_response == nullptr || out_len == 0) {
        // Caller bug. Nothing safe to write into; just classify. Note that
        // cliPassthroughIsAllowed(line) is called BEFORE normalization here,
        // so a phone-capitalized verb would classify as Denied -- but this
        // is the bug-handling fast path, not the user-facing path, so
        // strict classification is acceptable.
        return cliPassthroughIsAllowed(line) ? CliResult::Unknown
                                             : CliResult::Denied;
    }
    // Normalize verb + field to lowercase BEFORE allowlist gate or dispatch.
    // Values (after the second whitespace) are case-preserved. See
    // normalizeVerbAndFieldToLower docstring for full motivation; tl;dr
    // phone auto-capitalize tolerance, Strycher/LoRa#313.
    //
    // 512 bytes covers expected longest command surface (password,
    // long URL, etc.) with comfortable headroom over the channel-message
    // byte limit imposed upstream by SystemChannelCli.
    char normalized[512];
    normalizeVerbAndFieldToLower(normalized, sizeof(normalized),
                                  line ? trimLeading(line) : nullptr);
    if (!cliPassthroughIsAllowed(normalized)) {
        snprintf(out_response, out_len, "denied: not in allowlist");
        return CliResult::Denied;
    }
    // Try ObserverCli (Plan 2) first -- it handles mqtt.* keys + web.*
    // (Task 3 addition). Returns true if the command was recognized.
    if (dispatchObserverCli(normalized, out_response, out_len, wifiObserverPool())) {
        return CliResult::Ok;
    }
    // Fall through to wider CommonCLI dispatch deferred until Plan 3
    // Task 11 wire-up. For now, unmatched -> Unknown.
    snprintf(out_response, out_len, "unknown: %s", normalized);
    return CliResult::Unknown;
}

}  // namespace offband
