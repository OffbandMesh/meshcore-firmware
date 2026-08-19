#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

// Identity map across the whole nRF52840 GPIO space: index N -> P0.N for 0..31,
// P1.(N-32) for 32..47. That is what lets variant.h address pins as (0 + n) and
// (32 + n), matching the vendor BSP's own notation exactly.
//
// P0.00 / P0.01 are masked (0xff) because they are XL1 / XL2, the 32.768 kHz
// crystal pins -- the board fits the crystal (USE_LFXO), so they are not GPIO.
const uint32_t g_ADigitalPinMap[] = {
  0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
  14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47
};

void initVariant()
{
  // The USER button has an external pull-up and is active low, so a plain INPUT
  // is correct here -- do not add INPUT_PULLUP.
  pinMode(PIN_USER_BTN, INPUT);
}
