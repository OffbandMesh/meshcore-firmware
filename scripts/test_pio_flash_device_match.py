"""Device-identity matching tests for pio-flash (#323).

The defect these pin: `pio-flash` identified boards by the Windows DeviceID
instance (`8&XXXXXXX`), which is a **port path** -- it names the USB socket, not
the board. Move a board to another port and it stopped matching; plug a different
board into that port and it inherited the previous occupant's identity. Compounding
it, a registry entry with `discriminators.windows: null` matched its whole VID:PID
class and silently swallowed any unrecognised board of that chip family.

Real-world consequence: the tool reported a node that is physically mounted in the
owner's garage ceiling as sitting on the bench. A wrong match here writes firmware
to the wrong board.

These cases cannot be produced by unplugging hardware (you cannot easily make two
boards trade sockets on demand, and you certainly cannot fabricate a third board),
so they are pinned here. The hardware port-swap test required to close #323 is
complementary to this, not replaced by it.

Run: python -m pytest scripts/test_pio_flash_device_match.py -q
"""

import importlib.util
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "pio_flash", Path(__file__).resolve().parent / "pio-flash.py"
)
pio_flash = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(pio_flash)

find_in_registry = pio_flash.find_in_registry
norm_serial = pio_flash.norm_serial

VID = "303A:0002"


def reg(devices):
    return {"devices": devices, "foreign_devices": {}}


# --------------------------------------------------------------------------
# Serial normalisation
# --------------------------------------------------------------------------
def test_norm_serial_strips_separators_and_cases():
    # The same board reports both forms depending on which USB endpoint it
    # enumerates on -- observed on an ESP32-S3 across runtime vs bootloader.
    assert norm_serial("E8:F6:0A:CA:4E:54") == norm_serial("e8f60aca4e54")
    assert norm_serial("44-1B-F6-62-44-8C") == "441BF662448C"
    assert norm_serial(None) == ""


# --------------------------------------------------------------------------
# Tier 1: serial is identity
# --------------------------------------------------------------------------
def test_serial_matches_regardless_of_port_path():
    """The whole point: identity must survive moving to a different USB port."""
    r = reg({"Firestar": {"vid_pid": [VID], "usb_serial": "441BF662448C"}})
    # Port path deliberately bears no relation to anything recorded.
    kind, name, _ = find_in_registry(r, VID, "9&DEADBEEF", "441BF662448C")
    assert (kind, name) == ("device", "Firestar")


def test_serial_matches_in_other_usb_mode_with_different_pid_format():
    r = reg({"ST-P": {"vid_pid": [VID], "usb_serial": "E8F60ACA4E54"}})
    kind, name, _ = find_in_registry(r, VID, "8&519AF3A", "E8:F6:0A:CA:4E:54")
    assert (kind, name) == ("device", "ST-P")


def test_registered_device_does_not_impersonate_another_board():
    """A board with a recorded serial must NOT claim a port holding a different board.

    This is the LIBT failure: the entry matched on class/port-path and adopted a
    stranger's port.
    """
    r = reg({"LIBT": {"vid_pid": [VID], "usb_serial": "AAAABBBBCCCC"}})
    kind, name, _ = find_in_registry(r, VID, "8&3A6FE2F5", "441BF662448C")
    assert (kind, name) == (None, None)


# --------------------------------------------------------------------------
# Tier 3: class-only entries must never match
# --------------------------------------------------------------------------
def test_class_only_entry_never_matches():
    """`discriminators.windows: null` + no serial => matches nothing, ever.

    Previously this returned the entry whenever it was the SOLE class-only
    candidate -- which is exactly the situation that misreported the garage node
    as attached.
    """
    r = reg({"LIBT": {"vid_pid": [VID], "discriminators": {"windows": None}}})
    kind, name, _ = find_in_registry(r, VID, "8&3A6FE2F5", "441BF662448C")
    assert (kind, name) == (None, None)


def test_class_only_entry_does_not_shadow_a_real_serial_match():
    r = reg({
        "LIBT": {"vid_pid": [VID], "discriminators": {"windows": None}},
        "Firestar": {"vid_pid": [VID], "usb_serial": "441BF662448C"},
    })
    kind, name, _ = find_in_registry(r, VID, "8&3A6FE2F5", "441BF662448C")
    assert (kind, name) == ("device", "Firestar")


# --------------------------------------------------------------------------
# Tier 2: legacy fallback, for entries predating usb_serial
# --------------------------------------------------------------------------
def test_legacy_hash_still_matches_when_no_serial_recorded():
    r = reg({"Old": {
        "vid_pid": [VID],
        "discriminators": {"windows": {"runtime_deviceid_instance": "8&519AF3A"}},
    }})
    kind, name, _ = find_in_registry(r, VID, "8&519AF3A", "E8F60ACA4E54")
    assert (kind, name) == ("device", "Old")


def test_serial_entry_beats_legacy_hash_entry_on_same_port():
    """If one entry claims the port by stale port-path and another owns the serial,
    the serial wins -- otherwise a moved board is mislabelled by whoever inherited
    its old socket."""
    r = reg({
        "StaleOccupant": {
            "vid_pid": [VID],
            "discriminators": {"windows": {"runtime_deviceid_instance": "8&3A6FE2F5"}},
        },
        "Firestar": {"vid_pid": [VID], "usb_serial": "441BF662448C"},
    })
    kind, name, _ = find_in_registry(r, VID, "8&3A6FE2F5", "441BF662448C")
    assert (kind, name) == ("device", "Firestar")


# --------------------------------------------------------------------------
# The scenario that produced #323
# --------------------------------------------------------------------------
def test_two_boards_swapping_ports_each_resolve_to_themselves():
    r = reg({
        "A": {"vid_pid": [VID], "usb_serial": "AAAAAAAAAAAA"},
        "B": {"vid_pid": [VID], "usb_serial": "BBBBBBBBBBBB"},
    })
    port_a, port_b = "8&1111111", "8&2222222"
    # Before the swap
    assert find_in_registry(r, VID, port_a, "AAAAAAAAAAAA")[1] == "A"
    assert find_in_registry(r, VID, port_b, "BBBBBBBBBBBB")[1] == "B"
    # After swapping sockets -- identities must follow the boards, not the ports
    assert find_in_registry(r, VID, port_b, "AAAAAAAAAAAA")[1] == "A"
    assert find_in_registry(r, VID, port_a, "BBBBBBBBBBBB")[1] == "B"


def test_unknown_board_is_unregistered_not_someone_else():
    r = reg({
        "LIBT": {"vid_pid": [VID], "discriminators": {"windows": None}},
        "Firestar": {"vid_pid": [VID], "usb_serial": "441BF662448C"},
    })
    kind, name, _ = find_in_registry(r, VID, "8&9999999", "FFFFFFFFFFFF")
    assert (kind, name) == (None, None)


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-q"]))
