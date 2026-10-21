#include "St7789Dma.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <string.h>

St7789Dma* St7789Dma::s_instance = nullptr;

// Transaction `user` field: the DC level (0 command, 1 data).
static constexpr uintptr_t kUserData = 1;

// ST7789 commands (datasheet section 9).
static constexpr uint8_t CMD_SWRESET = 0x01;
static constexpr uint8_t CMD_SLPIN = 0x10;
static constexpr uint8_t CMD_SLPOUT = 0x11;
static constexpr uint8_t CMD_NORON = 0x13;
static constexpr uint8_t CMD_INVON = 0x21;
static constexpr uint8_t CMD_DISPOFF = 0x28;
static constexpr uint8_t CMD_DISPON = 0x29;
static constexpr uint8_t CMD_CASET = 0x2A;
static constexpr uint8_t CMD_RASET = 0x2B;
static constexpr uint8_t CMD_RAMWR = 0x2C;
static constexpr uint8_t CMD_MADCTL = 0x36;
static constexpr uint8_t CMD_COLMOD = 0x3A;

void IRAM_ATTR St7789Dma::preTransfer(spi_transaction_t* t) {
  uintptr_t u = reinterpret_cast<uintptr_t>(t->user);
  gpio_set_level(static_cast<gpio_num_t>(s_instance->_dc), (u & kUserData) ? 1 : 0);
}

bool St7789Dma::begin(spi_host_device_t host, const Pins& pins, uint32_t hz, uint8_t madctl,
                      size_t max_transfer) {
  s_instance = this;
  _dc = pins.dc;

  pinMode(pins.dc, OUTPUT);
  if (pins.rst >= 0) {
    pinMode(pins.rst, OUTPUT);
    digitalWrite(pins.rst, LOW);
    delay(10);
    digitalWrite(pins.rst, HIGH);
    delay(120);
  }

  spi_bus_config_t bus = {};
  bus.mosi_io_num = pins.mosi;
  bus.miso_io_num = pins.miso;
  bus.sclk_io_num = pins.sck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = static_cast<int>(max_transfer);
  if (spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = static_cast<int>(hz);
  dev.mode = 0;
  dev.spics_io_num = pins.cs;
  dev.queue_size = 2;
  dev.pre_cb = preTransfer;
  if (spi_bus_add_device(host, &dev, &_dev) != ESP_OK) return false;

  command(CMD_SWRESET);
  delay(150);
  command(CMD_SLPOUT);
  delay(120);
  const uint8_t colmod = 0x55;  // 16 bits per pixel, RGB565
  command(CMD_COLMOD, &colmod, 1);
  command(CMD_MADCTL, &madctl, 1);
  command(CMD_INVON);  // IPS panels need inversion on for true colors
  command(CMD_NORON);
  delay(10);
  command(CMD_DISPON);
  delay(10);
  return true;
}

void St7789Dma::waitPending() {
  if (!_pending) return;
  spi_transaction_t* done = nullptr;
  spi_device_get_trans_result(_dev, &done, portMAX_DELAY);
  _pending = false;
}

void St7789Dma::command(uint8_t cmd, const uint8_t* data, size_t len) {
  waitPending();
  spi_transaction_t t = {};
  t.length = 8;
  t.tx_buffer = &cmd;
  t.user = reinterpret_cast<void*>(uintptr_t(0));
  spi_device_polling_transmit(_dev, &t);
  if (data && len) {
    spi_transaction_t d = {};
    d.length = len * 8;
    d.tx_buffer = data;
    d.user = reinterpret_cast<void*>(kUserData);
    spi_device_polling_transmit(_dev, &d);
  }
}

void St7789Dma::setWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  const uint8_t ca[4] = {uint8_t(x1 >> 8), uint8_t(x1), uint8_t(x2 >> 8), uint8_t(x2)};
  const uint8_t ra[4] = {uint8_t(y1 >> 8), uint8_t(y1), uint8_t(y2 >> 8), uint8_t(y2)};
  command(CMD_CASET, ca, 4);
  command(CMD_RASET, ra, 4);
  command(CMD_RAMWR);
}

bool St7789Dma::writePixelsAsync(const uint8_t* data, size_t len) {
  waitPending();
  memset(&_pixel_trans, 0, sizeof(_pixel_trans));
  _pixel_trans.length = len * 8;
  _pixel_trans.tx_buffer = data;
  _pixel_trans.user = reinterpret_cast<void*>(kUserData);
  if (spi_device_queue_trans(_dev, &_pixel_trans, portMAX_DELAY) != ESP_OK) return false;
  _pending = true;
  return true;
}

bool St7789Dma::pollDone() {
  if (!_pending) return true;
  spi_transaction_t* done = nullptr;
  if (spi_device_get_trans_result(_dev, &done, 0) != ESP_OK) return false;
  _pending = false;
  return true;
}

void St7789Dma::sleep(bool on) {
  command(on ? CMD_SLPIN : CMD_SLPOUT);
  delay(120);
}

void St7789Dma::displayOn(bool on) {
  command(on ? CMD_DISPON : CMD_DISPOFF);
}

void St7789Dma::setMadctl(uint8_t madctl) {
  waitPending();   // a frame half written in the old orientation would land wrong
  command(CMD_MADCTL, &madctl, 1);
}
