"""The repeater lists its flood regions through one call (#1905).

A repeater answers a regions request with its flood-allowed region names, and its
CoreScope /neighbors report lists the same names as its own scopes. If the two built
the list differently, CoreScope would record a repeater's scopes one way from its own
report and another way from a neighbor's query. MyMesh::exportFloodScopes is the one
place the list is built (RegionMap::exportNamesTo, REGION_DENY_FLOOD,
FLOOD_SCOPES_MAX); this checks that the answer goes through it and builds nothing of
its own. RegionMap needs a filesystem and is not in the native test env, so the
wiring is checked here instead.

Run: python scripts/test_repeater_flood_scopes_wiring.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MYMESH = ROOT / "examples" / "simple_repeater" / "MyMesh.cpp"


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


EXPORT = re.compile(
    r"return\s+region_map\.exportNamesTo\(\s*dest\s*,\s*FLOOD_SCOPES_MAX\s*,\s*REGION_DENY_FLOOD\s*\)\s*;")
ANSWER = re.compile(r"return\s+8\s*\+\s*exportFloodScopes\(\s*\(\s*char\s*\*\s*\)\s*&reply_data\[8\]\s*\)\s*;")
REPORT = re.compile(
    r"char\s+self_scopes\s*\[\s*FLOOD_SCOPES_MAX\s*\]\s*;\s*exportFloodScopes\(\s*self_scopes\s*\)\s*;"
    r".*?in\.self_scopes\s*=\s*self_scopes\s*;", re.S)


def failures(text):
    code = strip_comments(text)
    got = []
    export = function_body(code, "int MyMesh::exportFloodScopes(char* dest)")
    if export is None:
        got.append("MyMesh::exportFloodScopes is gone")
    elif not EXPORT.search(export):
        got.append("exportFloodScopes no longer lists exportNamesTo(dest, FLOOD_SCOPES_MAX, REGION_DENY_FLOOD)")
    answer = function_body(code, "uint8_t MyMesh::handleAnonRegionsReq(")
    if answer is None:
        got.append("MyMesh::handleAnonRegionsReq is gone")
    else:
        if not ANSWER.search(answer):
            got.append("handleAnonRegionsReq no longer answers through exportFloodScopes")
        if "exportNamesTo" in answer:
            got.append("handleAnonRegionsReq builds its own list with exportNamesTo")
    # #1907: the /neighbors self report lists the same names, at the same size.
    report = function_body(code, "bool MyMesh::publishNeighborsReport()")
    if report is None:
        got.append("MyMesh::publishNeighborsReport is gone")
    elif not REPORT.search(report):
        got.append("publishNeighborsReport no longer takes self_scopes from exportFloodScopes")
    # exportNamesTo is called in exactly one place: exportFloodScopes.
    if len(re.findall(r"\bexportNamesTo\s*\(", code)) != 1:
        got.append("MyMesh.cpp calls exportNamesTo outside exportFloodScopes")
    return got


def test_the_real_tree_passes():
    got = failures(MYMESH.read_text(encoding="utf-8"))
    assert not got, "\n".join(got)


def test_negative_an_answer_that_builds_its_own_list_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "return 8 + exportFloodScopes((char *) &reply_data[8]);",
        "return 8 + region_map.exportNamesTo((char *) &reply_data[8], sizeof(reply_data) - 12, REGION_DENY_FLOOD);", 1)
    got = failures(text)
    assert any("no longer answers through exportFloodScopes" in f for f in got), got
    assert any("builds its own list" in f for f in got), got


def test_negative_a_different_mask_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "exportNamesTo(dest, FLOOD_SCOPES_MAX, REGION_DENY_FLOOD)",
        "exportNamesTo(dest, FLOOD_SCOPES_MAX, REGION_DENY_FLOOD, true)", 1)
    assert any("no longer lists" in f for f in failures(text))


def test_negative_a_different_size_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "exportNamesTo(dest, FLOOD_SCOPES_MAX,", "exportNamesTo(dest, 96,", 1)
    assert any("no longer lists" in f for f in failures(text))


def test_negative_a_commented_out_answer_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "return 8 + exportFloodScopes((char *) &reply_data[8]);",
        "// return 8 + exportFloodScopes((char *) &reply_data[8]);\n    return 8;", 1)
    assert any("no longer answers through exportFloodScopes" in f for f in failures(text))


def test_negative_a_report_with_its_own_list_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "  exportFloodScopes(self_scopes);\n",
        "  region_map.exportNamesTo(self_scopes, 96, REGION_DENY_FLOOD);\n", 1)
    got = failures(text)
    assert any("no longer takes self_scopes" in f for f in got), got
    assert any("outside exportFloodScopes" in f for f in got), got


def test_negative_a_report_with_a_smaller_buffer_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "char self_scopes[FLOOD_SCOPES_MAX];", "char self_scopes[96];", 1)
    assert any("no longer takes self_scopes" in f for f in failures(text))


def test_negative_a_report_scoped_to_the_default_only_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "in.self_scopes = self_scopes;", "in.self_scopes = getDefaultScopeName();", 1)
    assert any("no longer takes self_scopes" in f for f in failures(text))


def test_negative_a_removed_helper_is_caught():
    text = MYMESH.read_text(encoding="utf-8").replace(
        "int MyMesh::exportFloodScopes(char* dest)", "int MyMesh::exportFloodScopesX(char* dest)", 1)
    assert any("exportFloodScopes is gone" in f for f in failures(text))


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
