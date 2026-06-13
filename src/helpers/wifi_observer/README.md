# src/helpers/wifi_observer/

This subsystem implements the Offband WiFi+MQTT observer stack. It is
gated behind the `OFFBAND_OBSERVER` preprocessor flag and compiled into
the `*_companion_observer_wifi` env variants (V3/V4/XIAO).

## Vendored origin

The files `MqttUplink.{cpp,h}`, `MqttPrefs.{cpp,h}`, `MqttCaCerts.h`, and
`JwtHelper.{cpp,h}` originated as a verbatim copy of
`src/helpers/mqtt/*` from:

- Repo: <https://github.com/xJARiD/MeshCore-EastMesh>
- Commit: `374d6aa87a0123514d05b607310266a0c6a86b6a`
- Date:   `2026-05-23 20:44:45 +1000`

Renamed at vendoring time to match Offband's mixed-case header
convention (`MqttUplink` instead of `MQTTUplink`, etc.). The actual
file copy + rename happens in Plan 1 Task 3; the C++ class-name rename
happens in Plan 1 Task 4 as an isolated diff.

EastMesh is itself a fork of upstream <https://github.com/meshcore-dev/MeshCore>,
which is the same upstream Offband forks from. License inheritance is
identical (see `meshcore-firmware/license.txt`).

## Native to Offband (not vendored)

- `WifiObserverConfig.h` -- compile-time constants (Plan 1 Task 6)
- `WifiBootstrap.{cpp,h}` -- AP-mode + STA + serial CLI rescue (Plan 1 Task 9)
- `WifiObserver.{cpp,h}` -- subsystem entry point (Plan 1 Task 10)

## Long-term trajectory

Per #210 (Crosswire fork architectural restructure) and the
WiFi+BLE Observer design spec (2026-05-24), the vendored EastMesh code
is a starting point. Future iterations will progressively rewrite these
files for Offband-native idioms and remove the EastMesh dependency
entirely. New code added in Plan 1+ MUST be Offband-native, even when
it sits next to vendored files.
