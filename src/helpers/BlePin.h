#pragma once

#include <stdint.h>

// #2222: the companion's Bluetooth PIN, set from the phone (CMD_SET_DEVICE_PIN)
// or from the GUI's Settings › Connectivity. A new PIN is handed to the
// Bluetooth stack when it is set, so the next pairing uses it with no reboot.

namespace offband {

// CMD_SET_DEVICE_PIN's rule: 0 (none set: the board picks one), or six digits.
constexpr bool blePinValid(uint32_t pin) { return pin == 0 || (pin >= 100000 && pin <= 999999); }

// The PIN Bluetooth uses once `pin` is set. `stored` is the PIN set before,
// `active` the one in use now, and `unset_pin` what the board uses with none
// set (a random one each boot on a display board, else the build's).
// Clearing a PIN that was already clear keeps the one in use, so the PIN a
// phone was just shown doesn't change under it.
constexpr uint32_t blePinAfterSet(uint32_t stored, uint32_t active, uint32_t pin, uint32_t unset_pin) {
  return pin != 0 ? pin : (stored == 0 && active != 0 ? active : unset_pin);
}

}  // namespace offband
