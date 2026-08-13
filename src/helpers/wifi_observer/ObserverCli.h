// src/helpers/wifi_observer/ObserverCli.h
//
// Plan 2 v2 Task 10: serial CLI commands for mqtt config.
// Single entry point dispatchObserverCli(); CommonCLI calls it from its
// fall-through under #ifdef OFFBAND_OBSERVER.

#pragma once
#include "MqttBrokerPool.h"
#include <stddef.h>

namespace offband {

// Parses + dispatches "mqtt"-prefixed commands. Returns true if handled
// (reply populated with result/error), false if the command wasn't an
// observer command (caller falls through to its own unknown-command path).
//
// Commands handled:
//   mqtt status                                  -- pool summary (live state)
//   mqtt view <N>                                -- full stored config for slot N (secrets redacted)
//   mqtt enable <N>                              -- slot N: enabled=true + reload
//   mqtt disable <N>                             -- slot N: enabled=false + reload
//   mqtt clear <N>                               -- wipe slot N to empty (default slots re-seed at reboot)
//   get mqtt.broker.<N>.<key>                    -- read one stored field (#45; password write-only)
//   set mqtt.iata <code>                         -- global IATA
//   set mqtt.status_interval <sec>               -- 10..3600
//   set mqtt.broker.<N>.url <uri>
//   set mqtt.broker.<N>.port <port>
//   set mqtt.broker.<N>.transport <tcp|tls|wss>
//   set mqtt.broker.<N>.auth_type <none|basic|jwt>
//   set mqtt.broker.<N>.username <s>
//   set mqtt.broker.<N>.password <s>             -- reply elides value
//   set mqtt.broker.<N>.jwt_audience <url>
//   set mqtt.broker.<N>.jwt_refresh <60..86400>  -- re-mint interval (sec)
//   set mqtt.broker.<N>.jwt_owner <64-hex>       -- #63 JWT "owner" claim; "" clears
//   set mqtt.broker.<N>.jwt_email <s>            -- #63 JWT "email" claim; "" clears
//   set mqtt.broker.<N>.iata_override <code>
//   set mqtt.broker.<N>.topic_prefix <s>
//   set mqtt.broker.<N>.ca_cert <name>           -- letsencrypt, gts-r4, isrg-x2, ""
//   display always on | display normal           -- #141: keep the screen lit / restore the 15 s blank
//                                                    ("display always off" is accepted as an alias for "display normal")
//   display rotate <0|180> | display flip         -- #148: rotate the display 0/180 ("flip" toggles)
//
// All "set" commands write to NVS via ConfigSchema and call
// pool.reloadSlot(N) if a per-slot field changed. Slot range [0,
// OFFBAND_MAX_BROKERS).
bool dispatchObserverCli(const char* cmd, char* reply, size_t reply_size,
                         MqttBrokerPool& pool);

// The pool accessor dispatchObserverCli reads live state from (and that CommonCLI
// passes as the `pool` arg). Each role provides one definition: the observer via
// its pipeline (WifiObserver), the repeater via RepeaterMqttPool (#538). Declared
// here so CommonCLI sees it on both roles.
MqttBrokerPool& wifiObserverPool();

// Epic F (#165): typed config dispatch -- the wire path's set/get backend.
// A config key routes straight to the same handlers the _sys CLI uses (above),
// WITHOUT re-parsing a CLI string. Consumed by the companion-API config command
// (CMD_OFFBAND_CONFIG / OffbandConfigProtocol.h).
//
// #364 (Epic #300 item 1): the former public configSet()/configGet() are now
// file-static providers in ObserverCli.cpp, registered with the role-agnostic
// dispatcher in helpers/config/ConfigDispatch.h. Call
// offband::config::dispatchSet() / dispatchGet() instead -- same reply text,
// same "false == unknown key" contract, same wire behaviour (#143/#160
// unchanged), but the repeater can register its own keys alongside the
// observer's. Registration is automatic when this translation unit is linked.
//
// Secrets stay write-only on get (wifi.pwd / broker password|jwt_token).

// Epic F (#162): broker-pool enumeration for the OCFG_BROKERS paginated read.
// configBrokerSlotCount() = max slots to iterate; configBrokerSlotPopulated() =
// is slot N set (non-empty url); configRenderBrokerSlot() renders a populated
// slot's non-secret fields as wire "key=value\n" lines (string enums; password
// redacted; jwt_token omitted), returning bytes written (0 if empty).
//
// #172/#173 (additive, backward-compatible): when `rt` is provided, also emits
// `state=`/`last_error=` (the live connection state, not just config `enabled`);
// when `owner_default_hex` is provided, emits `jwt_owner_resolved=`/`iata_resolved=`
// for blank jwt_owner/iata_override -- the value used at connect, shown by the
// client as a hint and NOT written back as an override. Both nullable (omit -> no
// extra lines). The raw keys are unchanged, so old clients are unaffected.
int    configBrokerSlotCount();
bool   configBrokerSlotPopulated(uint8_t slot);
// #175: ring_lag / ring_lapped are the per-broker send backlog + lap flag from
// the pool's ring log (additive; -1 lag omits the line for callers without a
// pool handle). Old clients ignore the unknown keys.
size_t configRenderBrokerSlot(uint8_t slot, char* out, size_t out_size,
                              const BrokerRuntimeState* rt = nullptr,
                              const char* owner_default_hex = nullptr,
                              int32_t ring_lag = -1,
                              bool ring_lapped = false);

// #370: the display appliers (setDisplayAlwaysOnApplier / setDisplayRotationApplier
// / setDisplayRotationSupportedQuery) and the display.* NVS accessors moved to
// config/DisplayConfigProvider.h. Included below so existing includers of this
// header (e.g. examples/companion_radio/main.cpp) keep resolving them unchanged.

}  // namespace offband

// #370: forward ONLY the display header, so legacy includers of ObserverCli.h
// (examples/companion_radio/main.cpp) keep resolving the display appliers +
// display.* NVS accessors unchanged -- that is the only cross-TU surface they
// used from this header. The wifi.* handlers are NOT forwarded: the sole caller
// is ObserverCli.cpp's dispatchObserverCli, which includes WifiConfigProvider.h
// directly. (Gemini #370 review MINOR-3: keep the transitive surface minimal.)
#include "../config/DisplayConfigProvider.h"
