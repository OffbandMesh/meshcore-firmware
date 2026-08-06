#!/usr/bin/env python
"""Unit tests for offband-cmd's pure request builders (#567). No network/SSH."""
import importlib.util
import os
import shlex
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("offband_cmd", os.path.join(_HERE, "offband-cmd.py"))
oc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(oc)


class QueuePayload(unittest.TestCase):
    def test_cli_command(self):
        self.assertEqual(
            oc.build_queue_payload("cli", cli_cmd="caplog forward 300"),
            {"action": "cli", "expires_in_sec": 400, "params": {"cmd": "caplog forward 300"}},
        )

    def test_raw_action_no_params(self):
        self.assertEqual(
            oc.build_queue_payload("reboot"),
            {"action": "reboot", "expires_in_sec": 400},
        )

    def test_window_and_expires(self):
        self.assertEqual(
            oc.build_queue_payload("ota_enable", window_sec=900, expires_sec=900),
            {"action": "ota_enable", "expires_in_sec": 900, "params": {"window_sec": 900}},
        )

    def test_extra_params_merge(self):
        b = oc.build_queue_payload("cli", cli_cmd="get x", params={"foo": "bar"})
        self.assertEqual(b["params"], {"foo": "bar", "cmd": "get x"})


class Urls(unittest.TestCase):
    def test_cmds_url_strips_trailing_slash(self):
        self.assertEqual(oc.cmds_url("http://h:8765/", "stp-lab"),
                         "http://h:8765/devices/stp-lab/cmds")

    def test_result_url_wait(self):
        self.assertEqual(oc.result_url("http://h:8765", "stp-lab", 7, 120),
                         "http://h:8765/devices/stp-lab/cmds/7/result?wait=120")

    def test_state_url(self):
        self.assertEqual(oc.state_url("http://h:8765", "stp-lab"),
                         "http://h:8765/devices/stp-lab/state")

    def test_url_encodes_device_segment(self):
        # A '/' in a device name must not break path routing.
        self.assertEqual(oc.cmds_url("http://h:8765", "a/b"),
                         "http://h:8765/devices/a%2Fb/cmds")


class TailCmd(unittest.TestCase):
    def test_filters_by_device_tag_and_lines(self):
        argv = oc.caplog_tail_cmd("u@h", "stp-lab", "/var/log/x.log", 50)
        self.assertIn("u@h", argv)
        # Safe chars stay unquoted; the pattern + path + line count are intact.
        self.assertEqual(argv[-1], "grep -F caplog-stp-lab: /var/log/x.log | tail -n 50")

    def test_single_quote_in_device_is_injection_safe(self):
        argv = oc.caplog_tail_cmd("u@h", "x'; reboot; '", "/var/log/x.log", 5)
        pre_pipe = argv[-1].split("| tail")[0]
        toks = shlex.split(pre_pipe)          # parses the remote shell as the host would
        self.assertEqual(toks[:2], ["grep", "-F"])
        self.assertEqual(toks[2], "caplog-x'; reboot; ':")  # one intact pattern token
        self.assertNotIn("reboot", toks[3:])                # NOT a separable command


if __name__ == "__main__":
    unittest.main(verbosity=2)
