# Delivering firmware documentation with features

The public manual is source-controlled in `docs/user/` in this repository and
assembled by `OffbandMesh/offband-site` at
[docs.offband.org/firmware](https://docs.offband.org/firmware/). Technical research,
plans and support notes outside `docs/user` are not published automatically.

## Plan the user journey

A Feature or Epic that changes a user-visible command, setting, default,
compatibility limit or recovery procedure names:

- the user task and affected guide paths;
- the person or agent responsible for the guide changes;
- affected board, role, image and release/channel applicability;
- evidence needed to check steps, expected replies, persistence and restart needs;
- known gaps and any hardware validation still required.

Child tasks update the guides alongside the implementation. The Epic's PR includes
those updates and verifies the documented journey. A later documentation Epic is
not permission to defer documentation for behavior being delivered now. Feature
Integration Testing checks the complete path across Epics, including the website.

Use `docs/templates/USER-GUIDE.md` for a new journey. Add or update its entry in
`docs/user/features.yml`; keep evidence paths/snippets and release claims accurate.
Existing references are evidence to review, not an instruction to publish the
whole `docs/` directory. Do not copy private notes, actual credentials or identifying
network details into examples.

## Record the documentation impact

A PR changing firmware under `src/`, `examples/`, `variants/`, `include/`, `lib/`,
`boards/`, `arch/`, or root `platformio.ini` must add or change a decision record at
`docs/changes/<issue>-<short-description>.yml`. The record is checked against the
PR diff, not the presence of old records elsewhere in the repository.

For a change affecting users:

```yaml
schema_version: 1
issue: 1234
impact: updated
guides:
  - docs/user/guides/example.md
reason: The observer now requires a restart after this setting changes.
```

The named public Markdown guides must exist and change in the same PR. A catalog
change alone is not a user guide. Guide text must explain the new behavior and
its applicability; listing a path is not evidence of correctness.

For a change that does not affect users:

```yaml
schema_version: 1
issue: 1234
impact: none
reason: Moves the decoder into a helper; accepted inputs, output text, defaults and persistence are unchanged.
```

Explain which observable behaviors remain unchanged. `Internal change`, `N/A`,
an empty checkbox or a long generic sentence is not a useful review rationale.
The checker enforces shape and a minimum reason length; reviewers judge whether
the decision is true. A deletion or rename of firmware code is still a code
change. Never delete old impact records to satisfy the check.

Documentation-only PRs do not need a decision record, but they still run the
catalog/evidence checker, strict manual build and rendered-publication checks.
If a documentation-only PR includes a record, that record is validated too.

## Draft with an existing agent

Give the agent the approved issue, implementation diff, relevant code/tests,
existing guides and observed output. Ask for a Markdown patch and a list of claims
it could not verify. No extra AI service, API key or dynamic website backend is
needed. The same review applies to human-written and AI-drafted instructions.

Verify each command, success/error reply, default, supported board/role and release
against source and available observations. Do not infer that a feature is shipped
from its presence on a development branch. Do not invent hardware success; label
source-only checks and pending hardware trials explicitly. Resolve unsupported
claims or leave them out before merging. Keep unverified working notes outside
the public manual; deliberate public limitations belong in the guide.

## Verify and review

Install `requirements-docs.txt` in an isolated Python environment, then run:

```sh
python scripts/test_check_user_docs.py
python scripts/test_docs_impact.py
python scripts/check_user_docs.py
python scripts/check_docs_impact.py --base <base-commit> --head <checked-out-head> --mode pr
python -m mkdocs build --strict --config-file mkdocs.user.yml
python scripts/test_build_user_docs.py
```

The impact command compares the PR head with its merge base. CI checks out the
actual PR head and reads full SHA values from the event as data. On base-branch
pushes it compares the before and after trees. Missing history or malformed SHAs
fail visibly. Manual builds run the manual checks without inventing a PR diff.
The independent documentation workflow does not replace or weaken firmware
matrix/native checks or `ci-green`.

Review the rendered page as an end user: can someone identify their image, follow
the steps, recognize success and recover safely? Confirm the verification claims
match actual evidence. Record hardware testing separately from source checking.

## Merge, assemble, publish

Merge approved guide changes into `firmware-base`. The website's canonical build
reads that branch at an immutable commit and labels its source revision. An
unmerged local preview is explicitly marked as a preview and cannot publish.

Rebuild notification integration is tracked by Epic C task #1834 and blocked by
site verification gate #1800 plus a verified scoped credential. Do not assume a
merge currently sends a notification. Until enabled, the owner can start a site
build manually. A future notification builds an artifact; it does not publish.

Publication remains an explicit owner-initiated action on `offband-site` `main`
with `publish=true`. See that repository's `docs/documentation-publishing.md`.
Merging a feature or passing documentation checks does not itself publish the
manual, validate hardware, or close outstanding release/applicability gaps.
