#include "SensorViews.h"

#include <stdio.h>
#include <string.h>

namespace offband {
namespace gui {

namespace {

// Bytes of data after the channel and type bytes; 0 for a type not known.
size_t fieldSize(uint8_t type) {
  switch (type) {
    case kLppDigitalIn: case kLppDigitalOut: case kLppPresence: case kLppHumidity: case kLppPercentage:
    case kLppSwitch:
      return 1;
    case kLppAnalogIn: case kLppAnalogOut: case kLppLuminosity: case kLppTemperature: case kLppPressure:
    case kLppVoltage: case kLppCurrent: case kLppAltitude: case kLppConcentration: case kLppPower:
    case kLppDirection:
      return 2;
    case kLppColour:
      return 3;
    case kLppGeneric: case kLppFrequency: case kLppDistance: case kLppEnergy: case kLppUnixTime:
      return 4;
    case kLppAccelerometer: case kLppGyrometer:
      return 6;
    case kLppGps:
      return 9;
    default:
      return 0;
  }
}

uint32_t be(const uint8_t* p, size_t n) {
  uint32_t v = 0;
  for (size_t i = 0; i < n; i++) v = (v << 8) | p[i];
  return v;
}

int32_t signedBe(const uint8_t* p, size_t n) {
  uint32_t v = be(p, n);
  const uint32_t sign = 1u << (n * 8 - 1);
  return (v & sign) ? int32_t(v) - int32_t(sign << 1) : int32_t(v);
}

bool startsWith(const char* s, const char* prefix) { return s && strncmp(s, prefix, strlen(prefix)) == 0; }

const SensorReading* firstOf(const SensorReading* r, size_t n, uint8_t type) {
  for (size_t i = 0; i < n; i++) {
    if (r[i].type == type) return &r[i];
  }
  return nullptr;
}

}  // namespace

size_t decodeLpp(const uint8_t* buf, size_t len, SensorReading* out, size_t max) {
  size_t n = 0, at = 0;
  while (buf && at + 2 <= len && n < max) {
    const uint8_t ch = buf[at], type = buf[at + 1];
    const size_t size = fieldSize(type);
    if (size == 0 || at + 2 + size > len) break;   // unknown, or cut short
    const uint8_t* d = buf + at + 2;
    at += 2 + size;
    float v;
    switch (type) {
      case kLppAnalogIn: case kLppAnalogOut: v = signedBe(d, 2) / 100.0f; break;
      case kLppTemperature: v = signedBe(d, 2) / 10.0f; break;
      case kLppHumidity: v = d[0] / 2.0f; break;
      case kLppPressure: v = be(d, 2) / 10.0f; break;
      case kLppVoltage: v = be(d, 2) / 100.0f; break;
      case kLppCurrent: v = be(d, 2) / 1000.0f; break;
      case kLppAltitude: v = float(signedBe(d, 2)); break;
      case kLppDistance: case kLppEnergy: v = be(d, 4) / 1000.0f; break;
      case kLppGps: case kLppAccelerometer: case kLppGyrometer: case kLppColour: case kLppUnixTime:
        continue;   // not a reading this page shows
      default: v = float(be(d, size)); break;
    }
    out[n++] = SensorReading{ch, type, v};
  }
  return n;
}

const char* readingName(uint8_t type, const char* sensor) {
  const bool bsec = sensor && strstr(sensor, "BSEC") != nullptr;
  switch (type) {
    case kLppTemperature: return "Temperature";
    case kLppHumidity: return "Humidity";
    case kLppPressure: return "Pressure";
    case kLppAltitude: return "Altitude";
    case kLppVoltage: return "Voltage";
    case kLppCurrent: return "Current";
    case kLppPower: return "Power";
    case kLppLuminosity: return "Light";
    case kLppConcentration: return "Concentration";
    case kLppPercentage: return "Level";
    case kLppDistance: return "Distance";
    case kLppEnergy: return "Energy";
    case kLppFrequency: return "Frequency";
    case kLppDirection: return "Direction";
    case kLppPresence: return "Presence";
    case kLppSwitch: return "Switch";
    case kLppDigitalIn: case kLppDigitalOut: return "Digital";
    case kLppAnalogIn: case kLppAnalogOut: return bsec ? "IAQ accuracy" : "Analog";
    case kLppGeneric:
      if (bsec) return "Air quality (IAQ)";
      return startsWith(sensor, "BME680") ? "Gas resistance" : "Value";
    default: return "Value";
  }
}

void readingText(const SensorReading& r, const char* sensor, bool fahrenheit, char* value, size_t vn, char* unit,
                 size_t un) {
  const char* u = "";
  switch (r.type) {
    case kLppTemperature:
      snprintf(value, vn, "%.1f", fahrenheit ? r.value * 9.0f / 5.0f + 32.0f : r.value);
      u = fahrenheit ? "°F" : "°C";
      break;
    case kLppHumidity: snprintf(value, vn, "%.0f", r.value); u = "%"; break;
    case kLppPercentage: snprintf(value, vn, "%.0f", r.value); u = "%"; break;
    case kLppPressure: snprintf(value, vn, "%.1f", r.value); u = "hPa"; break;
    case kLppAltitude: snprintf(value, vn, "%.0f", r.value); u = "m"; break;
    case kLppVoltage: snprintf(value, vn, "%.2f", r.value); u = "V"; break;
    case kLppCurrent: snprintf(value, vn, "%.3f", r.value); u = "A"; break;
    case kLppPower: snprintf(value, vn, "%.0f", r.value); u = "W"; break;
    case kLppLuminosity: snprintf(value, vn, "%.0f", r.value); u = "lux"; break;
    case kLppConcentration: snprintf(value, vn, "%.0f", r.value); u = "ppm"; break;
    case kLppDistance: snprintf(value, vn, "%.3f", r.value); u = "m"; break;
    case kLppEnergy: snprintf(value, vn, "%.3f", r.value); u = "kWh"; break;
    case kLppFrequency: snprintf(value, vn, "%.0f", r.value); u = "Hz"; break;
    case kLppDirection: snprintf(value, vn, "%.0f", r.value); u = "°"; break;
    case kLppAnalogIn: case kLppAnalogOut: snprintf(value, vn, "%.2f", r.value); break;
    case kLppGeneric:
      if (sensor && strstr(sensor, "BSEC")) {
        snprintf(value, vn, "%.0f", r.value);
      } else if (startsWith(sensor, "BME680")) {
        snprintf(value, vn, "%.1f", r.value / 1000.0f);   // ohms, shown in kΩ
        u = "kΩ";
      } else {
        snprintf(value, vn, "%.0f", r.value);
      }
      break;
    default: snprintf(value, vn, "%.0f", r.value); break;
  }
  if (un) snprintf(unit, un, "%s", u);
}

bool sensorSummary(const SensorReading* r, size_t n, bool fahrenheit, char* value, size_t vn, char* sub, size_t sn) {
  if (sn) sub[0] = '\0';
  if (!r || n == 0) {
    if (vn) value[0] = '\0';
    return false;
  }
  const SensorReading* head = firstOf(r, n, kLppTemperature);
  if (!head) head = firstOf(r, n, kLppVoltage);
  if (!head) head = &r[0];
  char v[16], u[8];
  readingText(*head, nullptr, fahrenheit, v, sizeof v, u, sizeof u);
  snprintf(value, vn, "%s%s%s", v, u[0] && u[0] != '%' ? " " : "", u);
  // Under it: up to two more, in this order, never the one already shown.
  static const uint8_t kSubOrder[] = {kLppHumidity, kLppPressure, kLppVoltage};
  size_t used = 0, at = 0;
  for (uint8_t t : kSubOrder) {
    if (used == 2) break;
    const SensorReading* s = firstOf(r, n, t);
    if (!s || s == head) continue;
    readingText(*s, nullptr, fahrenheit, v, sizeof v, u, sizeof u);
    if (t == kLppPressure) snprintf(v, sizeof v, "%.0f", s->value);   // whole hPa fit the cell
    const int w = snprintf(sub + at, at < sn ? sn - at : 0, "%s%s%s%s", used ? " · " : "", v,
                           u[0] && u[0] != '%' ? " " : "", u);
    if (w > 0) at += size_t(w);
    if (at >= sn) {
      at = sn ? sn - 1 : 0;
    }
    used++;
  }
  return true;
}

void sensorsListValue(int found, const SensorReading* r, size_t n, bool fahrenheit, char* out, size_t on) {
  if (found <= 0) {
    snprintf(out, on, "None found");
    return;
  }
  const SensorReading* t = r ? firstOf(r, n, kLppTemperature) : nullptr;
  if (!t) {
    snprintf(out, on, "%d found", found);
    return;
  }
  char v[16], u[8];
  readingText(*t, nullptr, fahrenheit, v, sizeof v, u, sizeof u);
  snprintf(out, on, "%d found · %s %s", found, v, u);
}

}  // namespace gui
}  // namespace offband
