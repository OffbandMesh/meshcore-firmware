# Flood delivery: idle wait and echo-confirmed retry

Feature #2246, Epic #2247. Agreed by the owner on 2026-11-23.

### How it works today (read from `firmware-base` @8be9a020)
- `Mesh::routeRecvPacket()` appends this repeater's hash to the path, then queues the packet with a random hold from `MyMesh::getRetransmitDelay()`: 0 to 5 × `tx_delay_factor` (0.5) × airtime, so 0 to 2.5 airtimes.
- `Dispatcher::checkSend()` sends once the hold expires. Its only channel check is `_radio->isReceiving()` at that instant. If a reception has just *ended*, it transmits immediately. Field data shows this happening 71 ms after another packet.
- After the send, nothing tracks the packet. Any later copy is dropped by `_tables->wasSeen()` in `Mesh::onRecvPacket()`.

### Field evidence (one repeater, 2026-11-23, 34 channel sends it decoded and transmitted)
- The outcome was binary. Either 37–47 observers heard the message, or only the two observers in direct range of the repeater did.
- The repeater's transmit power did not change. Both nearby observers heard its transmission at the same RSSI (−46 to −53 dBm) on lost and delivered sends.
- When the repeater heard other traffic during its hold, 7 of 9 sends were lost. When it heard nothing, 5 of 25 were.

### Part 1: idle wait
- `Dispatcher` records when the last reception ended and how long that packet was on air.
- In `checkSend()`, a queued **flood** packet is held until the channel has been quiet for one airtime of that last packet, plus a 0–120 ms random jitter so neighbours waiting on the same packet don't fire together. Direct-route packets, ACKs and the repeater's own sends are not held.
- **Cap:** the extra hold is at most 3 s per packet. After that it transmits anyway, so a busy channel can't starve it.
- The hook is a virtual returning 0 by default, so companion and room-server builds behave exactly as today unless they opt in.

### Part 2: echo-confirmed retry
- New pure helper `src/helpers/FloodEchoTracker.h`, host-tested. It holds up to 4 recently forwarded flood packets: the hash, our path position, and a copy of the packet.
- **On send complete** of a flood packet that this repeater forwarded (its own hash is last in the path), the tracker records it with an echo deadline: 2.5 airtimes for the downstream hold, plus 1 airtime for their transmission, plus a 500 ms margin. For a 165-byte channel message that is about 2.6 s.
- **On receive,** before the duplicate check drops it, any copy of a tracked hash whose path is at least as long as ours counts as an echo. That entry is done.
- **At the deadline** with no echo, it queues one copy of the original packet, unchanged, through Part 1's idle wait. That entry is then finished: **never a second retry.** Downstream nodes that already have the packet drop the copy by hash.
- **Skipped:**
  - Adverts. They're periodic, big, and repeated anyway.
  - Packets the repeater sent itself.
  - Repeaters with no evidence of a downstream neighbour. Retries pause when no echo of any kind has been heard in the last 30 minutes.
- **Airtime:** the retry goes through the same queue and duty-cycle budget as every other send.

### Settings and visibility
- CLI: `set flood.quiet on|off` and `set flood.retry on|off`, stored in repeater prefs (appended fields, defaulting to ON for Offband repeaters). `get flood` shows both settings and the counters.
- Counters: forwards tracked, forwards echoed, retries sent, retries that then drew an echo, quiet-wait holds, quiet-wait cap hits. A periodic MeshLog line carries them through caplog.

### Tests (native, CI)
- Tracker: echo before the deadline means no retry. No echo means exactly one retry. A retry with no echo means no second retry. A shorter-path copy is not an echo. A full table evicts the oldest entry. Adverts are not tracked. A stale-neighbour window suppresses retries.
- Quiet-wait decision, as a pure function: quiet long enough means send; recently busy means hold; the cap means send; direct packets are never held.
- Both run in CI's native-tests job, which discovers every `test/test_*` folder under `[env:native]`.

### Lifecycle
PLAN #2248 → BUILD-1 #2249 tracker and tests → BUILD-2 #2250 Dispatcher/Mesh hooks and quiet wait → BUILD-3 #2251 CLI, prefs, counters and docs → TEST #2252 → push/PR/merge #2253. Each BUILD task gets a Gemini 2.5 review before it closes. Flashing the field repeater is a separate approval.

### Interop
No wire change and no new packet types. Stock repeaters, companions and the stock client see at most one extra copy of a packet, which they already drop by hash.
