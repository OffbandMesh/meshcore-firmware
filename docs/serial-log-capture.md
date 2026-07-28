# Capturing a serial log from a device

When a device misbehaves (e.g. an observer stops publishing to MQTT), we may ask you to capture its **serial log** and send it back. `scripts/_cap_serial.py` does this and **strips WiFi credentials and tokens** before writing the file.

**You need:** the device on USB, Python 3, and pyserial (`pip install pyserial`).

## Steps

1. Download the tool: [`scripts/_cap_serial.py`](https://raw.githubusercontent.com/OffbandMesh/meshcore-firmware/firmware-base/scripts/_cap_serial.py)
2. Plug the device into USB.
3. Run `python _cap_serial.py` — it auto-detects the port, handles DTR, and writes `observer-serial-<timestamp>.log`.
4. Reproduce the problem, then press **Ctrl-C** to stop.
5. Send us the `.log` file.

## Notes

- WiFi SSID/PSK and tokens are redacted automatically. Don't run any `get <secret>` command (e.g. `get guest_password`) while capturing.
- Several devices plugged in? Add `--port COMx`.
- Empty file? It auto-asserts DTR after 3s; if still empty the device isn't printing.
