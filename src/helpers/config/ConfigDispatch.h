// src/helpers/config/ConfigDispatch.h
//
// Epic #300 item 1 (#364): role-agnostic config-command dispatch.
//
// Extracted from wifi_observer/ObserverCli.cpp, where configSet/configGet were
// hardwired to observer keys and took MqttBrokerPool&. This component owns only
// the DISPATCH MECHANISM + the role-agnostic parse helpers; every key handler
// stays owned by its role.
//
// Backs the companion-API config command (CMD_OFFBAND_CONFIG /
// OffbandConfigProtocol.h). The firmware<->client wire contract (#143/#160) is
// UNCHANGED by this component: `reply` is the same NUL-terminated human text the
// role's handler produced, and a false return still means "no role claims this
// key" (the caller answers OCFG_R_ERR "unknown config key").
//
// Registration model (Epic #300 decomposition item 1, owner-approved "Option A"):
// each role registers ONE set/get function pair. The dispatcher asks each
// provider in registration order; the first to claim the key wins. A role's
// internal key ordering therefore stays entirely its own business -- which is
// what lets the observer's order-sensitive if-chain (exact `wifi.enabled` MUST be
// tested before the generic `wifi.` prefix, else the on/off switch is silently
// written as a wifi field named "enabled") move across untouched.
//
// The repeater (#301/#305) registers its own provider for `wifi.mode`, WiFi
// creds, command-queue and OTA keys without this file or the observer changing.
//
// Nothing here uses a platform-specific API beyond an #ifdef ARDUINO diagnostic
// print, which is why the component is portable to nRF52 as well as ESP32.

#pragma once
#include <stddef.h>

namespace offband {
namespace config {

// !! KEY SPACES MUST BE DISJOINT ACROSS ROLES -- FIRST PROVIDER WINS. !!
//
// Dispatch short-circuits on the first provider that returns true, so a provider
// registered EARLIER shadows a later one for any key both would claim, silently.
// This is a live hazard for #301, not a theoretical one: the observer provider
// claims the ENTIRE `wifi.` prefix (`wifi.ssid`, `wifi.pwd`), so a repeater
// provider registered after it will never be asked about those keys and its WiFi
// config would be unreachable over the wire with no diagnostic.
//
// Before adding a role: enumerate its keys against every already-registered
// role's keys and prefixes, and either keep them disjoint or deliberately reuse
// the owning role's handler. A build/debug-time overlap detector is tracked
// separately (see the #364 review follow-up) -- until it exists this is a manual
// review obligation.
//
// A role's config entry points.
//   reply  : NUL-terminated human text (what the client displays).
//   return : true  -> this key is mine; `reply` is populated (incl. an "ERROR: "
//                     string, which is still a handled key).
//            false -> not my key; the dispatcher tries the next provider.
//
// CONTRACT: a provider MUST NOT leave content in `reply` when it returns false.
// The dispatcher clears `reply` once before the walk; a provider that wrote
// partial text and then declined the key would leave that text visible to the
// next provider (and to a future provider that appends rather than overwrites).
// Providers may clear `reply` themselves -- that is deliberate, so each stays
// correct if ever called directly (e.g. from a test).
typedef bool (*SetFn)(const char* key, const char* value, char* reply, size_t reply_size);
typedef bool (*GetFn)(const char* key, char* reply, size_t reply_size);

// Provider table capacity. Observer + repeater today, with headroom. Fixed
// static storage -- no heap (tight-RAM boards; matches the raw-function-pointer
// precedent in ObserverCli.h).
static const int kMaxProviders = 4;

// Register a role's provider.
//   role_name    : static string, diagnostics only.
//   key_prefixes : static array of the keys this provider claims. CONVENTION:
//                  an entry ENDING in '.' is a PREFIX ("wifi." claims wifi.ssid,
//                  wifi.pwd, ...); an entry NOT ending in '.' is an EXACT leaf
//                  key ("mqtt.iata"). See prefixesCollide in the .cpp.
//   prefix_count : entries in key_prefixes (0 + nullptr allowed, but then this
//                  provider is EXEMPT from overlap detection -- discouraged).
// Returns false if the table is full (SAFELANE 6: the caller must not ignore).
//
// OVERLAP DETECTION (#366): at registration, this provider's entries are checked
// against each already-registered provider's. A collision (some key both would
// claim -- e.g. observer's `wifi.` prefix vs a repeater's `wifi.mode` leaf, the
// #301 trap) bumps overlapWarningCount() immediately AND stores a record. The
// human-visible LOUD diagnostic (Serial / stderr, naming both roles + entries)
// is emitted LAZILY on the first dispatchSet/dispatchGet -- NOT at registration,
// which runs during static init before Serial.begin() (review BLOCKER-1). This
// converts the silent first-provider-wins shadow into a visible error without
// depending on unready hardware. Registration still proceeds; dispatch is
// unchanged -- the diagnostic is only a signal.
bool registerProvider(SetFn set_fn, GetFn get_fn, const char* role_name,
                      const char* const* key_prefixes, int prefix_count);

// Number of registered providers (diagnostics / tests).
int providerCount();

// #366: total key-space collisions detected across all registerProvider calls.
// 0 in a correct build. Lets a regression test assert the detector fired.
int overlapWarningCount();

// Walk providers in registration order; first to claim `key` wins.
// Returns false if NO provider claimed the key.
bool dispatchSet(const char* key, const char* value, char* reply, size_t reply_size);
bool dispatchGet(const char* key, char* reply, size_t reply_size);

// ---------------------------------------------------------------------------
// Shared, role-agnostic parse helpers (moved verbatim in behaviour from
// ObserverCli.cpp so every role parses config values identically).
// ---------------------------------------------------------------------------

// NULL-safe strcmp == 0.
bool strEq(const char* a, const char* b);

// "1"/"true"/"on" -> true; "0"/"false"/"off" -> false; anything else leaves
// `out` untouched and returns false. Leading spaces skipped.
// (Was ObserverCli.cpp::parseConfigBool.)
bool parseBool(const char* v, bool& out);

// Parse "<prefix><index>.<field>" -> index + field pointer. Returns true iff the
// key is well-formed: `prefix` matches, an index in [0, max_index) follows, and a
// '.' precedes a NON-EMPTY field. `out_field` then points just past that '.'.
// (Was ObserverCli.cpp::parseBrokerKey, generalised off the hardcoded
// "mqtt.broker." / OFFBAND_MAX_BROKERS so the repeater can reuse it for its own
// indexed key spaces.)
bool parseIndexedKey(const char* key, const char* prefix, int max_index,
                     int& out_index, const char*& out_field);

// File-scope registrar: declaring one static instance in a role's .cpp registers
// that role during static initialisation. The provider table is POD and lives in
// .bss (zero-initialised before ANY dynamic initialiser runs), so this is safe
// regardless of translation-unit link order -- and it removes the "a new role
// forgot to call registerProvider()" failure mode entirely.
struct ProviderRegistrar {
    ProviderRegistrar(SetFn set_fn, GetFn get_fn, const char* role_name,
                      const char* const* key_prefixes, int prefix_count) {
        registerProvider(set_fn, get_fn, role_name, key_prefixes, prefix_count);
    }
};

}  // namespace config
}  // namespace offband
