// src/helpers/wifi_observer/CliPassthrough.h
//
// Allowlisted CLI passthrough for the web Console page.
//
// Allowed: any line starting with "get " or "set ". Each is further
// dispatched to ObserverCli (Plan 2's mqtt.* handlers) or, on miss,
// to CommonCLI (the existing repeater/companion CLI).
//
// Denied: anything else, including shell escape attempts, raw
// filesystem access, raw flash access, and the `reboot` / `format` /
// `clear` family. Denied responses include the literal phrase
// "denied: not in allowlist" so the web UI can flag the message.
//
// The allowlist itself is intentionally minimal in Plan 3; expand
// as user surfaces real needs (per spec: "Allowlist defined
// statically; expanded as users surface real needs").
//
// Plan 3 Task 4 (Strycher/LoRa#272).

#pragma once
#include <stddef.h>
#include <stdint.h>

namespace crosswire {

enum class CliResult : uint8_t {
    Ok      = 0,
    Denied  = 1,
    Unknown = 2,
};

// Execute one CLI line submitted from the web Console.
//
// Returns CliResult::Denied (and writes "denied: not in allowlist"
// to out) if `line` is not on the allowlist. Otherwise dispatches
// to ObserverCli (Plan 2). If ObserverCli does not handle the line,
// returns CliResult::Unknown (writes "unknown: <line>"). Fall-through
// to the wider CommonCLI surface is deferred until Plan 3 Task 11
// (web server wire-up); see plan doc.
//
// `out` must be non-null and `out_len` >= 1; the reply is written
// via snprintf and is always NUL-terminated.
CliResult cliPassthroughExecute(const char* line,
                                char* out_response, size_t out_len);

// For tests + introspection: returns true if this line WOULD be
// accepted by the allowlist (without executing).
bool cliPassthroughIsAllowed(const char* line);

}  // namespace crosswire
