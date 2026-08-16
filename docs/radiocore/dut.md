• nRF + 3.3 V BME680: measure boot, sleep, sensor, RX, and TX consumption using PPK2/INA228.
• ESP32 display: measure USB idle, brightness, animation, Wi-Fi/BLE, RX, and TX consumption using FNB58.
>
Integration:
 Test MeshCore/Meshtastic packets, pairing, reconnects, reference-node compatibility, display controls, and Wi-Fi HaLow if populated.
>
Development:
 nRF outdoor environmental-node firmware plus standalone ESP32 KrabOS with animated packet activity and optional Pi-linked analytics.
Stress:
 Repeated power cycles, connection loss/recovery, sensor faults, simultaneous display/radio activity, and 24/72-hour soak tests.
>
Reporting:
 Pi 5 automatically records timestamps, unit ID, firmware/configuration, power traces, packets, errors, resets, and comparison summaries.
>
Pass target: stable operation, automatic recovery, no unexplained resets, repeatable power results, reliable packet handling, and no display activity disrupting radio performance.