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
    SetFn              set_fn;
    GetFn              get_fn;
    const char*        role;
    const char* const* prefixes;      // static array; not owned, not copied
    int                prefix_count;
};

// POD with static storage duration -> zero-initialised in .bss during static
// initialisation, which completes before any dynamic initialiser runs. That is
// what makes a file-scope ProviderRegistrar in another TU safe at any link order.
Provider g_providers[kMaxProviders];
int      g_count;

// Latched so a client polling config cannot turn this into a log flood
// (SAFELANE 11 rule 10: diagnostics must not become the outage).
bool g_warned_empty;

// #366: count of key-space collisions detected across all registrations. Lets a
// host regression test assert the detector fired without scraping stderr. In a
// correct build this is 0 forever.
int g_overlap_count;

// Manifest-entry convention (#366 review MINOR-3): an entry ENDING in '.' is a
// PREFIX (claims every key starting with it, e.g. "wifi." -> wifi.ssid/wifi.pwd);
// an entry NOT ending in '.' is an EXACT leaf key (e.g. "mqtt.iata"). Two entries
// collide iff some key string would be claimed by both:
//   - prefix P vs anything X : collide iff X starts with P
//   - leaf  L vs leaf L2      : collide iff L == L2
// This avoids the false positive of a leaf "mqtt.iata" shadowing an unrelated
// "mqtt.iata_x". Empty strings never collide (a "" prefix would claim all keys,
// which is never a legitimate manifest entry).
bool prefixesCollide(const char* a, const char* b) {
    if (a == nullptr || b == nullptr || a[0] == '\0' || b[0] == '\0') return false;
    size_t la = strlen(a), lb = strlen(b);
    bool a_prefix = a[la - 1] == '.';
    bool b_prefix = b[lb - 1] == '.';
    if (a_prefix && strncmp(b, a, la) == 0) return true;   // b starts with prefix a
    if (b_prefix && strncmp(a, b, lb) == 0) return true;   // a starts with prefix b
    if (!a_prefix && !b_prefix) return strcmp(a, b) == 0;  // leaf vs leaf: exact only
    return false;
}

// #366: detected collisions are STORED here and printed LAZILY at first dispatch
// (review BLOCKER-1). registerProvider runs during static init -- before
// Serial.begin() -- so printing there is unreliable/lost on ESP32 + nRF52. The
// count still increments at registration (so a host test can assert immediately);
// the human-visible diagnostic is flushed once, on the first dispatchSet/Get,
// when Serial is up. Same deferral pattern as warnIfNoProvider below.
const int kMaxStoredOverlaps = 4;
struct OverlapRecord { const char* role_a; const char* pfx_a;
                       const char* role_b; const char* pfx_b; };
OverlapRecord g_overlaps[kMaxStoredOverlaps];
int  g_overlaps_stored = 0;
bool g_overlaps_flushed = false;

// Called from registration (static-init safe: only touches ints/pointers).
void detectOverlap(const char* new_role,
                   const char* const* new_prefixes, int new_count) {
    if (new_prefixes == nullptr || new_count <= 0) return;
    for (int i = 0; i < new_count; ++i) {
        const char* np = new_prefixes[i];
        for (int p = 0; p < g_count; ++p) {
            for (int j = 0; j < g_providers[p].prefix_count; ++j) {
                const char* ep = g_providers[p].prefixes[j];
                if (!prefixesCollide(np, ep)) continue;
                ++g_overlap_count;
                if (g_overlaps_stored < kMaxStoredOverlaps) {
                    g_overlaps[g_overlaps_stored++] = {
                        new_role ? new_role : "?", np,
                        g_providers[p].role ? g_providers[p].role : "?", ep };
                }
            }
        }
    }
}

// Flush stored overlap diagnostics once, at runtime (Serial up). Latched.
void flushOverlapWarnings() {
    if (g_overlaps_flushed || g_overlap_count == 0) return;
    g_overlaps_flushed = true;
    for (int i = 0; i < g_overlaps_stored; ++i) {
        const OverlapRecord& o = g_overlaps[i];
#ifdef ARDUINO
        Serial.printf("ERROR: config key-space OVERLAP: '%s' (role %s) collides "
                      "with '%s' (role %s) -- one silently shadows the other "
                      "(#366)\n", o.pfx_a, o.role_a, o.pfx_b, o.role_b);
#else
        fprintf(stderr, "ERROR: config key-space OVERLAP: '%s' (role %s) collides "
                "with '%s' (role %s) -- one silently shadows the other (#366)\n",
                o.pfx_a, o.role_a, o.pfx_b, o.role_b);
#endif
    }
    if (g_overlap_count > g_overlaps_stored) {
#ifdef ARDUINO
        Serial.printf("ERROR: config overlap: %d more collision(s) not shown "
                      "(#366)\n", g_overlap_count - g_overlaps_stored);
#else
        fprintf(stderr, "ERROR: config overlap: %d more collision(s) not shown "
                "(#366)\n", g_overlap_count - g_overlaps_stored);
#endif
    }
}

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

bool registerProvider(SetFn set_fn, GetFn get_fn, const char* role_name,
                      const char* const* key_prefixes, int prefix_count) {
    if (set_fn == nullptr && get_fn == nullptr) return false;
    // #366: check overlap against already-registered providers BEFORE adding
    // this one, so it never compares against itself. Stores records + bumps the
    // count here (static-init safe); the human-visible diagnostic is flushed at
    // first dispatch (Serial up).
    detectOverlap(role_name, key_prefixes, prefix_count);
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
    g_providers[g_count].set_fn       = set_fn;
    g_providers[g_count].get_fn       = get_fn;
    g_providers[g_count].role         = role_name;
    g_providers[g_count].prefixes     = key_prefixes;
    g_providers[g_count].prefix_count = prefix_count;
    ++g_count;
    return true;
}

int providerCount() { return g_count; }

int overlapWarningCount() { return g_overlap_count; }

bool dispatchSet(const char* key, const char* value, char* reply, size_t reply_size) {
    if (key == nullptr || value == nullptr || reply == nullptr || reply_size == 0) return false;
    reply[0] = '\0';
    warnIfNoProvider("set");
    flushOverlapWarnings();   // #366: emit any registration-time collisions now (Serial up)
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
    flushOverlapWarnings();   // #366: emit any registration-time collisions now (Serial up)
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
