#pragma once

// Where a crash happened, carried across the reset into the log (#1730).
//
// A tester with no serial console can only hand over what reaches the SD card,
// and a panic prints its registers and backtrace to the console alone. So the
// panic handler is wrapped (-Wl,--wrap=esp_panic_handler): before ESP-IDF's
// own handler runs, the faulting PC, the exception cause and address, and up
// to 16 backtrace frames go to RTC memory, which survives the panic reset.
// On the next boot panicRecordReport() writes them as one "[panic]" line,
// which the diag build copies to the card. Decode it against the exact image:
//
//   xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf 0x4200abcd 0x...
//
// Diag builds only (OFFBAND_PANIC_RECORD, with the linker wrap). Xtensa ESP32
// parts only; elsewhere both calls compile to nothing.

namespace offband {

// Writes the record left by the last panic to the log, once, then clears it.
// Call after MeshLog capture is on, so the line is kept.
void panicRecordReport();

}  // namespace offband
