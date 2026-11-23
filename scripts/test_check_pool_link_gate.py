#!/usr/bin/env python3
"""Tests for scripts/check_pool_link_gate.py (#2232): it passes the real tree,
fails an owner that drives a pool without setLinkUp(), and ignores files that
are not broker-pool users."""
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_pool_link_gate as chk  # noqa: E402


def tree(files):
    root = tempfile.mkdtemp()
    for rel, text in files.items():
        path = os.path.join(root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    return root


class CheckPoolLinkGate(unittest.TestCase):
    def test_real_tree_passes(self):
        self.assertEqual([], chk.owners_missing_gate(os.path.join(HERE, "..")))

    def test_repeater_owner_without_gate_fails(self):
        root = tree({"examples/r/main.cpp":
                     "#include \"RepeaterMqttPool.h\"\nvoid f(){ offband::repeaterMqttPool().loop(1); }\n"})
        self.assertEqual([os.path.join("examples", "r", "main.cpp")], chk.owners_missing_gate(root))

    def test_observer_owner_without_gate_fails(self):
        root = tree({"src/o/Obs.cpp": "MqttBrokerPool s_pool;\nvoid f(){ s_pool.loop(1); }\n"})
        self.assertEqual([os.path.join("src", "o", "Obs.cpp")], chk.owners_missing_gate(root))

    def test_owner_with_gate_passes(self):
        root = tree({"src/o/Obs.cpp":
                     "MqttBrokerPool s_pool;\nvoid f(){ s_pool.setLinkUp(true); s_pool.loop(1); }\n"})
        self.assertEqual([], chk.owners_missing_gate(root))

    def test_real_tree_sets_the_teardown_hook(self):
        self.assertEqual([], chk.transport_owners_missing_hook(os.path.join(HERE, "..")))

    def test_transport_owner_without_hook_fails(self):
        root = tree({"examples/r/main.cpp":
                     "t = new WifiMqttTransport(a);\n"
                     "void f(){ offband::repeaterMqttPool().setLinkUp(1); offband::repeaterMqttPool().loop(1); }\n"})
        self.assertEqual([os.path.join("examples", "r", "main.cpp")], chk.transport_owners_missing_hook(root))

    def test_transport_owner_with_hook_passes(self):
        root = tree({"examples/r/main.cpp":
                     "t = new WifiMqttTransport(a);\n"
                     "t->setBeforeTeardown([](){ offband::repeaterMqttPool().linkGoingDown(feed); });\n"
                     "void f(){ offband::repeaterMqttPool().loop(1); }\n"})
        self.assertEqual([], chk.transport_owners_missing_hook(root))

    def test_hook_without_watchdog_feed_fails(self):
        root = tree({"examples/r/main.cpp":
                     "t = new WifiMqttTransport(a);\n"
                     "t->setBeforeTeardown([](){ offband::repeaterMqttPool().linkGoingDown(); });\n"
                     "void f(){ offband::repeaterMqttPool().loop(1); }\n"})
        self.assertEqual([os.path.join("examples", "r", "main.cpp")], chk.transport_owners_missing_hook(root))

    def test_unrelated_pool_loop_is_ignored(self):
        root = tree({"src/x/Thread.cpp": "ThreadPool pool;\nvoid f(){ pool.loop(1); }\n"})
        self.assertEqual([], chk.owners_missing_gate(root))


if __name__ == "__main__":
    unittest.main()
