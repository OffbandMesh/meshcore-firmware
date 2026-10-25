# Firmware manual launch inventory

This is a maintainer inventory, not a list of completed guides. The published
manual uses only `docs/user/`. The feature catalog in that tree is checked by
`scripts/check_user_docs.py`; catalog metadata itself is excluded from the site.

| User journey | Existing material to assess | Manual coverage | Next owner |
|---|---|---|---|
| Choose an image, install, update and check version | README.md, VERSIONING.md, release notes | Planned | Epic D, installation |
| Connect an observer to WiFi | observer-cli-commands.md, WifiConfigProvider and WifiBootstrap | Documented, source-checked; new hands-on guide validation pending | Epic A |
| Publish observer data to MQTT | observer-cli-commands.md, broker configuration implementation | Planned | Epic D, observer operation |
| Set time/GPS and choose location sharing | observer-gps-location-config.md | Needs review before publication | Epic D, observer operation |
| Understand SafeBoot and recover safely | safeboot.md, SafeBoot implementation | Needs review before publication | Epic D, safety |
| Collect diagnostics for support | serial-log-capture.md, remote-diagnostics.md | Needs review before publication | Epic D, diagnostics |

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

Impact-record validation is supplied as a tested interface in Epic A. Epic C
owns making documentation-impact decisions part of Feature/Epic contribution
policy and activating their PR checks. The new docs workflow always validates
and builds the manual independently of firmware CI's docs-only classifier.
