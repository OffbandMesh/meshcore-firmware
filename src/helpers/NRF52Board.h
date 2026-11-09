#pragma once

#include <Arduino.h>
#include <MeshCore.h>

#if defined(NRF52_PLATFORM)

#ifdef NRF52_POWER_MANAGEMENT
// Shutdown Reason Codes (stored in GPREGRET before SYSTEMOFF)
#define SHUTDOWN_REASON_NONE          0x00
#define SHUTDOWN_REASON_LOW_VOLTAGE   0x4C  // 'L' - Runtime low voltage threshold
#define SHUTDOWN_REASON_USER          0x55  // 'U' - User requested powerOff()
#define SHUTDOWN_REASON_BOOT_PROTECT  0x42  // 'B' - Boot voltage protection

// Boards provide this struct with their hardware-specific settings and callbacks.
struct PowerMgtConfig {
  // LPCOMP wake configuration (for voltage recovery from SYSTEMOFF)
  uint8_t lpcomp_ain_channel;       // AIN0-7 for voltage sensing pin
  uint8_t lpcomp_refsel;            // REFSEL value: 0-6=1/8..7/8, 7=ARef, 8-15=1/16..15/16

  // Boot protection voltage threshold (millivolts)
  // Set to 0 to disable boot protection
  uint16_t voltage_bootlock;
};
#endif

class NRF52Board : public mesh::MainBoard {
#ifdef NRF52_POWER_MANAGEMENT
  void initPowerMgr();
#endif

protected:
  uint8_t startup_reason;
  char *ota_name;
  bool _wdt_started = false;          // #257: hardware watchdog has been started

  // #275: nap-independent green-LED heartbeat state (loop-driven; see startHeartbeat).
  bool _hb_started = false;
  bool _hb_on = false;
  bool _hb_msg_waiting = false;       // #1960
  uint32_t _hb_next_ms = 0;
  SoftwareTimer _hb_wake_timer;       // repeating ~10 Hz loop-wake (RTC1) so the blink isn't nap-gated

#ifdef PIN_STATUS_LED
  // #2074: one off-action, used by both setLedEnabled(false) and nothing else -- the tick
  // writes the live phase. LED_STATE_ON carries this variant's polarity, so "dark" is
  // correct on active-high and active-low boards without either being special-cased.
  void ledDark() { digitalWrite(PIN_STATUS_LED, !LED_STATE_ON); }
#endif

#ifdef NRF52_POWER_MANAGEMENT
  uint32_t reset_reason;              // RESETREAS register value
  uint8_t shutdown_reason;            // GPREGRET value (why we entered last SYSTEMOFF)
  uint16_t boot_voltage_mv;           // Battery voltage at boot (millivolts)

  bool checkBootVoltage(const PowerMgtConfig* config);
  void enterSystemOff(uint8_t reason);
  void configureVoltageWake(uint8_t ain_channel, uint8_t refsel);

  // Arm USB-attach (VBUS) wake from SYSTEMOFF. Extracted from
  // configureVoltageWake() so a board can have "plug it in to bring it back"
  // WITHOUT LPCOMP -- which is not free on every board. On one whose battery
  // divider is FET-gated (RC52), keeping LPCOMP fed through SYSTEMOFF means
  // holding the gate transistor on, and a board that shut down to protect a
  // depleted cell would then drain it faster while off than it could recover.
  // USB wake costs no standing current at all.
  void configureUsbWake();

  virtual void initiateShutdown(uint8_t reason);
#endif

public:
  NRF52Board(char *otaname) : ota_name(otaname) {}
  virtual void begin();
  virtual uint8_t getStartupReason() const override { return startup_reason; }
  virtual float getMCUTemperature() override;
  virtual void reboot() override { NVIC_SystemReset(); }
  virtual void shutdownPeripherals();
  virtual void powerOff() override;
  virtual bool getBootloaderVersion(char* version, size_t max_len) override;
  virtual bool startOTAUpdate(const char *id, char reply[]) override;
  virtual void sleep(uint32_t secs) override;
  bool isExternalPowered() override;

  // #257: hardware watchdog (independent of NRF52_POWER_MANAGEMENT).
  // startWatchdog() once at boot; feedWatchdog() from the MAIN LOOP only, so a
  // hung loop trips it -> auto-reboot + RESETREAS=DOG ("Watchdog") at next boot.
  // #446: now overrides the MainBoard runtime-watchdog interface.
  void startWatchdog(uint32_t timeout_secs) override;
  void feedWatchdog() override;
  bool isWatchdogArmed() const override { return _wdt_started; }   // #1159

  // #275 (P0): true, ungated green-LED heartbeat. startHeartbeat() once at boot;
  // heartbeatTick() from the MAIN LOOP only. The LED toggle is loop-driven (so a hung
  // loop freezes it -> the WDT trips), and a repeating SoftwareTimer wakes the loop
  // ~10 Hz so the blink is NOT gated by the power-save nap, UI, display, or traffic.
  void startHeartbeat();
  void heartbeatTick();
  // #1960: lengthens the blip while a message is waiting, where the variant sets a lit time.
  void setHeartbeatMessageWaiting(bool waiting) { _hb_msg_waiting = waiting; }

  // #2074: the operator can switch this indicator off. FOURTH LED shape in this tree --
  // not a transmit-hook TX LED like the ESP32/STM32/TBeam cases, but the heartbeat LED,
  // and the only one whose "dark" level comes from a per-variant macro (LED_STATE_ON), so
  // active-high and active-low boards are both already correct here.
  //
  // #275 is not a blocker on this and was checked rather than assumed: the watchdog is fed
  // by feedWatchdog() from the main loop and never touches an LED, and what keeps an idle
  // loop cycling is the 100 ms RTC1 wake whose callback is deliberately empty. The LED is
  // the HUMAN-readable view of liveness, not the mechanism. #275 forbids it being gated by
  // incidental state -- UI, display, connection, the nap, traffic -- so that it can never
  // lie while it is running; a deliberate request from the operator is not that, and
  // turning an indicator off is precisely what this setting exists for.
  // OFFBAND_LED_NOT_DISABLEABLE is a PER-BOARD OWNER OPT-OUT, set in a variant's own
  // platformio.ini -- today only the QCC badge, by the owner's decision for the conference
  // artifact. It is not a hardware fact and not a judgement this header gets to make: a
  // board that sets it will not honor the setting, so it must not advertise it.
  // #2075: P_LORA_TX_LED too, not just the heartbeat LED. An nRF52 board whose only LED is a
  // TX LED gates it in its own transmit hook but has no other way to ADVERTISE that it can --
  // xiao_nrf52, the Ikoka family, thinknode_m7, meshsmith_photon_nrf52 and others are in that
  // set. Without this they would honor the setting while reporting they cannot.
#if (defined(PIN_STATUS_LED) || defined(P_LORA_TX_LED)) && !defined(OFFBAND_LED_NOT_DISABLEABLE)
  bool canControlLed() const override { return true; }
  bool setLedEnabled(bool on) override {
    _led_enabled = on;
#ifdef PIN_STATUS_LED
    // The HEARTBEAT LED needs darkening now: it blinks continuously at ~10% duty, so waiting
    // for a transmit to end would leave it visibly blinking after the operator switched it
    // off. A TX-LED-only board needs nothing here -- its LED is lit only during a transmit
    // and its own onAfterTransmit() darkens it with its own level, which is also why this is
    // guarded: ledDark() is the heartbeat pin's off-write and does not exist on a board that
    // has no PIN_STATUS_LED. T096 and T114 are exactly that case.
    if (!on) ledDark();
#endif
    return true;
  }
#endif

#ifdef NRF52_POWER_MANAGEMENT
  uint16_t getBootVoltage() override { return boot_voltage_mv; }
  virtual uint32_t getResetReason() const override { return reset_reason; }
  uint8_t getShutdownReason() const override { return shutdown_reason; }
  const char* getResetReasonString(uint32_t reason) override;
  const char* getShutdownReasonString(uint8_t reason) override;
#endif
};

/*
 * The NRF52 has an internal DC/DC regulator that allows increased efficiency
 * compared to the LDO regulator. For being able to use it, the module/board
 * needs to have the required inductors and capacitors populated. If the
 * hardware requirements are met, this subclass can be used to enable the DC/DC
 * regulator.
 */
class NRF52BoardDCDC : virtual public NRF52Board {
public:
  NRF52BoardDCDC() {}
  virtual void begin() override;
};
#endif