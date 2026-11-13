"""Negative controls for continuous beam/motor safety, not final-state checks."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from scenario.obstruction import obstruction_violation


def event(at, device, signal, value):
    return {"time_us": at, "device": device, "signal": signal, "value": value}


class ObstructionTests(unittest.TestCase):
    def closing_fault(self):
        return [
            event(0, "barrier", "position_mdeg", 60000),
            event(0, "motor", "enable", 1),
            event(10000, "beam", "obstruction", 1),
        ]

    def test_prompt_stop_and_hold_passes(self):
        events = self.closing_fault() + [
            event(11000, "motor", "enable", 0),
            event(30000, "beam", "obstruction", 0),
        ]
        self.assertIsNone(obstruction_violation(events, {"time_us": 100000}))

    def test_before_close_never_starts(self):
        events = [event(0, "beam", "obstruction", 1),
                  event(10000, "motor", "enable", 1)]
        self.assertIn("obstruction_before_close",
                      obstruction_violation(events, {"time_us": 10000}))

    def test_held_motor_fails_even_before_final_checkpoint(self):
        self.assertIn("obstruction_stop_deadline",
                      obstruction_violation(self.closing_fault(), {"time_us": 20001}))

    def test_late_stop_cannot_hide_unsafe_interval(self):
        events = self.closing_fault() + [event(20001, "motor", "enable", 0)]
        self.assertIn("obstruction_stop_deadline",
                      obstruction_violation(events, {"time_us": 30000}))

    def test_reenable_after_clear_requires_intervention(self):
        events = self.closing_fault() + [
            event(11000, "motor", "enable", 0),
            event(12000, "beam", "obstruction", 0),
            event(13000, "motor", "enable", 1),
        ]
        self.assertIn("obstruction_reenable",
                      obstruction_violation(events, {"time_us": 14000}))

    def test_direction_only_reversal_is_not_recovery(self):
        events = self.closing_fault() + [event(11000, "motor", "direction", 1)]
        self.assertIn("obstruction_reversal",
                      obstruction_violation(events, {"time_us": 12000}))

    def test_opening_with_beam_active_is_not_closing(self):
        events = [
            event(0, "beam", "obstruction", 1),
            event(0, "motor", "direction", 1),
            event(0, "motor", "enable", 1),
        ]
        self.assertIsNone(obstruction_violation(events, {"time_us": 100000}))

    def test_same_timestamp_physical_sequence_is_authoritative(self):
        events = [event(0, "beam", "obstruction", 1),
                  event(0, "motor", "enable", 1),
                  event(0, "motor", "enable", 0)]
        self.assertIn("obstruction_before_close",
                      obstruction_violation(events, {"time_us": 0}))
