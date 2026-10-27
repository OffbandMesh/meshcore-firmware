# Firmware manual launch inventory

This is a maintainer coverage inventory, not a claim of hardware validation. The published
manual uses only `docs/user/`. The feature catalog in that tree is checked by
`scripts/check_user_docs.py`; catalog metadata itself is excluded from the site.

| User journey | Existing material to assess | Manual coverage | Next owner |
|---|---|---|---|
| Choose an image, install, update and check version | README.md, VERSIONING.md, release notes, pio-flash.py | Published, source-checked against beta1; new hardware trial pending | Epic D, installation |
| Connect an observer to WiFi | observer-cli-commands.md, WifiConfigProvider and WifiBootstrap | Documented, source-checked; new hands-on guide validation pending | Epic A |
| Publish observer data to MQTT | ObserverCli, broker pool and clock implementation | Published, source-checked against beta1; deeper authentication/rotation backfill below | Epic D, then H |
| Set time/GPS and choose location sharing | Companion preference/API, observer snapshot and clock paths | Published; app/hardware trial pending | Epic D, observer operation |
| Understand SafeBoot and recover safely | SafeBoot and ESP32Board boot-safety implementation | Published ESP32 guide; concept/value expansion below, no new low-voltage trial | Epic D, then G |
| Collect diagnostics for support | Capture/redaction scripts and role-specific command handlers | Published, source-checked against beta1; rig/build instruction backfill below | Epic D, then G |

These six journeys were merged in #1816/#1841 and manually published on 2026-10-26
by site run 40405972459. That delivery does not close hardware-only evidence gaps.

## Owner-requested initial-population backfill, 2026-10-27

The owner identified these as missing, frequently asked about or under-used.
They remain under Feature #1794, with named tasks rather than unbounded additions
to the already delivered Epic D. The grouping is the plan amendment's proposal.

| User question | Existing evidence to audit | Delivery |
|---|---|---|
| What is repeater with telemetry, what does it do, how is it configured? | README role/status, WifiTelemetry/RepeaterMqttPool and command/config handlers | F #1843, F1 #1846 |
| How does Home Assistant integration work and what entities are exposed? | Actual discovery/state emission in WifiTelemetry and released payloads; #440/#441/#445 provenance | F2 #1847, verification #1848 |
| What is SafeBoot and why is it useful? | Existing power/recovery guide, SafeBoot and sensing/wake/build paths | G #1844, G1 #1849 |
| How does the watchdog function and what should an operator do after a trigger? | Task/internal/external watchdog implementations and guard/self-hang tests, by platform/build | G2 #1850 |
| How are diags builds used with a rig, and how is a supported rig assembled? | diag-build workflow, diagnostics references, companion_harness and rig_boot_smoke; board pin/interface evidence | G3 #1851, verification #1852 |
| How is the complete observer configured and operated? | Shipped command/typed-config capability inventory, existing WiFi/MQTT/location/support pages | H #1845, map #1853 and setup #1854 |
| What do held/heap, failed and broker rotation mean, and what action fixes a fault? | Broker/pool state transitions, human and machine status formatting, reload/re-enable behavior | H3 #1855 |
| Which URI, transport, CA and JWT settings actually connect a broker? | URI construction, authentication/trust handlers, seeded config and broker-specific operator evidence | H4 #1856, verification #1857 |

Hardware or service behavior must not be inferred from a source flag alone.
Reference-rig wiring needs verified pin/voltage/interface assumptions; absent
evidence blocks that procedure. This documentation task does not build a rig,
flash a device, drain a battery or inject a watchdog hang. Do not publish private
lab addresses, device registry data or credentials from old working notes.

Preserve #755's board-capability matrix and #287's nRF SafeFlash work. Use delivered
results as evidence without reparenting or duplicating their implementation.
H1's release-specific capability map defines the observer backfill boundary;
future features and further discoveries become separate named backlog decisions.

The existing technical documents remain where they are; their existence is not
approval to publish them wholesale. Some contain maintainer procedures and
environment-specific examples. Import selected public explanations only after
checking behavior, release applicability, examples and recovery instructions.
Architecture, plans, handoffs, consultations and local files are outside the
manual boundary, regardless of whether they are tracked in Git.

Firmware #440/#445 retains ownership of diagnostic entity provenance and the
Why Offband page. Do not document proposed entities as delivered. The initial
WiFi guide deliberately does not expand into client instructions or claim that
a WiFi connection proves MQTT publishing.

## Evidence and verification limits

The observer command surface, write-only password reads, NVS persistence,
reboot-to-apply policy, state vocabulary and reserved channel are checked against
their implementations. The basic commands and observer images also exist in the
`offband-v1.4.0` source tag. This is source evidence, not a new hardware trial.
Later changes to CLI behavior or the catalog's literal assertions require guide
review; a passing text assertion alone does not establish semantic correctness.

Impact-record policy and real-diff enforcement were merged in #1836. C3's
automatic rebuild notification remains gated by #1800 and a scoped credential;
production publication is deliberately manual. The docs workflow validates and
builds the manual independently of firmware CI's docs-only classifier.
