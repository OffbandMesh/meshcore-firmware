#pragma once

// The radio's own sensors on the GUI (Epic M, #1660): the readings that
// EnvironmentSensorManager::querySensors() writes as CayenneLPP (the same
// bytes the phone's telemetry request gets), decoded, named and formatted.
// Free of LVGL and the mesh so it runs in the native tests. Field sizes and
// scales are the linked library's (Electronic Cats CayenneLPP 1.6.1).
// Owner decisions on #1660 (2026-10-19): °F or °C, default °F; Home's
// Airtime cell becomes the consolidated reading when a sensor is found.

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

// The LPP types the sensor drivers write.
enum LppType : uint8_t {
  kLppDigitalIn = 0, kLppDigitalOut = 1, kLppAnalogIn = 2, kLppAnalogOut = 3,
  kLppGeneric = 100, kLppLuminosity = 101, kLppPresence = 102, kLppTemperature = 103,
  kLppHumidity = 104, kLppAccelerometer = 113, kLppPressure = 115, kLppVoltage = 116,
  kLppCurrent = 117, kLppFrequency = 118, kLppPercentage = 120, kLppAltitude = 121,
  kLppConcentration = 125, kLppPower = 128, kLppDistance = 130, kLppEnergy = 131,
  kLppDirection = 132, kLppUnixTime = 133, kLppGyrometer = 134, kLppColour = 135,
  kLppGps = 136, kLppSwitch = 142,
};

struct SensorReading {
  uint8_t channel;
  uint8_t type;   // LppType
  float value;    // in the type's unit: °C, %, hPa, m, V, A, W, Ω, ...
};

// Decodes an LPP buffer into readings. GPS and the multi-axis types are
// skipped (the GPS section reads the provider directly). Stops, keeping what
// it has, at an unknown type or a field cut short.
size_t decodeLpp(const uint8_t* buf, size_t len, SensorReading* out, size_t max);

// A reading's name, from its type and the sensor that made it: a BME680's
// generic value is its gas resistance; with BSEC it is the air-quality index.
const char* readingName(uint8_t type, const char* sensor);

// The value and its unit: "71.5" "°F", "48" "%", "12.31" "V", "1013.2" "hPa".
void readingText(const SensorReading& r, const char* sensor, bool fahrenheit, char* value, size_t vn, char* unit,
                 size_t un);

// Home's consolidated cell (owner pick 5a): the temperature (else the first
// voltage, else the first reading) as the value, and up to two of humidity,
// pressure and voltage under it ("48% · 1013 hPa"). False with no readings.
// Temperature and humidity come from channel `prefer` (an SHT) when it has
// them, else the first sensor that does; pressure from whoever has it. The
// owner's Field_Compass rule (2026-10-19): the SHT reads air temperature and
// humidity better than a BME, which warms itself. 0: no preference.
bool sensorSummary(const SensorReading* r, size_t n, bool fahrenheit, char* value, size_t vn, char* sub, size_t sn,
                   uint8_t prefer = 0);

// The Settings list's value: "2 found · 71.5 °F", "1 found", "None found",
// with the temperature chosen as sensorSummary's.
void sensorsListValue(int found, const SensorReading* r, size_t n, bool fahrenheit, char* out, size_t on,
                      uint8_t prefer = 0);

// The channel whose sensor is preferred for temperature and humidity: the
// first whose name starts with "SHT". names[i] is channel first_ch + i's.
uint8_t preferredClimateChannel(const char* const* names, size_t count, uint8_t first_ch);

}  // namespace gui
}  // namespace offband
