#pragma once

// RadioLib on an SPI bus the radio shares under ESP-IDF's bus lock (Epic J
// #1434, J2 #1725; owner, 2026-10-21, option A).
//
// The T-Deck's display, micro-SD and SX1262 share SPI2 (SCK 40, MOSI 41,
// MISO 38). The GUI drives the display with ESP-IDF's spi_master (DMA) and the
// card with ESP-IDF's sdspi, both devices on SPI2. Arduino's SPIClass writes
// the same peripheral's registers underneath that driver, so the radio can't
// use it: this HAL keeps everything ArduinoHal does (GPIO, the DIO1 interrupt,
// delays, and the chip select RadioLib drives itself) and moves only the
// five SPI calls onto an ESP-IDF device on the same host. Each transaction
// holds the bus (SharedSpiSession), so a display flush can't land inside a
// radio command. Meshtastic does the same with LockingArduinoHal and a global
// mutex; here the lock is the one the display and card already use.
//
// ESP32 only; built by the envs that ask for it.

#include <RadioLib.h>
#include <driver/spi_master.h>

#include "SharedSpiSession.h"

class EspSharedSpiHal : public ArduinoHal, private offband::SpiBusOps {
public:
  // `unused` satisfies ArduinoHal and is never begun, so it never claims the
  // pins (that constructor leaves ArduinoHal's initInterface false). The bus
  // pins and `max_transfer` are used only if nothing has brought the bus up
  // yet, a headless boot where the display failed: ESP-IDF fixes the bus's
  // largest transfer at init, so it must be the display's DMA size, or a later
  // display start couldn't flush a full buffer.
  EspSharedSpiHal(SPIClass& unused, spi_host_device_t host, int sck, int miso, int mosi,
                  int max_transfer, int clock_hz = 8000000)
      : ArduinoHal(unused), _session(*this), _host(host), _sck(sck), _miso(miso), _mosi(mosi),
        _max_transfer(max_transfer), _hz(clock_hz) {}

  // ArduinoHal built this way skips spiBegin; this HAL needs it.
  void init() override {
    ArduinoHal::init();
    spiBegin();
  }

  void spiBegin() override {
    if (_dev) return;
    spi_bus_config_t bus = {};
    bus.sclk_io_num = _sck;
    bus.miso_io_num = _miso;
    bus.mosi_io_num = _mosi;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = _max_transfer;
    // Already up (the display brought it up first, with its own larger DMA
    // size): ESP_ERR_INVALID_STATE, and the radio just joins.
    const esp_err_t b = spi_bus_initialize(_host, &bus, SPI_DMA_CH_AUTO);
    if (b != ESP_OK && b != ESP_ERR_INVALID_STATE) return;
    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = _hz;
    dev.mode = 0;
    dev.spics_io_num = -1;   // RadioLib drives the radio's NSS itself
    dev.queue_size = 1;
    spi_bus_add_device(_host, &dev, &_dev);
  }

  void spiBeginTransaction() override { _session.begin(); }
  void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override { _session.transfer(out, in, len); }
  void spiEndTransaction() override { _session.end(); }
  void spiEnd() override {}   // the bus outlives the radio: the display and card are on it

private:
  bool acquire() override { return _dev && spi_device_acquire_bus(_dev, portMAX_DELAY) == ESP_OK; }
  void release() override { spi_device_release_bus(_dev); }
  bool transfer(const uint8_t* out, uint8_t* in, size_t len) override {
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = out;
    t.rx_buffer = in;
    return spi_device_polling_transmit(_dev, &t) == ESP_OK;
  }

  offband::SharedSpiSession _session;
  spi_device_handle_t _dev = nullptr;
  spi_host_device_t _host;
  int _sck, _miso, _mosi, _max_transfer, _hz;
};
