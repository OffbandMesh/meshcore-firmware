#pragma once

// The SD card on the panel's SPI bus (Epic I #1433, I3 #1677).
//
// On the R8's Expansion Kit V2 the card shares SPI2 with the ST7789 (SCK 16,
// MOSI 15, MISO 45; card CS GPIO3, schematic V2.03). GuiPanel brings the bus
// up; this adds the card as a second device on it through ESP-IDF's sdspi
// driver, whose bus lock serializes the card's transactions with the panel's
// DMA flushes. Nothing here starts a second SPI driver on those pins, and
// nothing in a GUI build may call Arduino's SPI.begin() (GuiPanel.cpp).
//
// There's no card-detect line, so SdMonitor decides when to call mount() and
// probe(); this only does what it's told.

#include <stdint.h>
#include <stdio.h>

#include <driver/sdmmc_types.h>   // sdmmc_card_t is an anonymous-struct typedef: no forward declaration

#include <helpers/gui/SdArchive.h>
#include <helpers/gui/SdMonitor.h>

class SdCard {
public:
  static constexpr const char* kMount = "/sd";

  // Adds the card to the panel's bus and mounts its FAT volume. NoCard when
  // nothing answers, Unreadable when a card answers but has no usable FAT.
  offband::gui::MountOutcome mount();
  // CMD13: false once the card has gone.
  bool probe();
  void unmount();
  bool mounted() const { return _card != nullptr; }

  // Bytes on the volume. False if not mounted.
  bool usage(uint64_t* total, uint64_t* free_bytes);

  // Erases the card with a fresh FAT volume and mounts it. Works on a card
  // that won't mount, which is the point of offering it there.
  bool format();

private:
  sdmmc_card_t* _card = nullptr;
};

// A file on the card, for SdArchive.
class SdFile : public offband::gui::RandomFile {
public:
  // Opens `path` for reading and writing, creating it empty if missing.
  bool open(const char* path);
  void close();
  bool isOpen() const { return _f != nullptr; }
  ~SdFile() { close(); }

  bool readAt(uint32_t offset, void* data, size_t len) override;
  bool writeAt(uint32_t offset, const void* data, size_t len) override;
  uint32_t size() override;
  bool flush() override;

private:
  FILE* _f = nullptr;
};
