# QCC badge interface: the owner's design, mapped onto the firmware

**Spec:** the owner's Claude Design mockups, `docs/Queen_City_Con/Offband MeshCore UI Mockups-handoff.zip`, file `Offband MeshCore 128x64.dc.html`. It's local and untracked. This document doesn't restate the design. It maps each screen to firmware and records where the hardware forced a change.

**Owner's direction (2026-09-15):**
- "Utilize the designs I took the time to put together as a starting framework to build out the additional screens."
- "In the beta/release build, I want the Unread/messages screen to become the Home screen instead of the PIN screen."

**Slice order:** each slice gets the owner's bench check before the next one starts.

## This slice (Epic #1188)

| Design | Firmware | Task |
|---|---|---|
| Message history, unread counts (implied by 1a) | `BadgeStore`: conversations, message pool, unread, fed by MyMesh | #1229 |
| 1a hero / 1c inverted count: **Messages** | Inbox, and **Home** in both badge builds | #1230 |
| 1a thread entry and settled, 1e inline compose, 1a compose full | Thread with inline compose; a full-screen editor past one line | #1230 |
| 1a message selected (caret, meta line, dead send) | ↑/↓ select; Enter resends a failed DM | #1230 |
| 1a new-message flash | The row jumps to the top and inverts for 1 s, with no popup | #1230 |
| 1f SW1 tap and breadcrumb | Messages → Contacts → Status | #1231 |
| 1a Contacts | Contacts; Enter opens the DM thread | #1231 |
| 1a Status (the empty state) | Status, and the BLE pairing PIN moves here | #1231 |

## Later slices (not started)

- The name tag (SW1 double-tap) and the supporter card with its contact beacon (SW1 hold), from 1f.
- Canned replies (1e, 2a) and Fn hints (1a).
- Pin and mute (Fn+P, Fn+M), and Fn+1..9 jump.
- Settings: the list, the three editors and the apply gate (3a).
- First boot: pick your handle (reference image).

## Where the hardware changed the design

- **Fonts.** The body font, 6×8, is the display's built-in font, so the 21×8 grid is exact. The 5×8 meta text also uses the 6×8 font for now, giving 21 columns instead of 25. A 5×8 font is a follow-up.
- **Times.** Rows show an age ("now", "4m", "2h") instead of a clock time. MeshCore keeps UTC only, and a standalone badge may never be synced, so a clock time needs a time zone setting. The Contacts mockup already uses ages.
- **Channel ticks.** A channel message has no receipt on the badge. MeshCore sends no ACK, and the repeat check lives in the phone client. So channel sends show `···` while queued and no mark after. DMs show ✓ or X. A tick for heard repeats is a follow-up.
- **DM threads drop the sender tag.** There's only one sender. Channel threads keep it.
- **SW1.** Tap cycles the three lists. Double-tap and hold keep their current jobs (back, and Enter) until the name tag and the supporter card exist. That way a badge whose keyboard fails can still get around.
- **Fn hints.** "Hold Fn" can't be seen. The keyboard latches Fn and reports only the finished key, so the hints need a different trigger. That's a later slice.
- **The old Home pages** (Bluetooth toggle, advert, hibernate, radio, recent adverts) stay reachable: Enter on Status opens them, until Settings replaces them.
- **Diag builds** get the same Home. They keep the self-test screen after the splash, and Tab still opens the key test.

## Store budget

- 24 conversations and 32 messages, about 8 KB of RAM.
- When the pool is full, the busiest conversation loses its oldest message first, so a busy #Public can't wipe out a quiet DM thread.
- Unread counts live with the conversation and survive eviction.
- The old popup's 4.6 KB preview queue isn't allocated on badge builds.

## Open questions for the owner (my default applies until answered)

1. **Inbox shape.** One merged inbox (1a, 1c) or split Channels and DMs lists (2a)? Default: merged, as "the Unread/messages screen". The row grammar is the same either way.
2. **Clock times.** Should there be a time zone setting, so rows show the con's local time? Default: ages.
3. **Channel ticks.** Should the badge detect heard repeats, so a channel send can show ✓? Default: no mark.
4. **The design's own settings questions:**
   - a region preset row;
   - TX above the regional limit: refuse or warn;
   - settings on the SW1 cycle, or Fn+S only.
