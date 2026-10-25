---
title: A task the user wants to complete
feature: catalog-feature-id
---

# A task the user wants to complete

Explain the result and why someone would use this feature. Write instructions
for the radio owner, without requiring them to read source code.

## Applies to

Name the firmware role, supported boards/images, checked release(s), and channel.
Distinguish beta/development behavior from stable releases. Mirror the feature
catalog's applicability. State whether this journey was tested on hardware or
only checked against code and automated tests.

## Before you start

List required equipment, firmware, connection, permissions and saved settings.
Explain how to find the right setting or command surface. Use synthetic names,
documentation addresses and angle-bracket placeholders for secrets.

## Steps

Give numbered actions in order, with exact command syntax when needed. State
what the user should see after each action and what an error means. Explain
when a change applies live, requires a retry, or needs a normal restart.

## Check it worked

Give a user-observable success check. Separate a setting being saved from the
feature actually operating. Do not invent output or claim unperformed tests.

## Troubleshooting

Cover likely symptoms and safe checks. Explain where to get help and what
information to report, without exposing passwords or private identifiers.

## Limits and recovery

Explain unsupported cases, persistence and how to undo the change. Distinguish
a restart from a factory reset, full erase or firmware replacement. Do not
suggest destructive recovery where reconfiguring a setting is sufficient.

Maintainer evidence belongs in `docs/user/features.yml`: repository-relative
source/test paths and literal assertions that need review if they change.
The template itself is not published.
