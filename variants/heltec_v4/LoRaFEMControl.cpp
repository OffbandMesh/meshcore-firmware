#include "LoRaFEMControl.h"
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <Arduino.h>

void LoRaFEMControl::init(void)
{
    // Power on FEM LDO — set registers before releasing RTC hold for
    // atomic transition (no glitch on deep sleep wake).
    pinMode(P_LORA_PA_POWER, OUTPUT);
    digitalWrite(P_LORA_PA_POWER, HIGH);
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_DEEPSLEEP) {
        delay(1);  // FEM startup time after cold power-on
    }

    // Auto-detect FEM type from the CSD level on the shared GPIO.
    //
    // #318: this is a BOARD STRAP, not a chip-internal pull. The previous comment
    // here claimed "internal pull-down / internal pull-up"; that is wrong -- the
    // GC1109 datasheet documents no internal pulls on CSD/CPS/CTX. From the Heltec
    // schematics:
    //   V4.2 / GC1109  : R4  10k from PA_CSD to RF_GND  (pull-down) -> reads LOW
    //   V4.3 / KCT8103L: R33 10k from PA_CSD to Vfem    (pull-up)   -> reads HIGH
    //
    // R33 pulls to Vfem -- downstream of the FEM LDO enabled a few lines above --
    // NOT to 3V3. That is why a settle window can exist at all here. Measured on a
    // GC1109 unit: LOW at 1 ms and still LOW after 50 ms, so the strap is
    // unambiguous there. Build with -D FEM_DEBUG_PROBE to re-measure on a board
    // whose FEM is unknown or suspect.
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_KCT8103L_PA_CSD);
    pinMode(P_LORA_KCT8103L_PA_CSD, INPUT);
    delay(1);
    csd_early = digitalRead(P_LORA_KCT8103L_PA_CSD);   // the level the decision uses
    if(csd_early==HIGH) {
        // FEM is KCT8103L (V4.3)
        fem_type= KCT8103L_PA;
        pinMode(P_LORA_KCT8103L_PA_CSD, OUTPUT);
        digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
        rtc_gpio_hold_dis((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
        pinMode(P_LORA_KCT8103L_PA_CTX, OUTPUT);
        digitalWrite(P_LORA_KCT8103L_PA_CTX, lna_enabled ? LOW : HIGH);
        setLnaCanControl(true);
    } else {
#ifdef FEM_DEBUG_PROBE
        // #318 (diagnostic only, does NOT affect the decision above): re-read the
        // same pin after a longer settle while it is still INPUT. A V4.3 whose R33
        // pull-up to Vfem had not yet asserted would read LOW here at 1 ms and HIGH
        // at 50 ms -- i.e. a genuine misdetection. Falsifiable: late==LOW means
        // settle time is NOT the explanation and the part really is a GC1109.
        delay(50);
        csd_late = digitalRead(P_LORA_KCT8103L_PA_CSD);
#endif
        // FEM is GC1109 (V4.2)
        fem_type= GC1109_PA;
        pinMode(P_LORA_GC1109_PA_EN, OUTPUT);
        digitalWrite(P_LORA_GC1109_PA_EN, HIGH);
        // #327: P_LORA_GC1109_PA_TX_EN is GPIO46 = an ESP32-S3 STRAPPING pin.
        // Driving it as an output here is datasheet-compliant -- audited against the
        // ESP32-S3 Series Datasheet v2.2 §3 Boot Configurations:
        //   - Table 3-1: GPIO46 default is a weak pull-DOWN, bit value 0.
        //   - "All strapping pins have latches. At Chip Reset, the latches sample the
        //      bit values ... and store them until the chip is powered down ... and the
        //      pins are freed up to be used as regular IO pins after reset."
        //   - Table 3-3: GPIO0=1 selects SPI boot for ANY GPIO46 value. Only
        //     GPIO0=0 + GPIO46=0 selects download boot.
        // The one theoretical hazard is a Chip Reset latching GPIO0=0 (BOOT held) while
        // GPIO46 is driven HIGH mid-TX -- a combination Table 3-3 does not define.
        // That cannot happen: across any Chip Reset (WDT, brownout, esp_restart, deep
        // sleep wake) the GPIO peripheral is reset and its output driver disabled
        // BEFORE the strapping latches sample, so the pin reverts to high-Z and the
        // datasheet weak pull-down makes it read 0. What WOULD break that is
        // rtc_gpio_hold_en() on this pin, which holds a driven level through reset --
        // it is deliberately not applied here (unlike PA_POWER and the KCT8103L lines).
        // NOTE the gpio_pulldown_en() call on this pin in setRxModeEnableWhenMCUSleep()
        // is a DEEP-SLEEP measure and plays no part in the reset argument above.
        // Upstream meshcore-dev/MeshCore#1249 proposed releasing this pin to INPUT
        // after each TX; it was closed and superseded by #1600. Not adopted here: it
        // adds TX-path state changes to guard a hazard the reset behaviour already
        // rules out.
        pinMode(P_LORA_GC1109_PA_TX_EN, OUTPUT);
        digitalWrite(P_LORA_GC1109_PA_TX_EN, LOW);
    }

#ifdef FEM_DEBUG_PROBE
    // #318: emit the evidence the FEM decision was made on. OFF by default -- this
    // is bench diagnostics, not production logging.
    //
    // Repeated because on ESP32-S3 native USB the port drops and re-enumerates
    // across a reset, so a host reader CANNOT hold the port through boot; it can
    // only re-attach afterwards, by which time a single print is long gone. Each
    // repeat costs 1 s of boot delay, and at ~1 Hz this WILL drown CLI replies on
    // the same serial endpoint -- which is exactly why it is opt-in. Bounded so it
    // can never become the outage it is meant to diagnose (SAFELANE §11 r10).
    //
    //   -D FEM_DEBUG_PROBE                      -> 60 lines / ~60 s boot delay
    //   -D FEM_DEBUG_PROBE -D FEM_DEBUG_PROBE_REPEAT=1  -> single line, no delay
    #ifndef FEM_DEBUG_PROBE_REPEAT
    #define FEM_DEBUG_PROBE_REPEAT 60
    #endif
    for (int i = 0; i < FEM_DEBUG_PROBE_REPEAT; i++) {
        // Only transmit when a host is actually attached: on native USB CDC with no
        // host, the TX buffer fills and Serial.flush() blocks, which can trip the
        // watchdog. The delay stays OUTSIDE this guard on purpose -- the whole point
        // of repeating is that the host attaches LATE (after the post-reset USB
        // re-enumeration), so the window must stay open even while nothing is
        // listening. Gating the delay too would burn all iterations instantly and
        // print nothing to the reader that shows up at t+10 s.
        if (Serial) {
            Serial.printf("[FEM] csd_early=%u csd_late=%u fem_type=%s lna_can_control=%u reset=%d\n",
                          (unsigned)csd_early, (unsigned)csd_late,
                          fem_type==KCT8103L_PA ? "KCT8103L(V4.3)" :
                          fem_type==GC1109_PA   ? "GC1109(V4.2)"   : "OTHER/UNDETECTED",
                          (unsigned)lna_can_control, (int)reason);
            Serial.flush();
        }
        if (i + 1 < FEM_DEBUG_PROBE_REPEAT) delay(1000);
    }
#endif
}

void LoRaFEMControl::setSleepModeEnable(void)
{
    if(fem_type==GC1109_PA) {
    /*
     * Do not switch the power on and off frequently.
     * After turning off P_LORA_PA_EN, the power consumption has dropped to the uA level.
     */
    digitalWrite(P_LORA_GC1109_PA_EN, LOW);
    digitalWrite(P_LORA_GC1109_PA_TX_EN, LOW);
    } else if(fem_type==KCT8103L_PA) {
        // shutdown the PA
        digitalWrite(P_LORA_KCT8103L_PA_CSD, LOW);
    }
}

void LoRaFEMControl::setTxModeEnable(void)
{
    if(fem_type==GC1109_PA) {
        digitalWrite(P_LORA_GC1109_PA_EN, HIGH);   // CSD=1: Chip enabled
        digitalWrite(P_LORA_GC1109_PA_TX_EN, HIGH); // CPS: 1=full PA, 0=bypass (for RX, CPS is don't care)
    } else if(fem_type==KCT8103L_PA) {
        digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
        digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
    }
}

void LoRaFEMControl::setRxModeEnable(void)
{
    if(fem_type==GC1109_PA) {
        digitalWrite(P_LORA_GC1109_PA_EN, HIGH);  // CSD=1: Chip enabled
        digitalWrite(P_LORA_GC1109_PA_TX_EN, LOW); 
    } else if(fem_type==KCT8103L_PA) {
        digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
        if(lna_enabled) {
            digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);   // LNA on
        } else {
            digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);  // LNA bypass
        }
    }
}

void LoRaFEMControl::setRxModeEnableWhenMCUSleep(void)
{
    digitalWrite(P_LORA_PA_POWER, HIGH);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);
    if(fem_type==GC1109_PA) {
        digitalWrite(P_LORA_GC1109_PA_EN, HIGH);
        rtc_gpio_hold_en((gpio_num_t)P_LORA_GC1109_PA_EN);
        gpio_pulldown_en((gpio_num_t)P_LORA_GC1109_PA_TX_EN);
    } else if(fem_type==KCT8103L_PA) {
        digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
        rtc_gpio_hold_en((gpio_num_t)P_LORA_KCT8103L_PA_CSD);
        if(lna_enabled) {
            digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);   // LNA on
        } else {
            digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);  // LNA bypass
        }
        rtc_gpio_hold_en((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
    }
}

void LoRaFEMControl::setLNAEnable(bool enabled)
{
    lna_enabled = enabled;
}
