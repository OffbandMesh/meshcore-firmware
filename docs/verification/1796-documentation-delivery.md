# Epic C: documentation delivery contract verification

Date: 2026-10-26. Agent: ChartreuseBasin (session 01a16bc1).
Parent Epic #1796. Tasks C1 #1832, C2 #1833, C3 #1834, C4 #1835.

## Implemented and verified

C1 adds the Feature/Epic guide ownership, applicability, evidence and review
contract to `docs/documentation-workflow.md`, contribution instructions and issue/PR
templates. It includes drafting with an existing agent, a list of unverified
claims, source/release checks and explicit hardware verification limits.

C2 activates an independent documentation-impact check. PRs compare the actual
checked-out head to the merge base; base-branch pushes compare before/after trees.
An existing unchanged decision cannot satisfy a new firmware change. Updated
guides must exist and change in the same diff. No-impact reasons remain reviewable
claims, not a semantic guarantee from CI. Missing history and malformed SHA input
fail visibly. Event SHA values are passed as data, not interpolated into commands.

Local evidence:

- 16 real-Git-diff regression tests pass. Missing-decision and unchanged-guide
  fixtures fail as intended; changed-guide and concrete no-impact fixtures pass.
  Coverage includes rename/deletion, deleted decisions/guides, old unchanged
  records, malformed/duplicate metadata, unknown fields, issue/filename mismatch,
  documentation-only changes, absent history, checked-out head mismatch, PR merge
  bases and before/after push diffs.
- 31 existing catalog/content/metadata tests and 6 rendered build tests pass.
- Actual catalog/evidence check and strict MkDocs build pass.
- Actual Epic C branch diff passes the impact checker with its explicit CI-policy
  no-user-impact record.
- Existing firmware change-classifier suite passes all 64 checks. `ci.yml`,
  matrix selection and `ci-green` configuration are unchanged.
- Actionlint 1.7.11 validates the documentation workflow.

Fresh independent review found that compiled `arch/` sources, including OTA code,
were missing from the original classifier. A real Git fixture reproduced the
bypass before the fix. `arch/` now requires a decision and the regression passes.

No firmware behavior, device configuration, hardware, hook or release changes are
part of this work. The public guide content itself is unchanged.

## Open prerequisite and acceptance work

C3 is not implemented or enabled. Site verification gate #1800 remains open,
including the site's outstanding acceptance and scoped sender credential. Do not
treat the fact that the manual is reachable as completion of that gate. The
separate site receiver is merged and its invalid-event/noncanonical/delayed-event
unit tests exist, but this branch does not claim a real sender delivery test.

C4 is partial: local policy failure/pass examples are exercised, but an accepted
guide revision reaching a preview through the sender remains pending C3 and
#1800. This Epic must stay open. CI results and owner review of the integrated
policy must also be recorded before declaring implementation accepted.

Public publishing stays manual and owner-initiated. Until notification integration
is ready, a site build can be started manually to assemble the latest merged
firmware guides. Notification integration, when enabled, will request an artifact
build and will not grant automatic production publication.
