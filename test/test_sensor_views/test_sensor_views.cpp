// Epic M (#1660, #1661): the radio's own sensors, decoded from the CayenneLPP
// querySensors() writes, named and formatted for the GUI.

#include <gtest/gtest.h>

#include <string.h>

#include "helpers/gui/SensorViews.h"

using namespace offband::gui;

namespace {

// GPS on channel 1 (skipped), a BME280 on 2, an SHT4x on 3, an INA219 on 4,
// as begin()'s channel order writes them.
const uint8_t kPacket[] = {
    1, kLppGps, 0x05, 0xA0, 0x10, 0xF4, 0x5B, 0x20, 0x00, 0x03, 0xE8,   // GPS: skipped
    2, kLppTemperature, 0x00, 0xDB,                                     // 21.9 °C
    2, kLppHumidity, 96,                                                // 48 %
    2, kLppPressure, 0x27, 0x94,                                        // 1013.2 hPa
    2, kLppAltitude, 0x00, 0x0A,                                        // 10 m
    3, kLppTemperature, 0xFF, 0xC9,                                     // -5.5 °C
    3, kLppHumidity, 121,                                               // 60.5 %
    4, kLppVoltage, 0x04, 0xCF,                                         // 12.31 V
    4, kLppCurrent, 0x01, 0xA4,                                         // 0.420 A
    4, kLppPower, 0x00, 0x05,                                           // 5 W
};

}  // namespace

TEST(Decode, ReadsEveryDriversFieldAndSkipsGps) {
  SensorReading r[16];
  const size_t n = decodeLpp(kPacket, sizeof kPacket, r, 16);
  ASSERT_EQ(n, 9u);
  EXPECT_EQ(r[0].channel, 2);
  EXPECT_EQ(r[0].type, kLppTemperature);
  EXPECT_NEAR(r[0].value, 21.9f, 0.01f);
  EXPECT_NEAR(r[1].value, 48.0f, 0.01f);
  EXPECT_NEAR(r[2].value, 1013.2f, 0.01f);
  EXPECT_NEAR(r[3].value, 10.0f, 0.01f);
  EXPECT_NEAR(r[4].value, -5.5f, 0.01f);    // signed
  EXPECT_NEAR(r[5].value, 60.5f, 0.01f);    // half-percent steps
  EXPECT_NEAR(r[6].value, 12.31f, 0.001f);
  EXPECT_NEAR(r[7].value, 0.42f, 0.0001f);
  EXPECT_NEAR(r[8].value, 5.0f, 0.01f);
}

TEST(Decode, StopsAtACutShortOrUnknownField) {
  SensorReading r[16];
  // The pressure field is one byte short: the two before it stay.
  const uint8_t cut[] = {2, kLppTemperature, 0x00, 0xDB, 2, kLppHumidity, 96, 2, kLppPressure, 0x27};
  EXPECT_EQ(decodeLpp(cut, sizeof cut, r, 16), 2u);
  // An unknown type ends it: nothing after it can be framed.
  const uint8_t odd[] = {2, kLppTemperature, 0x00, 0xDB, 2, 250, 1, 2, kLppHumidity, 96};
  EXPECT_EQ(decodeLpp(odd, sizeof odd, r, 16), 1u);
  EXPECT_EQ(decodeLpp(nullptr, 10, r, 16), 0u);
  EXPECT_EQ(decodeLpp(kPacket, 1, r, 16), 0u);
}

TEST(Decode, NeverWritesPastMax) {
  SensorReading r[3];
  memset(r, 0xAB, sizeof r);
  EXPECT_EQ(decodeLpp(kPacket, sizeof kPacket, r, 2), 2u);
  const uint8_t* tail = reinterpret_cast<const uint8_t*>(&r[2]);
  EXPECT_EQ(tail[0], 0xAB);
}

TEST(Text, UnitsAndFahrenheit) {
  char v[16], u[8];
  const SensorReading t{2, kLppTemperature, 21.9f};
  readingText(t, "BME280", true, v, sizeof v, u, sizeof u);
  EXPECT_STREQ(v, "71.4");
  EXPECT_STREQ(u, "°F");
  readingText(t, "BME280", false, v, sizeof v, u, sizeof u);
  EXPECT_STREQ(v, "21.9");
  EXPECT_STREQ(u, "°C");
  const SensorReading cold{3, kLppTemperature, -40.0f};   // the scales meet at -40
  readingText(cold, "SHT4X", true, v, sizeof v, u, sizeof u);
  EXPECT_STREQ(v, "-40.0");
  readingText(SensorReading{4, kLppVoltage, 12.31f}, "INA219", true, v, sizeof v, u, sizeof u);
  EXPECT_STREQ(v, "12.31");
  EXPECT_STREQ(u, "V");
  readingText(SensorReading{2, kLppPressure, 1013.2f}, "BME280", true, v, sizeof v, u, sizeof u);
  EXPECT_STREQ(v, "1013.2");
  EXPECT_STREQ(u, "hPa");
}

TEST(Text, Bme68xGasAndBsec) {
  char v[16], u[8];
  // A BME680/688's generic value is its gas resistance, in ohms: shown in kΩ.
  const uint8_t gas[] = {2, kLppGeneric, 0x00, 0x01, 0xE2, 0x40};   // 123456 Ω
  SensorReading r[2];
  ASSERT_EQ(decodeLpp(gas, sizeof gas, r, 2), 1u);
  EXPECT_STREQ(readingName(r[0].type, "BME680"), "Gas resistance");
  readingText(r[0], "BME680", true, v, sizeof v, u, sizeof u);
  EXPECT_STREQ(v, "123.5");
  EXPECT_STREQ(u, "kΩ");
  // With BSEC the same field is the air-quality index, and the analog one its accuracy.
  EXPECT_STREQ(readingName(kLppGeneric, "BME680+BSEC"), "Air quality (IAQ)");
  EXPECT_STREQ(readingName(kLppAnalogIn, "BME680+BSEC"), "IAQ accuracy");
  // Anyone else's generic value is just a value.
  EXPECT_STREQ(readingName(kLppGeneric, "RAK12035"), "Value");
  EXPECT_STREQ(readingName(kLppGeneric, nullptr), "Value");
}

TEST(Summary, TemperatureFirstThenHumidityAndPressure) {
  SensorReading r[16];
  const size_t n = decodeLpp(kPacket, sizeof kPacket, r, 16);
  char v[24], s[48];
  ASSERT_TRUE(sensorSummary(r, n, true, v, sizeof v, s, sizeof s));
  EXPECT_STREQ(v, "71.4 °F");
  EXPECT_STREQ(s, "48% · 1013 hPa");
}

TEST(Summary, PowerOnlyLeadsWithVoltage) {
  const SensorReading r[] = {{2, kLppVoltage, 12.31f}, {2, kLppCurrent, 0.42f}};
  char v[24], s[48];
  ASSERT_TRUE(sensorSummary(r, 2, true, v, sizeof v, s, sizeof s));
  EXPECT_STREQ(v, "12.31 V");
  EXPECT_STREQ(s, "");   // the voltage is already the value
}

TEST(Summary, NothingToShow) {
  char v[24] = "x", s[48] = "x";
  EXPECT_FALSE(sensorSummary(nullptr, 0, true, v, sizeof v, s, sizeof s));
  EXPECT_STREQ(v, "");
  EXPECT_STREQ(s, "");
  // A cell-sized buffer cuts the line but still ends it.
  SensorReading r[16];
  const size_t n = decodeLpp(kPacket, sizeof kPacket, r, 16);
  char tiny[6];
  ASSERT_TRUE(sensorSummary(r, n, false, v, sizeof v, tiny, sizeof tiny));
  EXPECT_EQ(strlen(tiny), sizeof tiny - 1);
}

TEST(ListValue, FoundCountAndTemperature) {
  SensorReading r[16];
  const size_t n = decodeLpp(kPacket, sizeof kPacket, r, 16);
  char out[32];
  sensorsListValue(3, r, n, true, out, sizeof out);
  EXPECT_STREQ(out, "3 found · 71.4 °F");
  sensorsListValue(1, r + 6, 3, true, out, sizeof out);   // the INA219 alone: no temperature
  EXPECT_STREQ(out, "1 found");
  sensorsListValue(0, nullptr, 0, true, out, sizeof out);
  EXPECT_STREQ(out, "None found");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
