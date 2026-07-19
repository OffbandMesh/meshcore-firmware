#pragma once
#include <stdint.h>

class LoRaFEMControl
{
  public:
    LoRaFEMControl() {}
    virtual ~LoRaFEMControl() {}
    void init(void);
    void setSleepModeEnable(void);
    void setTxModeEnable(void);
    void setRxModeEnable(void);
    void setRxModeEnableWhenMCUSleep(void);
    void setLNAEnable(bool enabled);
    // #298: read back the current LNA state (mirrors the heltec_v4 FEM control API)
    // so the board can implement MainBoard::isLoRaFemLnaEnabled().
    bool isLnaEnabled(void) const { return lna_enabled; }
    // #298: const so MainBoard::canControlLoRaFemLna() (a const method) can call it
    // without a const_cast. Read-only accessor.
    bool isLnaCanControl(void) const { return lna_can_control; }
    void setLnaCanControl(bool can_control) { lna_can_control = can_control; }

  private:
    bool lna_enabled = false;
    bool lna_can_control = false;
};
