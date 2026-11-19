#!/usr/bin/env python3
"""Exercise capture and comparison without a device connection."""

from copy import deepcopy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import compare_preview_replays as replays


FRAME = "http://192.0.2.10"
ORIGIN = "http://192.0.2.11:9116"


def example(lookahead):
    return {
        "frame": FRAME,
        "profile": {"run": str(lookahead), "sha256": "fixture"},
        "benchmark": {
            "direct_replay_lookahead": lookahead, "replay_origin": ORIGIN, "scripted": True, "source": "immich",
            "scenario": "rapid", "target": 48, "started": 48, "settled": 48, "wrong_slide": 0,
            "frame_period_us": 19_243, "drag_gap_limit_us": 28_864, "refill_limit_us": 500_000,
            "cold_starts": 20 if lookahead else 30, "cold_returns": 15 if lookahead else 27,
            "first_motion": {"count": 48, "worst_us": 47_500, "over_50_ms": 0},
            "drag_gaps": {"count": 80 if lookahead else 100, "worst_us": 38_000, "over_limit": 20},
            "refill": {"count": 3, "worst_us": 760_000, "over_limit": 1},
        },
        "preparation": {
            "prepared_transfer": {"count": 0},
            "prefetch_transfer": {"count": 16 if lookahead else 0},
            "prefetch_wait": {"count": 12 if lookahead else 0, "total_us": 1_200_000 if lookahead else 0},
        },
        "status": {"cache_slots": 8, "wifi_primary_channel": 6, "wifi_power_save": 0, "wifi_rssi_dbm": -66},
    }


class ReplayTest(unittest.TestCase):
    def test_comparison_reports_rates(self):
        report = replays.compare(example(False), example(True))
        self.assertIn("| Photos ready at swipe start | 18/48 | 28/48 |", report)
        self.assertIn("20/100 (20.0%), worst 38.0 ms | 20/80 (25.0%)", report)
        self.assertIn("12 of 16 completed attempts", report)
        self.assertIn("100.0 ms", report)

    def test_comparison_rejects_invalid_inputs(self):
        for section, key, value, message in (
            ("profile", "run", "False", "Restart"),
            ("profile", "sha256", "different", "dataset"),
            ("benchmark", "direct_replay_lookahead", False, "control capture"),
            ("benchmark", "settled", 47, "48 settled"),
            ("benchmark", "wrong_slide", 1, "wrong slide"),
            ("benchmark", "frame_period_us", 19_200, "timing thresholds"),
            ("preparation", "prepared_transfer", {"count": 1}, "preparation service"),
            ("status", "cache_slots", 7, "cache sizes"),
        ):
            with self.subTest(section=section, key=key):
                candidate = example(True)
                candidate[section][key] = value
                with self.assertRaisesRegex(ValueError, message):
                    replays.compare(example(False), candidate)

    @patch.object(replays, "read_json")
    @patch.object(replays, "sleep")
    def test_capture_waits_for_final_swipe(self, wait, get):
        run = example(True)
        profile, benchmark, preparation, status = (
            run["profile"], run["benchmark"], run["preparation"], run["status"],
        )
        get.side_effect = [profile, {**benchmark, "settled": 47}, benchmark, preparation, status, benchmark, profile]
        self.assertEqual(replays.capture(FRAME, ORIGIN, 2), run)
        wait.assert_called_once()

    @patch.object(replays, "read_json")
    def test_capture_rejects_a_server_restart(self, get):
        run = example(True)
        profile, benchmark = run["profile"], run["benchmark"]
        changed = deepcopy(profile)
        changed["run"] = "restarted"
        get.side_effect = [profile, benchmark, run["preparation"], run["status"], benchmark, changed]
        with self.assertRaisesRegex(ValueError, "restarted"):
            replays.capture(FRAME, ORIGIN, 2)


if __name__ == "__main__":
    unittest.main()
