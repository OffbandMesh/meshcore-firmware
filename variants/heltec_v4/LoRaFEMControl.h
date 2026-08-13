#pragma once
#include <stdint.h>

typedef enum {
    GC1109_PA,
    KCT8103L_PA,
    OTHER_FEM_TYPES
} LoRaFEMType;

class LoRaFEMControl
{
  public:
    LoRaFEMControl(){ }
    virtual ~LoRaFEMControl(){ }
    void init(void);
    void setSleepModeEnable(void);
    void setTxModeEnable(void);
    void setRxModeEnable(void);
    void setRxModeEnableWhenMCUSleep(void);
    void setLNAEnable(bool enabled);
    bool isLnaCanControl(void) const { return lna_can_control; }
    void setLnaCanControl(bool can_control) { lna_can_control = can_control; }
    bool isLNAEnabled(void) const { return lna_enabled; }
    LoRaFEMType getFEMType(void) const { return fem_type; }

    // #318: raw CSD levels captured during detection, for diagnosing which FEM a
    // given board actually has. csd_early is the level the fem_type decision is
    // made on. csd_late is a second read after a longer settle, sampled ONLY on
    // the GC1109 path and ONLY in FEM_DEBUG_PROBE builds -- it exists to test
    // whether the 1 ms wait in init() is too short for a V4.3's R33 pull-up to
    // Vfem to assert (see the header comment in init()). 0xFF = not sampled.
    uint8_t getCsdEarly(void) const { return csd_early; }
    uint8_t getCsdLate(void) const { return csd_late; }
  private:
    LoRaFEMType fem_type=OTHER_FEM_TYPES;
    bool lna_enabled=false;
    bool lna_can_control=false;
    uint8_t csd_early=0xFF;
    uint8_t csd_late=0xFF;
};

