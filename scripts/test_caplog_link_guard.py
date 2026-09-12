#!/usr/bin/env python3
"""Tests for check_caplog_link_guard (#1045).

Pure, no hardware, no compiler. Run: python scripts/test_caplog_link_guard.py
  or python -m pytest scripts/test_caplog_link_guard.py

The last test runs the guard over this repository, so the self-test also proves
the tree is clean.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_caplog_link_guard as g

MAIN = g.REPEATER_MAIN
CLI = g.COMMON_CLI

# The #1045 bug, in the shape the repeater has had since #1060: a role hook
# that moves the link when the command arms, and a service that reconnects.
MAIN_BEFORE = '''
static void wifi_telemetry_caplog_forward_service();
offband::CaplogForward& offband::caplogForwarder() {
    wifi_telemetry_set_persistent(300 * 1000UL);
    return g_caplog_fwd;
}
bool offband::caplogForwardLinkUp() { return WiFi.status() == WL_CONNECTED; }
static void wifi_telemetry_caplog_forward_service() {
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
}
'''

MAIN_AFTER = '''
static void wifi_telemetry_caplog_forward_service();
void wifi_telemetry_set_persistent(uint32_t duration_ms) { g_until = duration_ms; }
offband::CaplogForward& offband::caplogForwarder() {
    // The old arm called wifi_telemetry_set_persistent(0) here.
    return g_caplog_fwd;
}
bool offband::caplogForwardLinkUp() { return WiFi.status() == WL_CONNECTED; }
static void wifi_telemetry_caplog_forward_service() {
    /* WiFi.begin() is never called from here */
    if (WiFi.status() != WL_CONNECTED) return;
    loop_body();
}
void wifi_on_command() { wifi_telemetry_set_persistent(15 * 60000UL); }
'''

CLI_CLEAN = '''
void handle(char* command, char* reply) {
    if (memcmp(command, "wifi on", 7) == 0) {
      wifi_telemetry_set_persistent(15 * 60000UL);
    } else if (memcmp(command, "caplog forward", 14) == 0 && (command[14] == 0 || command[14] == ' ')) {
#if defined(OFFBAND_CAPLOG_FORWARD)
      const offband::CaplogForwardArg arg = offband::caplogParseForwardArg(&command[15]);
      offband::caplogApplyForwardArg(offband::caplogForwarder(), arg, millis());
      offband::caplogForwardReply(reply, 160, arg, true, true, offband::caplogForwardLinkUp(), "");
#else
      strcpy(reply, "caplog forward: not available on this build");
#endif
    } else if (memcmp(command, "caplog stop", 11) == 0) {
      strcpy(reply, "caplog off");
    }
}
'''


def run(main_src=MAIN_AFTER, cli_src=CLI_CLEAN, helper_h=None, helper_cpp=None):
    return g.analyze({MAIN: main_src, CLI: cli_src,
                      g.HELPER_FILES[0]: helper_h, g.HELPER_FILES[1]: helper_cpp})


# ------------------------------------------------------------ the real bug ---

def test_link_control_in_the_role_hooks_is_caught_on_both_calls():
    violations, missing = run(main_src=MAIN_BEFORE)
    assert missing == []
    calls = [(v[0], v[2]) for v in violations]
    assert calls == [(MAIN, "wifi_telemetry_set_persistent"), (MAIN, "WiFi.reconnect")], calls


def test_a_link_call_in_the_command_arm_is_caught():
    cli = CLI_CLEAN.replace("offband::caplogApplyForwardArg(offband::caplogForwarder(), arg, millis());",
                            "offband::caplogApplyForwardArg(offband::caplogForwarder(), arg, millis()); "
                            "wifi_telemetry_force_now();")
    violations, _ = run(cli_src=cli)
    assert [v[2] for v in violations] == ["wifi_telemetry_force_now"], violations


def test_a_link_call_in_the_helper_is_caught():
    violations, _ = run(helper_cpp="void CaplogForward::service() { WiFi.reconnect(); }")
    assert [(v[0], v[2]) for v in violations] == [(g.HELPER_FILES[1], "WiFi.reconnect")], violations


def test_a_link_call_in_the_udp_sink_is_caught():
    files = {MAIN: MAIN_AFTER, CLI: CLI_CLEAN,
             g.HELPER_FILES[2]: "void send() { udp_.beginPacket(h, p); WiFi.begin(s, k); }"}
    violations, _ = g.analyze(files)
    assert [(v[0], v[2]) for v in violations] == [(g.HELPER_FILES[2], "WiFi.begin")], violations


def test_a_link_call_in_the_command_layer_is_caught():
    files = {MAIN: MAIN_AFTER, CLI: CLI_CLEAN,
             g.HELPER_FILES[4]: "int caplogApplyForwardArg() { wifi_telemetry_set_persistent(0); return 0; }"}
    violations, _ = g.analyze(files)
    assert [(v[0], v[2]) for v in violations] == [(g.HELPER_FILES[4], "wifi_telemetry_set_persistent")], \
        violations


def test_an_esp_idf_link_call_is_caught():
    violations, _ = run(helper_cpp="void f() { esp_wifi_disconnect(); }")
    assert [v[2] for v in violations] == ["esp_wifi_disconnect"], violations


def test_line_numbers_point_at_the_call():
    violations, _ = run(main_src=MAIN_BEFORE)
    lines = MAIN_BEFORE.split("\n")
    for _, line, call in violations:
        assert call in lines[line - 1], (line, lines[line - 1])


# --------------------------------------------- what must NOT be reported ---

def test_the_fixed_repeater_code_is_clean():
    assert run() == ([], [])


def test_comments_are_not_calls():
    # MAIN_AFTER names wifi_telemetry_set_persistent and WiFi.begin in comments.
    assert run()[0] == []


def test_link_calls_outside_the_forward_paths_are_not_reported():
    # MAIN_AFTER defines wifi_telemetry_set_persistent and calls it from the
    # `wifi on` handler; CLI_CLEAN calls it from the `wifi on` arm. Operators own
    # the link, so those are correct.
    assert run()[0] == []


def test_reading_link_state_is_allowed():
    # The service and caplogForwardLinkUp() read WiFi.status().
    assert run()[0] == []


def test_the_forward_declaration_is_not_taken_for_the_definition():
    body = g.function_body(g.strip_comments(MAIN_AFTER), "wifi_telemetry_caplog_forward_service")
    assert body is not None
    assert "loop_body" in MAIN_AFTER[body[0]:body[1]]


def test_a_shorter_name_does_not_match_a_longer_one():
    clean = g.strip_comments(MAIN_AFTER)
    assert g.function_body(clean, "caplogForward") is None
    assert g.function_body(clean, "wifi_telemetry_caplog_forward") is None


def test_a_qualified_definition_is_found():
    body = g.function_body(g.strip_comments(MAIN_AFTER), "caplogForwarder")
    assert body is not None
    assert "return g_caplog_fwd" in MAIN_AFTER[body[0]:body[1]]


# ------------------------------------------------- missing targets fail ---

def test_a_renamed_forward_function_fails_rather_than_passing():
    renamed = MAIN_AFTER.replace("wifi_telemetry_caplog_forward_service", "forward_tick")
    violations, missing = run(main_src=renamed)
    assert violations == []
    assert missing == [f"{MAIN}: wifi_telemetry_caplog_forward_service()"], missing


def test_a_missing_role_hook_fails():
    gone = MAIN_AFTER.replace("caplogForwardLinkUp", "linkUp")
    _, missing = run(main_src=gone)
    assert missing == [f"{MAIN}: caplogForwardLinkUp()"], missing


def test_a_missing_command_arm_fails():
    _, missing = run(cli_src="void handle() {}")
    assert missing == [f"{CLI}: the \"caplog forward\" arm"], missing


def test_a_missing_file_fails():
    _, missing = g.analyze({MAIN: None, CLI: CLI_CLEAN})
    assert missing == [MAIN], missing


def test_absent_helper_files_are_not_required():
    assert run(helper_h=None, helper_cpp=None) == ([], [])


# ----------------------------------------------------- the real tree ------

def test_this_repository_is_clean():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    violations, missing = g.analyze(g.read_tree(root))
    assert missing == [], missing
    assert violations == [], violations


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as e:
                failures += 1
                print(f"FAIL {name}: {e}")
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
