"""The repeater's scope query to its neighbors is wired as planned (#1903).

The request and reply formats, the walk and the reply matching rules are shared,
Arduino-free and native-tested (ObserverNeighbors.h, RegionsPeer.h); this checks the
repeater-side wiring the native tests cannot reach.

#1909, the request (MyMesh::sendRegionsReq):
- it is an anonymous request built by buildRegionsRequest, encrypted to the neighbor,
  and sent zero-hop, so the neighbor answers zero-hop direct;
- the in-flight peer (its key and secret, which the reply path decrypts with) is kept
  only once a packet exists, and the tag only once the request has gone out.

#1910, the reply (searchPeersByHash / getPeerSharedSecret / onPeerDataRecv):
- the in-flight neighbor is matched under kRegionsPeerIdx, after the ACL, and only
  while a request is out; it decrypts with its own secret, never an ACL entry's;
- onPeerDataRecv routes through routePeerData before it picks up a client, hands the
  walk's reply to onRegionsReply, and stops anything else matched as the regions peer.

#1911, the walk (startScopeQuery / scopeQueryTick / releaseScopePeer):
- loop() steps it; each pass polls, sends at most one request and reports the send;
- the in-flight peer is forgotten, and its secret wiped, the moment its request closes
  (checked before and after poll(), which can close one by timeout);
- a walk starts over every filled row of the neighbor table, RSSI 0 (unknown).

Run: python scripts/test_repeater_scope_query_wiring.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MYMESH = ROOT / "examples" / "simple_repeater" / "MyMesh.cpp"


def strip_comments(text):
    """Comments out, string and char literals kept intact."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def function_body(code, signature):
    start = code.find(signature)
    if start < 0:
        return None
    open_at = code.find("{", start)
    depth, i = 0, open_at
    while i < len(code):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return code[open_at:i + 1]
        i += 1
    return None


def request_failures(text):
    code = strip_comments(text)
    body = function_body(code, "bool MyMesh::sendRegionsReq(")
    if body is None:
        return ["MyMesh::sendRegionsReq is gone"]
    got = []
    if not re.search(r"offband::buildRegionsRequest\(\s*t\s*,\s*req\s*\)", body):
        got.append("sendRegionsReq no longer builds the request with buildRegionsRequest")
    if not re.search(r"createAnonDatagram\(\s*PAYLOAD_TYPE_ANON_REQ\s*,\s*self_id\s*,\s*peer\s*,\s*secret\s*,", body):
        got.append("sendRegionsReq no longer sends an anonymous request encrypted to the neighbor")
    send = body.find("sendZeroHop(pkt);")
    if send < 0:
        got.append("sendRegionsReq no longer sends zero-hop")
    if re.search(r"\bsendFlood\w*\(", body):
        got.append("sendRegionsReq floods the request")
    m = re.search(r"if\s*\(\s*pkt\s*==\s*NULL\s*\)\s*\{?[^}]*?return\s+false\s*;", body)
    null_check = m.start() if m else -1
    peer_set = body.find("_regions_peer_set = true;")
    if null_check < 0 or peer_set < 0 or peer_set < null_check:
        got.append("sendRegionsReq keeps the in-flight peer before a packet exists")
    tag_set = body.find("*tag = t;")
    if tag_set < 0 or send < 0 or tag_set < send:
        got.append("sendRegionsReq sets the tag before the request went out")
    return got


MYMESH_H = ROOT / "examples" / "simple_repeater" / "MyMesh.h"


def matching_failures(text, header):
    """#1910: the regions peer is matched after the ACL, decrypts with its own secret, and
    is routed (RegionsPeer.h) before onPeerDataRecv touches a client."""
    code = strip_comments(text)
    got = []
    search = function_body(code, "int MyMesh::searchPeersByHash(")
    if search is None:
        return ["MyMesh::searchPeersByHash is gone"]
    loop_end = search.find("return n;")
    acl_loop = search.find("for (int i = 0; i < acl.getNumClients(); i++)")
    peer = search.find("matching_peer_indexes[n++] = offband::kRegionsPeerIdx;")
    if peer < 0:
        got.append("searchPeersByHash no longer matches the regions peer")
    elif acl_loop < 0 or peer < acl_loop or peer > loop_end:
        got.append("searchPeersByHash matches the regions peer before the ACL")
    elif "_regions_peer_set &&" not in search[:peer]:
        got.append("searchPeersByHash matches the regions peer with no request out")
    secret = function_body(code, "void MyMesh::getPeerSharedSecret(")
    if secret is None or not re.search(
            r"if\s*\(\s*i\s*==\s*offband::kRegionsPeerIdx\s*\)\s*\{\s*memcpy\(\s*dest_secret\s*,\s*_regions_peer_secret\s*,"
            r"\s*PUB_KEY_SIZE\s*\)\s*;\s*return\s*;", secret):
        got.append("getPeerSharedSecret no longer gives the regions peer its own secret")
    elif secret.find("kRegionsPeerIdx") > secret.find("acl.getClientByIdx"):
        got.append("getPeerSharedSecret looks the regions peer up in the ACL")
    data = function_body(code, "void MyMesh::onPeerDataRecv(")
    if data is None:
        return got + ["MyMesh::onPeerDataRecv is gone"]
    route = data.find("offband::routePeerData(i, acl.getNumClients(), is_response, reply)")
    client = data.find("ClientInfo* client = acl.getClientByIdx(i);")
    if route < 0:
        got.append("onPeerDataRecv no longer routes through routePeerData")
    elif client >= 0 and route > client:
        got.append("onPeerDataRecv picks up a client before routing")
    if not re.search(r"case\s+offband::PeerRoute::Regions:\s*onRegionsReply\(\s*reply\s*,\s*data\s*,\s*len\s*\)\s*;\s*return\s*;", data):
        got.append("onPeerDataRecv no longer hands the walk's reply to onRegionsReply and returns")
    if not re.search(r"case\s+offband::PeerRoute::Drop:\s*if\s*\(\s*i\s*==\s*offband::kRegionsPeerIdx\s*\)\s*return\s*;", data):
        got.append("onPeerDataRecv does not stop a dropped regions-peer packet")
    if not re.search(r"int\s+matching_peer_indexes\[\s*MAX_CLIENTS\s*\+\s*1\s*\]\s*;", strip_comments(header)):
        got.append("matching_peer_indexes has no room for the regions peer")
    return got


def walk_failures(text):
    """#1911: the walk is stepped from loop(), sends at most one request per pass, and
    the in-flight peer is forgotten the moment its request closes."""
    code = strip_comments(text)
    got = []
    loop = function_body(code, "void MyMesh::loop()")
    if loop is None or "scopeQueryTick();" not in loop:
        got.append("MyMesh::loop() no longer steps the walk")
    tick = function_body(code, "void MyMesh::scopeQueryTick()")
    if tick is None:
        return got + ["MyMesh::scopeQueryTick is gone"]
    poll = tick.find("_scope_run->poll(now)")
    send = tick.find("sendRegionsReq(next, &tag)")
    sent = tick.find("_scope_run->sent(ok, tag, now)")
    if poll < 0 or send < 0 or sent < 0 or not (poll < send < sent):
        got.append("scopeQueryTick no longer polls, sends, then reports the send")
    if tick.count("releaseScopePeer();") < 2 or tick.find("releaseScopePeer();", poll) < 0:
        got.append("scopeQueryTick no longer releases the peer before and after poll()")
    release = function_body(code, "void MyMesh::releaseScopePeer()")
    if release is None or "_regions_peer_set = false;" not in release:
        got.append("releaseScopePeer no longer forgets the in-flight peer")
    elif "memset(_regions_peer_secret, 0, PUB_KEY_SIZE);" not in release:
        got.append("releaseScopePeer no longer wipes the peer's secret")
    start = function_body(code, "bool MyMesh::startScopeQuery()")
    if start is None:
        return got + ["MyMesh::startScopeQuery is gone"]
    if "if (nb.heard_timestamp == 0) continue;" not in start or "r.rssi = 0;" not in start:
        got.append("startScopeQuery no longer snapshots every filled neighbor row")
    if "_scope_run->start(rows, n);" not in start:
        got.append("startScopeQuery no longer starts the walk over the snapshot")
    return got


def read(p):
    return p.read_text(encoding="utf-8")


def test_the_real_tree_passes():
    got = (request_failures(read(MYMESH)) + matching_failures(read(MYMESH), read(MYMESH_H))
           + walk_failures(read(MYMESH)))
    assert not got, "\n".join(got)


def test_negative_a_peer_that_is_never_forgotten_is_caught():
    text = read(MYMESH).replace("    _regions_peer_set = false;\n    memset(_regions_peer_secret, 0, PUB_KEY_SIZE);\n", "", 1)
    assert text != read(MYMESH)
    assert any("forgets the in-flight peer" in f for f in walk_failures(text))


def test_negative_a_secret_left_after_release_is_caught():
    text = read(MYMESH).replace("    memset(_regions_peer_secret, 0, PUB_KEY_SIZE);\n  }\n}", "  }\n}", 1)
    assert text != read(MYMESH)
    assert any("wipes the peer's secret" in f for f in walk_failures(text))


def test_negative_no_release_after_poll_is_caught():
    text = read(MYMESH).replace(
        "  releaseScopePeer();   // closed by a timeout inside poll(): forget it before the next is taken\n", "", 1)
    assert text != read(MYMESH)
    assert any("before and after poll()" in f for f in walk_failures(text))


def test_negative_a_walk_not_stepped_from_loop_is_caught():
    text = read(MYMESH).replace("  scopeQueryTick();       // #1911", "  // scopeQueryTick();", 1)
    assert text != read(MYMESH)
    assert any("no longer steps the walk" in f for f in walk_failures(text))


def test_negative_a_send_not_reported_is_caught():
    text = read(MYMESH).replace("  _scope_run->sent(ok, tag, now);\n", "", 1)
    assert any("polls, sends, then reports" in f for f in walk_failures(text))


def test_negative_the_regions_peer_matched_before_the_acl_is_caught():
    text = read(MYMESH)
    peer_block = ("  if (_regions_peer_set && mesh::Identity(_regions_peer_key).isHashMatch(hash)) {\n"
                  "    matching_peer_indexes[n++] = offband::kRegionsPeerIdx;\n  }\n")
    assert peer_block in text
    text = text.replace(peer_block, "", 1).replace(
        "int MyMesh::searchPeersByHash(const uint8_t *hash) {\n  int n = 0;\n",
        "int MyMesh::searchPeersByHash(const uint8_t *hash) {\n  int n = 0;\n" + peer_block, 1)
    assert any("before the ACL" in f for f in matching_failures(text, read(MYMESH_H)))


def test_negative_routing_after_the_client_lookup_is_caught():
    text = read(MYMESH)
    lookup = "  ClientInfo* client = acl.getClientByIdx(i);\n"
    at = text.find("void MyMesh::onPeerDataRecv(")
    route_at = text.find("  // #1910: a regions reply goes to the walk", at)
    assert route_at > 0 and lookup in text[at:]
    text = text[:route_at] + lookup + text[route_at:]
    assert any("picks up a client before routing" in f for f in matching_failures(text, read(MYMESH_H)))


def test_negative_a_dropped_peer_packet_that_falls_through_is_caught():
    text = read(MYMESH).replace("        if (i == offband::kRegionsPeerIdx) return;   // not the walk's: never a client's\n", "", 1)
    assert text != read(MYMESH)
    assert any("does not stop a dropped" in f for f in matching_failures(text, read(MYMESH_H)))


def test_negative_a_peer_secret_from_the_acl_is_caught():
    text = read(MYMESH).replace("    memcpy(dest_secret, _regions_peer_secret, PUB_KEY_SIZE);\n    return;\n", "", 1)
    assert text != read(MYMESH)
    assert any("its own secret" in f for f in matching_failures(text, read(MYMESH_H)))


def test_negative_no_room_for_the_peer_is_caught():
    header = read(MYMESH_H).replace("matching_peer_indexes[MAX_CLIENTS + 1];", "matching_peer_indexes[MAX_CLIENTS];", 1)
    assert any("no room" in f for f in matching_failures(read(MYMESH), header))


def test_negative_a_flooded_request_is_caught():
    text = read(MYMESH).replace("  sendZeroHop(pkt);\n  *tag = t;", "  sendFlood(pkt);\n  *tag = t;", 1)
    assert text != read(MYMESH)
    got = request_failures(text)
    assert any("floods the request" in f for f in got), got


def test_negative_a_request_that_is_not_anonymous_is_caught():
    text = read(MYMESH).replace("createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, peer, secret,",
                                "createAnonDatagram(PAYLOAD_TYPE_REQ, self_id, peer, secret,", 1)
    assert any("anonymous request" in f for f in request_failures(text))


def test_negative_a_peer_kept_before_the_packet_exists_is_caught():
    text = read(MYMESH)
    text = text.replace("  _regions_peer_set = true;\n  sendZeroHop(pkt);", "  sendZeroHop(pkt);", 1)
    text = text.replace("  mesh::Packet* pkt = createAnonDatagram(PAYLOAD_TYPE_ANON_REQ",
                        "  _regions_peer_set = true;\n  mesh::Packet* pkt = createAnonDatagram(PAYLOAD_TYPE_ANON_REQ", 1)
    assert any("before a packet exists" in f for f in request_failures(text))


def test_negative_a_tag_set_before_the_send_is_caught():
    text = read(MYMESH).replace("  sendZeroHop(pkt);\n  *tag = t;", "  *tag = t;\n  sendZeroHop(pkt);", 1)
    assert text != read(MYMESH)
    assert any("before the request went out" in f for f in request_failures(text))


if __name__ == "__main__":
    n_fail = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("PASS %s" % name)
            except AssertionError as e:
                n_fail += 1
                print("FAIL %s: %s" % (name, e))
    print("\n%d failure(s)" % n_fail)
    sys.exit(1 if n_fail else 0)
