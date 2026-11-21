#!/usr/bin/env python3
"""Exercise capture and comparison without a device connection."""

from copy import deepcopy
from http.client import RemoteDisconnected
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

    def test_slow_preview_comparison_keeps_stall_and_prefetch_settings_equal(self):
        control, candidate = example(True), example(True)
        candidate["profile"]["run"] = "second"
        for run, counts in ((control, (11, 7, 4)), (candidate, (9, 8, 0))):
            run["profile"]["previewStallMs"] = 2500
            run["benchmark"]["direct_replay_companion_prefetch"] = True
            run["replay_stats"] = {"stalledPreviews": dict(zip(("started", "completed", "interrupted"), counts))}
        report = replays.compare(control, candidate, slow_previews=True)
        self.assertIn("| Metric | Control | Candidate firmware |", report)
        self.assertIn("| Previews with a 2500 ms body pause started | 11 | 9 |", report)
        self.assertIn("| Paused previews completed | 7 | 8 |", report)
        self.assertIn("| Paused previews interrupted | 4 | 0 |", report)

        candidate["profile"]["previewStallMs"] = 2800
        with self.assertRaisesRegex(ValueError, "same preview stall duration"):
            replays.compare(control, candidate, slow_previews=True)
        candidate["profile"]["previewStallMs"] = 2500
        candidate["benchmark"]["direct_replay_companion_prefetch"] = False
        with self.assertRaisesRegex(ValueError, "same companion prefetch setting"):
            replays.compare(control, candidate, slow_previews=True)
        candidate["benchmark"]["direct_replay_companion_prefetch"] = True
        candidate["replay_stats"]["stalledPreviews"]["interrupted"] = 3
        with self.assertRaisesRegex(ValueError, "consistent counts"):
            replays.compare(control, candidate, slow_previews=True)

    def test_fill_comparison_checks_both_modes_and_pair_pixels(self):
        control, candidate = example(False), example(True)
        control["benchmark"]["replay_pair_fill"] = True
        with self.assertRaisesRegex(ValueError, "same portrait pair framing"):
            replays.compare(control, candidate)
        candidate["benchmark"]["replay_pair_fill"] = True
        control["preparation"]["new_slides"] = {"paired": 11, "single": 0, "pair_fill_edge_failures": 0}
        candidate["preparation"]["new_slides"] = {"paired": 13, "single": 0, "pair_fill_edge_failures": 0}
        report = replays.compare(control, candidate)
        self.assertIn("| Filled pairs with edge-to-edge coverage | 11 | 13 |", report)
        candidate["preparation"]["new_slides"]["pair_fill_edge_failures"] = 1
        with self.assertRaisesRegex(ValueError, "edge check failed for 1 of 13"):
            replays.compare(control, candidate)
        candidate["preparation"]["new_slides"] = {"paired": 0, "single": 1, "pair_fill_edge_failures": 0}
        with self.assertRaisesRegex(ValueError, "at least one portrait pair"):
            replays.compare(control, candidate)
        candidate["preparation"]["new_slides"]["paired"] = 13
        with self.assertRaisesRegex(ValueError, "render each new slide as a pair"):
            replays.compare(control, candidate)

    def test_profile_comparison_matches_the_source_library_and_firmware_options(self):
        control, candidate = example(True), example(True)
        for run, size, webp, transferred in ((control, 0, 0, 1_500_000), (candidate, 360, 80, 30_000)):
            run["profile"].update(run=str(size), sha256=f"preview-{size}", sourceSha256="originals",
                                  previewSize=size, baselineQuality=0, webpQuality=webp)
            run["benchmark"]["direct_replay_companion_prefetch"] = True
            run["preparation"]["transfer"] = {"bytes": transferred}
            run["preparation"]["prefetch_transfer"]["bytes"] = transferred
        report = replays.compare(control, candidate, preview_profiles=True)
        self.assertIn("| Metric | Control | Candidate profile |", report)
        self.assertIn("| Preview profile | source size, mixed JPEG and WebP | 360 px short edge, WebP quality 80 |", report)
        self.assertIn("| Photo bytes downloaded by frame | 3000000 | 60000 |", report)

        for section, key, value, message in (
            ("profile", "sourceSha256", "other-originals", "same synthetic source"),
            ("profile", "sha256", "preview-0", "different preview encodes"),
            ("benchmark", "direct_replay_companion_prefetch", False, "same companion prefetch"),
        ):
            with self.subTest(key=key):
                changed = deepcopy(candidate)
                changed[section][key] = value
                with self.assertRaisesRegex(ValueError, message):
                    replays.compare(control, changed, preview_profiles=True)

    def test_prepared_comparison_checks_that_the_service_supplied_the_slides(self):
        direct, prepared = example(True), example(True)
        prepared["profile"].update(run="prepared", warmRotation="0", warmPairFill=True)
        prepared["benchmark"].pop("direct_replay_lookahead")
        prepared["benchmark"]["replay_prepared"] = True
        for run in (direct, prepared):
            run["benchmark"]["replay_pair_fill"] = True
            run["preparation"]["new_slides"] = {"paired": 11, "single": 0, "pair_fill_edge_failures": 0}
        direct["preparation"]["transfer"] = {"count": 4, "bytes": 1_500_000}
        direct["preparation"]["prefetch_transfer"]["bytes"] = 9_500_000
        prepared["preparation"].update(transfer={"count": 0}, prefetch_transfer={"count": 0},
                                       prepared_transfer={"count": 12, "bytes": 2_000_000}, prepared_decode={"count": 11})
        result = replays.compare(direct, prepared, prepared_service=True)
        self.assertIn("| Metric | Control | Prepared slides |", result)
        self.assertIn("| Photo bytes downloaded by frame | 11000000 | 2000000 |", result)
        self.assertIn("| Preparation-service cache at start | — | prewarmed |", result)

        captured_direct, captured_prepared = deepcopy(direct), deepcopy(prepared)
        for run in (captured_direct, captured_prepared):
            run["profile"].update(capturedPreviews=True, warmPairFill=False)
            run["benchmark"]["replay_pair_fill"] = False
        captured = replays.compare(captured_direct, captured_prepared, prepared_service=True)
        self.assertIn("| Preview dataset | Captured Immich previews | Captured Immich previews |", captured)
        self.assertIn("| New slides rendered as portrait pairs | 11/11 | 11/11 |", captured)
        captured_prepared["profile"].pop("capturedPreviews")
        with self.assertRaisesRegex(ValueError, "same kind of preview dataset"):
            replays.compare(captured_direct, captured_prepared, prepared_service=True)

        prepared["preparation"]["transfer"]["count"] = 1
        with self.assertRaisesRegex(ValueError, "without falling back to direct previews"):
            replays.compare(direct, prepared, prepared_service=True)

    @patch.object(replays, "read_json")
    def test_capture_saves_server_counts_with_the_stall_profile(self, get):
        run = example(True)
        profile, benchmark = run["profile"], run["benchmark"]
        profile["previewStallMs"] = 2500
        run["replay_stats"] = {"stalledPreviews": {"started": 5, "completed": 4, "interrupted": 1}}
        get.side_effect = [profile, benchmark, run["preparation"], run["status"], run["replay_stats"], benchmark, profile]
        self.assertEqual(replays.capture(FRAME, ORIGIN, 2), run)
        get.assert_any_call(ORIGIN, "/replay-stats")

    @patch.object(replays, "read_json")
    @patch.object(replays, "sleep")
    def test_capture_waits_for_final_swipe(self, wait, get):
        run = example(True)
        profile, benchmark, preparation, status = (
            run["profile"], run["benchmark"], run["preparation"], run["status"],
        )
        get.side_effect = [
            profile, RemoteDisconnected(), {**benchmark, "settled": 47}, benchmark,
            ConnectionResetError(), benchmark, preparation, status, benchmark, profile,
        ]
        self.assertEqual(replays.capture(FRAME, ORIGIN, 2), run)
        self.assertEqual(wait.call_count, 3)

    @patch.object(replays, "read_json")
    @patch.object(replays, "sleep")
    def test_staged_capture_waits_for_detail_on_the_final_slide(self, wait, get):
        run = example(True)
        profile, benchmark, preparation = run["profile"], run["benchmark"], run["preparation"]
        profile.update(thumbnailSha256="thumbnail fixture", failFirstDetail=True)
        benchmark.update(replay_staged_detail=True, settled_slide=19)
        settled = {**run["status"], "thumbnail_slides": 21, "uptime_ms": 31_000,
                   "detail_presentations": 1, "last_detail_slide": 18}
        detailed = {**settled, "uptime_ms": 34_000, "detail_presentations": 2,
                    "last_detail_slide": 19, "last_detail_latency_ms": 1830}
        stats = {"failedDetails": 1}
        get.side_effect = [
            profile, benchmark, preparation, settled, RemoteDisconnected(), detailed, stats, benchmark, profile,
        ]
        result = replays.capture(FRAME, ORIGIN, 2)
        self.assertEqual(result["observed_detail_after_settle_ms"], 3000)
        self.assertEqual(result["status"], detailed)
        self.assertEqual(result["replay_stats"], stats)
        self.assertEqual(wait.call_count, 2)

        get.side_effect = [profile, benchmark, preparation, settled, detailed, {"failedDetails": 0}]
        with self.assertRaisesRegex(ValueError, "expected full-preview error"):
            replays.capture(FRAME, ORIGIN, 2)

    @patch.object(replays, "read_json")
    def test_capture_rejects_a_server_restart(self, get):
        run = example(True)
        profile, benchmark = run["profile"], run["benchmark"]
        changed = deepcopy(profile)
        changed["run"] = "restarted"
        get.side_effect = [profile, benchmark, run["preparation"], run["status"], benchmark, changed]
        with self.assertRaisesRegex(ValueError, "restarted"):
            replays.capture(FRAME, ORIGIN, 2)

    @patch.object(replays, "read_json")
    def test_capture_touch_preserves_landing_and_detail_reports(self, get):
        run = example(True)
        profile, benchmark = run["profile"], run["benchmark"]
        profile["thumbnailSha256"] = "thumbnail fixture"
        benchmark.update(replay_touch=True, scripted=False, replay_staged_detail=True,
                         physical_drags={"forward": 3, "backward": 2, "returned": 1,
                                         "cancelled": 0, "interrupted": 1})
        run["status"]["last_detail_slide"] = 2
        run["replay_stats"] = {"failedDetails": 0}
        get.side_effect = [profile, benchmark, run["status"], run["preparation"],
                           run["replay_stats"], benchmark, profile]
        self.assertEqual(replays.capture_touch(FRAME, ORIGIN), run)

    @patch.object(replays, "read_json")
    def test_capture_touch_rejects_scripted_and_moving_captures(self, get):
        run = example(True)
        profile, benchmark = run["profile"], run["benchmark"]
        get.side_effect = [profile, benchmark]
        with self.assertRaisesRegex(ValueError, "physical-touch trial"):
            replays.capture_touch(FRAME, ORIGIN)

        benchmark.update(replay_touch=True, scripted=False, replay_staged_detail=True)
        get.side_effect = [profile, benchmark]
        with self.assertRaisesRegex(ValueError, "matched thumbnail capture"):
            replays.capture_touch(FRAME, ORIGIN)

        profile["thumbnailSha256"] = "thumbnail fixture"
        get.side_effect = [profile, benchmark, run["status"], run["preparation"],
                           {}, {**benchmark, "cache_hits": 4}]
        with self.assertRaisesRegex(ValueError, "last swipe to settle"):
            replays.capture_touch(FRAME, ORIGIN)


if __name__ == "__main__":
    unittest.main()
