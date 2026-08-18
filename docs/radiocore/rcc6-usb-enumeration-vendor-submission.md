# RadioCore RCC6 — USB enumeration fails after chip reset on certain hubs

**Product:** Heltec RadioCore **RCC6** (ESP32-C6 + HT-RA62A / SX1262), beta programme
**Carrier:** RCC6-L62 V1.0
**Testing dates:** 2026-08-17 → 2026-08-18
**Sample size:** one unit

---

## 1. What we observed

After a **chip-level reset** — the RST button, or the carrier's RST line pulled low — the
RCC6 re-attaches to USB and **fails enumeration**, when connected through certain USB hubs.
The host reports `Device Descriptor Request Failed` and the device is unusable until the
USB cable is **physically unplugged and reconnected**. Further resets do not recover it.

The board itself is completely unaffected: it boots normally, the SX1262 initialises and
reports a live noise floor, and the display works. Only USB is lost. We confirmed this by
watching the boot on a separate UART0 wire while USB was absent.

On the failing configuration, the host detects the device as **low speed**. The RCC6 is a
full-speed device, so this is incorrect. Every control transfer then stalls, three
enumeration attempts fail, and the host abandons the device.

**This is not caused by application firmware.** It reproduces with no firmware executing at
all — see §5.

---

## 2. Test environment

| | |
|---|---|
| Board | RadioCore RCC6, ESP32-C6 (QFN40) rev v0.2, 16 MB flash |
| Base MAC | `58:8c:81:2f:91:f8` |
| USB | native USB-Serial/JTAG, enumerates `303A:1001` |
| Host | Windows 11 Pro 10.0.26220 |
| Host controller | Intel USB 3.20 eXtensible Host Controller 1.20 (Microsoft driver) |
| Cable | **one** USB-C cable, 240 W rated, used for **every** trial; manufacturer's permanently-attached USB-A adapter fitted when a USB-A port was used |
| Reset method | external open-drain pull-down on the carrier RST line (P1 pin 18), 100 ms; also reproduced with the RST button |
| Boot observation | second MCU sniffing RCC6 `U0TXD` (P1 pin 12) at 115200 8N1 |

Boot was observed on a **separate wire** throughout, because USB is the thing failing.

### Upstream ports tested

| Result | Product | Immediate parent hub |
|---|---|---|
| **FAILS** | Wenter 11-Port Powered USB 3.0 Hub (Amazon ASIN `B08YNPXPRW`) | Realtek **RTS5411** (`0BDA:5411`) |
| succeeds | Baseus Spacemate 11-in-1 Thunderbolt dock, **front USB-C port** (ASIN `B0CYSKGWCL`) | Terminus **FE1.1s** (`1A40:0101`) |
| succeeds | Same Baseus dock, **USB-A port** | **VIA Labs** (`2109:2822`) |
| succeeds | Motherboard xHCI root port, direct | Intel |

Full enumerated chains:

```
FAILS     RCC6 -> Realtek RTS5411 (0BDA:5411) -> Intel xHCI root
SUCCEEDS  RCC6 -> Terminus FE1.1s (1A40:0101) -> VIA Labs (2109:2822) -> Intel xHCI root
SUCCEEDS  RCC6 -> VIA Labs (2109:2822) -> Intel xHCI root
SUCCEEDS  RCC6 -> Intel xHCI root (direct)
```

**Four distinct upstream silicons; only the Realtek RTS5411 fails.**

Neither hub is faulty. Every other USB device on both worked normally throughout, and the
RCC6 enumerates through either after a power cycle.

---

## 3. Reproduction

1. Connect the RCC6 through a **Realtek RTS5411**-based hub. (On the other ports we tested,
   the fault does not occur — see §2.)
2. Confirm it enumerates normally as `303A:1001`.
3. Press RST, or pull the carrier RST line low for ~100 ms.
4. On UART0 you will see a completely normal boot:

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

5. On the host, the device is gone and a failed device appears:

   ```
   Status  : Error
   Problem : CM_PROB_FAILED_POST_START
   Device  : USB\VID_0000&PID_0002\...
   Name    : Unknown USB Device (Device Descriptor Request Failed)
   ```

**Reproduced 4 of 4 on the RTS5411**, across two of its ports. **0 of 5** on the other three
upstream ports.

Recovery: physically unplug and reconnect the USB cable.

---

## 4. Bus-level detail

Captured using the Windows `Microsoft-Windows-USB-USBHUB3-Analytic` channel, which records
hub port state and control-transfer results.

### Failing — Realtek RTS5411

```
15:51:54.415  port=1  status=0x0100  [POWER]
15:51:54.519  port=1  status=0x0301  [CONNECT|POWER|LOW_SPEED]
15:51:54.635  port=1  status=0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]
15:51:55.2xx  port=1  status=0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]    retry 1
15:51:55.8xx  port=1  status=0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]    retry 2
15:51:56.4xx  port=1  status=0x0303  [CONNECT|ENABLE|POWER|LOW_SPEED]    retry 3
```

With:

```
Request for Device Descriptor Failed          x4
Validation Failure of Device Descriptor       x4
Device Control Transfer Error                 x4
    UsbdStatus = 0xC0000004   USBD_STATUS_STALL_PID
    NtStatus   = 0xC0000001   STATUS_UNSUCCESSFUL
Retry Enumeration                             x3
```

`PORT_LOW_SPEED` (wPortStatus bit 9) is set at connect and **persists through every retry**.
Once the host has committed to low-speed signalling, the control transfers cannot succeed.

### Succeeding — Terminus FE1.1s, same board, same cable

```
16:30:08.455  port=3  status=0x0100  [POWER]
16:30:08.559  port=3  status=0x0301  [CONNECT|POWER|LOW_SPEED]
16:30:08.563  port=3  status=0x0101  [CONNECT|POWER]              <-- corrected after 4 ms
16:30:08.687  port=3  status=0x0103  [CONNECT|ENABLE|POWER]       -> enumerates, no failures
```

Second trial on the same hub, identical timing:

```
16:31:19.592  port=3  status=0x0301  [CONNECT|POWER|LOW_SPEED]
16:31:19.596  port=3  status=0x0101  [CONNECT|POWER]              <-- corrected after 4 ms
16:31:19.720  port=3  status=0x0103  [CONNECT|ENABLE|POWER]
```

**The same incorrect low-speed indication appears here — and is corrected 4 ms later.** This
hub re-samples; the RTS5411 does not.

### Reference — a normal power-cycle attach

```
11:25:31.016  port=1  status=0x0100  [POWER]
11:25:32.608  port=1  status=0x0101  [CONNECT|POWER]
11:25:32.728  port=1  status=0x0103  [CONNECT|ENABLE|POWER]
```

Descriptor then reads correctly:

```
DeviceInterfacePath : \??\USB#VID_303A&PID_1001#58:8C:81:2F:91:F8#{...}
DeviceDescriptor    : 12010002EF0201403A300110020101020301
                      bcdUSB 0200 | class EF/02/01 (IAD) | idVendor 303A | idProduct 1001
```

---

## 5. Firmware is not involved

Three separate images were tested — two of ours (a repeater role and a USB-companion role,
both MeshCore-derived) and **your own NV3001B TFT test sketch**. All three fail identically
on the RTS5411.

More conclusively: **the fault also occurs in ROM download mode, with no application image
running at all.** Holding BOOT across the reset stops the chip in the ROM serial loader:

```
rst:0x1 (POWERON),boot:0x5 (DOWNLOAD(USB/UART0/SDIO_FEI_REO))
waiting for download
```

The low-speed attach and the descriptor failures occur exactly as before. No firmware of any
kind is executing at that point.

Timing on the normal-boot trials agrees independently — the host detects the low-speed
condition **before the second-stage bootloader hands off to the application**:

```
15:28:14.544   host detects CONNECT|LOW_SPEED
15:28:14.577   UART0: entry 0x4086c110        <-- bootloader -> application, 33 ms LATER
   [3208] ms   first application output       <-- ~3.2 s after that
```

---

## 6. Variables eliminated

| Variable | Status | How |
|---|---|---|
| Application firmware | eliminated | reproduces with no firmware running (ROM download mode) |
| Firmware role / build | eliminated | three images incl. your own test sketch |
| Board hang or crash | eliminated | board runs normally throughout, observed on UART0 |
| Display / SPI activity | eliminated | reproduces with the display driver absent entirely |
| Host OS state | eliminated | full Windows reboot, behaviour unchanged |
| Host USB stack | eliminated | controllers and root hubs healthy; five other USB serial devices work throughout, including on the same hub |
| USB cable | eliminated | one cable used for every trial |
| USB-A adapter | eliminated | same cable and adapter into a USB-A port on a passing hub — succeeds |
| Hub port number | eliminated | reproduces on two ports of the failing hub |
| Faulty hub | eliminated | other devices on it work; RCC6 works through it after a power cycle |
| Reset duration | unlikely | longer assertions do not change the outcome |

---

## 7. What we think is happening — and what we have not proven

**What is measured:** on the two hubs that report an intermediate port state, the RCC6
presents an **incorrect low-speed indication at re-attach after a chip reset**. One hub
re-samples 4 ms later, reads full speed, and enumerates. The other latches the first reading
and fails permanently.

**Ruled out:** a static wiring or pull-up fault on D+/D−. A static condition could not
self-correct 4 ms later, and would be detected identically everywhere. It is not.

**Our hypothesis, unproven:** the D+ pull-up assertion after CHIP_PU reset is slow, or the
D+/D− lines are momentarily indeterminate, at the instant a hub samples for speed. A VBUS
power cycle produces a clean attach everywhere, consistent with a power-up ramp that a chip
reset does not reproduce.

**We have not confirmed this.** See §8.

---

## 8. Limits of this report

Stated plainly so nothing here is over-read:

- **We have one RCC6.** A defect specific to our sample is not excluded, and we would like
  to know whether it reproduces on yours.
- **We have no USB protocol analyser and have not scoped D+/D−.** All bus-level information
  comes from the host's own hub driver tracing. The electrical behaviour is inferred.
- **The transient was observed on only two of four upstream ports.** The VIA hub and the
  Intel root port recorded no intermediate low-speed state. Port-status tracing cannot
  distinguish "no transient occurred" from "re-sampled too quickly to report one", so the
  claim that the board always emits it rests on two observations, not four.
- **We have not tested watchdog, brownout, or software-reboot resets** — only external
  CHIP_PU reset and the RST button. Whether those produce the same transient is unverified.
- Only **one failing hub silicon** has been identified. We cannot say how common that
  behaviour is across hubs generally.

---

## 9. What we are asking

1. **Why does the RCC6 present low-speed signalling for the first few milliseconds after a
   chip reset**, given it is a full-speed device? Is the D+ pull-up assertion after CHIP_PU
   reset within USB specification for rise time and connect-debounce?
2. **Does this reproduce on your units?** We have one sample and cannot distinguish a design
   characteristic from a defective board.
3. **Is there a recommended mitigation** — a strapping option, an eFuse setting, or a
   firmware measure — to ensure a clean re-attach after reset?

### Why it matters

Because the outcome depends on the tolerance of whatever the board is plugged into, this
will present as intermittent and setup-specific in the field. On a strict hub, any
unattended reset — watchdog, brownout recovery, OTA reboot, a `reboot` command — leaves the
unit off the bus until someone physically unplugs it. On a fixed or remote installation that
is unrecoverable. On a tolerant hub or a direct port, the identical reset is harmless.

---

## 10. Separate feedback — documentation

Unrelated to the fault, found while investigating.

**`RCC6_Datasheet_1.0.0.pdf` contains RC32 content.** Retrieved 2026-08-18 from
`resource.heltec.cn/download/RadioCore/RCC6/datasheet/`:

- §3.2 reads *"This section specifies which **RC32** pins are used for various RF module
  configurations"* and *"**RC32-L62** is equipped with the HT-RA62A LoRa module"* — inside a
  document titled RCC6.
- The 20-pin header table lists **ESP32-S3 pins** (`MTDO, GPIO40`; `MTCK, GPIO39`;
  `GPIO38, FSPIWP`), which do not exist on the C6.
- Table 3.1 names the MCU **"ESP32-S3C6"**.

This is actively misleading: the §3.2.1 LoRa pin table appears under an RC32 heading in an
RCC6 document, and cost us significant time establishing which board the numbers applied to.

**The datasheet documents nothing about USB behaviour, strapping pins, or boot mode** — a
single mention, `USB-C; B2B; 2 x 10 Pin Headers`.

**A useful fact that deserves more prominence than a footnote.** Footnote ① on the
specification page:

> *"SPI0 and SPI1 are used to drive the Flash. When a LoRa module is installed, SPI2 is
> utilized for connecting the LoRa module."*

This is essential for display bring-up — it is the reason the T108 panel must be driven with
bit-banged SPI rather than a hardware SPI peripheral, and it is not stated anywhere in the
display documentation. We lost time to it before finding this footnote.
