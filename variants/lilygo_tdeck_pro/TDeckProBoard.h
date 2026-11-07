#pragma once

#include <Arduino.h>
#include "helpers/ESP32Board.h"

// LilyGo T-Deck Pro V1.1 (e-paper). Pins from LilyGo's V1.1 schematic and
// examples/factory2/utilities.h, branch HD-V2-250915 (#2015).
class TDeckProBoard : public ESP32Board {
public:
  void begin();

  const char* getManufacturerName() const override {
    return "LilyGo T-Deck Pro";
  }
};
