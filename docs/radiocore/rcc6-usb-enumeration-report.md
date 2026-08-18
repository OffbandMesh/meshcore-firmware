# RCC6 — USB enumeration fails after chip reset

**Unit:** Heltec RadioCore **RCC6** (ESP32-C6 + HT-RA62A / SX1262), beta programme
**Carrier schematic:** `RCC6-L62_V1.0`
**Reported by:** Offband / OffbandMesh — `meshcore-firmware#818`
**Date of testing:** 2026-08-17 → 2026-08-18

---

## 1. Summary

After **any chip-level reset** (CHIP_PU / RST button / external reset line), the RCC6
re-attaches to USB and then **fails enumeration at the descriptor stage**. The host marks
it `Device Descriptor Request Failed`. The board itself boots and runs completely normally
throughout — this is not a crash or a hang.

Recovery requires a **physical USB disconnect and reconnect** every time.

A reset issued by the flashing tool (DTR/RTS, through the USB-Serial/JTAG peripheral) does
**not** trigger the fault. Only a chip-level reset does.

**This reproduces on Heltec's own example firmware**, so it is not caused by our
application code.

---

## 2. Test environment

| | |
|---|---|
| Board | RadioCore RCC6, ESP32-C6 (QFN40) rev v0.2, 16 MB flash |
| Base MAC | `58:8c:81:2f:91:f8` |
| USB mode | USB-Serial/JTAG (native), enumerates `303A:1001` |
| Host | Windows 11 Pro 10.0.26220, Intel USB 3.20 xHCI |
| Reset source | external open-drain pull-down on the carrier's RST line (P1 pin 18), 100 ms assert |
| Boot observation | second board sniffing RCC6 `U0TXD` (P1 pin 12) at 115200 8N1 |

The UART0 sniffer is important to the method: because USB is the thing failing, **all boot
observation was done on a separate wire**, independent of USB.

---

## 3. Reproduction

Minimal, and reproduced many times over two sessions:

1. Power the RCC6 over USB-C. It enumerates normally as `303A:1001`.
2. Confirm the host has a working serial device (e.g. `COM45`).
3. Issue a **chip reset** — press RST, or pull the carrier's RST line low for ~100 ms.
4. Observe on the UART0 sniffer that the board boots normally:

   ```
   ESP-ROM:esp32c6-20220919
   Build:Sep 19 2022
   rst:0x1 (POWERON),boot:0xd (SPI_FAST_FLASH_BOOT)
   SPIWP:0xee
   mode:DIO, clock div:2
   load:0x40875720,len:0x1134
   load:0x4086c110,len:0xc44
   load:0x4086e610,len:0x2e7c
   entry 0x4086c110
   ```

5. Observe the host: the USB device is **gone**, and a failed device appears instead.

**Result — 100 % reproducible.**

### Host-side state after the reset

```
Status  : Error
Problem : CM_PROB_FAILED_POST_START
Device  : USB\VID_0000&PID_0002\7&188F3BF2&0&1
Name    : Unknown USB Device (Device Descriptor Request Failed)
```

`VID_0000&PID_0002` is the placeholder Windows assigns when it cannot read a device
descriptor. The audible device-disconnect notification sounds at the moment of reset, so
the host **does** observe the detach and **does** begin enumerating the returning device —
enumeration then fails.

### Recovery

Physically unplug and replug the USB-C cable. Enumeration then succeeds normally. Nothing
short of that recovers it: not repeated resets, not download-mode entry, not time.

---

## 4. The board is running the whole time

This is the part that makes the fault unusual, and it is why we are confident it is not a
crash.

With USB absent, the board continues to run and can be observed on UART0. On our own
firmware, with logging mirrored to raw UART0, the application runs indefinitely:

```
[3208] DEBUG: Set _preambleMillis=182
[3209] DEBUG: RX Boosted Gain Mode: Enabled
[3619] DEBUG: RadioLibWrapper: noise_floor = -98
[5235] DEBUG: RadioLibWrapper: noise_floor = -95
[7236] DEBUG: RadioLibWrapper: noise_floor = -99
   ... continues every 2 s
```

The SX1262 initialises and reports a live, varying noise floor. The T108 display
initialises and paints. Only USB is affected.

---

## 5. What has been ruled out, and how

| Candidate | Status | Method |
|---|---|---|
| Our application firmware | **Ruled out** | Reproduces on Heltec's own `radiocore-c6-tft-test.ino` |
| A specific firmware role | **Ruled out** | Reproduces on repeater, USB companion, and the vendor test sketch |
| Application hang / crash | **Ruled out** | Board runs for minutes with radio and display live, observed on UART0 |
| Display / SPI activity | **Ruled out** | Reproduces with the display driver absent entirely |
| Host OS state | **Ruled out** | Full Windows reboot; behaviour unchanged |
| Host USB stack fault | **Unlikely** | Both xHCI controllers and both root hubs report `OK`; five other USB serial devices on the same host work normally throughout |
| Reset duration | **Unlikely** | Longer reset assertions do not change the outcome |

### Firmware images tested

| Firmware | Origin | Enumerates after chip reset |
|---|---|---|
| `heltec_rcc6_repeater` (+ display, + diag) | ours | **no** |
| `heltec_rcc6_companion_radio_usb` | ours | **no** |
| `radiocore-c6-tft-test.ino` | **Heltec-derived** | **no** |

---

## 6. The discriminating observation

Two reset paths, different outcomes:

| Reset path | USB after reset |
|---|---|
| Flashing tool reset (DTR/RTS via USB-Serial/JTAG) | **enumerates normally** |
| Chip reset (CHIP_PU pulled low externally, or RST button) | **fails to enumerate** |
| VBUS cycle (physical replug) | **enumerates normally** |

In all three cases the board boots identically, as confirmed on UART0. The difference is
confined to USB enumeration.

---

## 7. Circuit context

Read from `RCC6-L62_V1.0` schematic, symbol U2 (`Type-C_16P`):

```
USB-C A6/B6 (DP)  ->  USB_P  ->  R5 22R  ->  D_P  ->  ESP32-C6 pin 19 (GPIO13)
USB-C A7/B7 (DN)  ->  USB_N  ->  R3 22R  ->  D_N  ->  ESP32-C6 pin 18 (GPIO12)
CC1 -> R4 5.1K -> GND
CC2 -> R7 5.1K -> GND
VBUS -> power rails only
```

Notable:

- **No external D+ pull-up** — bus attach is signalled solely by the C6's internal
  USB-Serial/JTAG pull-up.
- **VBUS is not routed to any SoC pin** — the SoC has no host-presence signal.
- 22 R series resistors only; no ESD parts on the D lines.

We are **not** proposing a mechanism from this. It is included as context for whoever has
the design files.

---

## 7a. Root cause — measured on the USB bus

Captured with the Windows `Microsoft-Windows-USB-USBHUB3-Analytic` channel enabled, which
records hub port state and control-transfer results. Both runs are on **the same physical
port** (hub port 1, port path 6→1→1) with **the same board**, minutes apart.

### Control — VBUS cycle (physical replug). Enumerates.

```
11:25:31.016  port=1  status=0x0100  [POWER]
11:25:32.608  port=1  status=0x0101  [CONNECT|POWER]
11:25:32.728  port=1  status=0x0103  [CONNECT|ENABLE|POWER]
```

Device descriptor then reads correctly:

```
fid_DeviceInterfacePath : \??\USB#VID_303A&PID_1001#58:8C:81:2F:91:F8#{...}
fid_DeviceDescriptor    : 12010002EF0201403A300110020101020301
                          bcdUSB 0200 | class EF/02/01 (IAD) | idVendor 303A | idProduct 1001
```

### Failure — chip reset (CHIP_PU). Does not enumerate.

```
11:28:14.440  port=1  status=0x0100  [POWER]
11:28:14.544  port=1  status=0x0301  [CONNECT|POWER|LOW_SPEED]     <-- note LOW_SPEED
11:28:14.660  port=1  status=0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]
11:28:15.212  port=1  status=0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]   retry 1
11:28:15.832  port=1  status=0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]   retry 2
11:28:16.452  port=1  status=0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]   retry 3
11:29:04.058  port=1  status=0x0103  [CONNECT|ENABLE|POWER]         <-- LOW_SPEED clears, too late
```

Accompanied by:

```
Event 160  Request for Device Descriptor Failed
Event  75  Validation Failure of Device Descriptor
Event 132  Device Control Transfer Error
             fid_UsbdStatus = 0xC0000004   USBD_STATUS_STALL_PID
             fid_NtStatus   = 0xC0000001   STATUS_UNSUCCESSFUL
Event  62  Retry Enumeration   x3
```

Then the placeholder device is created **on the same port**:

```
\??\USB#VID_0000&PID_0002#7&188f3bf2&0&1   port=1
```

### Reading

`PORT_LOW_SPEED` (wPortStatus bit 9) means the hub detected low-speed signalling at attach.
A low-speed device pulls up **D−**; a full-speed device pulls up **D+**. The ESP32-C6
USB-Serial/JTAG is a **full-speed** device and enumerates as such after a VBUS cycle — same
port, same board, minutes apart.

So following a CHIP_PU reset the device appears on the bus with **low-speed signalling**.
The host commits to low-speed, every subsequent control transfer stalls, and after three
retries enumeration is abandoned. The `LOW_SPEED` bit clears ~50 s later, but the host has
already given up and will not retry unattended.

**The single controlled difference between working and failing is the detected bus speed at
attach.** Everything else — port, board, host, cable, hub — is identical.

## 8. What we are asking

**Why does the RCC6 present low-speed USB signalling after a CHIP_PU reset, when the same
board on the same port presents full-speed correctly after a VBUS cycle?**

The host's speed detection is made from the D+/D- pull-up state at attach. That state is
wrong following a chip reset and correct following a power cycle, which points at the USB
PHY's pull-up configuration across CHIP_PU reset rather than at anything above it.

Specifically, we would like to know:

1. Is this **expected behaviour** for ESP32-C6 native USB, or particular to this carrier?
2. Is it **reproducible on other RCC6 units**? We have one unit. It is entirely possible
   this is a defect in our sample rather than a design issue, and we would rather establish
   that than assume either way.
3. Is there a **recommended workaround** — a strapping option, an eFuse setting, or a
   firmware step at boot?

### Why it matters in practice

Any deployment where the device resets without a human present cannot recover: watchdog
reset, brownout recovery, OTA reboot, or a `reboot` command all leave the unit off the bus
until someone physically unplugs it. On a fixed or remote node that is not recoverable at
all.

---

## 9. Separate feedback — datasheet defects

Found while investigating; unrelated to the fault but worth correcting.

`RCC6_Datasheet_1.0.0.pdf` (retrieved 2026-08-18 from
`resource.heltec.cn/download/RadioCore/RCC6/datasheet/`) contains **RC32 content**:

- §3.2 reads *"This section specifies which **RC32** pins are used for various RF module
  configurations"* and *"**RC32-L62** is equipped with the HT-RA62A LoRa module"*, inside a
  document titled RCC6.
- The 20-pin header table lists **ESP32-S3 pins** — `MTDO, GPIO40`, `MTCK, GPIO39`,
  `GPIO38, FSPIWP` — which do not exist on the C6.
- Table 3.1 names the MCU **"ESP32-S3C6"**.

This is actively misleading: the §3.2.1 LoRa pin table appears under an RC32 heading in an
RCC6 document, and cost us significant time establishing which board the numbers applied
to.

The datasheet also documents **nothing** about USB behaviour, strapping pins, or boot mode
— one mention, `USB-C; B2B; 2 x 10 Pin Headers`.

**Useful documentation that is correct and was not found in the datasheet:** footnote ① on
the specification page — *"SPI0 and SPI1 are used to drive the Flash. When a LoRa module is
installed, SPI2 is utilized for connecting the LoRa module."* That fact is essential for
display bring-up (it is why the T108 must be driven with bit-banged SPI) and deserves more
prominence than a footnote.

---

## 10. Appendix — raw evidence

### Boot across a failing reset (UART0, board running, USB absent)

```
>>> RST asserted
>>> RST released
ESP-ROM:esp32c6-20220919
Build:Sep 19 2022
rst:0x1 (POWERON),boot:0xd (SPI_FAST_FLASH_BOOT)
SPIWP:0xee
mode:DIO, clock div:2
load:0x40875720,len:0x1134
load:0x4086c110,len:0xc44
load:0x4086e610,len:0x2e7c
entry 0x4086c110
```

### ROM download mode also affected

Forcing BOOT+RST produces a healthy ROM download prompt on UART0 while USB stays absent:

```
rst:0x1 (POWERON),boot:0x5 (DOWNLOAD(USB/UART0/SDIO_FEI_REO))
waiting for download
```

The ROM is up and listening. No application code has executed at this point.

### Chip identification (during a successful enumeration)

```
Chip type          : ESP32-C6 (QFN40) (revision v0.2)
Features           : Wi-Fi 6, BT 5 (LE), IEEE802.15.4, Single Core + LP Core, 160MHz
Crystal frequency  : 40MHz
USB mode           : USB-Serial/JTAG
BASE MAC           : 58:8c:81:2f:91:f8
```

---

## 11. Limits of this report

Stated plainly so nothing here is over-read:

- **We have one RCC6.** A sample-specific defect is not excluded and is a live possibility.
- **We have no hardware USB analyser.** Port state and control-transfer results are taken
  from the host's own hub driver tracing (`USBHUB3-Analytic`), not from a bus capture. That
  is host-side truth about what the hub observed; it is not an oscilloscope on D+/D-.
- **We have not measured the D+/D- lines directly.** The low-speed detection is reported by
  the hub; we have not scoped which line is pulled up. That measurement would confirm the
  reading in section 7a and we would welcome it being done at your end.
- **Earlier revisions of this report proposed two other mechanisms, both wrong.** They were
  refuted by subsequent measurement and have been removed. Section 7a is measured, with a
  same-port control, and is the only mechanism we stand behind.
