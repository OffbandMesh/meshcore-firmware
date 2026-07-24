// src/helpers/config/ConfigDispatch.cpp
//
// Epic #300 item 1 (#364). See ConfigDispatch.h for the model and rationale.

#include "ConfigDispatch.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>    // fprintf -- the non-Arduino (host/test) diagnostic path

#ifdef ARDUINO
  #include <Arduino.h>   // Serial -- diagnostics only; Arduino-generic, NOT
                         // ESP-only, so this component builds for nRF52 too.
#endif

namespace offband {
namespace config {

namespace {

struct Provider {
    SetFn       set_fn;
    GetFn       get_fn;
    const char* role;
};

// POD with static storage duration -> zero-initialised in .bss during static
// initialisation, which completes before any dynamic initialiser runs. That is
// what makes a file-scope ProviderRegistrar in another TU safe at any link order.
Provider g_providers[kMaxProviders];
int      g_count;

// Latched so a client polling config cannot turn this into a log flood
// (SAFELANE 11 rule 10: diagnostics must not become the outage).
bool g_warned_empty;

// SAFELANE 6: an unregistered config surface must not silently masquerade as
// "unknown config key" to the client. Report it once, visibly.
void warnIfNoProvider(const char* op) {
    if (g_count != 0 || g_warned_empty) return;
    g_warned_empty = true;
    // #364 review MAJOR-1: must be loud on EVERY target, not just Arduino --
    // otherwise a host/unit-test build hides a dead config surface entirely.
#ifdef ARDUINO
    Serial.printf("ERROR: config %s with NO provider registered "
                  "(config surface is dead; a role failed to link/register)\n", op);
#else
    fprintf(stderr, "ERROR: config %s with NO provider registered "
                    "(config surface is dead; a role failed to link/register)\n", op);
#endif
}

}  // namespace

bool registerProvider(SetFn set_fn, GetFn get_fn, const char* role_name) {
    if (set_fn == nullptr && get_fn == nullptr) return false;
    if (g_count >= kMaxProviders) {
        // Loud on every target: a role silently dropped here loses its whole
        // config surface (#364 review MAJOR-1).
        const char* who = role_name != nullptr ? role_name : "?";
#ifdef ARDUINO
        Serial.printf("ERROR: config provider table full (%d); '%s' NOT registered\n",
                      kMaxProviders, who);
#else
        fprintf(stderr, "ERROR: config provider table full (%d); '%s' NOT registered\n",
                kMaxProviders, who);
#endif
        return false;
    }
    g_providers[g_count].set_fn = set_fn;
    g_providers[g_count].get_fn = get_fn;
    g_providers[g_count].role   = role_name;
    ++g_count;
    return true;
}

int providerCount() { return g_count; }

bool dispatchSet(const char* key, const char* value, char* reply, size_t reply_size) {
    if (key == nullptr || value == nullptr || reply == nullptr || reply_size == 0) return false;
    reply[0] = '\0';
    warnIfNoProvider("set");
    for (int i = 0; i < g_count; ++i) {
        if (g_providers[i].set_fn == nullptr) continue;
        if (g_providers[i].set_fn(key, value, reply, reply_size)) return true;
    }
    return false;   // no role claims this key
}

bool dispatchGet(const char* key, char* reply, size_t reply_size) {
    if (key == nullptr || reply == nullptr || reply_size == 0) return false;
    reply[0] = '\0';
    warnIfNoProvider("get");
    for (int i = 0; i < g_count; ++i) {
        if (g_providers[i].get_fn == nullptr) continue;
        if (g_providers[i].get_fn(key, reply, reply_size)) return true;
    }
    return false;   // no role claims this key
}

// ---------------------------------------------------------------------------
// Shared parse helpers
// ---------------------------------------------------------------------------

bool strEq(const char* a, const char* b) {
    return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}

bool parseBool(const char* v, bool& out) {
    if (v == nullptr) return false;
    while (*v == ' ') ++v;
    if (strEq(v, "1") || strEq(v, "true")  || strEq(v, "on"))  { out = true;  return true; }
    if (strEq(v, "0") || strEq(v, "false") || strEq(v, "off")) { out = false; return true; }
    return false;
}

bool parseIndexedKey(const char* key, const char* prefix, int max_index,
                     int& out_index, const char*& out_field) {
    if (key == nullptr || prefix == nullptr || max_index <= 0) return false;
    size_t pl = strlen(prefix);
    if (strncmp(key, prefix, pl) != 0) return false;
    const char* p = key + pl;
    if (*p < '0' || *p > '9') return false;          // index must be present
    long v = strtol(p, nullptr, 10);
    if (v < 0 || v >= (long)max_index) return false; // out of range
    while (*p >= '0' && *p <= '9') ++p;              // past index digit(s)
    if (*p != '.' || *(p + 1) == '\0') return false; // need '.' + non-empty field
    out_index = (int)v;
    out_field = p + 1;
    return true;
}

}  // namespace config
}  // namespace offband
