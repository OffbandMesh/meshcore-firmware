# RCC6 — USB enumeration fails after chip reset

**Unit:** Heltec RadioCore **RCC6** (ESP32-C6 + HT-RA62A / SX1262), beta programme
**Carrier schematic:** `RCC6-L62_V1.0`
**Reported by:** Offband / OffbandMesh — `meshcore-firmware#818`
**Date of testing:** 2026-08-17 → 2026-08-18

---

## 1. Summary

Following a chip-level reset (CHIP_PU / RST button / external reset line), the RCC6
re-attaches to USB and **fails enumeration at the descriptor stage** — but **only when the
board is connected through an intermediate USB hub**. On a direct motherboard (xHCI root)
port the same reset enumerates cleanly every time.

When it fails, the host detects the device as **low speed**, every control transfer stalls,
and after three retries the device is marked `Device Descriptor Request Failed`. Recovery
requires a physical USB disconnect and reconnect.

The board itself boots and runs completely normally throughout — this is not a crash or a
hang, and the fault occurs even in ROM download mode with no firmware executing.

**Two conditions are both required:** a chip-level reset, AND a hub in the path. Either one
alone is fine. A VBUS power cycle enumerates correctly even through the hub; a chip reset
enumerates correctly on a root port.

**This reproduces on Heltec's own example firmware and with no firmware running at all**, so
it is not caused by application code — ours or yours.

---

## 2. Test environment

| | |
|---|---|
| Board | RadioCore RCC6, ESP32-C6 (QFN40) rev v0.2, 16 MB flash |
| Base MAC | `58:8c:81:2f:91:f8` |
| USB mode | USB-Serial/JTAG (native), enumerates `303A:1001` |
| Host | Windows 11 Pro 10.0.26220, Intel USB 3.20 xHCI |
| Failing topology | RCC6 -> Realtek RTS5411 hub (`0BDA:5411`) -> host root port |
| Passing topology | RCC6 -> motherboard xHCI root port (`PCIROOT(80)#PCI(1400)#USBROOT(0)#USB(9)`) |
| Reset source | external open-drain pull-down on the carrier's RST line (P1 pin 18), 100 ms assert |
| Boot observation | second board sniffing RCC6 `U0TXD` (P1 pin 12) at 115200 8N1 |

The UART0 sniffer is important to the method: because USB is the thing failing, **all boot
observation was done on a separate wire**, independent of USB.

---

## 3. Reproduction

Minimal, and reproduced many times over two sessions:

0. **Connect the RCC6 through a USB hub.** This is required — see section 6.2. On a direct
   motherboard port the fault does not occur.
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

**Result — 100 % reproducible behind a hub (4 of 4 trials, two ports, two cables). Does not
reproduce on a direct root port (0 of 2 trials).**

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
| Host USB stack fault | **Ruled out** | Both xHCI controllers and both root hubs report `OK`; five other USB serial devices work normally throughout, including on the same hub |
| USB cable | **Ruled out** | Reproduces on a second, different USB-C cable |
| Hub port | **Ruled out** | Reproduces on two different ports of the same hub |
| Faulty hub | **Ruled out** | Other devices on that hub are unaffected; the RCC6 works through it after a VBUS cycle |
| Reset duration | **Unlikely** | Longer reset assertions do not change the outcome |

### Firmware images tested

| Firmware | Origin | Enumerates after chip reset |
|---|---|---|
| `heltec_rcc6_repeater` (+ display, + diag) | ours | **no** |
| `heltec_rcc6_companion_radio_usb` | ours | **no** |
| `radiocore-c6-tft-test.ino` | **Heltec-derived** | **no** |

---

## 6. The discriminating observations

### 6.1 Reset path

| Reset path | USB after reset |
|---|---|
| Flashing tool reset (DTR/RTS via USB-Serial/JTAG) | **enumerates normally** |
| Chip reset (CHIP_PU pulled low externally, or RST button) | **fails** (behind a hub) |
| VBUS cycle (physical replug) | **enumerates normally** |

In all three cases the board boots identically, as confirmed on UART0.

### 6.2 Topology — the second required condition

| Topology | Trials | Chip reset result |
|---|---|---|
| Behind Realtek RTS5411 hub, port 1 | 3 | **fails** — LOW_SPEED, stalls, 3 retries |
| Behind same hub, port 4, **different cable** | 1 | **fails** — identical signature |
| **Motherboard xHCI root port 9 (direct)** | **2** | **clean enumeration, no failures** |

Same board, same firmware, same reset method throughout.

**The hub is not faulty.** Every other device on it works normally throughout (three USB
serial devices), and the RCC6 itself enumerates correctly through that same hub after a
VBUS cycle. The fault requires the hub *and* a chip reset together.

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

### Reproduced three times, including with NO firmware running

| Trial | Reset type | Code executing | `PORT_LOW_SPEED` | Descriptor failures |
|---|---|---|---|---|
| A | CHIP_PU | application | **yes** | 4 / 4 / 4, 3 retries |
| B | CHIP_PU | application | **yes** | 4 / 4 / 4, 3 retries |
| C | BOOT+RST | **none — ROM download mode** | **yes** | 4 / 4 / 4, 3 retries |
| Control | VBUS cycle | application | **no** | none |

Trial C is the decisive one. With BOOT held across reset the chip stops in the ROM
serial loader — UART0 shows `boot:0x5 DOWNLOAD(USB/UART0/SDIO_FEI_REO)` and
`waiting for download`, and no application image is entered at all. The low-speed
attach still occurs:

```
15:52:44.536  port=1  0x0301  [CONNECT|POWER|LOW_SPEED]
15:52:44.656  port=1  0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]
15:52:45.208  port=1  0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]   retry 1
15:52:45.828  port=1  0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]   retry 2
15:52:46.448  port=1  0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]   retry 3
```

**No firmware — ours or Heltec's — is running when this happens.** The behaviour is
established by the ROM and the hardware alone, which removes application software from
consideration entirely rather than by comparison.

Timing on the application-mode trials points the same way independently:

```
15:28:14.544   host detects CONNECT|LOW_SPEED
15:28:14.577   UART0: entry 0x4086c110      <-- bootloader hands off, 33 ms LATER
   [3208] ms   first application output     <-- ~3.2 s after that
```

The bus condition is already set before the second-stage bootloader jumps to the
application.

### Reading

`PORT_LOW_SPEED` (wPortStatus bit 9) means the hub detected low-speed signalling at attach.
A low-speed device pulls up **D−**; a full-speed device pulls up **D+**. The ESP32-C6
USB-Serial/JTAG is a **full-speed** device.

**What this is NOT.** The board is not statically miswired or mis-pulled-up on D+/D−. A
static condition of that kind would be detected identically on a root port — and it is not:
the same board on a direct motherboard port reports `CONNECT|POWER` with no `LOW_SPEED`,
twice out of two.

**What the evidence points to.** The speed detection is *unstable across retries*, not
uniformly wrong. From the port-4 trial:

```
16:08:00.690  0x0303 [CONNECT|ENABLE|POWER|LOW_SPEED]
16:08:00.952  0x0103 [CONNECT|ENABLE|POWER]            <-- no LOW_SPEED
16:08:01.242  0x0303 [CONNECT|ENABLE|POWER|LOW_SPEED]  <-- and back
```

`[hypothesis:]` The re-attach transient produced by a chip reset appears to be marginal —
brief enough, or with a slow enough pull-up ramp, that a hub's speed sampling can latch an
indeterminate state. An xHCI root port tolerates it; this hub does not. A VBUS cycle, which
produces a full power-down and a clean re-attach, works through the same hub.

This is stated as hypothesis, not conclusion. We have not scoped D+/D- and cannot see the
electrical transient. What is measured is: the failure requires a chip reset AND a hub, the
detected speed is unstable rather than consistently wrong, and a root port never sees it.

## 8. What we are asking

**Why does the RCC6's re-attach after a chip reset fail speed detection behind a USB hub,
when the same board, cable and reset enumerate correctly on a direct xHCI root port?**

Specifically: is the device's re-attach transient after CHIP_PU reset within USB
specification for connect-debounce timing and pull-up rise time? The detected speed is
unstable across retries rather than consistently wrong, which suggests a marginal transient
rather than an incorrect steady state — but we cannot see the electrical behaviour and have
not scoped D+/D-.

Specifically, we would like to know:

1. Is this **expected behaviour** for ESP32-C6 native USB, or particular to this carrier?
2. Is it **reproducible on other RCC6 units**? We have one unit. It is entirely possible
   this is a defect in our sample rather than a design issue, and we would rather establish
   that than assume either way.
3. Is there a **recommended workaround** — a strapping option, an eFuse setting, or a
   firmware step at boot?

### Why it matters in practice

Any deployment where the device is behind a hub, dock or powered USB strip and resets
without a human present cannot recover: watchdog reset, brownout recovery, OTA reboot or a
`reboot` command would leave the unit off the bus until someone physically unplugs it.

We have **not** tested watchdog, brownout or software-reboot paths specifically — only
external CHIP_PU reset. Whether they produce the same transient is unverified, though there
is no obvious reason they would differ.

Direct-port connection is an effective workaround on our bench.

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

- **We have one RCC6, and one hub model.** A sample-specific defect is not excluded, and we
  have not tried a second hub — so "any hub" versus "this Realtek RTS5411" is untested.
- **An earlier revision of this report claimed the fault was unconditional.** That was wrong:
  it was written before a direct-root-port test, which we had not run. The topology
  dependence in section 6.2 was found only after the report was first drafted. It is
  recorded here because it is the single most important qualifier in the document.
- **We have no hardware USB analyser.** Port state and control-transfer results are taken
  from the host's own hub driver tracing (`USBHUB3-Analytic`), not from a bus capture. That
  is host-side truth about what the hub observed; it is not an oscilloscope on D+/D-.
- **We have not measured the D+/D- lines directly.** The low-speed detection is reported by
  the hub; we have not scoped which line is pulled up. That measurement would confirm the
  reading in section 7a and we would welcome it being done at your end.
- **Earlier revisions of this report proposed two other mechanisms, both wrong.** They were
  refuted by subsequent measurement and have been removed. Section 7a is measured, with a
  same-port control, and is the only mechanism we stand behind.
