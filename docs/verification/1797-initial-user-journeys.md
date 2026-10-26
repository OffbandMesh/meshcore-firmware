# Epic D: first firmware user journeys

Date: 2026-10-26. Agent: ChartreuseBasin (session 01a16bc1).
Epic #1797, Feature #1794. Tasks #1837, #1838, #1839, #1840.

## Scope delivered for review

Five guides join the existing WiFi page: install/update, observer MQTT,
time/location privacy, power/recovery, and support logs. The landing page links
the journeys, and the feature catalog records their roles, boards, release
applicability, source assertions and verification limits. This remains a bounded
first manual, not every firmware feature or supported board.

The new pages are checked against published prerelease `offband-v1.6.0-beta1`
(GitHub release is not draft and is marked prerelease) and the current source.
All literal assertions listed for the five new catalog entries were also checked
against that tag's file contents, not only today's branch. The existing WiFi
page retains its separately checked stable `offband-v1.4.0` evidence. No inference
that beta-only/current behavior is present in stable is made.

## Corrections made while reviewing existing references

- GPS driver integration is not proof of physically installed GPS hardware.
  The guide does not label bare Heltec boards as GPS-equipped, or send the old
  `set gps:1` recipe through the observer's unsupported management path.
- MQTT human states now distinguish `waiting-turn` from `held(low-heap)`.
  Saved configuration, live enable/reload, connection and received data are
  separate success checks.
- Fresh independent review found an ambiguous broker endpoint example. The
  connection implementation uses the URI directly; the guide now gives an
  explicit `mqtts://broker.example:8883` example and requires a consistent URI
  scheme, port/path and stored transport/port. A bare hostname or separate port
  field alone is not presented as sufficient.
- The capture helper imports `log_redact.py`; downloading `_cap_serial.py` alone
  is not the recommended workflow. Redaction is not described as proof that an
  arbitrary file is safe to publish.
- Early SafeBoot logging, OTA partition rollback and destructive USB erase are
  separated. No universal recovery command or rollback guarantee is invented.

## Local verification

The catalog initially failed for missing pages and release evidence, before the
guides and verified beta applicability were supplied. Final catalog/source/link
checks pass. A strict MkDocs build produces all five pages and search entries.
Rendered inspection finds no literal `text ` prefix in command blocks and
confirms beta applicability is visible on each new page. Search indexes safety,
broker and support content. The six existing rendered-publication tests pass.

An initial build into the new worktree's default output directory hit a local
filesystem permission boundary. Rebuilding into the task's authorized preview
artifact directory succeeded. This was a local output-path issue, not a passing
build claim for the failed attempt. No global dependencies were changed.

Independent source/usability review found the MQTT URI issue above and no other
important factual findings. No firmware code, client repository, hooks, hardware,
release tags or production site content was changed by authoring these guides.
Publication remains a separate manual owner action after merge.

## Acceptance and known limits

- Owner review of the complete rendered journeys and phone layout is pending.
- No device was flashed, battery drained, broker contacted or GPS tested for this
  work. App controls are described by firmware capability without inventing
  version-specific client screen names.
- New stable-release coverage beyond the existing WiFi evidence is not claimed.
- The ESP32 installation/recovery scope does not supply an unverified nRF52
  bootloader or recovery procedure.
- No proposed diagnostic entities from #440/#445 are imported as shipped;
  their existing parentage remains intact.
- Epic C's sender is still gated by #1800 and credentials. Until enabled, use
  the existing manual website build/publication path for accepted guide changes.
- CI, merged-source assembly and live publication verification must be recorded
  after the relevant steps happen. Do not close Epic D solely on draft page count.
