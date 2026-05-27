// src/helpers/wifi_observer/ObserverCli.h
//
// Plan 2 v2 Task 10: serial CLI commands for mqtt config.
// Single entry point dispatchObserverCli(); CommonCLI calls it from its
// fall-through under #ifdef CROSSWIRE_OBSERVER.

#pragma once
#include "MqttBrokerPool.h"
#include <stddef.h>

namespace crosswire {

// Parses + dispatches "mqtt"-prefixed commands. Returns true if handled
// (reply populated with result/error), false if the command wasn't an
// observer command (caller falls through to its own unknown-command path).
//
// Commands handled:
//   mqtt status                                  -- pool summary
//   mqtt enable <N>                              -- slot N: enabled=true + reload
//   mqtt disable <N>                             -- slot N: enabled=false + reload
//   set mqtt.iata <code>                         -- global IATA
//   set mqtt.status_interval <sec>               -- 10..3600
//   set mqtt.broker.<N>.url <uri>
//   set mqtt.broker.<N>.port <port>
//   set mqtt.broker.<N>.transport <tcp|tls|wss>
//   set mqtt.broker.<N>.auth_type <none|basic|jwt>
//   set mqtt.broker.<N>.username <s>
//   set mqtt.broker.<N>.password <s>             -- reply elides value
//   set mqtt.broker.<N>.jwt_audience <url>
//   set mqtt.broker.<N>.iata_override <code>
//   set mqtt.broker.<N>.topic_prefix <s>
//   set mqtt.broker.<N>.ca_cert <name>           -- letsencrypt, eastmesh, ""
//   set web.allow_initial <on|off>               -- re-enable derived
//                                                   initial password
//                                                   (recovery override;
//                                                    auto-clears on next
//                                                    successful set)
//
// All "set" commands write to NVS via ConfigSchema and call
// pool.reloadSlot(N) if a per-slot field changed. Slot range [0,
// CROSSWIRE_MAX_BROKERS).
bool dispatchObserverCli(const char* cmd, char* reply, size_t reply_size,
                         MqttBrokerPool& pool);

}  // namespace crosswire
