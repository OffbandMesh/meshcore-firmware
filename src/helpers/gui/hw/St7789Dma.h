#pragma once

// ST7789 panel driver for the GUI (#1441): pixels go out by SPI DMA so the CPU
// can draw the next buffer while the previous one transfers. The D4 bench
// measured the CPU-driven Adafruit path spending 35-48 ms per frame on the
// transfer alone (#1326); with DMA that time overlaps drawing.
//
// Commands are short and sent blocking; only pixel writes are asynchronous.
// One panel per build, so the SPI callbacks reach the instance through a
// static pointer.

#include <stddef.h>
#include <stdint.h>

#include <driver/spi_master.h>
#include <esp_attr.h>

class St7789Dma {
public:
  struct Pins {
    int sck, mosi, cs, dc, rst;
    // The bus's data-in. The panel never reads, but an SD card sharing the
    // bus does (the R8's Expansion Kit V2, #1677), so the bus comes up with
    // it when the board has one.
    int miso = -1;
  };

  // `max_transfer` is the largest pixel write in bytes (one draw buffer).
  bool begin(spi_host_device_t host, const Pins& pins, uint32_t hz, uint8_t madctl,
             size_t max_transfer);

  // Address window, inclusive. Waits for any pixel write still in flight.
  void setWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

  // Starts an asynchronous write of `len` bytes of RGB565 (big-endian) pixel
  // data into the current window. `data` must stay valid until pollDone()
  // returns true. Returns false if the write could not be queued.
  bool writePixelsAsync(const uint8_t* data, size_t len);

  // True once the last async pixel write has finished (or none is in
  // flight). Asks the SPI driver without blocking.
  bool pollDone();

  void sleep(bool on);
  void displayOn(bool on);
  // The orientation (Epic H): MADCTL, sent once any pixel write has finished.
  void setMadctl(uint8_t madctl);

private:
  void command(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0);
  void waitPending();

  static void IRAM_ATTR preTransfer(spi_transaction_t* t);

  spi_device_handle_t _dev = nullptr;
  spi_transaction_t _pixel_trans = {};
  bool _pending = false;
  int _dc = -1;

  static St7789Dma* s_instance;
};
