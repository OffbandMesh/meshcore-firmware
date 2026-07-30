// src/helpers/config/DisplayConfigProvider.h
//
// Epic #300 item 1 / #370: the shared `display.*` config provider.
//
// Owner Option A (2026-07-29): `display.*` is its OWN provider + its OWN
// capability bit (0x10, reserved in the canonical map; the constant + advertise
// land under #365's caps-byte surface, NOT here), separate from `wifi.*`, so the
// two are independently advertisable.
//
// This file also OWNS the display-preference NVS accessors (`getDisplayAlwaysOn`
// etc.), relocated out of wifi_observer/ConfigSchema. They are pure `Preferences`
// wrappers on the `offband_ui` namespace with zero broker/mqtt/observer coupling
// -- role-neutral, so any role with a screen links them without dragging the
// observer config schema. (This is the "decouple display" half of #370 -- owner
// said do it now since every display-capable role needs it. WiFi's WifiBootstrap
// coupling is deliberately NOT decoupled here; that's #365's call.)
//
// Keys: `display.always_on` (0|1), `display.rotation` (0|180). Persistence:
// `offband_ui` NVS namespace. Applied live via app-registered raw function
// pointers (no heap on tight-RAM boards).
//
// Handlers are exposed (not file-static) because the observer's `_sys` CLI
// (dispatchObserverCli, in ObserverCli.cpp) calls them too. The provider
// self-registers during static init.

#pragma once
#include <stddef.h>
#include <stdint.h>

namespace offband {

// --- display-preference persistence (offband_ui NVS; relocated from ConfigSchema) ---
bool    getDisplayAlwaysOn();
bool    setDisplayAlwaysOn(bool on);       // #181: false on NVS failure (logged)
uint8_t getDisplayRotation();              // clamped to {0,180}
bool    setDisplayRotation(uint8_t deg);   // #181: false on NVS failure (logged)

// --- live-apply appliers the app registers at boot (raw fn ptr, no heap) ---
void setDisplayAlwaysOnApplier(void (*fn)(bool));            // #141
void setDisplayRotationApplier(void (*fn)(uint8_t));         // #148
void setDisplayRotationSupportedQuery(bool (*fn)());         // #148: refuse rotate on unsupported drivers

// --- handlers (also called by the observer _sys CLI) ---
bool handleDisplayAlwaysOn(char* reply, size_t reply_size, bool on);
bool handleDisplayRotate(char* reply, size_t reply_size, uint8_t deg);
bool handleDisplayFlip(char* reply, size_t reply_size);

}  // namespace offband
