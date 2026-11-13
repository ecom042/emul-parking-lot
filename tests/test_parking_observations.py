"""Negative controls for the per-authorization-cycle green oracle."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from scenario.observations import assert_green_cycle


def event(time, signal, value, device="barrier", category="physical"):
    return dict(time_us=time, signal=signal, value=value, device=device, category=category)


def wire(time, text, device, signal):
    return [event(time, signal, ord(byte), device) for byte in text + "\n"]


def cycle():
    physical = [
        event(10, "direction", 1, "motor", "commanded"),
        event(20, "enable", 1, "motor", "commanded"),
        event(30, "position_mdeg", 90000),
        event(31, "enable", 0, "motor", "commanded"),
    ]
    protocol = wire(1, "C1 CAP 1", "camera", "tx_byte")
    protocol += wire(2, "C1 RESULT 1 ABC1D23 85", "camera", "rx_byte")
    protocol += wire(3, "P1 CHARGE current ABC1D23 200 BRL", "payment", "tx_byte")
    protocol += wire(4, "P1 RESULT current PAID 200 BRL", "payment", "rx_byte")
    protocol.append(event(32, "green", 1, "signals"))
    return physical, protocol


class GreenCycleTests(unittest.TestCase):
    def test_current_authorized_full_open_passes(self):
        assert_green_cycle(*cycle(), payment_required=True)

    def test_historical_upper_before_closing_cannot_pass(self):
        physical, protocol = cycle()
        physical.append(event(31, "position_mdeg", 45000))
        with self.assertRaisesRegex(AssertionError, "current full opening"):
            assert_green_cycle(physical, protocol, payment_required=True)

    def test_previous_cycle_paid_result_cannot_pass(self):
        physical, protocol = cycle()
        protocol.insert(0, event(5, "green", 0, "signals"))
        with self.assertRaisesRegex(AssertionError, "camera request in this cycle"):
            assert_green_cycle(physical, protocol, payment_required=True)

    def test_wrong_paid_uuid_cannot_pass(self):
        physical, protocol = cycle()
        protocol = [e for e in protocol if e["device"] != "payment" or e["signal"] != "rx_byte"]
        protocol += wire(4, "P1 RESULT stale PAID 200 BRL", "payment", "rx_byte")
        with self.assertRaisesRegex(AssertionError, "current matching paid"):
            assert_green_cycle(physical, protocol, payment_required=True)
