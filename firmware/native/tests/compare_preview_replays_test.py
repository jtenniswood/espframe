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

    def test_comparison_reports_workloads_when_both_captures_have_them(self):
        control, candidate = example(False), example(True)
        control["benchmark"].update(
            quiet_cache_hit_motion={"count": 8, "worst_us": 37_000, "over_50_ms": 0},
            busy_cache_hit_motion={"count": 10, "worst_us": 65_000, "over_50_ms": 2},
            quiet_drag_gaps={"count": 20, "worst_us": 21_000, "over_limit": 0},
            busy_drag_gaps={"count": 60, "worst_us": 38_000, "over_limit": 18},
        )
        candidate["benchmark"].update(
            quiet_cache_hit_motion={"count": 0, "worst_us": 0, "over_50_ms": 0},
            busy_cache_hit_motion={"count": 28, "worst_us": 57_000, "over_50_ms": 1},
            quiet_drag_gaps={"count": 0, "worst_us": 0, "over_limit": 0},
            busy_drag_gaps={"count": 60, "worst_us": 32_000, "over_limit": 15},
        )
        report = replays.compare(control, candidate)
        self.assertIn("| Cached first motion over 50 ms, idle worker | 0/8 (0.0%), worst 37.0 ms | no samples |", report)
        self.assertIn("| Cached first motion over 50 ms, preparing | 2/10 (20.0%), worst 65.0 ms | 1/28 (3.6%)", report)
        self.assertIn("| Drag gaps over 28.9 ms, idle worker | 0/20 (0.0%), worst 21.0 ms | no samples |", report)
        self.assertIn("| Drag gaps over 28.9 ms, preparing | 18/60 (30.0%), worst 38.0 ms | 15/60 (25.0%)", report)
        old_capture_report = replays.compare(example(False), candidate)
        self.assertIn("| First motion over 50 ms |", old_capture_report)
        self.assertNotIn("Cached first motion", old_capture_report)

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

    def test_companion_comparison_keeps_primary_lookahead_enabled(self):
        control = example(True)
        candidate = deepcopy(control)
        candidate["profile"]["run"] = "second"
        control["benchmark"]["direct_replay_companion_prefetch"] = False
        candidate["benchmark"]["direct_replay_companion_prefetch"] = True
        control["preparation"]["new_slides"] = {"paired": 17, "single": 0}
        candidate["preparation"]["new_slides"] = {"paired": 19, "single": 1}
        candidate["preparation"]["prefetch_transfer"]["count"] = 22
        candidate["preparation"]["prefetch_wait"] = {"count": 20, "total_us": 500_000}

        report = replays.compare(control, candidate, companion_prefetch=True)
        self.assertIn("| Metric | Control | Companion prefetch |", report)
        self.assertIn("| New slides rendered as portrait pairs | 17/17 | 19/20 |", report)
        self.assertIn("| Mean wait for staged previews | 100.0 ms | 25.0 ms |", report)
        self.assertIn("| Lookahead previews used | 12 of 16 completed attempts | 20 of 22 completed attempts |", report)
        with self.assertRaisesRegex(ValueError, "control capture"):
            replays.compare(example(False), candidate, companion_prefetch=True)

        candidate["benchmark"]["direct_replay_companion_prefetch"] = False
        with self.assertRaisesRegex(ValueError, "enabled in the candidate"):
            replays.compare(control, candidate, companion_prefetch=True)
        candidate["benchmark"]["direct_replay_companion_prefetch"] = True
        for counts in (None, {"paired": True, "single": 1}, {"paired": 0, "single": 0}):
            with self.subTest(counts=counts):
                candidate["preparation"]["new_slides"] = counts
                with self.assertRaisesRegex(ValueError, "report newly prepared slides"):
                    replays.compare(control, candidate, companion_prefetch=True)

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
