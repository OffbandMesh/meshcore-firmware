"""The repeater's scope query to its neighbors is wired as planned (#1903).

The request and reply formats, the walk and the reply matching rules are shared,
Arduino-free and native-tested (ObserverNeighbors.h, RegionsPeer.h); this checks the
repeater-side wiring the native tests cannot reach.

#1909, the request (MyMesh::sendRegionsReq):
- it is an anonymous request built by buildRegionsRequest, encrypted to the neighbor,
  and sent zero-hop, so the neighbor answers zero-hop direct;
- the in-flight peer (its key and secret, which the reply path decrypts with) is kept
  only once a packet exists, and the tag only once the request has gone out.

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
    null_check = body.find("if (pkt == NULL) return false;")
    peer_set = body.find("_regions_peer_set = true;")
    if null_check < 0 or peer_set < 0 or peer_set < null_check:
        got.append("sendRegionsReq keeps the in-flight peer before a packet exists")
    tag_set = body.find("*tag = t;")
    if tag_set < 0 or send < 0 or tag_set < send:
        got.append("sendRegionsReq sets the tag before the request went out")
    return got


def read(p):
    return p.read_text(encoding="utf-8")


def test_the_real_tree_passes():
    got = request_failures(read(MYMESH))
    assert not got, "\n".join(got)


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
