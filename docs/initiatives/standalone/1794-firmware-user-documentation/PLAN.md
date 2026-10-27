# Feature PLAN: Firmware documentation users can follow and maintainers keep current

| | |
|---|---|
| Feature | [Firmware #1794](https://github.com/OffbandMesh/meshcore-firmware/issues/1794), Citadel `Crosswire-uuu`. Standalone Feature. |
| Serves | Firmware users learning, configuring and troubleshooting Offband features. |
| Status | Original plan merged in #1802. Foundation, contribution policy and six guides delivered; manual site publication is live. Owner requested additional initial-population backfill on 2026-10-27. Operational acceptance and sender integration remain open. |
| Owner | Strycher |
| Last updated | 2026-10-27 |
| Plan of record | `docs/initiatives/standalone/1794-firmware-user-documentation/PLAN.md` in meshcore-firmware. |

## At a glance

- **What:** a searchable firmware manual at docs.offband.org, with documentation maintained as part of Features and Epics.
- **Why now:** the original discovery/publishing gaps are addressed. Users still need explanations and setup/recovery guides for under-used shipped capabilities, especially repeater telemetry/HA, reliability/diagnostic rigs and complete observer operation.
- **How:** establish approved documentation sources and checks in firmware; publish them from offband-site; add documentation requirements and update triggers; write the first complete user journeys; integration-test the whole process.
- **Done when:** someone can find a feature, identify whether their board, role and firmware version support it, follow its instructions, and verify the result. A subsequent feature change cannot silently bypass a documentation-impact decision.

Original planning task: [#1799](https://github.com/OffbandMesh/meshcore-firmware/issues/1799), completed by merged #1802. Backfill amendment task: #1858. See ISSUE-MAP.md for the native hierarchy, delivery evidence and Citadel IDs.

### Recorded owner decisions and current delivery

- The manual is live at `https://docs.offband.org/firmware/`. Firmware PR #1816 delivered the foundation; #1836 delivered C1/C2 policy; #1841 delivered the first five additional guides. The six-guide manual was manually published by site run 40405972459 from source `894d2585bb4f2247d6af53644aa741576bbbe094`.
- On 2026-10-26 the owner chose GitHub Free with **owner-only manual production publishing**, rather than upgrading for private-repository environment protection. Automatic rebuild notifications, once their scoped credential and receiver gate are ready, build artifacts only. They do not publish.
- On 2026-10-27 the owner explicitly placed the named historical-guide backfill under this Feature. Epics F/G/H below extend the bounded initial population before final Integration Testing E. This is a scope extension, not a claim that the original six guides or all firmware features are complete.
- Site rollback rehearsal, sender credential verification, C3 #1834, C4 #1835 and gate #1800 remain open. The owner merged the policy portion of C before the sender; its residual work needs a separately reviewed follow-up PR. It is not silently waived.
- Existing human-reviewed merges and named public-action approval are the delivery authority. The original grant proposal in section 5 is not an active or enlarged grant and does not authorize new backfill/follow-up merges automatically.

## 1. Diagnosis

| Finding | Evidence inspected | Effect |
|---|---|---|
| The publishing home has already been selected. | Owner decision recorded in meshcore-client#478, comment 5149569390: in-repo Markdown, MkDocs Material, offband-site build owner, Cloudflare Pages, docs.offband.org. | Reuse the decision; do not reopen the hosting comparison. |
| The existing client Initiative explicitly excludes firmware documentation. | meshcore-client#476 body; native children are #477 and #492. | Do not silently expand or reparent that Initiative. Today's firmware delivery needs its own tracked capability. |
| A shared publishing task exists but includes deferred client work. | meshcore-client#491 is type:task, board:ready, child of #477. | Split ownership explicitly before executing; do not duplicate its shared-site deliverable or claim it complete from firmware-only delivery. |
| Firmware has useful Markdown and a MkDocs configuration. | README.md, mkdocs.yml, docs/index.md, observer-cli-commands.md, observer-gps-location-config.md, safeboot.md, remote-diagnostics.md. | Reuse verified content, but separate tutorials from command/developer references. |
| The docs directory is not a safe publication boundary. | mkdocs.yml excludes several internal directories; firmware#1095 remains open concerning private details in tracked docs/scripts. | Publish an explicit user-content tree, not a bulk copy of docs/. Review selected text and generated output. |
| The website sends readers to the wrong firmware branch. | offband-site/content/docs.md and content/firmware.md link tree/main/docs; HTTPS request returned 404. Firmware checkout is firmware-base. | Correct both front-door links and source/edit links. |
| The docs hostname is not serving a verified site. | docs.offband.org did not resolve during the research checks. offband-site#33 tracks false claims that it is live. | Provisioning and public verification are explicit gates. A failed request to the marketing site is not proof that marketing is down. |
| No documentation publish workflow was found in the inspected checkouts. | Firmware workflows have no MkDocs invocation; website deploy-cloudflare.yml builds Hugo. | Add a separate build and deployment path without replacing Hugo. |
| Existing diagnostic documentation work has narrower scope. | Firmware#440 has native children #441 through #445; #445 writes Why Offband and depends on accurate provenance. | Link and reuse that work. Do not describe proposed entities as shipped or move it out of its Epic automatically. |
| Website release triggers are already being worked on. | offband-site#53 and child #57 own firmware-release dispatch reception; Citadel shows #53 in_progress while GitHub has board:todo. | Coordinate file ownership and reconcile status before claiming implementation. GitHub remains the source of truth. |

Native parent/child relationships and current labels are listed in ISSUE-MAP.md. Legacy documentation issues remain intact. New Feature, Epic and gate records are tracked explicitly; their GitHub labels and native links were read back during planning.

## 2. Scope and design

**In:** meshcore-firmware documentation, its contribution workflow and CI; offband-site documentation build, preview and deployment; a first useful manual; one evidence-driven feature-update exercise.

**Out:** client repository edits or client guide authoring; changes to DifferentWire standards; changes to Claude/Codex hooks; firmware behavior, hardware flashing, release tag creation; Hetzner services; chatbots; autonomous AI publishing; paid AI API integration; localization and historical version hosting.

### What lives where

| Repository | Artifact | Responsibility |
|---|---|---|
| meshcore-firmware | `docs/user/` | Only approved user documentation and public media. Original developer references remain in their existing locations. |
| meshcore-firmware | `docs/user/features.yml` | Feature IDs, user-visible names, role/board applicability, release availability, guide paths and source/test evidence. |
| meshcore-firmware | `docs/templates/USER-GUIDE.md` | Purpose, prerequisites, applicability, steps, expected result, persistence/reboot behavior, limitations, troubleshooting and recovery. Not published. |
| meshcore-firmware | `docs/documentation-workflow.md` | Feature/Epic documentation acceptance criteria and the existing-agent drafting/review procedure. Not part of the user manual. |
| meshcore-firmware | `docs/changes/<issue>-<description>.yml` | Per-work-item documentation-impact record: updated guides, or a specific no-user-impact reason. Not published. |
| meshcore-firmware | `scripts/check_user_docs.py`, `scripts/check_docs_impact.py` and tests | Validate catalog, records, source evidence, public boundaries and real Git diff coverage. |
| meshcore-firmware | `mkdocs.user.yml`, `requirements-docs.txt`, `.github/workflows/docs-check.yml` | Reproducible standalone user-docs build and PR checks, separate from existing firmware build classification. |
| meshcore-firmware | `.github/pull_request_template.md`, `CONTRIBUTING.md` | Put documentation impact, validation evidence and release applicability into normal delivery. Create CONTRIBUTING.md only if absent. |
| meshcore-firmware | `.github/workflows/docs-notify.yml` | Notify the website after approved docs changes land. Never send deployment credentials into untrusted PR code. |
| offband-site | `docs-sources.yml` | Firmware repository, firmware-base source branch, approved subtree and destination prefix. No client checkout today. |
| offband-site | `scripts/build_docs.py`, `scripts/test_build_docs.py` | Stage approved content under firmware/, record source SHA, rewrite source/edit links, validate the build. |
| offband-site | `mkdocs.yml`, `requirements-docs.txt`, `.github/workflows/docs.yml` | Build and preview docs; deploy to a distinct Cloudflare Pages project after approval. |
| offband-site | `content/docs.md`, `content/firmware.md` | Point users into the manual. Preserve marketing URLs. |

### Reader and publication contract

- One site, with firmware at `/firmware/`. Reserve `/app/` for later; do not publish an empty client manual.
- English first, without a language prefix. Adding another language later requires a separately reviewed URL/redirect plan.
- The manual follows approved documentation on firmware-base. Every feature page states which released versions/channels support it. Beta and development-only features are visibly labeled. Do not call the current branch the stable firmware.
- Every build records the exact firmware source commit. Fetch the branch once, resolve its SHA, and build only that checkout. A later branch change cannot alter the in-progress build. Render the short SHA and build time in the manual footer, linking the full SHA to its source commit. Publish a machine-readable `build-info.json` containing `source_repository`, `source_commit`, `source_ref` and `built_at`; acceptance checks compare that record with the fetched revision.
- Corrections can publish without cutting firmware tags. A feature is not described as shipped merely because its documentation merged. A release event can refresh availability only from verified release metadata; it does not generate user instructions.
- Production publication is always an explicit owner-initiated workflow on site `main`, with publish opt-in and the enable switch. Automatic push/rebuild events may assemble artifacts but cannot upload production. No paid GitHub environment protection is assumed.
- Source Markdown remains useful on GitHub. The site staging code owns the `/firmware/` prefix and source/edit URLs; it must not make users maintain duplicate copies or edit offband-site for each new guide.
- Publish only `docs/user/` Markdown and approved image assets. Do not stage the wider docs tree, local files, consultations, plan artifacts, feature-impact records or private examples.
- Existing technical pages are retained for provenance and developer use. User guides must be self-contained or link to an approved published reference, not leak into excluded content.
- Each page addition is discoverable through generated section navigation and search. Do not introduce a second manually maintained central page inventory.

### Documentation as part of Feature/Epic completion

- Feature planning names the user journeys, guide owners, release applicability and evidence needed to prove the instructions.
- Each Epic that changes a user-visible action, setting, default, compatibility limit or recovery path includes its guide updates in that Epic's branch and PR. A separate final documentation Epic must not become permission to postpone every guide.
- Epic verification tests the documented journey together with that Epic's implementation. Integration Testing verifies the complete user journey and published result across Epics.
- No-user-impact decisions require a concrete reason, not an empty checkbox. Code review can reject the reason; a checker cannot establish semantic correctness.
- Child tasks update guides as their behavior lands. A documentation-only update is valid and must still run docs checks even when firmware builds are legitimately skipped.

### AI assistance

Use the existing development agent while it implements the feature. Its input is the approved issue, diff, existing guide, relevant code/tests, and observed output. Its output is a documentation patch plus a list of unverified statements. A reviewer checks claims and the user steps before merge. No API key, background service or extra server is required for this first implementation.

AI must not invent board support, configuration defaults, success messages or release availability. Unknowns block the affected claim. Hardware behavior is labeled unverified until actual evidence exists. The instructions become portable Markdown regardless of which agent drafted them.

### Sender/receiver contract and credentials

The receiving workflow in `OffbandMesh/offband-site` supports `repository_dispatch` with event type **`firmware-docs`**. The sender in firmware posts to `repos/OffbandMesh/offband-site/dispatches` with this body:

```json
{
  "event_type": "firmware-docs",
  "client_payload": {
    "schema_version": 1,
    "source_repository": "OffbandMesh/meshcore-firmware",
    "source_ref": "refs/heads/firmware-base",
    "source_sha": "<full 40-character SHA of the accepted source commit>"
  }
}
```

Validate the schema, repository/ref literals and SHA format before checkout; never substitute payload values into shell text or arbitrary checkout locations. The receiver fetches the configured firmware-base branch, resolves its current head exactly once, and verifies the notified SHA belongs to that branch's history. A delayed valid event therefore rebuilds the newest accepted head, not an older public version. A mismatch fails visibly. Manual workflow dispatch resolves the same configured branch without requiring an event payload. Both paths use the same staging/build code. Deployment concurrency serializes production publication, so a slow older build cannot overwrite a newer deployment.

Before C3, B2 must verify a credential with permission to send this cross-repository event. A source repository's default GITHUB_TOKEN is not assumed to have that permission. Inspect the existing credential inventory and verify scopes without displaying values. Reuse an existing appropriately scoped credential; expose it only to the trusted post-merge sender under the secret name `OFFBAND_DOCS_DISPATCH_TOKEN`. Fork PR checks receive no dispatch or deployment credential. If no suitable credential exists, site#61 stays open and blocks this setup until the owner approves a scoped credential solution. A manual build is a recovery path, not acceptance of a missing automatic-rebuild requirement. Production publication is separately and deliberately manual under the owner's revised decision. Cloudflare uses its own existing credential.

## 3. Epics and tasks

Issue levels: standalone **type:feature #1794**, **type:epic** children A-H, and direct **type:task** children. A=#1795, B=offband-site#60, C=#1796, D=#1797, E=#1798, F=#1843, G=#1844 and H=#1845. F/G/H are the owner-requested backfill extension; E remains the final Integration Testing Epic. Use Stories only when one requirement needs several Tasks and the grouping helps. Existing parentage is preserved; backfill issues are not replacement parents for old work.

### Epic A (#1795): Firmware documentation foundation and checks (firmware)

| Task | Pts | Deliverable and proof |
|---|---|---|
| A1: Reproduce the gaps and define the public boundary | 3 | Catalog existing guides and features; approved/public/needs-review/missing status; failing fixtures for broken links, missing applicability and excluded files. No bulk publication of old docs. |
| A2: Guide contract and minimum useful manual | 4 | USER-GUIDE.md, features.yml, user index and a fully worked observer WiFi guide, checked against actual source and available test evidence. |
| A3: Docs checker and standalone build | 4 | check_user_docs.py, negative and positive tests, mkdocs.user.yml, pinned requirements and docs-check workflow. Unmapped behavior changes require an impact decision. |
| A4: Epic verification | 1 | Build a clean staged manual, inspect HTML/assets/search, follow the first guide as far as verified evidence permits; record remaining hardware-only checks. |

Test corpus includes missing guide, dangling source path, unknown board/role, missing release applicability, malformed metadata, a new user-facing code path with no impact record, a documentation-only change, and an internal/private canary outside the approved subtree. A synthetic fixture tests the canary boundary; no real secret is used.

### Epic B (offband-site#60): Searchable firmware manual on Cloudflare (offband-site)

| Task | Pts | Deliverable and proof |
|---|---|---|
| B1: Reproducible source staging and previews | 4 | docs-sources.yml, build_docs.py and tests. Pin source SHA, isolate temp checkout, stage firmware/ only; missing refs/checkout failures stop. Produce build-info.json and a visible source footer. Build/search result artifacts are inspectable before live publishing. |
| B2: Separate documentation deployment | 4 | docs.yml and separate Pages project. Implement the sender/receiver contract, serialized production deployment and source footer. Verify dispatch credential scope before C3; missing credentials remain gated by #61. Public repository reads use public access where verified; dispatch and Cloudflare publishing are checked separately. |
| B3: Front door and domain | 3 | Update docs.md/firmware.md and live docs hostname after explicit provisioning/publishing approval. Fold in site#33's factual correction without changing historical design documents. |
| B4: Epic verification | 1 | Manual workflow dispatch produces a searchable rendered firmware section with correct edit links, excluded content absent, working TLS and marketing site unchanged. Rehearse rollback to the prior deployment. |

The public deployment gate is [offband-site#61](https://github.com/OffbandMesh/offband-site/issues/61), Citadel `offband-site-cs7`, a child of Epic B. It names Pages project creation, DNS/custom-domain binding, credential setup if required, and production publishing separately. It blocks deployment, not local preview work.

### Epic C (#1796): Features/Epics maintain docs and request rebuild artifacts (firmware)

| Task | Pts | Deliverable and proof |
|---|---|---|
| C1: Contribution and acceptance contract | 3 | documentation-workflow.md, CONTRIBUTING.md and PR template: guide updates, evidence, applicability, and no-user-impact rationale. Includes the AI drafting procedure. |
| C2: Enforce documentation-impact decisions | 4 | docs/changes records and changed-file checks. Negative fixtures block missing/malformed decisions; explicit no-impact decisions remain reviewable. Existing ci-green/full-matrix behavior remains intact. |
| C3: Rebuild notification | 3 | Receiver and verified dispatch credentials from B exist first. Send the exact version-1 firmware-docs contract on accepted docs changes. Test invalid payload, noncanonical SHA and delayed-event handling. Dispatch failure fails visibly. No unchecked payload chooses a repo, shell command or source ref. Manual replay documented. |
| C4: Epic verification | 1 | Exercise an approved example behavior change: guide-required case fails before its docs patch; justified no-impact case passes; merged guide revision reaches a preview with the expected source SHA. |

Do not reuse `firmware-release` as if it meant docs-only updates. Coordinate with site#57; either extend the existing receiver with a separate event or implement the dedicated docs workflow without touching its marketing release job. Only one owner modifies any shared workflow at a time.

### Epic D (#1797): First complete firmware user journeys (firmware)

| Task | Pts | Deliverable and proof |
|---|---|---|
| D1: Getting started and applicability | 4 | Installation/update distinctions, choosing board/role/channel, checking installed version; link into site#53 downloads without taking ownership of that Epic. Preserve identity/config warnings. |
| D2: Observer operation | 4 | MQTT setup, time/GPS and location sharing, broker status and troubleshooting. Extend A's WiFi guide rather than duplicate it. Record supported boards, version, persistence and reboot requirements. |
| D3: Safety and diagnostics | 4 | SafeBoot behavior, SafeFlash recovery and collecting logs, only where implementation and support are verified. Reuse #445's Why Offband content when available; label its unimplemented entity work as unavailable rather than silently importing it. |
| D4: Epic verification | 1 | Review each documented journey against evidence; rendered pages are understandable without reading source. Owner judges usability. Known gaps are listed explicitly and are not represented as completed coverage. |

A1 defines the bounded launch inventory. D is not an assertion that every historical feature is documented. Uncovered features become individually tracked backlog with support/release applicability, rather than broadening D indefinitely.

### Epic F (#1843): Repeater telemetry and Home Assistant backfill

| Task | Pts | Deliverable and proof |
|---|---|---|
| F1 #1846: What repeater telemetry does and how to configure it | 4 | Explain role/purpose, radio versus WiFi/MQTT responsibilities, supported images, burst behavior, prerequisites, configuration, persistence and success/recovery. No assumption that observer commands also configure a repeater. |
| F2 #1847: Home Assistant setup and delivered entity reference | 4 | Discovery/setup and a source-backed list of emitted entities, keys, units, availability, meaning and useful actions. Match actual released payloads, including legacy names where applicable. Reuse #440/#441/#445 provenance without implementing or claiming proposed entities. |
| F3 #1848: Epic verification | 1 | Source/release matrix, public-safe examples, rendered setup/reference walkthrough, search/catalog and owner usability. Actual HA/device trials are stated separately; no service deployment is implied. |

### Epic G (#1844): Reliability, watchdog and diagnostic rig backfill

| Task | Pts | Deliverable and proof |
|---|---|---|
| G1 #1849: SafeBoot purpose and value | 3 | Extend the existing power/recovery guide with the problem the guard solves, why it is useful, observable decisions, sensing/wake/build limitations and appropriate operator responses. |
| G2 #1850: Watchdog behavior | 3 | Distinguish task/internal/external mechanisms by actual image/platform. Explain monitored activity, trigger/restart behavior, evidence to collect and recovery. Do not prescribe disabling production protection or hanging a live device as routine troubleshooting. |
| G3 #1851: Diag builds and a supported reference rig | 4 | Explain selecting/obtaining a diag image, source identity and flags; then parts, verified interfaces/pins/voltage assumptions, assembly/checks, capture and reproduction for a supported reference rig. Audit existing harness/smoke scripts and board references. Preserve #755 capabilities-matrix and #287 nRF SafeFlash ownership. Unknown wiring claims block that procedure rather than becoming plausible examples. |
| G4 #1852: Epic verification | 1 | Review source/release and physical-interface evidence, public-safe rig examples and rendered procedures. Hardware construction, flashing, low-voltage trials and fault injection are not performed under this documentation authorization. State missing bench validation explicitly. |

### Epic H (#1845): Complete observer operation backfill

| Task | Pts | Deliverable and proof |
|---|---|---|
| H1 #1853: Bounded capability-to-guide map | 2 | Audit shipped observer command/config handlers and applicability. Every supported operator capability in that audited release maps to a guide or a named gap; future features are excluded. This map defines what "everything observer" means for initial population. |
| H2 #1854: Complete first-time setup | 3 | Extend/link existing WiFi/MQTT/location pages into a coherent operator path: image/role, management surface, network/broker/time/privacy choices, expected results and persistence. No invented client screens. |
| H3 #1855: Rotation, held/heap states and failed recovery | 3 | Explain normal rotation versus memory deferral, clock holds, backoff and terminal failure, their causes, what to change/re-enable, and useful evidence. Distinguish human CLI labels from machine tokens and verify corrective actions against actual handlers. |
| H4 #1856: Usable authentication and trust recipes | 4 | Full URI/transport/port/path, supported CA selection, JWT identity/audience, seeded versus retained configuration and credential safety. Broker-specific assertions need implementation and operator evidence; no invented live connection trial. |
| H5 #1857: Epic verification | 1 | Coverage map/catalog, release/source evidence, coherent setup/recovery walkthroughs, rendered search/navigation, and explicit app/broker/hardware gaps. Extend existing pages rather than create competing copies of the same journey. |

The three backfill Epics are inside Feature #1794 as additional initial population, explicitly requested on 2026-10-27. Completion is the named journeys and audited observer capability map, not every historical feature or every possible board/rig. Further discoveries become individually tracked backlog with a separate scope decision; they do not silently keep this Feature open forever.
The three-Epic grouping is this amendment's delivery proposal for the owner's named topics.

### Backfill file handoffs and task execution

Keep the existing Markdown/MkDocs/Material stack, public root `docs/user`, source
evidence catalog and manual site publication. These file targets make the named
scope actionable; narrow or split a page when source review justifies it, without
silently adding another user journey.

| Owner task | Public/maintainer files | Consumes / produces |
|---|---|---|
| F1 #1846 | Create `docs/user/guides/repeater-telemetry.md` | Consume README role definitions, repeater config/command handlers and `src/helpers/wifi_telemetry/`; produce the prerequisite setup journey used by F2. |
| F2 #1847 | Create `docs/user/guides/home-assistant.md` | Consume F1 and actual discovery/state emission; produce integration steps and a delivered entity table with units/availability/meaning. Existing provenance tasks remain authoritative for their implementation. |
| G1 #1849 | Extend `docs/user/guides/power-and-recovery.md` | Consume SafeBoot's actual decision/sensing/wake paths; produce a clear purpose/value explanation without replacing existing recovery cautions. |
| G2 #1850 | Create `docs/user/guides/watchdog.md` | Consume watchdog helpers, task guard/self-hang tests and build flags; produce image/platform-specific behavior and operator evidence/recovery. |
| G3 #1851 | Create `docs/user/guides/diagnostic-builds.md` and `docs/user/guides/diagnostic-rig.md` | Consume `diag-build.yml`, `companion_harness.py`, `rig_boot_smoke.py`, relevant tests and verified board interface references; produce image selection plus a reproducible supported reference rig. Exclude private registry entries and unsupported wiring. |
| H1 #1853 | Create maintainer `docs/observer-user-coverage.md` | Consume the shipped observer command/config manifests and selected release; produce the bounded capability-to-guide map for H2-H4, not a second public navigation list. |
| H2 #1854 | Create `docs/user/guides/observer-setup.md`; extend/link existing WiFi/MQTT/location/support pages | Consume H1's scope and the existing individual journeys; produce an end-to-end first-time setup sequence with explicit success checks. |
| H3 #1855 | Create `docs/user/guides/observer-broker-states.md`; update links in `observer-mqtt.md` | Consume broker/pool transitions and human/wire formatters; produce causes and exact corrective actions for rotation, held/heap, backoff and terminal failed states. |
| H4 #1856 | Create `docs/user/guides/observer-broker-auth.md`; extend `observer-mqtt.md` | Consume URI construction, trust/auth code, seeded settings and operator evidence; produce usable, versioned recipes and a safe readback/enable/verification path. |
| F3/G4/H5 | Update `docs/user/features.yml`, relevant index links and an Epic evidence record under `docs/verification/` | Consume the authoring tasks; produce validated coverage, rendered search/navigation and explicit owner/hardware acceptance limits. Serialize shared catalog/index updates. |

For every authoring task:

- [ ] Audit the selected released/tagged implementation and existing reference; list unsupported or unverified claims before drafting.
- [ ] Draft from `docs/templates/USER-GUIDE.md`, covering purpose/value, applicability, prerequisites, steps, expected result, persistence, troubleshooting and recovery.
- [ ] Record literal source/test evidence and release/build applicability. A rolling `diag-builds` prerelease is not a stable version; document the exact selected source/flags rather than inventing release support.
- [ ] Build with the catalog/content checker and strict standalone MkDocs config; reject missing evidence or broken links. Add a targeted regression only for a real parser/rendering/validation defect, not tests that merely repeat prose.
- [ ] Review the rendered journey as a user, fix factual findings and commit under its child task. No hardware actions or production publication are performed by writing the guide.

Each final verification task also builds the assembled site preview from the
committed source SHA, follows the user path and search, checks exclusions and
release labels, records remaining live-device/service trials and requests owner
usability review. After merge, publish only through the existing manual owner lane.

Review focus: observer versus repeater/serial management surfaces; retained broker
layouts versus fresh defaults; normal rotation versus actual heap/terminal failure;
actual emitted HA entities versus proposed ones; and physical board/pin/voltage
facts versus compile-time capability flags. The affected authoring task must
resolve each uncertainty or explicitly block the unsupported claim/procedure.

### Epic E (#1798): Feature Integration Testing (firmware)

| Task | Pts | Deliverable and proof |
|---|---|---|
| E1: Whole-system acceptance | 4 | From website entry to correct guide and source edit link; known stable/beta distinction; docs-impact failure then fix; accepted docs commit produces the expected rebuild artifact and owner manual publication matches its source SHA; search finds it; marketing still works. Include F/G/H journeys. |
| E2: Failure and rollback exercise | 3 | Unknown source ref, failed fetch, invalid guide, missing credential, failed dispatch and deployment failure all surface visibly; existing production remains intact. Restore a prior docs deployment and verify its SHA. |
| E3: Outcomes and Epic verification | 1 | OUTCOMES.md beside the plan: plain-language result, linked evidence, content coverage, unverified hardware claims, deferred client scope and owner sign-off. |

## 4. Order, ownership and dependencies

Original order was A -> B -> C -> D -> E. Actual delivery has A/D and C1/C2 merged and the manual live; B operational acceptance and C3/C4 remain open. Backfill F/G/H can progress on the existing manual lane while those prerequisites are resolved. Final E depends on A/B/C/D/F/G/H and the existing site gate. Each backfill Epic ends with its own verification; F2 builds on F1's setup and H2-H4 use H1's bounded map. Planning and external setup remain separately tracked.

Use one worktree/branch per new backfill Epic, child task commits and an Epic review PR. Use `firmware-base` in firmware and `main` in the site. Already merged delivery and separately reviewed site setup/fix PRs are recorded history, not retroactively one PR. C's residual sender and operational follow-ups require their own reviewed PRs; no extra merge grant is inferred from this amendment.

Native GitHub parentage includes site#60 and F/G/H beneath firmware#1794. Citadel execution remains project-scoped. Gate #1800 records site verification and still gates C3 notification integration and final E. Do not close it from reachable pages alone: scoped credentials, operational evidence and verification are required. The site public-action gate is separate. Final E's dependencies include all three new backfill Epics. Original planning task #1799 is complete; amendment task #1858 records this extension.

| Shared file/surface | Writers | Ordering |
|---|---|---|
| Firmware catalog and first WiFi guide | A2, D1-D3 | A before D; D tasks serialized for shared catalog updates. |
| Docs checker/tests/workflow | A3, C2 | C2 depends on A3. |
| Website docs front door | B3 and site#53 when it changes firmware.md | Reserve/coordinate and serialize overlapping edits. |
| Website publishing workflow | B and site#57 | Prefer dedicated docs.yml; if deploy-cloudflare.yml must change, #57 and B cannot edit it concurrently. |
| Existing Why Offband content | firmware#445 and D3 | Keep #445 parented to #440. Reuse reviewed completed content; no duplicate competing writer or premature closure. |
| Feature catalog/index and shared observer/safety pages | F/G/H | Serialize shared catalog/index changes; G1 extends the existing safety page, H extends observer pages, and no second central guide inventory is introduced. |
| Entity provenance and hardware capabilities | #440/#441/#445, #755/#287, F/G | Reference delivered work without moving existing issues. A missing prerequisite blocks the affected claim or procedure, not permission to invent it. |

AI/API automation, if later wanted, is a separate plan. It must settle provider, budget, secret handling and untrusted-diff handling; this Feature does not quietly introduce it.

## 5. Grants and authorization

The original plan merged as #1802. Subsequent owner approvals authorized implementation, named site public actions, manual publication and the 2026-10-27 backfill extension. No hardware actions, release tags, paid service or hook changes are authorized. This amendment is reviewed separately under #1858.

The grant table/terms below preserve the **original proposal**, not an active grant or authorization for the additional F/G/H and follow-up PRs. No grant was minted by this session. Existing owner-reviewed merges are not an automatically replenished budget. Do not extend a repository grant, its chain, expiry or budget by interpreting this scope amendment as a mint/renewal request.

The merge gate checks that a grant's repository matches the target PR. Therefore one firmware grant cannot cover the site Epic. The proposal is a four-merge firmware grant below and one separately owner-approved site merge for Epic B. No unsupported cross-repository grant is implied. If a site plan grant is preferred, prepare a site-owned plan record and a one-merge grant there before site execution.

| Term | Proposal | Basis |
|---|---|---|
| Firmware implementation | Epics #1795, #1796, #1797, #1798; C and E remain blocked by #1800 until site verification. | Four firmware-owned delivery units. |
| Firmware merges | 4 | One PR per firmware Epic; no plan PR, unrelated issue or site PR consumes this budget. |
| Website merge | 1, separately approved for site#60. | Repository-scoped gate cannot consume a firmware grant. |
| Verification | Owner reviews recorded Epic evidence. | Usable instructions need review beyond a successful static build. |
| Duration | 168 hours per implementation run. | Multi-repo content and user-journey verification; no assumed renewal. |
| Flash/reset | No flashing; reset not required. | No firmware or device deployment is in scope. |
| Public actions | Named owner approval through site#61 before Pages creation, credential changes, domain binding or first public publication. | These are external state changes and must be reversible. |
| Spending | None for a new service. | Existing agents draft docs; no new paid AI integration. Existing required review tooling is reused. |
| Stops | Failed checks, uncertain claims, private content, missing authorization or conflicting active work. | No silent skipping or scope expansion. |

```grant-terms
{
  "epic_chain": [1795, 1796, 1797, 1798],
  "chain": true,
  "verification": {"mode": "owner"},
  "merge": {"budget": 4, "basis": "One implementation PR for each firmware Epic: 1795, 1796, 1797 and 1798. C (1796) and E (1798) additionally require completed site-verification gate 1800. Excludes the plan PR and offband-site Epic 60."},
  "flash": null,
  "reset": null,
  "expires_after_hours": 168
}
```

The owner mints any grant; this planning session does not. A grant does not close the site prerequisite or approve deployment, hardware tests, release tags or additional services. The firmware chain records order within that repository only. Owner verification of C and E requires readback that #1800 is complete with site receiver/verification evidence; the grant JSON by itself does not enforce that cross-repository prerequisite.

## 6. Can't-tell branches

| Where | Can't confirm | Action |
|---|---|---|
| Source build | Branch/ref absent, checkout failed, source changed mid-build | Stop. Build only the resolved SHA. |
| Applicability | Supported board/role/version/default lacks evidence | Stop the claim; require evidence or explicitly narrow coverage. |
| Impact check | No record, malformed record or uncovered changed path | Fail the check. Reviewer resolves impact. |
| Privacy | Unknown file class or potentially identifying example | Exclude from staging and stop publication pending review. |
| Release metadata | Missing or contradictory release/channel | Stop channel assignment. Never relabel unreleased behavior stable. |
| Dispatch | Missing token, refusal, timeout or no observable receipt | Fail visibly; manual retry from the recorded revision. |
| Deployment | Build/upload/custom domain fails | Fail visibly and keep the prior production deployment; verify actual rollback state. |
| AI draft | Source and prose disagree; output contains unknown facts | Reject the claim and review. No autonomous publish. |
| Coordination | Shared file reserved or issue already being implemented | Stop overlapping work and coordinate. |

## 7. Verification

Proposed deterministic commands, implemented by the named tasks:

- Firmware: `python scripts/test_check_user_docs.py`.
- Firmware: `python scripts/check_user_docs.py --catalog docs/user/features.yml --content-root docs/user`.
- Firmware: `python -m mkdocs build --strict --config-file mkdocs.user.yml`.
- Website: `python scripts/test_build_docs.py`.
- Website: `python scripts/build_docs.py --sources docs-sources.yml --output .build/docs` (resolves source SHA and writes the manifest).
- Website: `python -m mkdocs build --strict --config-file .build/docs/mkdocs.yml` after staging; the root file is a template.
- Website marketing regression: `hugo --gc --minify` plus rendered home/firmware/docs smoke checks.

Add docs-check.yml for every pull_request and push to firmware-base without workflow-level path exclusions. Its documentation build/impact jobs run independently of ci.yml and do not consume the classifier's docs_only output. Keep the existing ci.yml classifier and independent ci-green classification intact: they already handle legitimate docs-only matrix skips. Mixed/code changes retain the existing full firmware checks plus the new documentation checks. Configure link/anchor validation explicitly: --strict is not proof that every external URL or user instruction works. Do not change the protected firmware build matrix or required checks without separate owner approval.

Every test has a positive and a relevant negative case. The final evidence links to CI runs, source SHAs, rendered previews and exact user-step verification, not just screenshots of green jobs.

## 8. Done when

- The manual is searchable and reachable from offband.org without navigating GitHub.
- The launch inventory is documented, with explicit support and release applicability.
- Existing source/developer content remains intact and unpublished material stays excluded.
- A real example demonstrates Feature/Epic documentation maintenance, the expected rebuild artifact and an explicit owner manual publication; no automatic production publication is required or allowed.
- The named F/G/H backfill is delivered with support/release evidence and the bounded observer coverage map; deferred claims remain visible rather than reported as complete.
- A new guide needs one source-repo PR, not a second site nav edit.
- Failure cases do not replace a good public site with a partial or stale build presented as current.
- Owner accepts the published instructions and OUTCOMES.md.
- Each PR has actually merged and external publication is verified before corresponding delivery is called complete.

## 9. Owner decisions for this draft

| Question | Recommendation | Alternative |
|---|---|---|
| Tracking structure, approved | Standalone firmware Feature #1794, with site Epic #60. Existing client Initiative and issue parentage remain unchanged. Record shared publishing responsibility explicitly against client#491 without implementing client content. | Explicitly broaden/restructure the client Initiative into an Offband-wide Initiative, which exceeds today's two-repo issue-edit scope. |
| How current is the manual? | Maintain corrected docs from firmware-base with mandatory per-feature release/channel applicability. | Separate stable/beta/development snapshots; more build/navigation/storage complexity and content backfill. |
| AI assistance at launch? | Existing development agents draft reviewed changes as part of the Feature/Epic. | A new automated AI drafting service, scoped and budgeted separately. |

The original five-Epic plan and subsequent delivery were approved in recorded reviews/chat. The owner explicitly requested the named F/G/H backfill on 2026-10-27 and chose manual publication on the existing free plan. Existing unrelated issue parentage is preserved. New merge grants, hardware actions and broader content scope are not implied; publication remains an explicit owner action.

## 10. Carried-in work

| Existing item | Treatment |
|---|---|
| Client#478, closed research | Preserve its accepted hosting decision and corrections; no implementation here. |
| Client#477/#491 | Split/reconcile shared publishing responsibility with owner approval. Leave client content/bootstrap and client dispatch deferred; do not close #491 as delivered today. |
| Client#476/#492 | Client-only initiative and future offline help remain out of scope. |
| Site#33 | Correct current domain claims as part of B; preserve historical designs. |
| Firmware#440/#445 | Keep provenance/diagnostic-entity work under #440. Link to the manual; document only verified shipped behavior. |
| Firmware#1095 | Publication safety dependency for affected imported pages; do not wait to publish safe unrelated guides, and do not claim this privacy issue remediated by an allowlist alone. |
| Site#53/#57 | Coordinate downloads links, release event names and shared files. Not a prerequisite for a plain working docs manual. |
| Site#36 | Hetzner host platform is not needed by this static-documentation Feature. |

## Review check

This plan covers sources, reader journeys, publication, update automation, evidence, failure handling and cross-repository ownership. Actual Feature/Epic/gate IDs and repository-scoped grant terms are present. It is ready for owner review; implementation Tasks, exact dependency pins and deployment prerequisites must be resolved before executing the affected work. No client implementation or new paid-service dependency is included.

External plan review completed with Gemini 2.5 Pro on 2026-10-25. Disposition: define the dispatch schema and trusted-source behavior; make credential verification an explicit B2/#61 prerequisite; clarify cross-repository verification in the grant basis; expose the source SHA publicly. The claimed docs-only CI contradiction was not supported by the existing classifier/ci-green implementation; this plan now explicitly preserves that implementation and adds independent docs checks. Review completion is not implementation or merge approval.
