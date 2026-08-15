#ifndef _VARIANT_HELTEC_RC32_
#define _VARIANT_HELTEC_RC32_

#define BUTTON_PIN 0

#define I2C_SCL 18
#define I2C_SDA 21
// #704: SENSOR_INT_PIN 42 removed -- dead define, and GPIO42 is the
//       vendor's motor-IN2 / servo-PWM2 accessory pin.
#define PERIPHERAL_WARMUP_MS 100

#define LORA_SCK 11
#define LORA_MISO 13
#define LORA_MOSI 12
#define LORA_CS 10
#define LORA_DIO0 RADIOLIB_NC
#define LORA_DIO1 14
#define LORA_RESET 9

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 1
#define SX126X_RESET LORA_RESET

#define BATTERY_PIN 7
#define ADC_CHANNEL ADC_CHANNEL_6
#define ADC_CTRL 15
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5

#endif
