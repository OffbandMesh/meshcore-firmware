# Remote diagnostics: caplog live forward + command surface

Capture a node's in-flight log lines off-device, live, during a bounded window —
so the lead-up to a crash survives the reboot that wipes the on-device RAM ring.
This is the tool for a node that panics in the field where a serial cable isn't
practical (a rooftop repeater, a solar node).

- **caplog** is the on-device capture: a small RAM ring that tees the mesh log
  (boot / error / debug / packet levels). It is wiped on every reboot, so a
  panic destroys exactly the evidence you want.
- **caplog forward** streams each captured line off-device as it happens, as a
  UDP syslog datagram, to a sink you run (rsyslog on a Pi or any Linux box). The
  lines land in a file that outlives the node's reboot.
- **The command surface** (`offband-cmd`) lets you arm/disarm the forward and
  read the results from your workstation over the cmdrelay HTTP API — no manual
  SSH-and-curl on the sink host.

```
  node (repeater/observer/…)                 sink host (Pi)            you
  ┌────────────────────────┐   UDP :514   ┌──────────────────┐
  │ caplog ring  ──forward─┼──────────────▶│ rsyslog (imudp)  │
  │ (boot/err/dbg/packet)  │  local0.info  │   ─► offband-    │
  └───────────▲────────────┘  caplog-<node>│      caplog.log  │
              │                             └──────────────────┘
              │ admin CLI (set syslog.*, caplog forward)              ┌──────────┐
              └───────────────── cmdrelay HTTP ─────────────────────── │offband-  │
                                                                       │  cmd     │
                                                                       └──────────┘
```

The forward is **telemetry-only** and only compiled into WiFi-telemetry builds
(e.g. `*_repeater_telemetry`). A node with no sink configured never forwards.

---

## 1. Stand up the receiver (once, on the sink host)

On the Pi (or any Debian/systemd host running `rsyslog`):

```bash
sudo scripts/offband-caplog-receiver-setup.sh
```

That installs an rsyslog drop-in (`/etc/rsyslog.d/30-offband-caplog.conf`) that
binds a UDP listener on **:514** and routes the devices' datagrams — matched by
their `caplog-<node>:` tag — into their own file at
`/var/log/offband-caplog.log`, plus a weekly logrotate policy (8 kept). It's
idempotent — safe to re-run.

> The routing matches the devices' **tag** (`caplog-<node>:`, i.e. programname
> starting with `caplog-`), so only caplog lines land in the file and the host's
> own syslog is untouched — no facility is hijacked. `stop` keeps those matched
> lines out of `/var/log/syslog` too.

Options (`--help` prints the full man-style version):

- `--port <n>` — UDP port to listen on. Range 1–65535, default 514. Must match
  the device's `set syslog.port`.
- `--log-path <path>` — file caplog lines are written to. Default
  `/var/log/offband-caplog.log`; pass the same value on `--uninstall` if you
  changed it.
- `--retention <weeks>` — weekly-rotated logs to keep. Integer ≥ 0, default 8
  (~two months); 0 keeps none.
- `--uninstall` — remove the drop-in + logrotate policy and restart rsyslog;
  leaves existing logfiles in place.
- `--dry-run` — print every change without modifying anything; combines with
  install or `--uninstall`.

If the host already loads `imudp`, the script adds only the routing rule (it
won't double-bind the listener) and tells you to confirm the existing UDP input
covers your port.

**Verify the sink before you touch a device:**

```bash
logger -n <this-host-ip> -P 514 -p local0.info -t caplog-test "hello"
tail -n1 /var/log/offband-caplog.log        # -> ... caplog-test hello
```

Routing is by the **tag** — the `caplog-` prefix (programname), which is why the
test line uses `-t caplog-test`. The `-p local0.info` just mirrors what the
device sends; it isn't what selects the file.

If nothing lands: check the host firewall allows inbound **UDP 514**
(`ufw allow 514/udp` if you run ufw), and that `logger` targeted the right IP.

> **Security.** UDP syslog is plaintext and unauthenticated — anyone on the LAN
> can write to the port, and the source is spoofable. Keep the sink on a trusted
> network; don't expose :514 to the internet. The file may contain packet
> metadata but no keys or PSKs (the firmware logs only derived properties).

---

## 2. Arm the forward on the node

The sink target is a **runtime pref** (#566) — set it once and it persists across
reboots. Set it over the serial console or remotely via the command surface
(§3). Over serial:

```
set syslog.host <this-host-ip>
set syslog.port 514            # only if you changed it from the default
caplog forward 300            # arm a 300-second window; streams to the sink
```

- `caplog forward <sec>` opens a bounded window (min 30 s, default 300),
  enables capture, and brings WiFi up for the duration so lines stream live. It
  auto-reverts (drops WiFi, closes the window) when the timer expires.
- `caplog forward off` disarms immediately and stops capture.
- `get syslog.host` reads back the configured sink (empty = forward off).
- `caplog start [boot|error|debug|packet]` sets the capture verbosity; `packet`
  is the most detail (every LoRa RX/TX). Higher levels = more datagrams.

Choose the window to match the failure. For a node that panics under thermal or
charging stress, arm it when the conditions are present (e.g. mid-day on a solar
node) and keep the window long enough to catch the event — the lines already on
the sink survive the reboot even though the node's own ring does not.

---

## 3. Drive it from your workstation (`offband-cmd`)

`scripts/offband-cmd.py` wraps the cmdrelay admin API so you can queue commands,
watch the queue, and pull results without logging into the sink host. Invoke it
as `python scripts/offband-cmd.py …`, or symlink it onto your `PATH` as
`offband-cmd` (used below). Configure it once via env (no secret lives in the
repo):

```bash
export OFFBAND_CMDRELAY_URL=http://<cmdrelay-host>:8765
export OFFBAND_CMDRELAY_ADMIN_TOKEN=<admin-bearer-token>
export OFFBAND_PI_SSH=<user>@<sink-host>          # only for `caplog tail`
export OFFBAND_CAPLOG_PATH=/var/log/offband-caplog.log
```

Then:

```bash
offband-cmd caplog <node> forward 300      # arm the forward (queues a CLI cmd)
offband-cmd status <node>                   # liveness: last poll, queue, recent cmds
offband-cmd queue  <node> "get syslog.host" # run any admin CLI line on the node
offband-cmd result <node> <cmd_id> --wait 120
offband-cmd caplog <node> off               # disarm + stop capture
offband-cmd caplog <node> tail -n 50        # last 50 forwarded lines for this node
```

The node polls cmdrelay on its own schedule, so a queued command runs on the
next poll — `status` shows when it last checked in. Queued CLI commands are
rate-limited to one per second on the device; queue them one at a time.

> `caplog tail` is the one operation that still SSHes into the sink host (to read
> the file). Everything else is pure HTTP. That SSH goes away once cmdrelay is
> adopted in-repo with a native `/caplog` endpoint (#572).

---

## 4. Read the logs

```bash
tail -f /var/log/offband-caplog.log                 # everything, live
grep -F 'caplog-<node>:' /var/log/offband-caplog.log # one node
```

rsyslog stamps its own receive time and the source host on ingest; each line
also carries the device's own `[millis]` prefix, so you can line up device time
against wall-clock. After a reboot, the last lines before the gap are the
lead-up to the event.

### Feeding a central log store (optional)

The sink is standard RFC-3164 syslog, so any log pipeline can consume it without
a custom parser:

- **Loki + Grafana** — the log analog of Prometheus. Point Promtail or Fluent
  Bit at `/var/log/offband-caplog.log`, or add an rsyslog `omfwd` action to the
  drop-in to relay the matched `caplog-*` lines straight to Loki. Best fit if you
  already run Grafana.
- **ELK / Graylog / Splunk** — ingest the file, or `omfwd` the matched lines on.

This is **log** data. Prometheus is metrics-only and does not consume it — the
metrics surface (battery %, RSSI, heap) is Offband's separate MQTT telemetry,
which a Prometheus MQTT exporter can scrape.

---

## 5. Roll back

```bash
sudo scripts/offband-caplog-receiver-setup.sh --uninstall   # sink host
# device: caplog forward off   (or)   set syslog.host        (empty = off)
```

Uninstall removes the rsyslog drop-in and logrotate policy and restarts rsyslog;
it leaves the existing logfile in place (delete it by hand if you want it gone).

---

## Troubleshooting

| Symptom | Check |
|---|---|
| `logger` test line never lands | firewall (UDP 514 inbound); right sink IP; `systemctl status rsyslog` |
| Device armed, no lines | `get syslog.host` set? window still open (re-arm)? node has WiFi credentials + can associate? |
| Lines in `/var/log/syslog` too | the drop-in's `stop` didn't take — confirm `30-offband-caplog.conf` is present and rsyslog was restarted |
| Duplicate-input / bind error on restart | host already loads `imudp`; re-run the script (it detects this) or remove the older UDP input |
| No datagrams during a panic | the forward window must be open *before* the crash — arm a window long enough to span the failure |
