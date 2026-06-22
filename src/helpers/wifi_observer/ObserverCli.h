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
//   set mqtt.broker.<N>.ca_cert <name>           -- letsencrypt, eastmesh, ""
//   display always on | display normal           -- #141: keep the screen lit / restore the 15 s blank
//                                                    ("display always off" is accepted as an alias for "display normal")
//   display rotate <0|180> | display flip         -- #148: rotate the display 0/180 ("flip" toggles)
//
// All "set" commands write to NVS via ConfigSchema and call
// pool.reloadSlot(N) if a per-slot field changed. Slot range [0,
// OFFBAND_MAX_BROKERS).
bool dispatchObserverCli(const char* cmd, char* reply, size_t reply_size,
                         MqttBrokerPool& pool);

// Epic F (#165): typed config dispatch -- the wire path's set/get backend.
// configSet/configGet route a config key straight to the same handlers the
// _sys CLI uses (above), WITHOUT re-parsing a CLI string. Consumed by the
// companion-API config command (CMD_OFFBAND_CONFIG / OffbandConfigProtocol.h).
// `reply` is NUL-terminated human text; returns true if the key was handled
// (reply populated, incl. an ERROR string), false if the key is unknown.
// Secrets stay write-only on get (wifi.pwd / broker password|jwt_token).
bool configSet(const char* key, const char* value, char* reply, size_t reply_size,
               MqttBrokerPool& pool);
bool configGet(const char* key, char* reply, size_t reply_size);

// Epic F (#162): broker-pool enumeration for the OCFG_BROKERS paginated read.
// configBrokerSlotCount() = max slots to iterate; configBrokerSlotPopulated() =
// is slot N set (non-empty url); configRenderBrokerSlot() renders a populated
// slot's non-secret fields as wire "key=value\n" lines (string enums; password
// redacted; jwt_token omitted), returning bytes written (0 if empty).
int    configBrokerSlotCount();
bool   configBrokerSlotPopulated(uint8_t slot);
size_t configRenderBrokerSlot(uint8_t slot, char* out, size_t out_size);

// #141: register a callback the app provides so a `display always on/off`
// command applies to the live UITask immediately (no reboot). Raw function
// pointer (not std::function) to avoid heap on tight-RAM boards.
void setDisplayAlwaysOnApplier(void (*fn)(bool));

// #148: register the applier for `display rotate`/`display flip` (degrees 0/180).
void setDisplayRotationApplier(void (*fn)(uint8_t));

// #148: register a query so `display rotate`/`flip` can report "not supported on
// this display" (instead of a silent no-op) when the live driver has no verified
// runtime-rotation override (TFT variants, e-ink).
void setDisplayRotationSupportedQuery(bool (*fn)());

}  // namespace offband
