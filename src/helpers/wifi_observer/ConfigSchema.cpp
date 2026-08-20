// src/helpers/wifi_observer/ConfigSchema.cpp
#include "ConfigSchema.h"

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Preferences.h>
  #include "../prefs/PrefsRead.h"   // #899: prefStr() -- isKey-guarded optional read
  #if defined(ESP_PLATFORM)
    #include <nvs.h>        // #181: nvs_get_stats() -- write-failure diagnostic (real device only)
    #include <helpers/diagnostics/CrashLog.h>   // #181: crashLogf() -- persistent + Serial failure log
  #endif
#else
  // Host build: provide a thin Preferences shim so the file compiles
  // for host-runnable tests. The shim is in scripts/test_*.py harness.
  #include <cstdio>
  #include <cstring>
#endif

namespace offband {

void mqttBrokerNamespace(uint8_t broker_index, char* out, size_t out_len) {
    // "mqtt_b0".."mqtt_b5" -- 7 chars, well under 15-char NVS limit.
    if (broker_index >= OFFBAND_MAX_BROKERS || out_len < 8) {
        if (out_len > 0) out[0] = '\0';
        return;
    }
    snprintf(out, out_len, "mqtt_b%u", broker_index);
}

#ifdef ARDUINO

// #181: surface a config-write failure (SAFELANE 6 -- never silent) AND measure
// the cause (free-entry count answers "is NVS full?" with evidence, not a guess).
// Shared by every NVS writer below. Real-device only; the host round-trip test
// gets the no-op stub (it defines ARDUINO but not ESP_PLATFORM).
#if defined(ESP_PLATFORM)
static void logCfgWriteFailure(const char* where, const char* ns) {
    nvs_stats_t st;
    if (nvs_get_stats(NULL, &st) == ESP_OK) {
        crashLogf("[cfg] %s WRITE FAILED ns=%s nvs[used=%u free=%u total=%u]", where, ns,
                  (unsigned)st.used_entries, (unsigned)st.free_entries, (unsigned)st.total_entries);
    } else {
        crashLogf("[cfg] %s WRITE FAILED ns=%s (nvs_get_stats unavailable)", where, ns);
    }
}
#else
static void logCfgWriteFailure(const char*, const char*) {}
#endif

// #181/#182: the config READERS default to a SAFE value on a miss (empty iata,
// 30s interval, display off/0deg, empty-url broker), and the Arduino read-only
// API can't distinguish "key absent" -- the legitimate first-boot/unset state --
// from a deeper error, so silence-with-safe-default IS the correct, documented
// contract for a read-miss (NOT a SAFELANE 6 violation). This is exactly why
// writeBrokerConfig can safely REMOVE an empty field's key (#182): an absent key
// just reads back as the default.
//
// The same logic deliberately covers a read-only begin() returning false: on a
// readonly handle that is the EXPECTED "namespace not yet created" miss (nvs_open
// -> ESP_ERR_NVS_NOT_FOUND), which the Arduino wrapper collapses into the same
// `false` as a genuine NVS error. Logging it would flag every fresh / unconfigured
// namespace as an error and flood a first-boot log (violating §1). A SYSTEMIC NVS
// failure still surfaces -- the WRITE path and the boot counter DO log their
// begin() failures (a read-WRITE begin failure is genuinely anomalous;
// NVS_READWRITE creates the namespace). So the readers intentionally do not log
// begin()==false; the safe get-defaults flow through unchanged.

bool readGlobalIata(char* out, size_t out_len) {
    Preferences p;
    p.begin(kNvsMqtt, /*readOnly=*/true);
    String v = prefStr(p, kKeyMqttIata);
    p.end();
    if (v.isEmpty()) {
        if (out_len > 0) out[0] = '\0';
        return false;
    }
    strncpy(out, v.c_str(), out_len);
    out[out_len - 1] = '\0';
    return true;
}

bool writeGlobalIata(const char* iata) {
    Preferences p;
    if (!p.begin(kNvsMqtt, /*readOnly=*/false)) {   // #181: never silent -- report begin failure
        logCfgWriteFailure("writeGlobalIata.begin", kNvsMqtt);
        return false;
    }
    size_t wrote = p.putString(kKeyMqttIata, iata);
    p.end();
    // putString returns chars written (0 on NVS failure). An empty value also
    // returns 0 but is a legitimate clear, so only treat 0 as failure when there
    // was actually content to store.
    if (wrote == 0 && iata != nullptr && iata[0] != '\0') {
        logCfgWriteFailure("writeGlobalIata.putString", kNvsMqtt);
        return false;
    }
    return true;
}

uint16_t readStatusIntervalSec() {
    Preferences p;
    p.begin(kNvsMqtt, /*readOnly=*/true);
    uint16_t v = p.getUShort(kKeyMqttStatusInterval, kDefaultStatusIntervalSec);
    p.end();
    // Clamp to valid range; defends against legacy values outside [10, 3600].
    if (v < kMinStatusIntervalSec) v = kMinStatusIntervalSec;
    if (v > kMaxStatusIntervalSec) v = kMaxStatusIntervalSec;
    return v;
}

bool writeStatusIntervalSec(uint16_t seconds) {
    if (seconds < kMinStatusIntervalSec) seconds = kMinStatusIntervalSec;
    if (seconds > kMaxStatusIntervalSec) seconds = kMaxStatusIntervalSec;
    Preferences p;
    if (!p.begin(kNvsMqtt, /*readOnly=*/false)) {
        logCfgWriteFailure("writeStatusIntervalSec.begin", kNvsMqtt);
        return false;
    }
    size_t wrote = p.putUShort(kKeyMqttStatusInterval, seconds);
    p.end();
    if (wrote != sizeof(uint16_t)) {    // putUShort returns 2 on success, 0 on failure
        logCfgWriteFailure("writeStatusIntervalSec.putUShort", kNvsMqtt);
        return false;
    }
    return true;
}

// #370: the display.* NVS accessors (getDisplayAlwaysOn / setDisplayAlwaysOn /
// getDisplayRotation / setDisplayRotation) moved to
// src/helpers/config/DisplayConfigProvider.cpp. They were role-neutral
// "offband_ui" Preferences wrappers with no broker/mqtt coupling, so they belong
// with the display provider where any role can link them without this schema.

// #182: broker config is stored as individual per-key NVS entries. The #181
// single-blob attempt was reverted: a ~1.1KB blob needs one large contiguous
// allocation that a near-full partition cannot place -- measured on HV4 as
// nvs_set_blob NOT_ENOUGH_SPACE at used=504 free=126 total=630 (the #179 root) --
// whereas small per-key writes slot into fragmented free space. The old migration
// blob key is removed on the next write (writeBrokerConfig) to reclaim its entries.
static constexpr const char* kKeyBrokerBlob = "cfg_blob";

bool readBrokerConfig(uint8_t slot, BrokerConfig& out) {
    if (slot >= OFFBAND_MAX_BROKERS) return false;
    char ns[16];
    mqttBrokerNamespace(slot, ns, sizeof(ns));
    Preferences p;
    p.begin(ns, /*readOnly=*/true);
    // #182: per-key read. A missing key returns its default (empty / sentinel),
    // which is correct because writeBrokerConfig REMOVES empty fields rather than
    // storing blanks. Any #181 migration blob is ignored here and is cleaned up on
    // the next write.
    out.enabled   = p.getBool(kKeyBrokerEnabled, false);
    String url    = prefStr(p, kKeyBrokerUrl);
    strncpy(out.url, url.c_str(), sizeof(out.url));
    out.url[sizeof(out.url) - 1] = '\0';
    out.transport = (BrokerTransport)p.getUChar(kKeyBrokerTransport, (uint8_t)BrokerTransport::Tcp);
    uint16_t default_port;
    switch (out.transport) {
        case BrokerTransport::Tls: default_port = kDefaultTlsPort; break;
        case BrokerTransport::Wss: default_port = kDefaultWssPort; break;
        default:                   default_port = kDefaultTcpPort; break;
    }
    out.port      = p.getUShort(kKeyBrokerPort, default_port);
    out.auth_type = (BrokerAuthType)p.getUChar(kKeyBrokerAuthType, (uint8_t)BrokerAuthType::None);
    String u  = prefStr(p, kKeyBrokerUsername);
    String pw = prefStr(p, kKeyBrokerPassword);
    String jw = prefStr(p, kKeyBrokerJwtToken);
    String tp = prefStr(p, kKeyBrokerTopicPrefix, kDefaultTopicPrefix);
    String io = prefStr(p, kKeyBrokerIataOverride);
    strncpy(out.username,      u.c_str(),  sizeof(out.username));      out.username[sizeof(out.username)-1] = '\0';
    strncpy(out.password,      pw.c_str(), sizeof(out.password));      out.password[sizeof(out.password)-1] = '\0';
    strncpy(out.jwt_token,     jw.c_str(), sizeof(out.jwt_token));     out.jwt_token[sizeof(out.jwt_token)-1] = '\0';
    strncpy(out.topic_prefix,  tp.c_str(), sizeof(out.topic_prefix));  out.topic_prefix[sizeof(out.topic_prefix)-1] = '\0';
    strncpy(out.iata_override, io.c_str(), sizeof(out.iata_override)); out.iata_override[sizeof(out.iata_override)-1] = '\0';
    // Plan 2 v2 additions
    String ja = prefStr(p, kKeyBrokerJwtAudience);
    out.jwt_refresh_sec = p.getULong(kKeyBrokerJwtRefresh, 3600);
    String cc = prefStr(p, kKeyBrokerCaCertName);
    strncpy(out.jwt_audience,  ja.c_str(), sizeof(out.jwt_audience));  out.jwt_audience[sizeof(out.jwt_audience)-1]  = '\0';
    strncpy(out.ca_cert_name,  cc.c_str(), sizeof(out.ca_cert_name));  out.ca_cert_name[sizeof(out.ca_cert_name)-1]  = '\0';
    // #63 additions: JWT identity claims
    String jo = prefStr(p, kKeyBrokerJwtOwner);
    String je = prefStr(p, kKeyBrokerJwtEmail);
    strncpy(out.jwt_owner,     jo.c_str(), sizeof(out.jwt_owner));     out.jwt_owner[sizeof(out.jwt_owner)-1]         = '\0';
    strncpy(out.jwt_email,     je.c_str(), sizeof(out.jwt_email));     out.jwt_email[sizeof(out.jwt_email)-1]         = '\0';
    p.end();
    return true;
}

bool writeBrokerConfig(uint8_t slot, const BrokerConfig& cfg) {
    if (slot >= OFFBAND_MAX_BROKERS) return false;
    char ns[16];
    mqttBrokerNamespace(slot, ns, sizeof(ns));
    Preferences p;
    if (!p.begin(ns, /*readOnly=*/false)) {     // never silent -- report a begin failure
        logCfgWriteFailure("writeBrokerConfig.begin", ns);
        return false;
    }
    // #182: per-key writes (reverted from the #181 blob -- a ~1.1KB blob needs one
    // large contiguous allocation a near-full partition can't place; small per-key
    // writes slot into fragmented free space). The string fields live in a table so
    // they can be processed in two phases (removes, then puts). topic_prefix, the
    // numerics, and enabled are handled separately below.
    const struct { const char* key; const char* val; } kStrFields[] = {
        {kKeyBrokerUrl,          cfg.url},
        {kKeyBrokerUsername,     cfg.username},
        {kKeyBrokerPassword,     cfg.password},
        {kKeyBrokerJwtToken,     cfg.jwt_token},
        {kKeyBrokerIataOverride, cfg.iata_override},
        {kKeyBrokerJwtAudience,  cfg.jwt_audience},
        {kKeyBrokerCaCertName,   cfg.ca_cert_name},
        {kKeyBrokerJwtOwner,     cfg.jwt_owner},
        {kKeyBrokerJwtEmail,     cfg.jwt_email},
    };

    // Phase 1 -- REMOVES first: drop the migration blob + every empty field so their
    // NVS entries become reclaimable BEFORE the value puts. On a near-full partition
    // this lets the puts complete in one shot (GC reclaims the freed entries) instead
    // of erroring on the first attempt -- and stores no blanks either (#182). The
    // removes always land here even if a later put can't, so the space reclaim is
    // what the one-time boot migration (migrateBrokerStorage) relies on.
    p.remove(kKeyBrokerBlob);
    for (const auto& f : kStrFields) {
        if (f.val[0] == '\0') p.remove(f.key);
    }

    // Phase 2 -- value puts, each CHECKED (the #181 SAFELANE-6 honesty is kept).
    // Only non-empty strings; topic_prefix is ALWAYS stored because its read-default
    // is "meshcore", not "" -- removing it would lose an explicit value to the default.
    bool ok = true;
    for (const auto& f : kStrFields) {
        if (f.val[0] != '\0') ok &= (p.putString(f.key, f.val) == strlen(f.val));
    }
    ok &= (p.putString(kKeyBrokerTopicPrefix, cfg.topic_prefix) == strlen(cfg.topic_prefix));
    ok &= (p.putUShort(kKeyBrokerPort,      cfg.port)               == sizeof(uint16_t));
    ok &= (p.putUChar (kKeyBrokerTransport, (uint8_t)cfg.transport) == sizeof(uint8_t));
    ok &= (p.putUChar (kKeyBrokerAuthType,  (uint8_t)cfg.auth_type) == sizeof(uint8_t));
    ok &= (p.putULong (kKeyBrokerJwtRefresh, cfg.jwt_refresh_sec)   == sizeof(uint32_t));

    // Phase 3 -- enabled LAST, and only if every value put succeeded. enabled is the
    // gate that decides whether the broker goes live; writing it last means a failed
    // write leaves the enable/disable state UNCHANGED, so the slot's live state stays
    // consistent with the returned ERROR (a failed write never silently flips it).
    if (ok) ok &= (p.putBool(kKeyBrokerEnabled, cfg.enabled) == sizeof(uint8_t));
    p.end();

    if (!ok) {     // a put failed -- surface the cause (free-entry stats) + don't ACK success
        logCfgWriteFailure("writeBrokerConfig.put", ns);
        return false;
    }
    return true;
}

// #182: one-time boot migration so an UPGRADED observer reclaims NVS space behind
// the scenes -- the user never sees the interactive "first write errors, then
// works", because the reclaim already happened here at boot. Gated by a schema
// version flag so it runs exactly once; idempotent (re-running on a clean slot is
// cheap). For each configured slot it rewrites the config into the per-key /
// no-blank format; writeBrokerConfig's REMOVES phase frees that slot's blanks even
// if its puts can't all complete on a near-full partition, so the reclaim always
// lands. A retry completes the rewrite once the first pass has freed enough.
// Old values are preserved on a failed put, so a partial migration never loses
// config. Call once at observer startup, after populateDefaultBrokers().
void migrateBrokerStorage() {
    Preferences obs;
    if (obs.begin(kNvsObserver, /*readOnly=*/true)) {
        uint8_t ver = obs.getUChar(kKeyCfgSchema, 0);
        obs.end();
        if (ver >= kCfgSchemaVersion) return;   // already migrated
    }

    uint8_t migrated = 0, failed = 0;
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
        BrokerConfig cfg;
        if (!readBrokerConfig(slot, cfg)) continue;
        if (cfg.url[0] == '\0') continue;        // unconfigured slot -- nothing to migrate
        // Retry once: the first write frees the slot's blanks, so a retry fits if the
        // first couldn't fully complete (writeBrokerConfig self-logs any failure).
        if (writeBrokerConfig(slot, cfg) || writeBrokerConfig(slot, cfg)) migrated++;
        else failed++;
    }

    // Stamp the schema version so this runs exactly once. Even if a slot's rewrite
    // didn't fully complete, its blanks were reclaimed (removes land first) and its
    // old values are preserved, so a later interactive write finishes the job in one
    // shot -- the user never hits the first-write error.
    Preferences w;
    if (w.begin(kNvsObserver, /*readOnly=*/false)) {
        w.putUChar(kKeyCfgSchema, kCfgSchemaVersion);
        w.end();
    }

    // #182: the migration is invisible to the user by design, so record that it ran
    // (+ outcome) to the persistent crash-log -- observable for validation + field
    // diagnosis, never user-facing. Real-device only (host has no crashLogf here).
#if defined(ESP_PLATFORM)
    crashLogf("[cfg] migrateBrokerStorage: schema->%u migrated=%u failed=%u",
              (unsigned)kCfgSchemaVersion, (unsigned)migrated, (unsigned)failed);
#else
    (void)migrated; (void)failed;
#endif
}

// #98: clear a broker slot by WIPING its NVS namespace (every key removed), so
// readBrokerConfig returns defaults (empty url) and populateDefaultBrokers
// re-seeds a default slot at the next boot. Writing an empty BrokerConfig is
// NOT sufficient: ESP32 NVS does not reliably clear a key via putString("")
// (the old value persists), which left `mqtt clear` ineffective -- the slot
// re-appeared in `mqtt status` and survived a reboot. Wiping is definitive.
bool clearBrokerConfig(uint8_t slot) {
    if (slot >= OFFBAND_MAX_BROKERS) return false;
    char ns[16];
    mqttBrokerNamespace(slot, ns, sizeof(ns));
    Preferences p;
    if (!p.begin(ns, /*readOnly=*/false)) return false;
    bool ok = p.clear();   // remove ALL keys in this broker's namespace
    p.end();
    return ok;
}

// ---------------------------------------------------------------------------
// populateDefaultBrokers — Plan 2 v2 Task 3 Step 4 (layout revised in #95)
// ---------------------------------------------------------------------------
// Seeds the public-broker registry, but ONLY into slots whose url is currently
// empty (cfg.url[0] == '\0'). User-set or previously-defaulted values are
// never overwritten -- so the new defaults only affect fresh-NVS devices, and
// the function is idempotent across boots. (Because seeding is skip-if-present,
// changing this layout does NOT re-shuffle slots on a device already seeded
// under an older layout; it takes effect only on a fresh NVS.)
//
//   slot 0  OKIMesh mqtt1      mqtt://mqtt1.okimesh.org:1883      tcp / anon   disabled  (#707)
//   slot 1  OKIMesh mqtt2      wss://mqtt2.okimesh.org:9002/mqtt  wss / anon   disabled  (#592)
//   slot 2  MeshMapper         wss://mqtt.meshmapper.net        wss / jwt    disabled
//   slot 3  CoreComms.net      wss://mqtt.corecomms.net         wss / jwt    disabled  (#677)
//   slot 4  Eastmesh.au        wss://mqtt2.eastmesh.au          wss / jwt    disabled
//   slot 5     (MQTT Custom)   left empty for the operator to fill
//
// #317 reseat: the OKI Mesh's own two brokers hold slots 0-1 (mqtt1 was
// formerly labelled "CoreScope Dayton"; mqtt2 is new). LetsMesh-US and
// LetsMesh-EU were DROPPED from the seed to make room -- they remain fully
// supported for operators who add them by hand ("gts-r4" still resolves in
// lookupCaCertPem(), and the bare-host audience note below still applies).
// MeshMapper and Eastme.sh swapped to 2/3; Eastmesh.au moved 5 -> 4.
//
// #592 moved slots 0-1 from plaintext to wss; #707 moved SLOT 0 BACK to
// plaintext (see the block on the slot-0 row). Slot 1 remains wss://:9002/mqtt over
// TLS with a "letsencrypt" CA. Auth stays anonymous (BrokerAuthType::None) --
// TLS secures the transport; there is no MQTT-layer credential, so no JWT
// audience/owner and no username. Unlike the plaintext form, a wss slot cannot
// complete its TLS handshake until the wall clock is sane (NTP/GPS), so it
// reads state=held(no-clock) in `mqtt status` until then -- deferred, not
// failing (#69).
//
// As of #262 NO slot is enabled by default -- a fresh flash must not
// auto-publish to any upstream broker. Slot 0 was formerly the sole
// default-enabled slot (validated live on HV3 -- #42/#48), which made
// out-of-region fresh flashes feed OKIMesh CoreScope tagged as Dayton/HAO.
// The wss brokers stay disabled until the operator opts in; their ca_cert
// names ("letsencrypt", "isrg-x2" -- plus "gts-r4" for a hand-added LetsMesh)
// all resolve in MqttBroker.cpp's lookupCaCertPem(), and while disabled they
// never connect anyway.
//
// JWT identity (#95 / #63 / #68) -- the seed deliberately leaves username,
// jwt_owner, jwt_email EMPTY; the connect-time auth layer fills what it can:
//   * The MQTT CONNECT username is auto-built at connect as
//     "v1_<UPPERCASE hex(device pubkey)>" (MqttAuth.cpp); it is NOT read from
//     the config for JWT brokers, so seeding it would be dead config.
//   * jwt_owner -- RESOLVED in #95: defaults to THIS device's own pubkey
//     (owner==device, the verified-working convention) at connect, in
//     MqttAuthJwt::apply(). Applied at connect (not seeded) so it covers every
//     JWT slot on every device, fresh NVS or not. The seed leaves it empty so
//     that default takes effect; `set mqtt.broker.<N>.jwt_owner <64-hex>` still
//     overrides per slot if a broker ever needs a different owner.
//   * jwt_email is the operator's email claim -- optional, not device-derivable;
//     set via `set mqtt.broker.<N>.jwt_email` only if a broker requires it.
//
// Audiences are the BARE host (e.g. "mqtt-us-v1.letsmesh.net"), NOT the
// scheme-qualified "https://..." form: LetsMesh validates the "aud" claim
// strictly and rejects the scheme form; CoreComms.net is lenient (#95, verified
// live 2026-06-11 as Eastme.sh -- the service rebranded, #677 / PR #282).
//
// Invoke once at WifiObserver::begin() before MqttBrokerPool::begin().

namespace {
struct DefaultBrokerSpec {
    bool             enabled;
    const char*      url;
    BrokerTransport  transport;
    uint16_t         port;
    BrokerAuthType   auth_type;
    const char*      jwt_audience;   // "" when not JWT
    const char*      ca_cert_name;   // "" when no TLS cert (tcp)
};

// Slots 0-4. Slot 5 (MQTT Custom) is intentionally absent so it stays empty.
// jwt_audience is the BARE host (#95); ca_cert names resolve in MqttBroker.cpp.
constexpr DefaultBrokerSpec kDefaultBrokerSpecs[] = {
    // SLOT 0 IS PLAINTEXT BY DESIGN -- DO NOT "UPGRADE" IT TO wss/TLS.
    // A tcp broker holds no mbedTLS context, so the rotation scheduler exempts
    // it entirely: rotateTlsIfDue() never selects it as a victim and its
    // budget_ok is unconditionally true. That exemption is the ONLY thing
    // making slot 0 an always-on primary. #592 moved this row to wss and
    // silently demoted it into the rotating TLS pool, where it was evicted
    // every dwell (measured on HV3: 178 evictions in a 6.9h soak) -- the
    // always-on property was lost without a single scheduler line changing.
    // The trade is explicit: this feed is unencrypted in exchange for being
    // the one broker that never rotates out. Slots 1-5 carry TLS and share
    // the OFFBAND_MAX_LIVE_TLS budget by rotation.
    {false, "mqtt://mqtt1.okimesh.org:1883",          BrokerTransport::Tcp, 1883, BrokerAuthType::None, "",                        ""},
    {false, "wss://mqtt2.okimesh.org:9002/mqtt",      BrokerTransport::Wss, 9002, BrokerAuthType::None, "",                        "letsencrypt"},
    {false, "wss://mqtt.meshmapper.net:443/mqtt",     BrokerTransport::Wss, 443,  BrokerAuthType::Jwt,  "mqtt.meshmapper.net",     "isrg-x2"},
    // #677: Eastme.sh rebranded to CoreComms.net (re-implements external PR
    // #282 with credit). Both rows' chains verified live 2026-08-13: they now
    // terminate at GTS Root R4, not ISRG X1 -- the old "letsencrypt" pin on
    // eastmesh.au no longer validates at all.
    {false, "wss://mqtt.corecomms.net:443/mqtt",      BrokerTransport::Wss, 443,  BrokerAuthType::Jwt,  "mqtt.corecomms.net",      "gts-r4"},
    {false, "wss://mqtt2.eastmesh.au:443/mqtt",       BrokerTransport::Wss, 443,  BrokerAuthType::Jwt,  "mqtt2.eastmesh.au",       "gts-r4"},
};
constexpr uint8_t kNumDefaultBrokers =
    sizeof(kDefaultBrokerSpecs) / sizeof(kDefaultBrokerSpecs[0]);
}  // namespace

void populateDefaultBrokers() {
    bool all_ok = true;
    // Seed a NON-geographic placeholder IATA ("XYZ") if unset -- #262. A fresh
    // flash must not masquerade as a real region; HAO (Dayton/SWOH) was the old
    // default and mislabeled out-of-region devices. The operator sets their real
    // region via the global `mqtt iata`.
    char iata[8] = {0};
    if (!readGlobalIata(iata, sizeof(iata)) || iata[0] == '\0') {
        all_ok &= writeGlobalIata("XYZ");
    }

    for (uint8_t slot = 0;
         slot < kNumDefaultBrokers && slot < OFFBAND_MAX_BROKERS; ++slot) {
        BrokerConfig cur;
        readBrokerConfig(slot, cur);
        if (cur.url[0] != '\0') {
            // Slot already has a URL (user-set or previously-defaulted) -- skip.
            continue;
        }
        const DefaultBrokerSpec& spec = kDefaultBrokerSpecs[slot];
        BrokerConfig def;  // default-constructed sentinel values
        def.enabled   = spec.enabled;
        strncpy(def.url, spec.url, sizeof(def.url)); def.url[sizeof(def.url)-1] = '\0';
        def.transport = spec.transport;
        def.port      = spec.port;
        def.auth_type = spec.auth_type;
        strncpy(def.jwt_audience, spec.jwt_audience, sizeof(def.jwt_audience)); def.jwt_audience[sizeof(def.jwt_audience)-1] = '\0';
        def.jwt_refresh_sec = 3600;
        strncpy(def.ca_cert_name, spec.ca_cert_name, sizeof(def.ca_cert_name)); def.ca_cert_name[sizeof(def.ca_cert_name)-1] = '\0';
        strncpy(def.topic_prefix, kDefaultTopicPrefix, sizeof(def.topic_prefix)); def.topic_prefix[sizeof(def.topic_prefix)-1] = '\0';
        // username + password + jwt_token + jwt_owner + jwt_email +
        // iata_override stay empty by design (see header): the CONNECT username
        // is auto-built at connect, and jwt_owner/jwt_email are the operator's
        // identity claims, set via `set mqtt.broker.<N>.jwt_owner|jwt_email`
        // (#63). NOT device-derivable here -- see header comment (#95).
        all_ok &= writeBrokerConfig(slot, def);
    }
    // #181: each failed write already self-logged its NVS cause + free-entry
    // stats; surface a single boot-time summary so an incomplete default-seed
    // is never silent (SAFELANE 6). Boot-time best-effort: a failed seed retries
    // on the next boot (populateDefaultBrokers is idempotent / skip-if-present).
    if (!all_ok) {
        logCfgWriteFailure("populateDefaultBrokers", "(default-seed: 1+ write failed)");
    }
}

#endif  // ARDUINO

// ---------------------------------------------------------------------------
// formatBrokerConfig — #98 (mqtt view <N>) + reusable by #96 (export)
// ---------------------------------------------------------------------------
// Pure renderer (no NVS/Arduino deps) so it compiles + host-tests on both the
// device and the bench harness. Lives outside the ARDUINO guard.
//
// Small enum->name helpers are file-local here; ObserverCli.cpp has its own
// transportStr/authStr for its status line (minor dup, noted for #97).
namespace {
const char* brokerTransportName(BrokerTransport t) {
    switch (t) {
        case BrokerTransport::Tcp: return "tcp";
        case BrokerTransport::Tls: return "tls";
        case BrokerTransport::Wss: return "wss";
        default:                   return "?";
    }
}
const char* brokerAuthName(BrokerAuthType a) {
    switch (a) {
        case BrokerAuthType::None:  return "none";
        case BrokerAuthType::Basic: return "basic";
        case BrokerAuthType::Jwt:   return "jwt";
        default:                    return "?";
    }
}
}  // namespace

// Bounded append: only writes if room remains; a field that would overflow is
// simply skipped (graceful truncation -- never overruns out_size).
#define CW_VIEW_APPEND(...) do {                                       \
    if (n >= 0 && (size_t)n < out_size) {                             \
        int added = snprintf(out + n, out_size - (size_t)n, __VA_ARGS__); \
        if (added > 0) n += added;                                    \
    }                                                                 \
} while (0)

size_t formatBrokerConfig(uint8_t slot, const BrokerConfig& cfg,
                          char* out, size_t out_size) {
    if (out == nullptr || out_size == 0) return 0;
    out[0] = '\0';
    int n = 0;

    // Field ORDER matches the operator's familiar layout: url, port, transport,
    // auth_type, username, jwt_audience, jwt_owner, jwt_email, jwt_refresh,
    // ca_cert, iata. PACKED so the whole view stays <= 6 lines: the observer
    // _sys channel sends each reply LINE as its own frame through an 8-deep ring
    // buffer that DROPS THE OLDEST when full (kSystemChannelQueueDepth,
    // SystemChannelCli.h), and interceptMsg enqueues a reply's lines
    // synchronously -- a reply longer than the queue silently loses its EARLIEST
    // lines. The header avoids a leading "<word>: " because the MeshCore
    // companion renders "<word>: <text>" as sender:message (which mangled the
    // old "mqtt.broker.N:" header). JWT fields render only for jwt brokers.
    const bool is_jwt = (cfg.auth_type == BrokerAuthType::Jwt);

    CW_VIEW_APPEND("mqtt.broker.%u  %s\n",
                   (unsigned)slot, cfg.enabled ? "ENABLED" : "disabled");
    CW_VIEW_APPEND("url=%s\n", cfg.url[0] ? cfg.url : "(unset)");
    CW_VIEW_APPEND("port=%u transport=%s auth_type=%s\n",
                   (unsigned)cfg.port,
                   brokerTransportName(cfg.transport),
                   brokerAuthName(cfg.auth_type));

    if (is_jwt) {
        // username auto-derives at connect (v1_+pubkey) when unset.
        CW_VIEW_APPEND("username=%s jwt_audience=%s\n",
                       cfg.username[0] ? cfg.username : "auto(v1_+pubkey)",
                       cfg.jwt_audience[0] ? cfg.jwt_audience : "(unset)");
        // jwt_owner: explicit value, else the connect-time default = this
        // device's own pubkey (#95). Own line (64 hex).
        CW_VIEW_APPEND("jwt_owner=%s\n",
                       cfg.jwt_owner[0] ? cfg.jwt_owner : "auto(device-pubkey)");
        CW_VIEW_APPEND("jwt_email=%s jwt_refresh=%u ca_cert=%s iata=%s\n",
                       cfg.jwt_email[0]     ? cfg.jwt_email     : "(unset)",
                       (unsigned)cfg.jwt_refresh_sec,
                       cfg.ca_cert_name[0]  ? cfg.ca_cert_name  : "(none)",
                       cfg.iata_override[0] ? cfg.iata_override : "(global)");
    } else if (cfg.auth_type == BrokerAuthType::Basic) {
        // password redacted to set/unset -- never the value (CLAUDE.md).
        CW_VIEW_APPEND("username=%s password=%s\n",
                       cfg.username[0] ? cfg.username : "(unset)",
                       cfg.password[0] ? "(set)" : "(unset)");
        CW_VIEW_APPEND("ca_cert=%s iata=%s\n",
                       cfg.ca_cert_name[0]  ? cfg.ca_cert_name  : "(none)",
                       cfg.iata_override[0] ? cfg.iata_override : "(global)");
    } else {
        // None (anon, e.g. CoreScope): no credentials.
        CW_VIEW_APPEND("ca_cert=%s iata=%s\n",
                       cfg.ca_cert_name[0]  ? cfg.ca_cert_name  : "(none)",
                       cfg.iata_override[0] ? cfg.iata_override : "(global)");
    }

    // snprintf returns the would-be length, so on truncation n can exceed
    // out_size; report the ACTUAL bytes written (== strlen(out)), capped.
    if (n < 0) return 0;
    return ((size_t)n >= out_size) ? (out_size - 1) : (size_t)n;
}

#undef CW_VIEW_APPEND

}  // namespace offband
