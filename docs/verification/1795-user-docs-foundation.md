# Epic A verification: firmware documentation foundation

Feature #1794, Epic #1795. Implementation commit: `a8de1a8bfe09a95766be33850c43fc01a1ab665e`.
Verified locally on 2026-10-25, Windows, Python 3.14, in an isolated environment.
CI uses Python 3.12; its result is reported separately on the Epic PR.

| Check | Result |
|---|---|
| Validator regression suite | 31 tests passed, including negative metadata, role/board/release, evidence, paths, symlinks, HTML navigation, private examples and impact decisions. |
| Strict standalone build | Passed with mkdocs.user.yml and pinned direct dependencies. |
| Rendered publication suite | 6 tests passed: manual/guide HTML, exact command blocks in numbered steps without language-label prefixes, firmware source edit URL, WiFi search result, no external font request, and exclusion of catalog metadata plus synthetic internal canaries. |
| Existing firmware classifier | 64 checks passed; no classifier or matrix changes. |
| Whitespace check | Passed. |
| Fresh whole-tree code review | Found an HTML-navigation bypass. Four negative cases failed before the parser restriction and pass after it; fenced HTML examples remain non-navigation. |

The WiFi journey was compared with WifiConfigProvider, WifiBootstrap,
SystemChannelCli, ObserverCli and declared observer environments. Basic command
behavior and images were also checked in the offband-v1.4.0 source tag. Source
assertions in features.yml are drift tripwires, not hardware or semantic proof.

## Limits

No radio was flashed or reconfigured, no new hardware trial was performed, and
no production site was deployed. User usability sign-off is still required.
The manual explicitly labels hardware validation as pending. Remaining launch
journeys are cataloged as planned or needing review, not completed guides.

Documentation-impact validation is an optional tested interface; Epic C owns
activating PR policy. Public hosting, dispatch, visible build revision metadata
and full launch content belong to later Epics. The current build is a local
preview, not a claim that docs.offband.org is live.

Material for MkDocs announces end of life on May 5, 2027
([upstream notice](https://squidfunk.github.io/mkdocs-material/)). The approved
stack is retained here; generator migration is separate maintenance work.
