# Remote diagnostics: caplog live forward + command surface

Capture a node's log lines off-device, live, so the lead-up to a crash survives
the reboot that wipes the node's own RAM ring. This is the tool for a node that
fails in the field where a serial cable isn't practical: a rooftop repeater, a
solar node, an observer in someone else's house.

- **caplog** is the on-device capture: a small RAM ring that tees the mesh log
  (boot / error / debug / packet levels). It is wiped on every reboot, so a
  crash destroys exactly the evidence you want.
- **caplog forward** sends each captured line off-device as it happens, as a
  UDP syslog datagram, to a sink you run (rsyslog on a Pi or any Linux box). The
  lines land in a file that outlives the node's reboot.
- **The command surface** (`offband-cmd`, repeater only) lets you arm the
  forward and read the results from your workstation over the cmdrelay HTTP API.

```
  node (observer / repeater)                  sink host (Pi)
  ┌────────────────────────┐   UDP :514   ┌──────────────────┐
  │ caplog ring  ──forward─┼──────────────▶│ rsyslog (imudp)  │
  │ (boot/err/dbg/packet)  │ caplog-<tag>: │   ─► offband-    │
  └────────────────────────┘               │      caplog.log  │
                                            └──────────────────┘
```

**Which builds have it.** All six observer builds, and the telemetry repeater
(`heltec_v4_repeater_telemetry`). They are built with `OFFBAND_CAPLOG_FORWARD`.
On any other build, `caplog forward` answers `caplog forward: not available on
this build`. A node with no sink set never sends anything.

**The forward needs a WiFi link, and never creates one.** It does not bring WiFi
up, take it down, or keep it up. It sends while a link is there:

- An **observer** keeps its WiFi up all the time for MQTT, so the forward simply
  uses it.
- A **repeater** runs WiFi in short telemetry bursts. Hold the link up with
  `wifi on <minutes>` for as long as you want lines to flow.

While there is no link, captured lines wait on the node, and they are sent when
the link comes back. A long outage can overflow the node's ring; §2 covers what
each role does then.

---

## 1. Stand up the receiver (once, on the sink host)

On the Pi (or any Debian/systemd host running `rsyslog`):

```bash
sudo scripts/offband-caplog-receiver-setup.sh
```

This installs an rsyslog drop-in, `/etc/rsyslog.d/30-offband-caplog.conf`. It
binds a UDP listener on **:514** and routes the devices' datagrams, matched by
their `caplog-<tag>:` tag, into their own file at `/var/log/offband-caplog.log`.
It also installs a weekly logrotate policy (8 kept). The script is idempotent,
so it's safe to re-run.

> The routing matches the devices' **tag** (programname starting with
> `caplog-`), so only caplog lines land in the file, and the host's own syslog
> is untouched: no facility is hijacked. `stop` keeps those matched lines out of
> `/var/log/syslog` too.
>
> A receiver installed before this version routed by facility (`local0.*`)
> instead. Re-run the script to switch it to the tag.

Options (`--help` prints the full man-style version):

- `--port <n>`: UDP port to listen on. Range 1–65535, default 514. Must match
  the device's `set syslog.port`.
- `--log-path <path>`: the file caplog lines are written to. Default
  `/var/log/offband-caplog.log`. Pass the same value on `--uninstall` if you
  changed it.
- `--retention <weeks>`: weekly-rotated logs to keep. Integer ≥ 0, default 8
  (about two months); 0 keeps none.
- `--uninstall`: remove the drop-in and logrotate policy and restart rsyslog.
  Existing logfiles are left in place.
- `--dry-run`: print every change without making it. Combines with install or
  `--uninstall`.

If the host already loads `imudp`, the script adds only the routing rule, so it
won't bind the listener twice. It then tells you to confirm the existing UDP
input covers your port.

**Verify the sink before you touch a device:**

```bash
logger -n <sink-ip> -P 514 -p local0.info -t caplog-test "hello"
tail -n1 /var/log/offband-caplog.log        # -> ... caplog-test hello
```

Routing is by the **tag** (the `caplog-` prefix), which is why the test line
uses `-t caplog-test`. The `-p local0.info` just mirrors what the device sends;
it isn't what selects the file.

If nothing lands, check two things: that the host firewall allows inbound
**UDP 514** (`ufw allow 514/udp` if you run ufw), and that `logger` targeted the
right IP.

> **Security.** UDP syslog is plaintext and unauthenticated. Anyone on the LAN
> can write to the port, and the source is spoofable. Keep the sink on a trusted
> network, and don't expose :514 to the internet. The file may contain packet
> metadata, but no keys or PSKs: the firmware logs only derived properties.

---

## 2. Arm the forward on the node

**Where to type the commands:**

- **Observer:** in the paired app, open the `_sys @ <8hex>` channel and send each
  command as a message; the reply comes back in the same channel. Or type it on
  the USB serial console.
- **Repeater:** the serial console, remote admin over LoRa, or `offband-cmd` (§3).

**The sequence:**

```
set syslog.host <sink-ip>     # where to send; saved, survives a reboot
set syslog.port 514           # only if you changed it from the default
caplog start debug            # capture on; the forward never switches it on
wifi on 30                    # repeater only: hold its WiFi up for 30 minutes
caplog forward on             # send until you turn it off, reboots included
caplog status                 # check: fwd=until-off, and no link=down
```

**Use an IP address for the sink, not a hostname.** The node resolves a hostname
through DNS on the same main loop that services the radio, and a slow or
unreachable DNS server can stall it for seconds. An observer warns about this
in its boot log.

### The commands

These are the same on the observer and the repeater.

| Command | What it does |
|---|---|
| `caplog forward on` | Sends until you turn it off. Survives a reboot: the node re-arms at boot. |
| `caplog forward <sec>` | Sends for a set window: at least 30 s, up to 2147483 s (about 24.8 days). A bare `caplog forward` is 300 s. A window does **not** survive a reboot. |
| `caplog forward off`, `caplog forward 0` | Stops immediately. Capture and WiFi are left as they are. |
| `caplog start [boot\|error\|debug\|packet]` | Capture on, at that level (default `debug`). `packet` is the most detail (every LoRa RX/TX) and the most datagrams. Saved across reboots. |
| `caplog stop` | Capture off. Saved across reboots. |
| `set syslog.host <host>` / `get syslog.host` | The sink address, at most 63 characters; a longer one is refused. |
| `set syslog.port <n>` / `get syslog.port` | The sink port, 1–65535, default 514. |
| `caplog status` | Capture state and the forward's state (below). |

Arming answers with what it will do, or with the first thing that will stop lines
reaching the sink:

| Reply ends with | Meaning |
|---|---|
| `(streaming to syslog)` | Armed, with capture on, a sink set and a link up. |
| `-- capture is off, nothing to send (caplog start)` | Armed, but nothing is being captured. |
| `-- no sink; set syslog.host <host>` | Armed, but there is nowhere to send. |
| `-- no WiFi link; lines send once one is up` | Armed. Lines wait until there is a link. The repeater adds `(wifi on <min>)`. |

A malformed argument answers `ERR: caplog forward on|off|<seconds>`, and a
window longer than 2147483 seconds answers `caplog forward: max 2147483s; use
caplog forward on`. Neither one changes a forward that is already running.

### Which mode is a node in?

`caplog status` answers on one line:

```
caplog: on level=debug used=812/16384 fwd=until-off sink=<sink-ip>:514 sent=40 lost=0
```

- `caplog: on|off`: whether capture is running, then its level and how full the
  ring is.
- `fwd=`: `off`, `until-off`, or the seconds left in a window, e.g. `fwd=287s`.
- `link=down`: appears right after `fwd=` while the forward is armed without a
  WiFi link.
- `sink=`: where lines go, or `none`.
- `sent=`: lines sent. `lost=`: bytes that never reached the sink (see §4).

### Observer and repeater differences

| | Observer | Repeater |
|---|---|---|
| WiFi | Always up | In bursts; hold it with `wifi on <min>` |
| After a line is sent | It stays in caplog, so the app's caplog download still has it | It leaves the ring |
| Lines lost while the link is down | Reported to the sink as `[caplog] forward lost N bytes`, and counted in `lost=` | If the ring overflows, the oldest lines are dropped without a report |
| Tag on each line | The first 16 hex characters of its public key | Its build's node name, at most 24 characters, with anything but letters, digits, `-` and `_` turned into `-` |
| `offband-cmd` | Not available (no cmdrelay) | Available (§3) |

When a save fails, the observer says so, e.g. `ERROR: failed to save syslog.host
(NVS write failed)`. The command still takes effect for this boot, but a reboot
won't keep it.

---

## 3. Drive a repeater from your workstation (`offband-cmd`)

`scripts/offband-cmd.py` wraps the cmdrelay admin API, the telemetry repeater's
HTTP command channel. It lets you queue commands, watch the queue and pull
results without logging into the sink host. Observers don't use cmdrelay; use
their `_sys` channel instead (§2).

Invoke it as `python scripts/offband-cmd.py …`, or symlink it onto your `PATH` as
`offband-cmd` (used below). Configure it once through the environment, so no
secret lives in the repo:

```bash
export OFFBAND_CMDRELAY_URL=http://<cmdrelay-host>:8765
export OFFBAND_CMDRELAY_ADMIN_TOKEN=<admin-bearer-token>
export OFFBAND_PI_SSH=<user>@<sink-host>          # only for `caplog tail`
export OFFBAND_CAPLOG_PATH=/var/log/offband-caplog.log
```

Then:

```bash
offband-cmd queue  <node> "caplog start debug"         # run any admin CLI line on the node
offband-cmd queue  <node> "wifi on 60"                 # hold the repeater's WiFi up
offband-cmd queue  <node> "caplog forward on"          # until off
offband-cmd caplog <node> forward 300                  # or a 300-second window
offband-cmd status <node>                               # liveness: last poll, queue, recent cmds
offband-cmd result <node> <cmd_id> --wait 120
offband-cmd caplog <node> off                           # disarm (capture and WiFi are left as they are)
offband-cmd caplog <node> tail -n 50                    # last 50 forwarded lines for this node
```

The node polls cmdrelay on its own schedule, so a queued command runs on the
next poll; `status` shows when it last checked in. Queued CLI commands are
rate-limited to one per second on the device, so queue them one at a time.

> `caplog tail` is the one operation that still SSHes into the sink host, to read
> the file. Everything else is pure HTTP. That SSH goes away once cmdrelay is
> adopted in-repo with a native `/caplog` endpoint (#572).

---

## 4. Read the logs

```bash
tail -f /var/log/offband-caplog.log                   # everything, live
grep -F 'caplog-<tag>:' /var/log/offband-caplog.log   # one node
grep -F 'forward on: id=' /var/log/offband-caplog.log # which tag is which node
```

**What the node sends:**

- Every line arrives as `caplog-<tag>: [millis] <text>`.
- **The tag.** For an observer it's the first 16 hex characters of its public
  key, which is also the start of its MQTT device id. For a repeater it's the
  node name it was built with.
- **When a forward starts,** the node sends one line naming itself and the sink:
  `[caplog] forward on: id=<full public key> sink=<host>:<port>`. That's how you
  match a tag to a node.
- **After a gap,** an observer sends `[caplog] forward lost N bytes` ahead of the
  lines that follow it. The node's ring filled before those bytes could be sent,
  usually because the link or the sink was down for a while. The count also
  shows in `caplog status` as `lost=`.

rsyslog stamps its own receive time and the source host on ingest. Each line
also carries the device's own `[millis]` prefix, so you can line up device time
against wall-clock. After a reboot, the last lines before the gap are the lead-up
to the event.

### Feeding a central log store (optional)

The sink is standard RFC-3164 syslog, so any log pipeline can consume it without
a custom parser:

- **Loki + Grafana**, the log analog of Prometheus. Point Promtail or Fluent Bit
  at `/var/log/offband-caplog.log`, or add an rsyslog `omfwd` action to the
  drop-in to relay the matched `caplog-*` lines straight to Loki. This is the best
  fit if you already run Grafana.
- **ELK / Graylog / Splunk:** ingest the file, or `omfwd` the matched lines on.

This is **log** data. Prometheus is metrics-only and doesn't consume it. The
metrics surface (battery %, RSSI, heap) is Offband's separate MQTT telemetry,
which a Prometheus MQTT exporter can scrape.

---

## 5. Roll back

```bash
sudo scripts/offband-caplog-receiver-setup.sh --uninstall   # sink host
```

On the node, `caplog forward off` stops sending; add `caplog stop` to stop
capturing too.

Uninstall removes the rsyslog drop-in and logrotate policy and restarts rsyslog.
It leaves the existing logfile in place; delete it by hand if you want it gone.

---

## Troubleshooting

| Symptom | Check |
|---|---|
| `logger` test line never lands | The firewall (UDP 514 inbound), the sink IP, `systemctl status rsyslog`. |
| Armed, but no lines arrive | `caplog status`. `caplog: off` → `caplog start`. `sink=none` → `set syslog.host`. `link=down` → the node has no WiFi (on a repeater, `wifi on <min>`). |
| `[caplog] forward lost N bytes` in the file | The link or the sink was down long enough for the node's ring to fill. Keep the link up, or capture at a lower level. |
| Lines also land in `/var/log/syslog`, or caplog lines are missing from the file | An older receiver routes by facility, or the drop-in's `stop` didn't take. Re-run the setup script and restart rsyslog. |
| Duplicate-input / bind error on restart | The host already loads `imudp`. Re-run the script (it detects this), or remove the older UDP input. |
| Nothing from the minutes before a crash | The forward must already be running when the crash happens. Use `caplog forward on`, which stays armed across reboots. |
