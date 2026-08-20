// src/helpers/wifi_observer/MqttBroker.cpp
//
// Plan 2 v2 Task 7. esp_mqtt_client wrapper with pluggable auth.

#include "MqttBroker.h"
#include "WifiObserverConfig.h"  // #171: OFFBAND_TLS_HEAP_FLOOR_BYTES
#include <cstring>
#include <cstdio>
#include <ctime>          // #69: std::time() for the wall-clock sanity gate

#ifdef ARDUINO
  #include <Arduino.h>
  #include "MqttCaCerts.h"  // embedded CA PEMs (Plan 1 vendored, retained)
#endif

namespace offband {

#if defined(ARDUINO) && defined(ESP_PLATFORM)
namespace {
// RAII guard for the per-broker recursive client mutex. Take on
// construction, give on destruction -- covers all early-return paths
// (#53).
struct ClientLockGuard {
    SemaphoreHandle_t m_;
    explicit ClientLockGuard(SemaphoreHandle_t m) : m_(m) {
        if (m_ != nullptr) xSemaphoreTakeRecursive(m_, portMAX_DELAY);
    }
    ~ClientLockGuard() { if (m_ != nullptr) xSemaphoreGiveRecursive(m_); }
    ClientLockGuard(const ClientLockGuard&) = delete;
    ClientLockGuard& operator=(const ClientLockGuard&) = delete;
};
}  // namespace
#endif

// ---------------------------------------------------------------------------
// Backoff schedule -- shared free function.
// ---------------------------------------------------------------------------
uint32_t brokerBackoffMs(uint32_t retry_count) {
    static const uint32_t kSchedule[] = {5000, 15000, 30000, 60000, 120000};
    const size_t n = sizeof(kSchedule) / sizeof(kSchedule[0]);
    return retry_count < n ? kSchedule[retry_count] : kSchedule[n - 1];
}

// #69: the wall clock is "sane" (SNTP or BLE has set real time) once it is past
// this epoch. Below it -- e.g. the build-time placeholder of May 2024 -- TLS
// server certs read "not yet valid" and JWT iat/exp are garbage, so a wss/TLS
// connect only burns backoff cycles. Threshold = 2025-01-01 UTC.
static bool wallClockSane() {
    return std::time(nullptr) > 1735689600;
}

// #171: the TLS heap budget is "ok" when free heap is above the floor -- a
// catastrophe backstop to the pool's count cap (OFFBAND_MAX_LIVE_TLS) so a
// wss/TLS handshake (transiently ~60KB) can't be started into an OOM. Host
// builds have no esp heap, so they always pass.
static bool tlsHeapBudgetOk() {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    return ESP.getFreeHeap() >= OFFBAND_TLS_HEAP_FLOOR_BYTES;
#else
    return true;
#endif
}

// ---------------------------------------------------------------------------
// CA cert lookup table.
// ---------------------------------------------------------------------------
// MqttCaCerts.h embeds the ISRG Root X1 cert under the name
// kEastmeshIsrgRootX1Pem (the symbol keeps its EastMesh-era name).
//
// Naming convention exposed to operators: "letsencrypt", "eastmesh"
// (legacy alias), or "isrg-x1" (alias) all map to ISRG Root X1 (Let's
// Encrypt RSA). "isrg-x2" = Let's Encrypt ECDSA root (MeshMapper);
// "gts-r4" = Google Trust Services Root R4, which as of #677 covers the
// vendored wss/jwt brokers -- CoreComms.net (nee EastMesh), Eastmesh.au,
// and LetsMesh all chain to GTS Root R4 (verified live 2026-08-13). New
// brokers add a name here + a PEM in MqttCaCerts.h. #48 Item 2.
const char* lookupCaCertPem(const char* name) {
    if (name == nullptr || name[0] == '\0') return nullptr;
#ifdef ARDUINO
    if (strcmp(name, "letsencrypt") == 0) return mqtt_ca_certs::kEastmeshIsrgRootX1Pem;
    if (strcmp(name, "eastmesh")    == 0) return mqtt_ca_certs::kEastmeshIsrgRootX1Pem;
    if (strcmp(name, "isrg-x1")     == 0) return mqtt_ca_certs::kEastmeshIsrgRootX1Pem;
    if (strcmp(name, "isrg_root_x1")== 0) return mqtt_ca_certs::kEastmeshIsrgRootX1Pem;
    if (strcmp(name, "isrg-x2")     == 0) return mqtt_ca_certs::kIsrgRootX2Pem;
    if (strcmp(name, "gts-r4")      == 0) return mqtt_ca_certs::kGtsRootR4Pem;
#else
    (void)name;
#endif
    return nullptr;  // unknown name -- caller treats as missing cert
}

// ---------------------------------------------------------------------------
// populateBaseConfig -- the ONE place transport-level esp_mqtt fields are set.
// ---------------------------------------------------------------------------
// #506: esp-mqtt defaults the MQTT keepalive to 120 s, but a broker can cap it
// via max_keepalive and REJECT an over-limit CONNECT with a MISLEADING CONNACK
// 0x02 "identifier rejected" (confirmed live against a broker with
// max_keepalive 60 -- a paho client is accepted at k=60, rejected at k=120,
// identical to this firmware). 60 s is accepted by capped and uncapped brokers
// alike; OWNER RULING 2026-08-02: it stays a FIXED 60 (#516, per-broker
// configurability, is deferred and must not be gated on).
//
// #532: this is called from ALL THREE config build sites -- begin() and the two
// JWT-refresh paths in tryConnect()/loop(). The refresh paths previously set
// only uri+cert and pushed that through esp_mqtt_set_config(), leaving keepalive
// at the 120 default, so a JWT broker behind a cap silently reverted after a
// token refresh. Any new field that must survive a refresh belongs HERE, not in
// an individual call site.
#if defined(ARDUINO) && defined(ESP_PLATFORM)
void MqttBroker::populateBaseConfig(esp_mqtt_client_config_t& mqcfg) const {
    const int keepalive_sec = 60;
#if ESP_IDF_VERSION_MAJOR >= 5
    mqcfg.broker.address.uri = cfg_.url;
    mqcfg.session.keepalive  = keepalive_sec;
    if (cfg_.transport == BrokerTransport::Tls ||
        cfg_.transport == BrokerTransport::Wss) {
        const char* pem = lookupCaCertPem(cfg_.ca_cert_name);
        if (pem != nullptr) mqcfg.broker.verification.certificate = pem;
    }
#else
    mqcfg.uri       = cfg_.url;
    mqcfg.keepalive = keepalive_sec;
    if (cfg_.transport == BrokerTransport::Tls ||
        cfg_.transport == BrokerTransport::Wss) {
        const char* pem = lookupCaCertPem(cfg_.ca_cert_name);
        if (pem != nullptr) mqcfg.cert_pem = pem;
    }
#endif
}
#endif

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
MqttBroker::~MqttBroker() {
    shutdown();
}

void MqttBroker::initLock() {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (client_lock_ == nullptr) {
        client_lock_ = xSemaphoreCreateRecursiveMutex();
    }
#endif
}

// #175: connection teardown ONLY -- no deconfigure. shutdown() is this plus the
// #98 slot wipe; TLS rotation uses this directly so the victim stays configured
// (and therefore visible to the pool loops, which all gate on isConfigured()).
void MqttBroker::releaseClient(bool keep_failed) {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    ClientLockGuard _g(client_lock_);
    if (client_ != nullptr) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }
    started_ = false;  // LoRa#327: client gone -> next start() is a fresh first start
#endif
    if (auth_ != nullptr) {
        delete auth_;
        auth_ = nullptr;
    }
    // #848 Facet A: the reaper reaps a TERMINALLY-FAILED broker's client but must
    // PRESERVE the Failed state -- a plain rt_ = {} resets it to Down, which is the
    // real cause of #823 (the broker looks Down, gets re-promoted, re-fails, is
    // reaped again -> churn, and each heavy teardown risks the Facet-B overflow).
    // With state kept Failed, the pool's Failed-guards (tryConnect/reconcileSlot)
    // hold it out of rotation until an operator reconfigure clears it. Only an
    // actual reap of a Failed broker preserves; every other caller (rotation) still
    // gets the full reset to Down.
    if (keep_failed && rt_.state == BrokerState::Failed) {
        rt_ = BrokerRuntimeState{};
        rt_.state = BrokerState::Failed;
    } else {
        rt_ = BrokerRuntimeState{};
    }
    budget_hold_.onReleased();  // #720: the budget slot is freed on either path;
                                // the next acquisition stamps a fresh hold clock.
    // slot_ and cfg_ deliberately RETAINED -- that is the whole difference from
    // shutdown(). A rotated-out broker must stay configured so the pool can see
    // it, honor its cooldown, and promote it back via reconcileSlot->tryConnect.
}

void MqttBroker::shutdown() {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    // Recursive mutex: releaseClient() re-takes it. Holding it across the
    // slot_/cfg_ wipe preserves the original single-critical-section behaviour.
    ClientLockGuard _g(client_lock_);
#endif
    releaseClient();   // client + auth teardown + rt_ reset
    // #98: reset to UNCONFIGURED (slot_ = 0xFF) so isConfigured() goes false and
    // `mqtt status` drops the slot immediately after a clear/empty reload, rather
    // than showing its stale cached cfg_ until the next reboot. begin() re-sets
    // slot_ + cfg_ for any (re)configured slot, so this is safe on the re-init
    // path. Protected by the reconciling_[] guard (loopTask skips a slot mid-
    // reconcile), same as the rt_ reset above.
    slot_ = 0xFF;
    cfg_  = BrokerConfig{};
}

#ifdef ARDUINO
bool MqttBroker::begin(uint8_t slot, const BrokerConfig& cfg,
                       const mesh::LocalIdentity& identity,
                       bool may_alloc_client) {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    ClientLockGuard _g(client_lock_);
#endif
    shutdown();  // idempotent re-init
    if (cfg.url[0] == '\0') {
        return false;  // disabled-by-config
    }
    slot_ = slot;
    cfg_  = cfg;
    // #53: a DISABLED slot is "configured" (status reflects cfg_) but holds
    // NO live client/auth -- the esp_mqtt client exists only while enabled.
    // Enabling later routes through reloadSlot()->begin() which creates it.
    if (!cfg.enabled) {
        rt_ = BrokerRuntimeState{};
        return true;
    }
    // #534: TLS/wss allocation admission. When the pool already holds its full
    // quota of TLS clients, store the config and stay CONFIGURED BUT CLIENT-LESS
    // rather than allocating a ~18KB client this broker cannot use. HeldNoHeap
    // marks it as deferred-not-failed (same semantics tryConnect uses), so the
    // pool's re-drive promotes it via reconcileSlot() once budget frees. NOT a
    // failure: return true. tcp brokers hold no mbedTLS context and are exempt.
    if (!may_alloc_client &&
        (cfg.transport == BrokerTransport::Tls ||
         cfg.transport == BrokerTransport::Wss)) {
        rt_ = BrokerRuntimeState{};
        rt_.state = BrokerState::HeldBudget;   // #715: budget, not heap
        return true;
    }
    auth_ = makeAuth(cfg, identity);
    if (auth_ == nullptr) {
        return false;  // bad auth config (e.g., Jwt without audience)
    }

#if defined(ESP_PLATFORM)
    esp_mqtt_client_config_t mqcfg = {};
    populateBaseConfig(mqcfg);   // #532: uri + keepalive + CA cert

    // Apply auth strategy (sets username + password/token fields).
    if (!auth_->apply(mqcfg, /*now_ms=*/0)) {
        // Auth setup failed (e.g., JWT mint error). Leave broker Down.
        rt_.state = BrokerState::Down;
        rt_.last_error_class = BrokerErrorClass::Auth;
        return false;
    }

    client_ = esp_mqtt_client_init(&mqcfg);
    if (client_ == nullptr) {
        rt_.state = BrokerState::Down;
        rt_.last_error_class = BrokerErrorClass::Other;
        return false;
    }
    // ESP_EVENT_ANY_ID is `-1` (int); cast to esp_mqtt_event_id_t to
    // satisfy esp_mqtt_client_register_event's stricter signature.
    esp_mqtt_client_register_event(client_,
                                   (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   &MqttBroker::eventHandler, this);
#endif  // ESP_PLATFORM

    rt_.state = BrokerState::Down;  // Down until tryConnect
    return true;
}
#else
bool MqttBroker::begin(uint8_t slot, const BrokerConfig& cfg) {
    shutdown();
    if (cfg.url[0] == '\0') return false;
    slot_ = slot;
    cfg_  = cfg;
    // Host build: no esp_mqtt, no identity -> auth set to None for shape.
    auth_ = new MqttAuthNone();
    rt_.state = BrokerState::Down;
    return true;
}
#endif

// ---------------------------------------------------------------------------
// tryConnect: state machine entry point. Initiates a connection attempt
// if the broker is eligible (enabled + Down + backoff window expired).
// ---------------------------------------------------------------------------
bool MqttBroker::tryConnect(uint32_t now_ms, bool tls_budget_ok) {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    ClientLockGuard _g(client_lock_);
#endif
    if (!isConfigured() || !cfg_.enabled) {
        rt_.state = BrokerState::Down;
        return false;
    }
    if (rt_.state == BrokerState::Connecting || rt_.state == BrokerState::Up) {
        return true;
    }
    // #739: Failed is terminal -- give up until the operator re-enables/reconfigures.
    // Never burns a rotation turn or a connect attempt.
    if (rt_.state == BrokerState::Failed) {
        return false;
    }
    if (rt_.state == BrokerState::Backoff) {
        uint32_t elapsed = now_ms - rt_.last_attempt_ms;
        // #739: backoff length tracks the STICKY penalty, not retry_count (which a
        // single success zeroes). A flaky broker escalates instead of oscillating
        // at the schedule floor.
        if (elapsed < brokerBackoffMs(health_.fail_penalty)) {
            return false;
        }
    }

    // #69: wss/TLS brokers can't complete the TLS handshake until the wall clock
    // is valid (server certs read "not yet valid"; JWT iat/exp would be wrong
    // too). Hold off -- without burning a backoff cycle -- until SNTP sets a sane
    // clock; we retry on the next drive tick. tcp brokers are unaffected.
    if ((cfg_.transport == BrokerTransport::Tls ||
         cfg_.transport == BrokerTransport::Wss) && !wallClockSane()) {
        // #87: surface the clock-hold as a distinct state so `mqtt status` reads
        // "held(no-clock)" instead of "down"/"backoff" (which look like a failure).
        // No retry_count bump -- nothing was attempted. The pool keeps driving
        // tryConnect for HeldNoClock slots, so this releases on the first tick
        // after wallClockSane() becomes true (NTP sync or GPS lock).
        rt_.state = BrokerState::HeldNoClock;
        return false;
    }

    // #171: each wss/TLS broker holds a ~60KB mbedTLS context, and a bring-up's
    // handshake transient can drive HV3's heap to an OOM reboot. Defer -- without
    // burning a backoff cycle -- when the pool's TLS budget is full
    // (tls_budget_ok=false: already OFFBAND_MAX_LIVE_TLS live) or free heap is
    // below the floor. The pool re-drives HeldNoHeap slots each tick, so this
    // releases the moment a TLS slot frees or heap recovers (mirrors the
    // HeldNoClock hold above). tcp brokers hold no mbedTLS context -- exempt.
    if ((cfg_.transport == BrokerTransport::Tls ||
         cfg_.transport == BrokerTransport::Wss) &&
        (!tls_budget_ok || !tlsHeapBudgetOk())) {
        // #715: report WHICH condition deferred us. Budget-full is normal rotation
        // (this broker is queued); below-floor heap is real resource pressure.
        // Budget is checked first: when both are true the operator-actionable fact
        // is the heap floor, so heap wins only when the budget was actually free.
        rt_.state = tls_budget_ok ? BrokerState::HeldNoHeap    // budget free -> heap
                                  : BrokerState::HeldBudget;   // waiting its turn
        return false;
    }

    rt_.last_attempt_ms = now_ms;
    rt_.state = BrokerState::Connecting;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (client_ == nullptr) {
        rt_.state = BrokerState::Down;
        return false;
    }
    // For JWT, re-apply if needsRefresh -- the apply may have been at init
    // time and the token may have aged out before first connect attempt.
    if (auth_ != nullptr && auth_->needsRefresh(now_ms)) {
        esp_mqtt_client_config_t mqcfg = {};
        // #532: MUST include keepalive -- this config is pushed via
        // esp_mqtt_set_config() below, and omitting it reverted a capped broker
        // to esp-mqtt's default 120 (-> CONNACK 0x02, #506).
        populateBaseConfig(mqcfg);
        if (!auth_->apply(mqcfg, now_ms)) {
            rt_.retry_count++;
            rt_.last_error_class = BrokerErrorClass::Auth;
            escalateFailureOnce(now_ms);   // #739/#838
            return false;
        }
        esp_mqtt_set_config(client_, &mqcfg);
    }
    // LoRa#327: pair start with a preceding stop on retries. esp-mqtt expects a
    // single start(); re-calling esp_mqtt_client_start() on an already-started
    // client (after a dropped/failed connection that left it in Backoff) leaks
    // the transport + mbedTLS allocations -- ~68KB bled to exhaustion in the
    // field, which then starves WiFi/lwIP and takes the whole device down.
    // Stop here in the pool-task context, NOT in onDisconnected: that runs in
    // the mqtt event-handler/task context, where stop() self-joins the calling
    // task and deadlocks. Each attempt is now a clean stop->start cycle; the
    // JWT was already re-applied above so the fresh start uses a current token.
    if (started_) {
        esp_mqtt_client_stop(client_);
        started_ = false;
        // LoRa#327 (Gemini review): re-assert Connecting AFTER stop(). stop()
        // joins the old mqtt task, so by here a late onDisconnected from the
        // dying task can no longer fire and clobber state to Backoff while we
        // are actually reconnecting (which would cause reconnect stutter).
        rt_.state = BrokerState::Connecting;
    }
    esp_err_t err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        rt_.retry_count++;
        rt_.last_error_class = BrokerErrorClass::Other;
        escalateFailureOnce(now_ms);   // #739/#838
        return false;
    }
    started_ = true;
#endif
    return true;
}

// ---------------------------------------------------------------------------
// loop: drive auth refresh + housekeeping. Connection lifecycle is event-
// driven via eventHandler, so this is mostly a token-refresh poke point.
// ---------------------------------------------------------------------------
void MqttBroker::loop(uint32_t now_ms) {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    ClientLockGuard _g(client_lock_);
#endif
    if (!isConfigured() || auth_ == nullptr) return;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    // While Up, periodically check whether the JWT token has aged out.
    // If so, re-apply (which re-mints) and push the new credential into
    // the client. esp_mqtt will use it on the next reconnect cycle.
    if (rt_.state == BrokerState::Up && client_ != nullptr) {
        if (auth_->needsRefresh(now_ms)) {
            esp_mqtt_client_config_t mqcfg = {};
            // #532: same reassert requirement as the tryConnect() path -- this is
            // the mid-session JWT refresh, where a silent revert to 120 would only
            // surface on the NEXT reconnect, making it especially hard to trace.
            populateBaseConfig(mqcfg);
            if (auth_->apply(mqcfg, now_ms)) {
                esp_mqtt_set_config(client_, &mqcfg);
            }
        }
    }
#else
    (void)now_ms;
#endif
}

// ---------------------------------------------------------------------------
// publish
// ---------------------------------------------------------------------------
bool MqttBroker::publish(const char* topic, const uint8_t* payload, size_t len,
                         bool retain) {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    ClientLockGuard _g(client_lock_);
#endif
    if (rt_.state != BrokerState::Up) return false;
    if (topic == nullptr || payload == nullptr) return false;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (client_ == nullptr) return false;
    int rc = esp_mqtt_client_enqueue(client_, topic, (const char*)payload, (int)len,
                                     /*qos=*/1, retain ? 1 : 0, /*store=*/true);
    if (rc < 0) {
        return false;
    }
    rt_.last_publish_ms = millis();
    return true;
#else
    (void)topic; (void)payload; (void)len; (void)retain;
    return false;  // host stub
#endif
}

// ---------------------------------------------------------------------------
// fillPayloadCtx
// ---------------------------------------------------------------------------
void MqttBroker::fillPayloadCtx(MqttPayloadCtx& ctx,
                                const char* global_iata,
                                const char* device_id,
                                const char* node_name,
                                const char* client_version,
                                const char* firmware_version,
                                const char* model) const {
    ctx.iata = (cfg_.iata_override[0] != '\0') ? cfg_.iata_override
                                               : global_iata;
    ctx.device_id        = device_id;
    ctx.node_name        = node_name;
    ctx.topic_prefix     = (cfg_.topic_prefix[0] != '\0') ? cfg_.topic_prefix
                                                          : kDefaultTopicPrefix;
    ctx.client_version   = client_version;
    ctx.firmware_version = firmware_version;
    ctx.model            = model;
}

// ---------------------------------------------------------------------------
// esp_mqtt event handler (ARDUINO + ESP_PLATFORM only)
// ---------------------------------------------------------------------------
#if defined(ARDUINO) && defined(ESP_PLATFORM)

void MqttBroker::eventHandler(void* handler_args,
                              esp_event_base_t /*base*/,
                              int32_t event_id,
                              void* event_data) {
    MqttBroker* self = static_cast<MqttBroker*>(handler_args);
    if (self == nullptr) return;
    uint32_t now_ms = millis();
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            self->onConnected(now_ms);
            break;
        case MQTT_EVENT_DISCONNECTED:
            self->onDisconnected(now_ms, BrokerErrorClass::Tcp);
            break;
        case MQTT_EVENT_ERROR: {
            BrokerErrorClass err = BrokerErrorClass::Other;
            const esp_mqtt_error_codes_t* eh =
                (event != nullptr) ? event->error_handle : nullptr;
            if (eh != nullptr) {
                // Classify by error_type. Connection refused is auth-y;
                // TLS errors get their own class.
                if (eh->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    err = BrokerErrorClass::Auth;
                } else if (eh->esp_tls_last_esp_err != 0 ||
                           eh->esp_tls_stack_err != 0) {
                    err = BrokerErrorClass::Tls;
                } else if (eh->esp_transport_sock_errno != 0) {
                    err = BrokerErrorClass::Tcp;
                }
                // SAFELANE no-silent-failure: surface the ACTUAL reason
                // esp-mqtt/esp-tls reported, not just the coarse class.
                //   sock_errno   -> ECONNREFUSED(111)=refused, EHOSTUNREACH(113/118)
                //                   =unreachable, ETIMEDOUT(110)=timeout (TCP layer)
                //   tls_stack_err+cert_flags -> mbedTLS rejected the chain; cert_flags
                //                   are MBEDTLS_X509_BADCERT_* bits (e.g. a pinned
                //                   X1-only CA that can't build an ECDSA/X2 path)
                //   connack      -> MQTT CONNACK refusal code (a max_keepalive-exceeded
                //                   rejection also surfaces here as connack=2, #506)
                Serial.printf(
                    "[mqtt-err] s%u type=%d sock_errno=%d tls_esp_err=0x%X "
                    "tls_stack_err=-0x%04X cert_flags=0x%08X connack=%d -> class=%d\n",
                    (unsigned)self->slot_, (int)eh->error_type,
                    eh->esp_transport_sock_errno,
                    (unsigned)eh->esp_tls_last_esp_err,
                    (unsigned)(-eh->esp_tls_stack_err),
                    (unsigned)eh->esp_tls_cert_verify_flags,
                    (int)eh->connect_return_code, (int)err);
            } else {
                Serial.printf("[mqtt-err] s%u (no error_handle)\n",
                              (unsigned)self->slot_);
            }
            self->onError(now_ms, err);
            break;
        }
        default:
            // PUBLISHED, SUBSCRIBED, etc. -- no state change needed.
            break;
    }
}

bool MqttBroker::hasClient() const {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    return client_ != nullptr;
#else
    return true;   // host stub: no real client; never force a reconcile
#endif
}

void MqttBroker::onConnected(uint32_t now_ms) {
    // #739/#746: never let a connect event resurrect a terminally-Failed broker.
    // Primary defense is the pool reaping a Failed broker's client (nothing to
    // reconnect); this is belt-and-suspenders for a connect event in flight when
    // the broker went Failed. Failed clears only by operator re-enable/reconfigure.
    if (rt_.state == BrokerState::Failed) return;
    // #720: same belt-and-suspenders as the Failed guard above, for the rotation
    // path. releaseClient() destroys client_ then resets state to Down; a stale
    // MQTT_EVENT_CONNECTED already queued before the teardown can be dispatched
    // afterwards. Without this guard it would set state=Up + stamp budget_hold_
    // with no client behind it -- a "zombie" that occupies the TLS budget slot
    // (rotation would evict it within a dwell, but the inconsistency is real).
    if (!hasClient()) return;
    rt_.state = BrokerState::Up;
    rt_.went_up_ms = now_ms;   // #175: dwell clock for TLS rotation (display/age)
    budget_hold_.onConnected(now_ms);  // #720: eviction clock -- stamped once per
                                       // occupancy, NOT reset by this reconnect
    rt_.retry_count = 0;
    rt_.last_error_class = BrokerErrorClass::None;
    fail_window_.reset();   // #906: a clean connect ends the escalation window,
                            // so a later failure escalates instead of being
                            // masked by the pre-success failure's window.
}

void MqttBroker::onDisconnected(uint32_t now_ms, BrokerErrorClass err) {
    // Move to Backoff (esp_mqtt will not auto-retry; pool's tryConnect
    // honors backoff schedule and re-initiates).
    rt_.last_error_ms = now_ms;
    rt_.retry_count++;
    rt_.last_error_class = err;
    escalateFailureOnce(now_ms);   // #739/#838: escalate; may promote Backoff -> Failed
}

// #838/#906: escalate the health penalty once per failed attempt. The window /
// millis()==0 / reset-on-success logic lives in FailEscalateWindow (header-only,
// host-tested in test/test_fail_escalate_window); see that header for rationale
// and the BEFORE_CONNECT-boolean design that failed on hardware.
void MqttBroker::escalateFailureOnce(uint32_t now_ms) {
    if (!fail_window_.shouldEscalate(now_ms)) return;
    noteFailure();
}

// #739: single choke point for a connection failure. Escalates the health
// penalty (sticky -- a bare later success will not clear it) and picks the
// resulting state: Failed once the penalty passes the terminal threshold,
// otherwise Backoff. Callers have already set last_error_* / retry_count.
void MqttBroker::noteFailure() {
    health_.onFailure();
    rt_.state = health_.isTerminal() ? BrokerState::Failed : BrokerState::Backoff;
}

void MqttBroker::onError(uint32_t now_ms, BrokerErrorClass err) {
    // Record the class so a subsequent DISCONNECTED transition (if one follows)
    // reports a meaningful reason.
    rt_.last_error_ms = now_ms;
    rt_.last_error_class = err;
    // #838: a connect-layer failure (refused/unreachable/connect-timeout) surfaces
    // as MQTT_EVENT_ERROR with NO following DISCONNECTED, so escalation must happen
    // here too -- otherwise a dead broker retries forever and never reaches Failed.
    // escalateFailureOnce()'s window collapses the DISCONNECTED that DOES follow a
    // mid-session TLS/auth failure into this same escalation, so it never
    // double-counts one attempt.
    escalateFailureOnce(now_ms);
}

#endif  // ARDUINO && ESP_PLATFORM

}  // namespace offband
