# Documentation delivery issue map

Updated 2026-10-27. Original plan merged in #1802; the foundation, C1/C2 policy
and six initial journeys are delivered. Manual publishing is live. F/G/H track
the owner's additional initial-population request; operational/sender and final
Integration Testing gates remain open.

| Work | GitHub | Citadel |
|---|---|---|
| feature | [OffbandMesh/meshcore-firmware#1794](https://github.com/OffbandMesh/meshcore-firmware/issues/1794) | `Crosswire-uuu` |
| A | [OffbandMesh/meshcore-firmware#1795](https://github.com/OffbandMesh/meshcore-firmware/issues/1795) | `Crosswire-kzm` |
| B | [OffbandMesh/offband-site#60](https://github.com/OffbandMesh/offband-site/issues/60) | `offband-site-21w` |
| C | [OffbandMesh/meshcore-firmware#1796](https://github.com/OffbandMesh/meshcore-firmware/issues/1796) | `Crosswire-jyi` |
| D | [OffbandMesh/meshcore-firmware#1797](https://github.com/OffbandMesh/meshcore-firmware/issues/1797) | `Crosswire-km2` |
| E | [OffbandMesh/meshcore-firmware#1798](https://github.com/OffbandMesh/meshcore-firmware/issues/1798) | `Crosswire-grs` |
| F: repeater telemetry/HA backfill | [#1843](https://github.com/OffbandMesh/meshcore-firmware/issues/1843) | `Crosswire-3p1` |
| G: reliability/diagnostic rig backfill | [#1844](https://github.com/OffbandMesh/meshcore-firmware/issues/1844) | `Crosswire-c6p` |
| H: observer operation backfill | [#1845](https://github.com/OffbandMesh/meshcore-firmware/issues/1845) | `Crosswire-6io` |
| plan | [OffbandMesh/meshcore-firmware#1799](https://github.com/OffbandMesh/meshcore-firmware/issues/1799) | `Crosswire-gzi` |
| backfill plan amendment | [#1858](https://github.com/OffbandMesh/meshcore-firmware/issues/1858) | `Crosswire-crn` |
| siteGate | [OffbandMesh/offband-site#61](https://github.com/OffbandMesh/offband-site/issues/61) | `offband-site-cs7` |
| crossGate | [OffbandMesh/meshcore-firmware#1800](https://github.com/OffbandMesh/meshcore-firmware/issues/1800) | `Crosswire-fyx` |

Native GitHub children of firmware#1794: A-H, original planning task #1799,
amendment task #1858 and cross-project gate #1800. Site#61 remains a native child
of site#60. All backfill tasks below are native children of their listed Epic and
have existing-board items and matching Citadel records.

Original planning #1799 is complete. A was delivered by #1816, C1/C2 by #1836,
and D by #1841. B's production site is live, with rollback/acceptance still open.
C3 #1834 and C4 #1835 remain gated by #1800 and a scoped credential; they were not
completed merely by merging the policy PR. E is final and depends on F/G/H in
addition to the preceding implementation and site verification.

| Backfill task | GitHub | Citadel |
|---|---|---|
| F1: repeater purpose/setup | #1846 | `Crosswire-vvn` |
| F2: HA integration/entities | #1847 | `Crosswire-md5` |
| F3: verification | #1848 | `Crosswire-h7l` |
| G1: SafeBoot purpose | #1849 | `Crosswire-442` |
| G2: watchdog behavior | #1850 | `Crosswire-lvy` |
| G3: diagnostic builds/reference rig | #1851 | `Crosswire-hro` |
| G4: verification | #1852 | `Crosswire-vu4` |
| H1: capability-to-guide map | #1853 | `Crosswire-ru2` |
| H2: complete setup | #1854 | `Crosswire-gc5` |
| H3: rotation/held/heap/failure | #1855 | `Crosswire-1ii` |
| H4: authentication/trust recipes | #1856 | `Crosswire-5sy` |
| H5: verification | #1857 | `Crosswire-scn` |

Each backfill verification task depends on its Epic's authoring tasks. F2 uses
F1's setup and H2-H4 use H1's audited scope. All three new Epics precede final E.

Existing client#476 -> #477 -> #478 (closed research) and #491 remain unchanged.
Client#492 stays deferred. Firmware#440 -> #441-#445, #755 and #287 retain their
ownership. Site#33 and #53/#57 remain related work, not replacement parents.

No legacy issue has been closed, moved, reparented or relabeled by this plan.
