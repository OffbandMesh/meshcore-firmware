/* Linker script to configure memory regions. */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx)     : ORIGIN = 0x26000, LENGTH = 0xED000 - 0x26000

  /* SRAM required by Softdevice depend on
   * - Attribute Table Size (Number of Services and Characteristics)
   * - Vendor UUID count
   * - Max ATT MTU
   * - Concurrent connection peripheral + central + secure links
   * - Event Len, HVN queue, Write CMD queue
   */ 
  RAM (rwx) :  ORIGIN = 0x20006000, LENGTH = 0x20040000 - 0x20006000
}

SECTIONS
{
  . = ALIGN(4);
  .svc_data :
  {
    PROVIDE(__start_svc_data = .);
    KEEP(*(.svc_data))
    PROVIDE(__stop_svc_data = .);
  } > RAM
  
  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM

  /* Offband #361 (Epic #350): retained RAM for the nRF52 crash breadcrumb.
   *
   * The Adafruit nRF52 core linker defines no .noinit section, and
   * gcc_startup_nrf52840.S zeroes .bss (__STARTUP_CLEAR_BSS) on every reset,
   * so an ordinary variable cannot survive a watchdog reboot. GPREGRET2 is not
   * an alternative -- all 8 bits are allocated (SafeBoot owns bit 7 plus
   * attempts/backoff/unclean; NRF52Board writes SHUTDOWN_REASON_* codes).
   *
   * NOLOAD: nothing is copied in from flash, so contents persist across a soft
   * or watchdog reset. NOT retained across a power cut -- accepted trade.
   *
   * INSERT AFTER .data places this BEFORE .bss, i.e. outside
   * __bss_start__..__bss_end__, which is what keeps the startup zero-loop off
   * it -- the same reason .svc_data / .fs_data above survive.
   *
   * Lives in this repo-local override, never in the vendored PlatformIO core
   * script: that copy is machine-local, unversioned, absent from CI, and is
   * destroyed on package reinstall.
   */
  .noinit (NOLOAD) :
  {
    PROVIDE(__start_noinit = .);
    KEEP(*(.noinit))
    KEEP(*(.noinit.*))
    PROVIDE(__stop_noinit = .);
  } > RAM
} INSERT AFTER .data;

INCLUDE "nrf52_common.ld"
