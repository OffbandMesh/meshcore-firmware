# Feature PLAN: Firmware documentation users can follow and maintainers keep current

| | |
|---|---|
| Feature | [Firmware #1794](https://github.com/OffbandMesh/meshcore-firmware/issues/1794), Citadel `Crosswire-uuu`. Standalone Feature. |
| Serves | Firmware users learning, configuring and troubleshooting Offband features. |
| Status | Five-Epic structure approved in chat on 2026-10-24. Implementation plan awaits owner review and plan-PR merge. No production changes or spending authorized yet. |
| Owner | Strycher |
| Last updated | 2026-10-25 |
| Proposed repository destination | `docs/initiatives/standalone/1794-firmware-user-documentation/PLAN.md` in meshcore-firmware. |

## At a glance

- **What:** a searchable firmware manual at docs.offband.org, with documentation maintained as part of Features and Epics.
- **Why now:** useful guides exist, but users are sent to a GitHub path that returns 404, there is no inspected documentation publishing workflow, and feature completion does not explicitly require a checked user guide.
- **How:** establish approved documentation sources and checks in firmware; publish them from offband-site; add documentation requirements and update triggers; write the first complete user journeys; integration-test the whole process.
- **Done when:** someone can find a feature, identify whether their board, role and firmware version support it, follow its instructions, and verify the result. A subsequent feature change cannot silently bypass a documentation-impact decision.

Planning task: [#1799](https://github.com/OffbandMesh/meshcore-firmware/issues/1799), Citadel `Crosswire-gzi`. The owner approved establishing this hierarchy and preparing this plan; implementation follows separate plan approval. See ISSUE-MAP.md for the actual native hierarchy and Citadel IDs.

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
| meshcore-firmware | `docs/changes/<issue-number>.yml` | Per-work-item documentation-impact record: updated guides, or a specific no-user-impact reason. Not published. |
| meshcore-firmware | `scripts/check_user_docs.py` and `scripts/test_check_user_docs.py` | Validate the catalog, impact records, evidence paths, publication boundaries and changed-file coverage. |
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

Before C3, B2 must verify a credential with permission to send this cross-repository event. A source repository's default GITHUB_TOKEN is not assumed to have that permission. Inspect the existing credential inventory and verify scopes without displaying values. Reuse an existing appropriately scoped credential; expose it only to the trusted post-merge sender under the secret name `OFFBAND_DOCS_DISPATCH_TOKEN`. Fork PR checks receive no dispatch or deployment credential. If no suitable credential exists, site#61 stays open and blocks this setup until the owner approves a scoped credential solution. A manual trigger is a recovery path, not acceptance of a missing automatic-update requirement. Cloudflare publication uses its own existing credential and is a separate permission check.

## 3. Epics and tasks

Issue levels: standalone **type:feature #1794**, five **type:epic** children, and direct **type:task** children. Epic IDs are A=#1795, B=offband-site#60, C=#1796, D=#1797 and E=#1798. Use Stories only if one user requirement needs several Tasks and that grouping adds clarity. The Feature, Epics, planning task and explicit gates already have GitHub board items and matching Citadel records. Implementation child Tasks and verification dependencies are instantiated after plan approval; all Epics remain backlog until then.

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

### Epic C (#1796): Features/Epics maintain docs and approved changes republish (firmware)

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

### Epic E (#1798): Feature Integration Testing (firmware)

| Task | Pts | Deliverable and proof |
|---|---|---|
| E1: Whole-system acceptance | 4 | From website entry to correct guide and source edit link; known stable/beta distinction; docs-impact failure then fix; accepted docs commit republishes and source SHA matches; search finds it; marketing still works. Use a preview for the test change. |
| E2: Failure and rollback exercise | 3 | Unknown source ref, failed fetch, invalid guide, missing credential, failed dispatch and deployment failure all surface visibly; existing production remains intact. Restore a prior docs deployment and verify its SHA. |
| E3: Outcomes and Epic verification | 1 | OUTCOMES.md beside the plan: plain-language result, linked evidence, content coverage, unverified hardware claims, deferred client scope and owner sign-off. |

## 4. Order, ownership and dependencies

Order: A -> B -> C -> D -> E. E depends on every preceding Epic. Each child depends on its Epic; each Epic's verification gates its successor. Planning and external setup tasks are outside the implementation chain where appropriate.

One worktree/branch per Epic in the repository that owns it, child task commits on that branch, one Epic PR after verification. Four implementation Epic PRs in firmware (A/C/D/E), one in offband-site (B). Use the repositories' actual base branches: firmware-base and main respectively.

Native GitHub parentage includes site#60 beneath firmware#1794. Citadel execution remains project-scoped. [Firmware#1800](https://github.com/OffbandMesh/meshcore-firmware/issues/1800), `Crosswire-fyx`, records verification of site#60 and mechanically blocks C and E. Complete it only from actual merged receiver and Epic verification evidence. A/C/D/E also depend on planning task #1799; final child-verification gates are added when implementation Tasks are instantiated. The site publication gate remains separate from this receiver-verification gate.

| Shared file/surface | Writers | Ordering |
|---|---|---|
| Firmware catalog and first WiFi guide | A2, D1-D3 | A before D; D tasks serialized for shared catalog updates. |
| Docs checker/tests/workflow | A3, C2 | C2 depends on A3. |
| Website docs front door | B3 and site#53 when it changes firmware.md | Reserve/coordinate and serialize overlapping edits. |
| Website publishing workflow | B and site#57 | Prefer dedicated docs.yml; if deploy-cloudflare.yml must change, #57 and B cannot edit it concurrently. |
| Existing Why Offband content | firmware#445 and D3 | Keep #445 parented to #440. Reuse reviewed completed content; no duplicate competing writer or premature closure. |

AI/API automation, if later wanted, is a separate plan. It must settle provider, budget, secret handling and untrusted-diff handling; this Feature does not quietly introduce it.

## 5. Grants and authorization

This plan is not approved until its PR merges. Structural approval in chat authorized planning and issue organization, not implementation, release tags or public infrastructure changes. The planning PR is outside the implementation merge budget.

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
- Website: `python -m mkdocs build --strict --config-file mkdocs.yml` against that staged tree.
- Website marketing regression: `hugo --gc --minify` plus rendered home/firmware/docs smoke checks.

Add docs-check.yml for every pull_request and push to firmware-base without workflow-level path exclusions. Its documentation build/impact jobs run independently of ci.yml and do not consume the classifier's docs_only output. Keep the existing ci.yml classifier and independent ci-green classification intact: they already handle legitimate docs-only matrix skips. Mixed/code changes retain the existing full firmware checks plus the new documentation checks. Configure link/anchor validation explicitly: --strict is not proof that every external URL or user instruction works. Do not change the protected firmware build matrix or required checks without separate owner approval.

Every test has a positive and a relevant negative case. The final evidence links to CI runs, source SHAs, rendered previews and exact user-step verification, not just screenshots of green jobs.

## 8. Done when

- The manual is searchable and reachable from offband.org without navigating GitHub.
- The launch inventory is documented, with explicit support and release applicability.
- Existing source/developer content remains intact and unpublished material stays excluded.
- A real example demonstrates Feature/Epic documentation maintenance and automatic republishing.
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

Only the five-Epic structure and planning work are approved so far. Release-alignment and assistance choices are implementation proposals for this plan review. Existing issues were not moved, reparented or closed. Grants and publication remain explicit owner actions.

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
