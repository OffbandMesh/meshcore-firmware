# Feature PLAN: Offband-owned host (broker first)

| | |
|---|---|
| **Feature** | [#1022](https://github.com/OffbandMesh/meshcore-firmware/issues/1022) · Citadel `Crosswire-hw0` |
| **Serves** | No Initiative above it. Offband's own server surface: an MQTT broker, private build delivery, the website's dynamic half. |
| **Status** | Draft. Approved when this PR merges. |
| **Owner** | Ben (Strycher) |
| **Last updated** | 2026-10-16 |

## At a glance

- **What:** an Offband MQTT broker on `offband-prod`, reachable from anywhere over wss on 443 with a username and password, so Offband observers (first, the R8 SAW beta board) can publish from any network.
- **Why now:** the R8's beta RF data only reaches Offband infrastructure while it is on the home LAN. The Pi's Mosquitto is closed to the internet, as it should be, and a phone hotspot will not carry the R8 through the phone's VPN.
- **How:** Epic #1024 adds the broker as a service in `OffbandMesh/offband-infra`, behind the host's existing nginx and Cloudflare front door. #1025 (private builds) and #1026 (website origin) follow under their own Epic plans. #1027 integration-tests the whole host last.
- **Done when:** the R8 publishes status and packets to the Offband broker over wss/443 from a non-home network, the data lands in the beta log, and the host still answers only on 22, 80 and 443.

## 1. Diagnosis

| # | Finding | Evidence | Effect |
|---|---|---|---|
| 1 | The host is provisioned and hardened (#1023, closed). | `checks/run_all.py --host offband-prod`: all PASS on 2026-10-16. `/opt/offband` is a clean clone of `offband-infra` at `79b230e`. Data volume mounted. | The broker is one more service on an existing base, not a new host. |
| 2 | The host's front door is nginx on 443 behind Cloudflare, with Authenticated Origin Pulls. Services listen on `127.0.0.1` only. | `nginx/sites/notify.offband.org` (`ssl_verify_client on` with the origin-pull CA). `notify/compose.yaml` publishes `127.0.0.1:8080` only. | The broker follows the same shape: a loopback websocket listener, nginx on 443, Cloudflare in front. |
| 3 | The observer firmware can publish over wss with username/password auth. | `docs/observer-cli-commands.md:69-72` (`transport` tcp/tls/wss, `auth_type` none/basic/jwt). | No firmware change for option A. |
| 4 | The firmware's JWT is self-signed by the node's own key and carries that key; `alg` is the non-standard `"Ed25519"`. | `src/helpers/wifi_observer/JwtHelper.cpp:43-87`. | Stock Mosquitto cannot verify it, and a valid token only proves key possession. The owner chose username/password (option A) on 2026-10-16. |
| 5 | The Pi's Mosquitto accepts anonymous clients and is not reachable from the internet. | `/etc/mosquitto/conf.d/unfound.conf` (`listener 1883`, `allow_anonymous true`). External TCP probe of 1883 from Hetzner on 2026-10-16: closed/filtered. | Keep it closed. The R8 off-LAN needs a broker that is safe to reach from anywhere. |
| 6 | Today the R8 publishes to the Pi on slot 5, and a listener on the Pi records it. | `meshcore/CVG/4F1A27195AAE0452…/{status,packets}` seen on the Pi broker; `~/offband-beta/r8-mqtt.log` filling (#1483). | The beta log already exists. The new broker extends it to off-LAN operation. |
| 7 | **Not yet verified:** the R8's TLS client trusts the certificate Cloudflare presents at the edge, and its MQTT keepalive fits Cloudflare's idle limit for websockets. | Firmware `ca_cert` choices are named roots (`letsencrypt`, `gts-r4`, …). The edge certificate's issuer for the chosen name is not yet read. | Task A1 measures both before anything is built. A mismatch changes the CA setting or the proxy mode, not the design. |

## 2. Scope

**In (Epic #1024):**
- Mosquitto (`eclipse-mosquitto:2`, arm64) in Docker, websocket listener bound to `127.0.0.1` only, `allow_anonymous false`, password file and ACL from `/opt/offband/secrets/` (root-only), persistence on the data volume.
- An nginx site for the broker's name, TLS on 443, websocket upgrade proxied to the loopback listener, behind Cloudflare like `notify`.
- One user per publishing node. The ACL lets each node write only under its own `meshcore/+/<node id>/#`. One read-only user for the capture.
- A capture service on the host that writes the R8's status and packets to the data volume, plus a pull of that file into the per-host beta log.
- Extend `checks/run_all.py`: the broker's ports never answer from outside, its published port is loopback, its secrets are owner-only.
- Point the R8 at the broker and prove it from a non-home network.

**Out:**
- JWT/allowlist auth (option B). Revisit only if community nodes are to publish here.
- Opening 1883 or 8883 anywhere, on this host or at home.
- Home Assistant sensors for observer data.
- #1025 and #1026 design detail: each gets its own Epic plan when started. This plan's grant does not cover them.

## 3. Epics

### Epic #1024: Offband MQTT broker over wss/443

| Task | Pts |
|---|---|
| **A1 · Measure the unknowns:** read the edge certificate's issuer for the chosen name, and the R8's MQTT keepalive and protocol version (3.1.1 or 5) in source. Then connect a desktop MQTT-over-websocket client through Cloudflare to a throwaway loopback listener and hold it past Cloudflare's idle limit. Record the right `ca_cert` value, the protocol version, and whether proxied mode works. | 3 |
| **A2 · Broker service:** `mosquitto/compose.yaml`, `mosquitto.conf` (websocket listener on loopback, no anonymous, persistence on the volume, `log_type` limited to error, warning and notice so connection chatter is not logged), data folder created by `host/apply.py data`. Checked by starting the container and doing an authenticated publish and subscribe on the loopback listener, plus the existing loopback check. | 3 |
| **A3 · Front door:** `nginx/sites/<broker name>` with TLS and websocket upgrade, DNS and certificate through the existing Cloudflare API tooling (#511). The broker's hostname gets no Cloudflare feature that rewrites traffic (no Rocket Loader, minification or transforming WAF rule). Checked by `nginx -t` and an anonymous connect being refused. | 3 |
| **A4 · Users and ACL:** password file and ACL as root-only secrets. Users for the R8 and the capture. A test that the R8 user cannot publish outside its own topic and the capture user cannot publish at all. | 3 |
| **A5 · Capture and log:** a capture container that subscribes as the read-only user and appends ISO-timestamped lines to the data volume. The Pi pulls that file into `~/offband-beta/` on a schedule. Checked by lines appearing and surviving a container restart. | 3 |
| **A6 · Checks:** extend `checks/run_all.py` and its tests: no broker port outside, broker secrets owner-only, published port loopback. | 2 |
| **A7 · R8 cutover:** on `_sys`, configure a slot for the broker (`transport wss`, `auth_type basic`, the R8's user and password, the A1 CA). `mqtt status` shows `up`. Then from a phone hotspot: status and packets reach the capture file. | 2 |
| **A8 · Epic verification:** `checks/run_all.py` all PASS with the broker live; the hotspot run's capture lines recorded on the issue; rollback rehearsed by stopping the service and confirming the site returns nothing. | 1 |

### Epic #1025: private build delivery (own plan later)
Signed, expiring links through nginx. Gated on the owner's policy ruling in #1025. Not authorized by this plan.

### Epic #1026: website origin (own plan later)
Split vs full migration. Not authorized by this plan.

### Epic #1027: Integration Testing (last)
Proves the whole host end to end once #1024-#1026 are in. Its first criterion, a real observer over wss/443, is this plan's A7.

## 4. Order and dependencies

#1023 is done. #1024 comes first because the beta needs it now. A1 runs before any file is written, because its two answers decide the CA setting and the proxy mode. A2 to A4 touch separate files (`mosquitto/`, `nginx/sites/`, secrets). A6 edits `checks/` only. #1025 and #1026 are independent of #1024 and of each other. #1027 depends on all three.

Work happens in `OffbandMesh/offband-infra`, one branch and one PR for the Epic, deployed to the host by `git pull` in `/opt/offband`, never scp.

## 5. Grants and authorization

| Term | Proposed | Why |
|---|---|---|
| **What merging this plan authorizes** | Building Epic #1024 in `offband-infra` and deploying it to `offband-prod`: one new container service, one nginx site, one DNS name, secrets on the host. | It is the scope the owner asked for. #1025/#1026 are excluded. |
| **Merges** | 1: the Epic #1024 PR in `offband-infra`. | One Epic, one PR (canon). |
| **Epic verification** | Owner approves the PR after A8's evidence is on the issue. | A new public service; the owner sees the proof before it merges. |
| **Owner steps during the run** | Type the R8's password on `_sys` (A7), and carry the R8 onto a hotspot for the off-LAN test. | The R8's password should not pass through chat; the hotspot test needs the device. |
| **Stops** | Stop and report if A1 finds the R8 cannot validate the edge certificate in any configured CA, or Cloudflare drops the websocket. Stop if any check FAILs after a deploy. | Each changes the design and needs the owner. |
| **Flash** | None. | No firmware change. |

```grant-terms
{
  "epic_chain": [1024],
  "chain": false,
  "verification": { "mode": "owner" },
  "merge": { "budget": 1, "basis": "Epic #1024 is one PR in OffbandMesh/offband-infra; no other merges are needed for it." },
  "flash": null,
  "reset": null,
  "expires_after_hours": 168
}
```

## 6. Can't-tell branches

| Where | Can't tell what | Does |
|---|---|---|
| Broker auth | A client presents no user, an unknown user or a wrong password | **stop**: refused (`allow_anonymous false`, password file) |
| Broker ACL | A user publishes to a topic not granted | **stop**: denied (ACL default-deny) |
| Broker start | Password file or ACL missing or unreadable | **stop**: the container fails to start and the check reports FAIL; it never starts open |
| nginx | Request for an unknown name or a non-websocket path | **stop**: closed without an answer, as for every other site |
| Capture | Subscriber disconnects | **stop and retry**: logs the error to its own file and reconnects; never silent |
| Pi pull | Remote file missing or unreachable | **stop**: logs to `export.err`, keeps the last good copy |

No fail-open branches.

## 7. Verification

- **Named command:** `python checks/run_all.py --host offband-prod` (all PASS), plus `python -m unittest discover -s checks/tests -v`.
- **Acceptance bar:** the R8, on a phone hotspot, publishes status and packets that appear in the capture file, while an anonymous client and a wrong password are both refused.

## 8. Done when

- The R8 publishes to the Offband broker over wss/443 from a non-home network.
- Its status and packets land in the beta log on the volume and in `~/offband-beta/` on the Pi.
- `checks/run_all.py` is all PASS with the broker live; only 22, 80 and 443 answer from outside.
- Rollback is written in `offband-infra`'s README and has been rehearsed.

## 9. Your choices

| # | Question | Option A | Option B | Rec. | Your choice |
|---|---|---|---|---|---|
| 1 | Broker name | `mqtt.offband.org` | another name you prefer | ⭐ A | A (2026-10-16) |
| 2 | Where the R8 data is recorded | on `offband-prod` (capture container), pulled to the Pi | the Pi subscribes to the Offband broker directly | ⭐ A: no dependency on a websocket-capable client on the Pi | A (2026-10-16) |
| 3 | Keep the R8 also publishing to the Pi on slot 5 while home | yes | no | ⭐ A: two independent records during the beta | A (2026-10-16) |

## 10. Carried-in work

| Item | Goes to |
|---|---|
| R8 beta data log and the Pi listener (#1483) | A5 extends it; the Pi listener stays for the home path |
| Option B (JWT with an allowlist of node keys) | Out of scope; a future Epic if community nodes publish here |
| #1023 (closed, delivered via `offband-infra` / standards#476) | Basis for this plan |
